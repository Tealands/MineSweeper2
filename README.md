# MineSweeper2

ブラウザーで遊べるマインスイーパーです。バックエンドはTypeScript（Node.js）で動作し、盤面とゲーム状態をセッションごとに管理します。`field/field1.txt` の `?` がプレイ可能なマス、その他の文字が壁です。

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
- `POST /api/action` — `{ "row": 0, "col": 0, "action": "open" | "flag" }` で操作

各APIリクエストには `X-Session-Id` ヘッダーが必要です。フロントエンドはブラウザーのセッションIDを送信します。

`Minesweeper2.cpp` は既存のコンソール版として残しています。Webバックエンドの起動にC++ビルドは不要です。
