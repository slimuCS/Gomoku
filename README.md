# 五子棋 Gomoku

以 C++20 與 [FTXUI](https://github.com/ArthurSonzogni/ftxui) 實作的終端機五子棋遊戲。

## 功能

- 15×15 標準棋盤，黑白雙方輪流落子
- 方向鍵移動游標，Enter / Space 落子
- 即時判斷勝負並顯示贏家
- 主選單、遊戲頁面、結束頁面三段流程
- 支援「再玩一局」與「回首頁」

## 環境需求

| 工具 | 版本 |
|------|------|
| CMake | ≥ 3.20 |
| C++ 編譯器 | 支援 C++20（GCC 10+、Clang 12+、MSVC 2019+） |
| 網路連線 | 首次建置時 CMake 會自動下載 FTXUI |

## 建置與執行

```bash
# 1. 產生建置檔案
cmake -S . -B build

# 2. 編譯
cmake --build build

# 3. 執行
./build/Gomoku_Project   # Linux / macOS
build\Debug\Gomoku_Project.exe  # Windows
```

## 操作方式

| 按鍵 | 動作 |
|------|------|
| ↑ ↓ ← → | 移動游標 |
| Enter / Space | 落子 |
| Enter（選單）| 確認選項 |

## 遊戲規則

- 黑子先行，雙方輪流下棋
- 不可在已有棋子的位置再次落子
- 最先在橫、豎或斜方向連成五枚同色棋子的玩家獲勝

## 專案結構

```
Gomoku/
├── CMakeLists.txt          # 建置設定，自動拉取 FTXUI
├── include/
│   └── gomoku/
│       ├── main.h          # Board、GameEngine 類別宣告
│       └── ui_controller.h # UI::Controller 類別宣告
└── src/
    ├── app.cpp             # 程式進入點
    ├── main.cpp            # Board、GameEngine 實作
    └── ui_controller.cpp   # FTXUI 介面與事件處理
```

## 依賴套件

- [FTXUI v6.1.9](https://github.com/ArthurSonzogni/ftxui) — 由 CMake FetchContent 自動下載，無需手動安裝
