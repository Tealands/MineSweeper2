#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <string>
#include <algorithm>
#include <limits> // numeric_limits for input handling
#include <fstream>
#include <random>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <memory>
#include <cctype>
#include <climits>
#include <cstring>
#include <unordered_map>

#ifndef _WIN32
using SOCKET = int;
constexpr int INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
#define closesocket close
#define SD_SEND SHUT_WR
#define InetPtonA inet_pton
constexpr int SOCKET_SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SOCKET_SEND_FLAGS = 0;
#endif

using namespace std;

int socketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// 定数定義
// SIZE は field.txt に合わせて動的に決定する
enum class CellState { CLOSED, OPENED, FLAGGED };
const char MINE_CHAR = '*';
const char CLOSED_CHAR = '?';
const char FLAGGED_CHAR = 'F';
const int INITIAL_OPEN_COUNT = 3; // 最初に自動で開くマスの数（最初のクリックを含む）

// カラーコード（ANSI）
constexpr const char* COLOR_RESET = "\033[0m";
constexpr const char* COLOR_BLUE = "\033[34m";   // 壁
constexpr const char* COLOR_GREEN = "\033[32m";  // F, J フラグ
constexpr const char* COLOR_YELLOW = "\033[33m"; // その他

// セル（マス）の情報を保持する構造体
struct Cell {
    bool hasMine = false;
    int neighborMines = 0; // 周囲の地雷数
    CellState state = CellState::CLOSED;
    bool jumpFlag = false; // J用の旗
    bool hasMovingMine = false; // 移動地雷の存在
};

// 列ラベルを決めるヘルパー (A-Z, a-z の順に割り当て)
char columnLabel(int idx) {
    if (idx < 26) return (char)('A' + idx);
    if (idx < 52) return (char)('a' + (idx - 26));
    return '?';
}

// マインスイーパーの盤面を管理するクラス
class Board {
private:
    vector<vector<Cell>> grid;
    int totalMines; // 現在表示する残り地雷数（初期は配置される地雷数）
    int safeCellsToOpen; // 開くべき安全なマスの数
    int minesHitCount = 0; // 新規: 地雷を踏んだ回数
    int flagsCount = 0;    // 新規: 現在設置されている旗の数
    int rows = 0;
    int cols = 0;
    vector<vector<bool>> playable; // プレイ可能セルかどうか
    vector<pair<int,int>> jumpCandidates; // 候補座標リスト
    vector<vector<bool>> isCandidate; // 候補かどうかマップ
    bool movingEnabled = false;
    int movingCount = 0;

    // 周囲の地雷数を計算する
    void calculateNeighborMines(int r, int c) {
        if (!playable[r][c] || grid[r][c].hasMine) return;

        int count = 0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int nr = r + dr;
                int nc = c + dc;

                if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && playable[nr][nc] && grid[nr][nc].hasMine) {
                    count++;
                }
            }
        }
        grid[r][c].neighborMines = count;
    }

    // 周囲のマスを再帰的に開く (0のマスを開けたとき)
    void recursiveOpen(int r, int c) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return;
        if (!playable[r][c]) return;
        if (grid[r][c].state != CellState::CLOSED) return;

        // 旗が立っていたら解除扱いにしない（既存挙動を維持）
        grid[r][c].state = CellState::OPENED;
        safeCellsToOpen--; // 開いたマスの数を減らす

        if (grid[r][c].neighborMines == 0) {
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    recursiveOpen(r + dr, c + dc);
                }
            }
        }
    }

public:
    // fieldLines: ファイルから読み込んだマップ行（'!' が壁、その他をプレイ可能とみなす）
    Board(int mines, const vector<string>& fieldLines) : totalMines(mines) {
        vector<string> effectiveField = fieldLines;
        // rows, cols を設定
        rows = static_cast<int>(effectiveField.size());
        cols = 0;
        for (const auto& ln : effectiveField) cols = max(cols, (int)ln.size());

        if (rows == 0 || cols == 0) {
            // フィールドファイルがない場合もプレイ可能な盤面にする
            rows = cols = 7;
            effectiveField.assign(rows, string(cols, '?'));
        }

        grid.assign(rows, vector<Cell>(cols));
        playable.assign(rows, vector<bool>(cols, false));

        // fieldLines をパースして playable を決定
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                char ch = (c < (int)effectiveField[r].size()) ? effectiveField[r][c] : '!';
                // '?' をプレイ可能セル、'!' を壁とする
                if (ch == '?') playable[r][c] = true;
                else playable[r][c] = false;
            }
        }

        // safeCellsToOpen はプレイ可能セル数 - mines
        int playableCount = 0;
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) if (playable[r][c]) playableCount++;

        safeCellsToOpen = playableCount - mines;
        minesHitCount = 0;
        flagsCount = 0;
        srand((unsigned)time(0));
    }

    // 地雷を配置し、周囲の地雷数を計算する
    void initializeBoard(int initialR, int initialC) {
        // プレイ可能セルの座標リストを作る
        vector<pair<int, int>> coords;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (!playable[r][c]) continue;
                if (r == initialR && c == initialC) continue; // 最初の操作マスには置かない
                coords.emplace_back(r, c);
            }
        }

        int toPlace = totalMines;
        if (toPlace > (int)coords.size()) toPlace = (int)coords.size();

        // ランダムに選んで地雷を配置
        // shuffle を使ってランダムに並べ替え
        std::shuffle(coords.begin(), coords.end(), std::default_random_engine((unsigned)time(0)));
        for (int i = 0; i < toPlace; ++i) {
            int r = coords[i].first;
            int c = coords[i].second;
            grid[r][c].hasMine = true;
        }

        // 周囲の地雷数を計算
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                calculateNeighborMines(r, c);
            }
        }
    }

    // 盤面と統計情報を表示する
    void display(bool showMines = false) const {
        // 新規: 統計情報の表示
        cout << "🚩 残り地雷数: " << totalMines - flagsCount
            << " / 旗の数: " << flagsCount
            << " / 💥 踏んだ回数: " << minesHitCount << endl;
        cout << "---------------------------------" << endl;

        cout << "  " << " ";
        for (int c = 0; c < cols; ++c) {
            cout << columnLabel(c) << " ";
        }
        cout << endl;

        for (int r = 0; r < rows; ++r) {
            // 行番号は2桁表示（1桁は先頭0付き）
            cout << setw(2) << setfill('0') << (r + 1) << setfill(' ') << " ";

            for (int c = 0; c < cols; ++c) {
                const Cell& cell = grid[r][c];

                if (!playable[r][c]) {
                    // 壁セル（青）
                    cout << COLOR_BLUE << '!' << COLOR_RESET << " ";
                    continue;
                }

                // 開かれたセル
                if (cell.state == CellState::OPENED) {
                    // 候補地は開いても J を表示
                    if (!isCandidate.empty() && isCandidate[r][c]) {
                        cout << COLOR_GREEN << 'J' << COLOR_RESET << " ";
                    }
                    else if (cell.hasMine) {
                        cout << COLOR_YELLOW << MINE_CHAR << COLOR_RESET << " "; // 地雷を踏んだ場合
                    }
                    else {
                        // 動く地雷も含めて動的に周囲の地雷数を計算して表示
                        int dynCount = 0;
                        for (int dr = -1; dr <= 1; ++dr) {
                            for (int dc = -1; dc <= 1; ++dc) {
                                if (dr == 0 && dc == 0) continue;
                                int nr = r + dr;
                                int nc = c + dc;
                                if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                                if (!playable[nr][nc]) continue;
                                if (grid[nr][nc].hasMine || grid[nr][nc].hasMovingMine) dynCount++;
                            }
                        }
                        // 0の場合は空白ではなく '0' を表示
                        if (dynCount == 0) cout << COLOR_YELLOW << '0' << COLOR_RESET << " ";
                        else cout << COLOR_YELLOW << dynCount << COLOR_RESET << " ";
                    }
                }
                else if (cell.jumpFlag) {
                    cout << COLOR_GREEN << 'J' << COLOR_RESET << " ";
                }
                else if (cell.state == CellState::FLAGGED) {
                    cout << COLOR_GREEN << FLAGGED_CHAR << COLOR_RESET << " ";
                }
                else { // CLOSED
                    if (showMines && cell.hasMine) {
                        cout << COLOR_YELLOW << MINE_CHAR << COLOR_RESET << " "; // ゲームオーバー時に地雷を表示
                    }
                    else {
                        cout << COLOR_YELLOW << CLOSED_CHAR << COLOR_RESET << " ";
                    }
                }
            }
            cout << endl;
        }
    }

    // マスを開ける操作
    void openCell(int r, int c, bool firstMove, bool verbose = true) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return;

        // 最初の操作の場合は地雷を配置
        if (firstMove) {
            initializeBoard(r, c);
        }

        Cell& cell = grid[r][c];

        if (cell.state == CellState::OPENED) {
            cout << "既に開かれているマスです。" << endl;
            return;
        }

        if (cell.state == CellState::FLAGGED) {
            cout << "旗が立てられています。旗を解除してから開いてください。" << endl;
            return;
        }

        // 静的地雷を踏んだ場合のみ死亡扱い
        if (cell.hasMine) {
            // 地雷を踏んだ: カウントを増やし、残り地雷数を減らす
            cell.state = CellState::OPENED;
            minesHitCount++; // 踏んだ回数を増やす
            if (totalMines > 0) {
                totalMines--; // 残り地雷数を減らす
            }
            if (verbose) {
                cout << "\n💥 地雷を踏みました！残り地雷数が1つ減りました。ゲームは続行します。💥" << endl;
            }
            return;
        }

        // 周囲が0の場合は再帰的に開く
        recursiveOpen(r, c);

        // 最初の操作なら、選択マスに加えていくつかの安全マスをランダムに開く
        if (firstMove) {
            int openedExtra = 1; // 既に1マスは開いた
            int attempts = 0;
            while (openedExtra < INITIAL_OPEN_COUNT && attempts < 1000) {
                int rr = rand() % rows;
                int cc = rand() % cols;
                if (!playable[rr][cc]) { attempts++; continue; }
                Cell& extra = grid[rr][cc];
                if (!extra.hasMine && extra.state == CellState::CLOSED) {
                    recursiveOpen(rr, cc);
                    openedExtra++;
                }
                attempts++;
            }
        }
    }

    // 旗を立てる/解除する操作
    void toggleFlag(int r, int c, bool verbose = true) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return;
        if (!playable[r][c]) {
            if (verbose) cout << "壁には旗を立てられません。" << endl;
            return;
        }

        Cell& cell = grid[r][c];

        if (cell.state == CellState::OPENED) {
            if (verbose) cout << "既に開かれたマスには旗を立てられません。" << endl;
            return;
        }

        if (cell.state == CellState::CLOSED) {
            cell.state = CellState::FLAGGED;
            flagsCount++; // 新規: カウントを増やす
            if (verbose) cout << "旗を立てました。" << endl;
        }
        else if (cell.state == CellState::FLAGGED) {
            cell.state = CellState::CLOSED;
            flagsCount--; // 新規: カウントを減らす
            if (verbose) cout << "旗を解除しました。" << endl;
        }
    }

    // Jフラグのトグル
    void toggleJumpFlag(int r, int c) {
        if (r < 0 || r >= rows || c < 0 || c >= cols) return;
        if (!playable[r][c]) { cout << "壁には旗を立てられません。" << endl; return; }

        Cell& cell = grid[r][c];
        if (cell.state == CellState::OPENED) { cout << "既に開かれたマスには旗を立てられません。" << endl; return; }
        cell.jumpFlag = !cell.jumpFlag;
        cout << (cell.jumpFlag ? "ジャンプフラグを立てました。" : "ジャンプフラグを解除しました。") << endl;
    }

    // 移動地雷機能の設定
    void setupMovingMines(int candidateCnt, int movingCnt) {
        movingEnabled = true;
        movingCount = movingCnt;
        isCandidate.assign(rows, vector<bool>(cols, false));

        // プレイ可能セルから候補を収集
        vector<pair<int,int>> coords;
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) if (playable[r][c]) coords.emplace_back(r,c);

        int cnt = min(candidateCnt, (int)coords.size());
        std::shuffle(coords.begin(), coords.end(), std::default_random_engine((unsigned)time(0)));
        jumpCandidates.clear();
        for (int i = 0; i < cnt; ++i) {
            jumpCandidates.push_back(coords[i]);
            isCandidate[coords[i].first][coords[i].second] = true;
        }

        // 初期配置
        moveMovingMines();
    }

    // 毎ターン移動させる
    void moveMovingMines() {
        if (!movingEnabled) return;
        // 既存のマークをクリア
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) grid[r][c].hasMovingMine = false;

        if (jumpCandidates.empty()) return;

        // 有効な候補（開かれていないもの）を収集
        vector<pair<int,int>> valid;
        for (auto &p : jumpCandidates) {
            int rr = p.first, cc = p.second;
            // 開かれているマスには移動地雷を置かない
            if (grid[rr][cc].state == CellState::OPENED) continue;
            valid.push_back(p);
        }
        if (valid.empty()) return;

        std::shuffle(valid.begin(), valid.end(), std::default_random_engine((unsigned)time(0)));
        int place = min((int)valid.size(), movingCount);
        for (int i = 0; i < place; ++i) {
            int rr = valid[i].first, cc = valid[i].second;
            grid[rr][cc].hasMovingMine = true;
        }
    }

    // ゲームクリアの判定
    bool checkWin() const {
        return safeCellsToOpen == 0;
    }

    int getRows() const { return rows; }
    int getCols() const { return cols; }
    int getMinesLeft() const { return totalMines - flagsCount; }
    int getHitCount() const { return minesHitCount; }
    int getFlagsCount() const { return flagsCount; }
    bool isPlayable(int r, int c) const { return playable[r][c]; }
    bool isOpened(int r, int c) const { return grid[r][c].state == CellState::OPENED; }
    bool isFlagged(int r, int c) const { return grid[r][c].state == CellState::FLAGGED; }
    bool hasMineAt(int r, int c) const { return grid[r][c].hasMine; }
    bool isJumpCandidateAt(int r, int c) const {
        return !isCandidate.empty() && isCandidate[r][c];
    }
    int getNeighborMineCount(int r, int c) const {
        int count = 0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                const int nr = r + dr;
                const int nc = c + dc;
                if (nr >= 0 && nr < rows && nc >= 0 && nc < cols &&
                    playable[nr][nc] && (grid[nr][nc].hasMine || grid[nr][nc].hasMovingMine)) {
                    ++count;
                }
            }
        }
        return count;
    }
};

// --- 関数による画面遷移管理 ---

// ルール説明画面
void showRules() {
    cout << "\n======================== ルール説明 ========================" << endl;
    cout << "7x7の盤面のうちいくつかの地雷が隠れています。" << endl;
    cout << "開いたときに表示されている数は、そのマスに隣接している地雷の数です。" << endl;
    cout << "もし周囲に地雷が一つもない場合は連鎖オープンといい周囲が全て公開されます。" << endl;
    cout << "例えば「A5 O」を入力して4と表示されたら、それは縦横斜めの隣接する8マスのうち4つに地雷が隠れているということです。" << endl;
    cout << "地雷を踏んだ回数がなるべく少なくなるように、すべての安全マスを開いてください。" << endl;
    cout << "------------------------------------------------------------" << endl;
    cout << "O: Open (開ける) / F: Flag (旗を立てる)" << endl;
    cout << "============================================================\n" << endl;
    cout << "ENTERキーを押すとホーム画面に戻ります...";
    cin.ignore(numeric_limits<streamsize>::max(), '\n'); // ENTER待ち
    cin.get();
}

// field.txt を読み込むヘルパー
vector<string> readFieldFile(const string& filename) {
    vector<string> lines;
    ifstream ifs(filename);
    if (!ifs) return lines;
    string line;
    while (getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

filesystem::path projectFilePath(const string& filename) {
        const filesystem::path inWorkingDirectory = filesystem::current_path() / filename;
        if (filesystem::exists(inWorkingDirectory)) return inWorkingDirectory;

#ifdef _WIN32
        wchar_t executablePath[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            filesystem::path directory = filesystem::path(executablePath).parent_path();
            for (int depth = 0; depth < 5 && !directory.empty(); ++depth) {
                const filesystem::path candidate = directory / filename;
                if (filesystem::exists(candidate)) return candidate;
                const filesystem::path parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
        }
#endif

        return inWorkingDirectory;
    }

    string jsonEscape(const string& value) {
        string escaped;
        for (const unsigned char ch : value) {
            switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[ch >> 4];
                    escaped += hex[ch & 0x0f];
                }
                else {
                    escaped += static_cast<char>(ch);
                }
            }
        }
        return escaped;
    }

    string jsonStringValue(const string& json, const string& key) {
        const string marker = "\"" + key + "\"";
        const size_t keyPosition = json.find(marker);
        if (keyPosition == string::npos) return {};
        const size_t colon = json.find(':', keyPosition + marker.size());
        if (colon == string::npos) return {};
        const size_t firstQuote = json.find('"', colon + 1);
        if (firstQuote == string::npos) return {};
        const size_t lastQuote = json.find('"', firstQuote + 1);
        if (lastQuote == string::npos) return {};
        return json.substr(firstQuote + 1, lastQuote - firstQuote - 1);
    }

    bool jsonIntegerValue(const string& json, const string& key, int& value) {
        const string marker = "\"" + key + "\"";
        const size_t keyPosition = json.find(marker);
        if (keyPosition == string::npos) return false;
        const size_t colon = json.find(':', keyPosition + marker.size());
        if (colon == string::npos) return false;
        size_t valuePosition = colon + 1;
        while (valuePosition < json.size() &&
            isspace(static_cast<unsigned char>(json[valuePosition]))) {
            ++valuePosition;
        }

        const size_t valueEnd = json.find_first_not_of("-0123456789", valuePosition);
        if (valueEnd == valuePosition || valuePosition == json.size()) return false;
        try {
            value = stoi(json.substr(valuePosition, valueEnd - valuePosition));
        }
        catch (const exception&) {
            return false;
        }
        return true;
    }

    string requestHeaderValue(const string& request, const string& name) {
        const size_t headerEnd = request.find("\r\n\r\n");
        size_t lineStart = request.find("\r\n");
        if (lineStart == string::npos || headerEnd == string::npos) return {};
        lineStart += 2;

        string expectedName = name;
        transform(expectedName.begin(), expectedName.end(), expectedName.begin(),
            [](unsigned char ch) { return static_cast<char>(tolower(ch)); });

        while (lineStart < headerEnd) {
            const size_t lineEnd = request.find("\r\n", lineStart);
            if (lineEnd == string::npos || lineEnd > headerEnd) break;
            const size_t colon = request.find(':', lineStart);
            if (colon != string::npos && colon < lineEnd) {
                string headerName = request.substr(lineStart, colon - lineStart);
                transform(headerName.begin(), headerName.end(), headerName.begin(),
                    [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
                if (headerName == expectedName) {
                    size_t valueStart = colon + 1;
                    while (valueStart < lineEnd &&
                        isspace(static_cast<unsigned char>(request[valueStart]))) {
                        ++valueStart;
                    }
                    return request.substr(valueStart, lineEnd - valueStart);
                }
            }
            lineStart = lineEnd + 2;
        }
        return {};
    }

    struct WebGame {
        unique_ptr<Board> board;
        int mineSetting = 100;
        bool firstMove = true;
        bool won = false;

        string stateJson(const string& message = {}) const {
            if (!board) {
                return "{\"active\":false,\"message\":\"ゲームを開始してください。\"}";
            }

            ostringstream json;
            json << "{\"active\":true,\"rows\":" << board->getRows()
                << ",\"cols\":" << board->getCols()
                << ",\"mineSetting\":" << mineSetting
                << ",\"minesLeft\":" << board->getMinesLeft()
                << ",\"hitCount\":" << board->getHitCount()
                << ",\"flagsCount\":" << board->getFlagsCount()
                << ",\"started\":" << (firstMove ? "false" : "true")
                << ",\"won\":" << (won ? "true" : "false")
                << ",\"message\":\"" << jsonEscape(message) << "\",\"cells\":[";

            for (int r = 0; r < board->getRows(); ++r) {
                if (r != 0) json << ',';
                json << '[';
                for (int c = 0; c < board->getCols(); ++c) {
                    if (c != 0) json << ',';
                    const bool playable = board->isPlayable(r, c);
                    const bool opened = playable && board->isOpened(r, c);
                    const bool mine = playable && board->hasMineAt(r, c);
                    json << "{\"playable\":" << (playable ? "true" : "false")
                        << ",\"opened\":" << (opened ? "true" : "false")
                        << ",\"flagged\":" << (playable && board->isFlagged(r, c) ? "true" : "false")
                        << ",\"mine\":" << (mine && (opened || won) ? "true" : "false")
                        << ",\"candidate\":" << (opened && board->isJumpCandidateAt(r, c) ? "true" : "false")
                        << ",\"value\":" << (opened && !mine ? board->getNeighborMineCount(r, c) : 0)
                        << '}';
                }
                json << ']';
            }
            json << "]}";
            return json.str();
        }
    };

    string httpResponse(int status, const string& reason, const string& contentType, const string& body) {
        ostringstream response;
        response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type, X-Session-Id\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n\r\n"
            << body;
        return response.str();
    }

    void sendHttpResponse(SOCKET client, const string& response) {
        size_t sent = 0;
        while (sent < response.size()) {
            const int count = send(client, response.data() + sent,
                static_cast<int>(min(response.size() - sent, static_cast<size_t>(INT_MAX))),
                SOCKET_SEND_FLAGS);
            if (count == SOCKET_ERROR || count == 0) return;
            sent += static_cast<size_t>(count);
        }
    }

    void handleApiRequest(SOCKET client, const string& method, const string& path,
        const string& request, WebGame& game);
    void handleNonApiRequest(SOCKET client, const string& method, const string& path);

    void handleHttpRequest(SOCKET client, const string& request,
        unordered_map<string, WebGame>& games) {
        const size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd == string::npos) {
            sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                "{\"error\":\"不正なHTTPリクエストです。\"}"));
            return;
        }

        istringstream requestLine(request.substr(0, request.find("\r\n")));
        string method, path, version;
        requestLine >> method >> path >> version;
        if (method.empty() || path.empty() || version.empty()) {
            sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                "{\"error\":\"リクエスト行が不正です。\"}"));
            return;
        }

        if (method == "OPTIONS") {
            sendHttpResponse(client, httpResponse(204, "No Content",
                "application/json; charset=utf-8", ""));
            return;
        }

        const bool isApiPath = path == "/api/state" ||
            path == "/api/new" || path == "/api/action";
        if (isApiPath) {
            const string sessionId = requestHeaderValue(request, "X-Session-Id");
            if (sessionId.empty()) {
                sendHttpResponse(client, httpResponse(400, "Bad Request",
                    "application/json; charset=utf-8",
                    "{\"error\":\"セッションIDが指定されていません。\"}"));
                return;
            }
            WebGame& game = games[sessionId];
            handleApiRequest(client, method, path, request, game);
            return;
        }

        handleNonApiRequest(client, method, path);
    }

    void handleApiRequest(SOCKET client, const string& method, const string& path,
        const string& request, WebGame& game) {
        const size_t headerEnd = request.find("\r\n\r\n");
        const string body = request.substr(headerEnd + 4);
        if (method == "GET" && path == "/api/state") {
            sendHttpResponse(client, httpResponse(200, "OK", "application/json; charset=utf-8",
                game.stateJson()));
            return;
        }

        if (method == "POST" && path == "/api/new") {
            int mines = 0;
            if (!jsonIntegerValue(body, "mines", mines) ||
                (mines != 100 && mines != 200 && mines != 300)) {
                sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                    "{\"error\":\"地雷数は100、200、300のいずれかを指定してください。\"}"));
                return;
            }

            const vector<string> field = readFieldFile(projectFilePath("field.txt").string());
            if (field.empty()) {
                sendHttpResponse(client, httpResponse(500, "Internal Server Error",
                    "application/json; charset=utf-8",
                    "{\"error\":\"field.txt を読み込めません。\"}"));
                return;
            }
            auto newBoard = make_unique<Board>(mines, field);
            int playableCount = 0;
            for (int r = 0; r < newBoard->getRows(); ++r) {
                for (int c = 0; c < newBoard->getCols(); ++c) {
                    if (newBoard->isPlayable(r, c)) ++playableCount;
                }
            }
            if (mines >= playableCount) {
                sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                    "{\"error\":\"このフィールドには指定された数の地雷を配置できません。\"}"));
                return;
            }

            game.board = move(newBoard);
            game.mineSetting = mines;
            game.firstMove = true;
            game.won = false;
            if (mines >= 300) game.board->setupMovingMines(30, 5);
            sendHttpResponse(client, httpResponse(200, "OK", "application/json; charset=utf-8",
                game.stateJson("ゲーム開始！マスを選んでください。")));
            return;
        }

        if (method == "POST" && path == "/api/action") {
            if (!game.board) {
                sendHttpResponse(client, httpResponse(409, "Conflict", "application/json; charset=utf-8",
                    "{\"error\":\"先にゲームを開始してください。\"}"));
                return;
            }
            int row = -1;
            int col = -1;
            const string action = jsonStringValue(body, "action");
            if (!jsonIntegerValue(body, "row", row) || !jsonIntegerValue(body, "col", col) ||
                (action != "open" && action != "flag") ||
                row < 0 || row >= game.board->getRows() ||
                col < 0 || col >= game.board->getCols() ||
                !game.board->isPlayable(row, col)) {
                sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                    "{\"error\":\"操作またはマスの指定が不正です。\"}"));
                return;
            }
            if (game.won) {
                sendHttpResponse(client, httpResponse(200, "OK", "application/json; charset=utf-8",
                    game.stateJson("ゲームクリア済みです。もう一度遊ぶ場合はホームへ戻ってください。")));
                return;
            }

            string message;
            if (action == "open") {
                const int previousHits = game.board->getHitCount();
                game.board->openCell(row, col, game.firstMove, false);
                game.firstMove = false;
                if (game.board->getHitCount() > previousHits) {
                    message = "地雷を踏みました。ゲームは続行します。";
                }
                else {
                    message = "マスを開きました。";
                }
            }
            else {
                game.board->toggleFlag(row, col, false);
                message = "旗を切り替えました。";
            }

            game.board->moveMovingMines();
            game.won = game.board->checkWin();
            if (game.won) message = "クリア！すべての安全なマスを開きました。";
            sendHttpResponse(client, httpResponse(200, "OK", "application/json; charset=utf-8",
                game.stateJson(message)));
            return;
        }

        sendHttpResponse(client, httpResponse(404, "Not Found", "application/json; charset=utf-8",
            "{\"error\":\"指定されたURLは見つかりません。\"}"));
    }

    void handleNonApiRequest(SOCKET client, const string& method, const string& path) {
        if (method == "GET" && (path == "/" || path == "/index.html")) {
            ifstream html(projectFilePath("index.html"), ios::binary);
            if (!html) {
                sendHttpResponse(client, httpResponse(500, "Internal Server Error",
                    "text/plain; charset=utf-8", "index.html を読み込めません。"));
                return;
            }
            const string contents((istreambuf_iterator<char>(html)), istreambuf_iterator<char>());
            sendHttpResponse(client, httpResponse(200, "OK", "text/html; charset=utf-8", contents));
            return;
        }

        sendHttpResponse(client, httpResponse(404, "Not Found", "application/json; charset=utf-8",
            "{\"error\":\"指定されたURLは見つかりません。\"}"));
    }

    int runWebServer() {
#ifdef _WIN32
        WSADATA winsockData;
        const int startupResult = WSAStartup(MAKEWORD(2, 2), &winsockData);
        if (startupResult != 0) {
            cerr << "Winsock の初期化に失敗しました: " << startupResult << endl;
            return 1;
        }
#endif

        SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == INVALID_SOCKET) {
            cerr << "サーバーソケットを作成できません: " << socketError() << endl;
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        const char* portValue = getenv("PORT");
        int port = 8080;
        if (portValue != nullptr) {
            try {
                port = stoi(portValue);
            }
            catch (const exception&) {
                cerr << "PORT の値が不正です。" << endl;
                closesocket(server);
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }
        }
        address.sin_port = htons(static_cast<unsigned short>(port));
#ifdef _WIN32
        const char* bindAddress = "127.0.0.1";
#else
        const char* bindAddress = "0.0.0.0";
#endif
        if (port < 1 || port > 65535 ||
            InetPtonA(AF_INET, bindAddress, &address.sin_addr) != 1 ||
            bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(server, SOMAXCONN) == SOCKET_ERROR) {
            cerr << "Webサーバーを起動できません: " << socketError() << endl;
            closesocket(server);
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }

        cout << "Web版マインスイーパーをポート " << port << " で起動しました。" << endl;
        cout << "終了するには Ctrl+C を押してください。" << endl;
        unordered_map<string, WebGame> games;
        while (true) {
            SOCKET client = accept(server, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                cerr << "接続を受け付けられません: " << socketError() << endl;
                continue;
            }

            string request;
            char buffer[4096];
            size_t expectedSize = 0;
            while (request.size() < 65536) {
                const int received = recv(client, buffer, sizeof(buffer), 0);
                if (received == SOCKET_ERROR || received == 0) break;
                request.append(buffer, static_cast<size_t>(received));

                const size_t headerEnd = request.find("\r\n\r\n");
                if (headerEnd != string::npos) {
                    const string headers = request.substr(0, headerEnd);
                    const string contentLengthKey = "Content-Length:";
                    const size_t contentLengthPosition = headers.find(contentLengthKey);
                    size_t contentLength = 0;
                    if (contentLengthPosition != string::npos) {
                        const size_t lengthStart = contentLengthPosition + contentLengthKey.size();
                        try {
                            contentLength = static_cast<size_t>(stoul(headers.substr(lengthStart)));
                        }
                        catch (const exception&) {
                            contentLength = 65537;
                        }
                    }
                    expectedSize = headerEnd + 4 + contentLength;
                    if (expectedSize > 65536) break;
                    if (request.size() >= expectedSize) break;
                }
            }

            if (request.size() >= 65536 || (expectedSize != 0 && request.size() < expectedSize)) {
                sendHttpResponse(client, httpResponse(400, "Bad Request", "application/json; charset=utf-8",
                    "{\"error\":\"HTTPリクエストが大きすぎるか不完全です。\"}"));
            }
            else {
                handleHttpRequest(client, request, games);
            }
            shutdown(client, SD_SEND);
            closesocket(client);
        }

        closesocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }
// ホーム画面
void showHome(int& selectedMines) {
    int choice = 0;
    while (choice < 1 || choice > 5) { // 選択肢を5まで増やす
        cout << "============= マインスイーパー =============" << endl;
        cout << "1. 簡単 (地雷 100個)" << endl;
        cout << "2. 普通 (地雷 200個)" << endl;
        cout << "3. 難しい (地雷 300個)" << endl;
        cout << "4. ルール説明" << endl;
        cout << "5. 終了" << endl;
        cout << "選択してください (1-5): ";

        if (!(cin >> choice)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }

        switch (choice) {
        case 1: selectedMines = 100; return;
        case 2: selectedMines = 200; return;
        case 3: selectedMines = 300; return;
        case 4: showRules(); return; // ルール説明画面へ
        case 5: exit(0);
        default: cout << "無効な入力です。1から5の数字を入力してください。" << endl;
        }
    }
}

// ゲーム画面（メインループ）
void runGame(int mines) {
    // field.txt を読み込んでマップを作成
    vector<string> field = readFieldFile("field.txt");
    Board board(mines, field);
    // 難しいモード時は移動地雷を設定（候補30、移動地雷5）
    if (mines >= 300) {
        board.setupMovingMines(30, 5);
    }
    bool isGameWin = false;
    bool firstMove = true;

    cout << "============= ゲーム開始 (地雷: " << mines << "個) =============" << endl;

    while (!isGameWin) {
        cout << "\n--- 現在の盤面 ---" << endl;
        board.display();
        cout << "------------------" << endl;

        string inputCoord, inputOperation;
        cout << "操作を入力してください (例: A1 O, C4 F): ";

        // 入力処理の改良 (文字列全体を読み込む)
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        if (!(cin >> inputCoord >> inputOperation)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cout << "不正な入力形式です。" << endl;
            continue;
        }

        // 座標の解析 (例: A1 -> r=0, c=0)
        if (inputCoord.length() != 2) {
            cout << "座標の形式が不正です (例: A1)。" << endl;
            continue;
        }
        char colCh = inputCoord[0];
        int c = -1;
        if (colCh >= 'A' && colCh <= 'Z') c = colCh - 'A';
        else if (colCh >= 'a' && colCh <= 'z') c = 26 + (colCh - 'a');
        else { cout << "座標の列が不正です。" << endl; continue; }
        int r = inputCoord[1] - '1';

        // 座標の範囲チェック
        if (r < 0 || r >= board.getRows() || c < 0 || c >= board.getCols()) {
            cout << "座標が盤面の範囲外です。" << endl;
            continue;
        }

        // 操作の実行
        char op = toupper(inputOperation[0]);
        if (op == 'O') {
            board.openCell(r, c, firstMove);
            firstMove = false;
            if (board.checkWin()) {
                isGameWin = true;
            }
        }
        else if (op == 'F') {
            board.toggleFlag(r, c);
        }
        else if (op == 'J') {
            // ジャンプフラグ操作
            board.toggleJumpFlag(r, c);
        }
        else {
            cout << "無効な操作です。O (Open) または F (Flag) を入力してください。" << endl;
        }

        // 各ターンの最後に移動地雷を更新
        board.moveMovingMines();
    }

    // ゲームクリア時の処理
    cout << "\n==============================================" << endl;
    if (isGameWin) {
        cout << "!!! CONGRATULATIONS !!!" << endl;
        cout << "全ての安全なマスを開けました！ゲームクリアです！" << endl;
        cout << "--- 最終盤面 ---" << endl;
        board.display();
    }
    cout << "==============================================\n" << endl;
    cout << "ホーム画面に戻ります..." << endl;
    for(int time=0; time<5; time++) {
        this_thread::sleep_for(chrono::seconds(1));
    }
}

// メイン関数 (画面遷移の制御)
int main(int argc, char* argv[]) {
    if (argc > 1 && string(argv[1]) == "--web") {
        return runWebServer();
    }

    // コンソールクリア用の仮コード
    auto clearConsole = []() {
#ifdef _WIN32
        system("cls");
#else
        // UNIX/Linux/macOS
        cout << "\033[2J\033[1;1H";
        // system("clear"); 
#endif
        };
    while (true) {
        clearConsole();
        int selectedMines = 0;
        showHome(selectedMines); // 難易度を選択（またはルール説明へ）

        if (selectedMines > 0) {
            clearConsole();
            runGame(selectedMines); // ゲーム実行
            cout << "続行するにはENTERキーを押してください...";
            cin.get();
        }
    }

    return 0;
}