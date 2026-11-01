import { createServer, IncomingMessage, ServerResponse } from 'node:http';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

type CellState = 'closed' | 'opened' | 'flagged';
type FlagColor = 'red' | 'blue' | 'yellow' | 'green';
type Cell = { playable: boolean; mine: boolean; movingMine: boolean; removed: boolean; exploded: boolean; state: CellState; flagColor: FlagColor | null; allyDanger: boolean };
type CellView = { playable: boolean; opened: boolean; flagged: boolean; flagColor: FlagColor | null; mine: boolean; removed: boolean; exploded: boolean; candidate: boolean; allyDanger: boolean; value: number };

class Board {
  readonly rows: number;
  readonly cols: number;
  readonly grid: Cell[][];
  readonly candidates = new Set<string>();
  totalMines: number;
  safeCellsToOpen: number;
  hits = 0;
  flags = 0;
  initialized = false;
  private readonly movingCount: number;

  constructor(mines: number, field: string[]) {
    const lines = field.length ? field : Array(7).fill('???????');
    this.rows = lines.length;
    this.cols = Math.max(0, ...lines.map(line => line.length)) || 7;
    this.grid = lines.map(line => Array.from({ length: this.cols }, (_, c) => ({
      playable: line[c] === '?', mine: false, movingMine: false, removed: false, exploded: false, state: 'closed' as CellState, flagColor: null, allyDanger: false,
    })));
    this.totalMines = mines;
    this.safeCellsToOpen = this.grid.flat().filter(cell => cell.playable).length - mines;
    // 「理不尽モード」の移動爆弾は無効化する。
    this.movingCount = 0;
  }

  private key(r: number, c: number): string { return `${r},${c}`; }
  private coordinates(): Array<[number, number]> {
    return this.grid.flatMap((line, r) => line.flatMap((cell, c) => cell.playable ? [[r, c] as [number, number]] : []));
  }
  private shuffle<T>(items: T[]): T[] {
    for (let i = items.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1)); [items[i], items[j]] = [items[j], items[i]];
    }
    return items;
  }
  private neighbors(r: number, c: number): Array<[number, number]> {
    const result: Array<[number, number]> = [];
    for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      const nr = r + dr, nc = c + dc;
      if (this.grid[nr]?.[nc]?.playable) result.push([nr, nc]);
    }
    return result;
  }
  private initialize(safeR?: number, safeC?: number): void {
    const coords = this.shuffle(this.coordinates().filter(([r, c]) => safeR === undefined || r !== safeR || c !== safeC));
    for (const [r, c] of coords.slice(0, Math.min(this.totalMines, coords.length))) this.grid[r][c].mine = true;
    this.initialized = true;
  }
  private open(r: number, c: number): void {
    const cell = this.grid[r]?.[c];
    if (!cell?.playable || cell.state !== 'closed' || cell.mine) return;
    cell.state = 'opened'; this.safeCellsToOpen--;
    if (this.neighbors(r, c).every(([nr, nc]) => !this.grid[nr][nc].mine)) {
      this.neighbors(r, c).forEach(([nr, nc]) => this.open(nr, nc));
    }
  }
  act(r: number, c: number, action: 'open' | 'flag' | 'ally' | 'remove', flagColor: FlagColor): boolean {
    const cell = this.grid[r][c];
    let hit = false;
    if (cell.exploded) return hit;
    const firstOpen = action === 'open' && !this.initialized;
    if (action === 'flag') {
      if (cell.state === 'closed') { cell.state = 'flagged'; cell.flagColor = flagColor; this.flags++; }
      else if (cell.state === 'flagged') { cell.state = 'closed'; cell.flagColor = null; this.flags--; }
      this.moveMovingMines();
      return hit;
    }
    if (action === 'ally') {
      this.grid.flat().forEach(cell => { cell.allyDanger = false; });
      if (!this.initialized) this.initialize(r, c);
      const candidates = this.shuffle(this.coordinates().filter(([nr, nc]) => {
        const target = this.grid[nr][nc];
        return target.playable && target.state === 'closed' && !target.allyDanger;
      }));
      const target = candidates[0];
      if (!target) return hit;
      const [tr, tc] = target;
      const targetCell = this.grid[tr][tc];
      if (targetCell.mine) {
        targetCell.allyDanger = true;
        return hit;
      }
      this.open(tr, tc);
      return hit;
    }
    if (action === 'remove') {
      if (!this.initialized) this.initialize();
      if (cell.state === 'flagged') {
        this.flags--;
        cell.state = 'closed';
        cell.flagColor = null;
      }
      if (cell.mine) {
        cell.mine = false;
        cell.removed = true;
        cell.allyDanger = false;
        this.totalMines = Math.max(0, this.totalMines - 1);
        cell.state = 'opened';
      } else {
        this.open(r, c);
      }
      return hit;
    }
    if (!this.initialized) this.initialize(r, c);
    if (cell.state === 'closed') {
      if (cell.mine) {
        cell.state = 'opened';
        cell.exploded = true;
        this.hits++;
        this.totalMines = Math.max(0, this.totalMines - 1);
        hit = true;
      }
      else {
        this.open(r, c);
        if (firstOpen && this.totalMines > 0) {
          // The first click reveals up to two additional safe cells, matching the original game.
          let opened = this.grid.flat().filter(item => item.state === 'opened' && !item.mine).length;
          for (const [er, ec] of this.shuffle(this.coordinates())) {
            if (opened >= 3) break;
            const extra = this.grid[er][ec];
            if (extra.state === 'closed' && !extra.mine) { this.open(er, ec); opened++; }
          }
        }
      }
    }
    this.moveMovingMines();
    return hit;
  }
  private moveMovingMines(): void {
    this.grid.flat().forEach(cell => { cell.movingMine = false; });
    if (!this.movingCount) return;
    const available = this.shuffle([...this.candidates].filter(key => {
      const [r, c] = key.split(',').map(Number); return this.grid[r][c].state !== 'opened';
    }));
    available.slice(0, this.movingCount).forEach(key => {
      const [r, c] = key.split(',').map(Number); this.grid[r][c].movingMine = true;
    });
  }
  view(): CellView[][] {
    return this.grid.map((line, r) => line.map((cell, c) => {
      const opened = cell.playable && cell.state === 'opened';
      return { playable: cell.playable, opened, flagged: cell.state === 'flagged', flagColor: cell.flagColor,
        mine: cell.playable && cell.mine && (opened || this.safeCellsToOpen === 0), removed: cell.removed, exploded: cell.exploded,
        candidate: opened && this.candidates.has(this.key(r, c)),
        allyDanger: cell.playable && cell.allyDanger && cell.state === 'closed',
        value: opened && !cell.mine ? this.neighbors(r, c).filter(([nr, nc]) => this.grid[nr][nc].mine || this.grid[nr][nc].movingMine).length : 0 };
    }));
  }
}

type Game = { board?: Board; mineSetting: number; firstMove: boolean; won: boolean; money: number; reward: number; allyCost: number; removeCost: number };
const games = new Map<string, Game>();
const fieldPath = resolve(process.env.FIELD_FILE || 'field/field.txt');
const htmlPath = resolve(process.env.HTML_FILE || 'index.html');

function send(res: ServerResponse, status: number, body: unknown, type = 'application/json; charset=utf-8'): void {
  const payload = typeof body === 'string' ? body : JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': type, 'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS', 'Access-Control-Allow-Headers': 'Content-Type, X-Session-Id',
    'Cache-Control': 'no-store' });
  res.end(payload);
}
function state(game: Game, message = ''): object {
  const board = game.board;
  if (!board) return { active: false, message: 'ゲームを開始してください。' };
  return { active: true, rows: board.rows, cols: board.cols, mineSetting: game.mineSetting,
    minesLeft: board.totalMines, hitCount: board.hits, flagsCount: board.flags,
    money: game.money, reward: game.reward, allyCost: game.allyCost, removeCost: game.removeCost,
    started: !game.firstMove, won: game.won, message, cells: board.view() };
}
async function readBody(req: IncomingMessage): Promise<any> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) chunks.push(Buffer.from(chunk));
  return JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
}

createServer(async (req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, X-Session-Id');
  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }
  const path = new URL(req.url || '/', 'http://localhost').pathname;
  if (req.method === 'GET' && (path === '/' || path === '/index.html')) {
    try { send(res, 200, readFileSync(htmlPath, 'utf8'), 'text/html; charset=utf-8'); }
    catch { send(res, 500, 'index.html を読み込めません。', 'text/plain; charset=utf-8'); }
    return;
  }
  if (!['/api/state', '/api/new', '/api/action'].includes(path)) { send(res, 404, { error: '指定されたURLは見つかりません。' }); return; }
  const sessionId = req.headers['x-session-id'];
  if (typeof sessionId !== 'string' || !sessionId) { send(res, 400, { error: 'セッションIDが指定されていません。' }); return; }
  let game = games.get(sessionId);
  if (!game) { game = { mineSetting: 100, firstMove: true, won: false, money: 100000, reward: 100000, allyCost: 500, removeCost: 100 }; games.set(sessionId, game); }
  try {
    if (req.method === 'GET' && path === '/api/state') { send(res, 200, state(game)); return; }
    const body = await readBody(req);
    if (req.method === 'POST' && path === '/api/new') {
      if (![100, 200, 300].includes(body.mines)) { send(res, 400, { error: '地雷数は100、200、300のいずれかを指定してください。' }); return; }
      const field = readFileSync(fieldPath, 'utf8').split(/\r?\n/).filter((line, i, all) => !(i === all.length - 1 && line === ''));
      if (!field.length) { send(res, 500, { error: 'field/field.txt を読み込めません。' }); return; }
      const board = new Board(body.mines, field);
      if (body.mines >= board.grid.flat().filter(cell => cell.playable).length) { send(res, 400, { error: 'このフィールドには指定された数の地雷を配置できません。' }); return; }
      game.board = board; game.mineSetting = body.mines; game.firstMove = true; game.won = false;
      game.reward = body.mines * 1000; game.money = game.reward; game.allyCost = 500; game.removeCost = 100;
      send(res, 200, state(game, 'ゲーム開始、マスを選んでください。')); return;
    }
    if (req.method === 'POST' && path === '/api/action') {
      const board = game.board;
      if (!board) { send(res, 409, { error: '先にゲームを開始してください。' }); return; }
      const { row, col, action, flagColor } = body;
      const validAction = ['open', 'flag', 'ally', 'remove'];
      if (!validAction.includes(action) || !Number.isInteger(row) || !Number.isInteger(col)) {
        send(res, 400, { error: '操作またはマスの指定が不正です。' }); return;
      }
      if (row < 0 || row >= board.rows || col < 0 || col >= board.cols) {
        send(res, 400, { error: '操作またはマスの指定が不正です。' }); return;
      }
      if (action !== 'ally' && !board.grid[row][col].playable) {
        send(res, 400, { error: '操作またはマスの指定が不正です。' }); return;
      }
      if (board.grid[row][col].exploded) {
        send(res, 409, { error: 'このマスの地雷はすでに爆発しています。' }); return;
      }
      if (game.won) { send(res, 200, state(game, 'ゲームクリア済みです。もう一度遊ぶ場合はホームへ戻ってください。')); return; }
      if (action === 'ally' && game.money < game.allyCost) {
        send(res, 400, { error: `仲間を使うには${game.allyCost}$が必要です。` }); return;
      }
      if (action === 'remove' && game.money < game.removeCost) {
        send(res, 400, { error: `撤去依頼には${game.removeCost}$が必要です。` }); return;
      }
      const colors: FlagColor[] = ['red', 'blue', 'yellow', 'green'];
      const selectedColor = colors.includes(flagColor) ? flagColor as FlagColor : 'red';
      const removedMine = action === 'remove' && board.grid[row][col].mine;
      const hit = board.act(row, col, action, selectedColor);
      if (action === 'ally') game.money -= game.allyCost;
      if (action === 'remove') game.money -= game.removeCost;
      if (hit) game.money -= 100000;
      if (action === 'open' || action === 'remove') game.firstMove = false;
      game.won = board.safeCellsToOpen === 0;
      const message = game.won ? 'クリア、すべての安全なマスを開きました。'
        : hit ? '地雷を踏みました。治療費100000$を支払いました。'
        : action === 'open' ? 'マスを開きました。'
        : action === 'ally' ? '仲間が手伝い、マスを確認しました。'
        : action === 'flag' ? '旗を更新しました。'
        : removedMine ? '地雷を撤去しました。'
        : 'このマスに地雷はありませんでした。撤去費用が発生し、マスを開きました。';
      send(res, 200, state(game, message)); return;
    }
    send(res, 404, { error: '指定されたURLは見つかりません。' });
  } catch (error) {
    send(res, error instanceof SyntaxError ? 400 : 500, { error: error instanceof SyntaxError ? '不正なJSONです。' : 'サーバー内部エラーです。' });
  }
}).listen(Number(process.env.PORT || 8080), '0.0.0.0', () => {
  const port = Number(process.env.PORT || 8080);
  console.log(`Minesweeper is running at http://localhost:${port}`);
});
