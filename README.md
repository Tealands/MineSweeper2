# MineSweeper2

ブラウザーで遊べるマインスイーパーです。バックエンドはTypeScript（Node.js）で動作し、盤面とゲーム状態をセッションごとに管理します。`field/field.txt` の `?` がプレイ可能なマス、その他の文字が壁です。

## ローカルで起動

Node.js 22以降を用意し、リポジトリのルートで実行します。

```powershell
npm install
npm run dev
```

ブラウザーで http://127.0.0.1:8080 を開きます。開発時は `npm run dev`、通常起動は `npm run build` の後に `npm start` を使います。ポートは `PORT` 環境変数で変更できます。

## Docker / Cloud Run

```sh
docker build -t minesweeper .
docker run --rm -p 8080:8080 minesweeper
```

Cloud Run へは Google Cloud CLI からデプロイできます。

```sh
gcloud run deploy minesweeper-api --source . --region asia-northeast1 --allow-unauthenticated --max 1 --concurrency 1
```

デプロイ後、表示されたURLを `index.html` の `api-base-url` メタタグに設定してGitHub Pagesへpushします。Cloud Run側は `PORT` で指定されたポートを `0.0.0.0` で待ち受けます。ゲーム状態はインスタンスのメモリ上にあるため、インスタンスの再起動時にリセットされます。

## API

- `GET /api/state` — 現在のセッションの状態
- `POST /api/new` — `{ "mines": 100 | 200 | 300 }` で新規ゲーム
- `POST /api/action` — `{ "row": 0, "col": 0, "action": "open" | "flag" | "ally" | "remove" }` で操作

`ally` と `remove` は追加の経済アクションです。`GET /api/state` のレスポンスには `money`、`reward`、`allyCost`、`removeCost` が含まれます。

各APIリクエストには `X-Session-Id` ヘッダーが必要です。フロントエンドはブラウザーのセッションIDを送信します。

`Minesweeper2.cpp` は既存のコンソール版として残しています。Webバックエンドの起動にC++ビルドは不要です。

## ゲームシステム

この版では、通常のマインスイーパーに「経済ルール」を追加しています。

- 受注報酬: 初期所持金は `地雷数 × 1000$`
- 仲間支援: `🤝 仲間モード` を使うと 1 回につき `500$` を消費して、ランダムな未開封マスを調査します
- 撤去依頼: `💸 撤去依頼` を押して対象マスを選びます。地雷があれば撤去してチェックマークを表示し、地雷がなくても `100$` を支払ってそのマスを開きます
- 治療費: 地雷を踏むたびに `100000$` を支払います
- 収支: 盤面を安全に進めるほど残金が残り、最終的な残額をゲームの成績として扱えます
