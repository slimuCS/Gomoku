# Gomoku (C++20 Terminal Edition)

這是一個以 **C++20 + FTXUI** 實作的終端機五子棋專案，支援：

- 本機雙人 (`PvP`)
- 本機人機 (`PvE`，內建 C++ reward-engine AI)
- 區網遠端連線對戰 (`Host` / `Join`)
- 對局存檔與讀檔（以走子歷史回放重建棋盤）
- 回放模式與回合計時（可在設定頁開關）

## 功能總覽

- 15x15 棋盤對局
- 人機模式 AI 走子診斷狀態顯示
- 遊戲內快捷鍵（`S` 設定、`U` 悔棋、`L` 存檔/離開、遠端 `Q` 斷線）
- 遠端連線握手與快照同步
- 回歸測試涵蓋遠端同步與存檔失敗路徑

## 系統需求

- CMake `>= 3.20`
- 支援 C++20 的編譯器
  - GCC 10+
  - Clang 12+
  - MSVC 2019+
- 首次 CMake 設定時需要網路（會透過 `FetchContent` 下載 FTXUI）

## 快速開始

### 1. Configure

```bash
cmake -S . -B cmake-build-debug
```

### 2. Build

```bash
cmake --build cmake-build-debug --target Gomoku_Project
```

### 3. 執行主程式

Windows:

```powershell
.\cmake-build-debug\Gomoku_Project.exe
```

macOS / Linux:

```bash
./cmake-build-debug/Gomoku_Project
```

### 4. 建置與執行回歸測試

```bash
cmake --build cmake-build-debug --target gomoku_regression_tests
ctest --test-dir cmake-build-debug --output-on-failure
```

或直接執行測試 binary：

```powershell
.\cmake-build-debug\gomoku_regression_tests.exe
```

## 操作方式

### 主選單

- `Start Game (PvP)`：本機雙人
- `Start Game (PvE)`：玩家對 AI
- `Host Remote Game`：建立遠端房間
- `Join Remote Game`：連線到遠端房間
- `Setup`：設定悔棋與回合計時
- `Load Game`：載入存檔
- `Exit`：離開

### 對局快捷鍵

- `↑ ↓ ← →`：移動游標
- `Enter` / `Space`：落子
- `U`：悔棋（需在設定開啟）
- `S`：開啟設定
- `L`：開啟存檔/離開選單
- `Esc`：返回上一層

### 遠端模式

- Host 端輸入埠號後啟動，等待對手連入
- Join 端輸入 `Host` 與 `Port` 連線
- 遠端對局中可用 `Q` 主動斷線

## 存檔與資料

- 預設存檔目錄：`saves/`
- 檔名格式：`save_YYYYMMDD_HHMMSS_mmm.gomoku`
- 檔案內容包含：
  - `mode PVP|PVE`
  - `size <board_size>`
  - 每一步座標（`x y`）

## 音效資源

程式會嘗試載入 `assets/audio/` 下列檔案：

- `backGround.mp3`
- `click.mp3`
- `placeStoneVoice.mp3`

## 專案結構

```text
Gomoku/
|-- CMakeLists.txt
|-- README.md
|-- docs/
|   |-- scoring.md
|   `-- screenshots/
|-- include/gomoku/
|   |-- ai/
|   |-- audio/
|   |-- core/
|   |-- net/
|   `-- ui/
|-- src/
|   |-- ai/
|   |-- app/
|   |-- audio/
|   |-- core/
|   |-- net/
|   `-- ui/
|-- assets/
|   `-- audio/
`-- tests/
    `-- regression_tests.cpp
```

## 已驗證指令（2026-04-20）

以下指令在此 repo 已實際執行通過：

```powershell
cmake --build cmake-build-debug --target Gomoku_Project
ctest --test-dir cmake-build-debug --output-on-failure
.\cmake-build-debug\gomoku_regression_tests.exe
```
