import { createServer, IncomingMessage, ServerResponse } from 'node:http';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

type CellState = 'closed' | 'opened' | 'flagged';
type FlagColor = 'red' | 'blue' | 'yellow' | 'green';
type Cell = { playable: boolean; mine: boolean; movingMine: boolean; state: CellState; flagColor: FlagColor | null };
type CellView = { playable: boolean; opened: boolean; flagged: boolean; flagColor: FlagColor | null; mine: boolean; candidate: boolean; value: number };

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

  constructor(mines: number, field1: string[]) {
    const lines = field1.length ? field1 : Array(7).fill('???????');
    this.rows = lines.length;
    this.cols = Math.max(0, ...lines.map(line => line.length)) || 7;
    this.grid = lines.map(line => Array.from({ length: this.cols }, (_, c) => ({
      playable: line[c] === '?', mine: false, movingMine: false, state: 'closed' as CellState, flagColor: null,
    })));
    this.totalMines = mines;
    this.safeCellsToOpen = this.grid.flat().filter(cell => cell.playable).length - mines;
    this.movingCount = mines >= 300 ? 5 : 0;
    if (this.movingCount) {
      const available = this.coordinates();
      this.shuffle(available).slice(0, 30).forEach(([r, c]) => this.candidates.add(this.key(r, c)));
      this.moveMovingMines();
    }
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
  private initialize(firstR: number, firstC: number): void {
    const coords = this.shuffle(this.coordinates().filter(([r, c]) => r !== firstR || c !== firstC));
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
  act(r: number, c: number, action: 'open' | 'flag', flagColor: FlagColor): boolean {
    const cell = this.grid[r][c];
    let hit = false;
    const firstOpen = action === 'open' && !this.initialized;
    if (action === 'flag') {
      if (cell.state === 'closed') { cell.state = 'flagged'; cell.flagColor = flagColor; this.flags++; }
      else if (cell.state === 'flagged') { cell.state = 'closed'; cell.flagColor = null; this.flags--; }
    } else {
      if (!this.initialized) this.initialize(r, c);
      if (cell.state === 'closed') {
        if (cell.mine) { cell.state = 'opened'; this.hits++; this.totalMines = Math.max(0, this.totalMines - 1); hit = true; }
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
        mine: cell.playable && cell.mine && (opened || this.safeCellsToOpen === 0),
        candidate: opened && this.candidates.has(this.key(r, c)),
        value: opened && !cell.mine ? this.neighbors(r, c).filter(([nr, nc]) => this.grid[nr][nc].mine || this.grid[nr][nc].movingMine).length : 0 };
    }));
  }
}

type Game = { board?: Board; mineSetting: number; firstMove: boolean; won: boolean };
const games = new Map<string, Game>();
const fieldPath = resolve(process.env.FIELD_FILE || 'field/field1.txt');
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
    minesLeft: board.totalMines - board.flags, hitCount: board.hits, flagsCount: board.flags,
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
  if (!game) { game = { mineSetting: 100, firstMove: true, won: false }; games.set(sessionId, game); }
  try {
    if (req.method === 'GET' && path === '/api/state') { send(res, 200, state(game)); return; }
    const body = await readBody(req);
    if (req.method === 'POST' && path === '/api/new') {
      if (![100, 200, 300].includes(body.mines)) { send(res, 400, { error: '地雷数は100、200、300のいずれかを指定してください。' }); return; }
      const field = readFileSync(fieldPath, 'utf8').split(/\r?\n/).filter((line, i, all) => !(i === all.length - 1 && line === ''));
      if (!field.length) { send(res, 500, { error: 'field/field1.txt を読み込めません。' }); return; }
      const board = new Board(body.mines, field);
      if (body.mines >= board.grid.flat().filter(cell => cell.playable).length) { send(res, 400, { error: 'このフィールドには指定された数の地雷を配置できません。' }); return; }
      game.board = board; game.mineSetting = body.mines; game.firstMove = true; game.won = false;
      send(res, 200, state(game, 'ゲーム開始、マスを選んでください。')); return;
    }
    if (req.method === 'POST' && path === '/api/action') {
      const board = game.board;
      if (!board) { send(res, 409, { error: '先にゲームを開始してください。' }); return; }
      const { row, col, action, flagColor } = body;
      if (!Number.isInteger(row) || !Number.isInteger(col) || !['open', 'flag'].includes(action) ||
          row < 0 || row >= board.rows || col < 0 || col >= board.cols || !board.grid[row][col].playable) {
        send(res, 400, { error: '操作またはマスの指定が不正です。' }); return;
      }
      if (game.won) { send(res, 200, state(game, 'ゲームクリア済みです。もう一度遊ぶ場合はホームへ戻ってください。')); return; }
      const colors: FlagColor[] = ['red', 'blue', 'yellow', 'green'];
      const selectedColor = colors.includes(flagColor) ? flagColor as FlagColor : 'red';
      const hit = board.act(row, col, action, selectedColor);
      if (action === 'open') game.firstMove = false;
      game.won = board.safeCellsToOpen === 0;
      send(res, 200, state(game, game.won ? 'クリア、すべての安全なマスを開きました。' : hit ? '地雷を踏みました。ゲームは続行します。' : action === 'open' ? 'マスを開きました。' : '旗を切り替えました。'));
      return;
    }
    send(res, 404, { error: '指定されたURLは見つかりません。' });
  } catch (error) {
    send(res, error instanceof SyntaxError ? 400 : 500, { error: error instanceof SyntaxError ? '不正なJSONです。' : 'サーバー内部エラーです。' });
  }
}).listen(Number(process.env.PORT || 8080), '0.0.0.0', () => {
  const port = Number(process.env.PORT || 8080);
  console.log(`Minesweeper is running at http://localhost:${port}`);
});
