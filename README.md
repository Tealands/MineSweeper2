# MineSweeper2

`Minesweeper2.cpp` をゲーム処理に使用する、C++製バックエンド付きのマインスイーパーです。ブラウザー版では `field.txt` の地形を読み込み、地雷配置・開封・旗・クリア判定を C++ 側で行います。

## 起動方法

### Visual Studio からビルドして起動する

1. Visual Studio で `Minesweeper2.slnx` を開きます。
2. 構成を `Debug`、プラットフォームを `x64` にして、`ビルド` → `ソリューションのビルド` を実行します。
3. リポジトリのルートフォルダーで PowerShell を開き、ビルドしたサーバーを起動します。

   ```powershell
   chcp 65001
   .\x64\Debug\Minesweeper2.exe --web
   ```

   `Release` 構成でビルドした場合は、実行ファイルのパスを `.\x64\Release\Minesweeper2.exe --web` に変更してください。
4. ブラウザーで [http://127.0.0.1:8080](http://127.0.0.1:8080) を開きます。
5. 終了するには、サーバーを起動したコンソールで `Ctrl+C` を押します。

Visual Studio のデバッガーから起動する場合は、プロジェクトのプロパティで `デバッグ` → `コマンド引数` に `--web` を設定して実行してください。

サーバーはローカルPCの `127.0.0.1` のみで待ち受けます。`index.html` と `field.txt` は実行時にリポジトリまたは実行ファイルの近くから読み込みます。

## 遊び方

- 簡単・普通・難しいから地雷数（100・200・300個）を選びます。
- マスをクリックすると開きます。旗モード中のクリック、または右クリックで旗を切り替えます。
- 地雷を踏んでもゲームは続行し、踏んだ回数が記録されます。
- すべての安全なマスを開くとクリアです。

通常の起動（`--web` を付けない場合）は、従来のコンソール版です。

## GitHub Pages と Cloud Run にデプロイする

GitHub Pages は静的ファイルのみを配信するため、C++バックエンドは Cloud Run で動かします。

1. Google Cloud CLI をインストールし、対象プロジェクトを選択して課金を有効にします。
2. リポジトリのルートで次を実行します（`asia-northeast1` は利用するリージョンに変更できます）。

   ```powershell
   gcloud run deploy minesweeper-api --source . --region asia-northeast1 --allow-unauthenticated --max 1 --concurrency 1
   ```

3. デプロイ完了時に表示される Cloud Run の URL をコピーします。
4. `index.html` の `api-base-url` メタタグの `content` にその URL を設定し、変更を GitHub に push します。例:

   ```html
   <meta name="api-base-url" content="https://minesweeper-api-xxxxx-an.a.run.app">
   ```

5. GitHub リポジトリの `Settings` → `Pages` で公開先を設定し、Pages の URL で動作を確認します。

Cloud Run 側は `PORT` 環境変数で指定されたポートを `0.0.0.0` で待ち受けます。フロントエンドからのクロスオリジンAPI呼び出しとブラウザーの事前確認リクエストにも対応しています。各ブラウザータブのゲーム状態はセッションIDごとに分離されますが、状態はCloud Runインスタンスのメモリ上にあるため、インスタンスの再起動時にはゲームがリセットされます。
