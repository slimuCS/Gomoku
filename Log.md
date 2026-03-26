# 開發日誌

## 2026-03-19

### 文件初始化（slimu）
- 建立 `README.md`、`Log.md`、`scoring.md`，完成專案文件骨架

### 核心遊戲邏輯（aionyx）
- 新增 `CMakeLists.txt`，設定 C++20 編譯環境並整合 FTXUI（FetchContent）
- 實作 `include/gomoku/main.h`：定義 `Stone` 列舉（EMPTY / BLACK / WHITE）、`Board` 類別（15×15 棋盤、placeStone、getStone）、`GameEngine` 類別（checkWin 四方向連線判斷）
- 實作 `src/main.cpp`：Board 與 GameEngine 成員函式
- 實作 `src/app.cpp`：程式進入點
- 程式碼風格調整（file header、命名規範）

---

## 2026-03-22

### 終端介面建置（aionyx）
- 新增 `include/gomoku/ui_controller.h`：定義 `UI::Controller` 類別，封裝 FTXUI ScreenInteractive 與遊戲狀態（游標位置、當前玩家）
- 新增 `src/ui_controller.cpp`：
  - 實作三頁面流程：主選單（FrontPage）、遊戲棋盤（GameBoard）、結束頁面（EndPage）
  - 以 `Container::Tab` 切換頁面
  - 方向鍵移動游標、Enter / Space 落子、即時勝負判斷
  - 自訂 `InteractiveBoard` 元件（繼承 `ComponentBase`）處理鍵盤事件
  - 棋盤格子以 `○`（黑子）`●`（白子）`+`（空位）顯示，游標位置以藍底標示
  - 支援「再玩一局」與「回首頁」並重置棋盤狀態

---

## 2026-03-24

### 引擎重構與 Python 綁定（aionyx）
- 將 `main.h` / `main.cpp` 更名為 `engine.h` / `engine.cpp`，提高模組語意清晰度
- 強化引擎：新增玩家狀態管理，補強 win condition 邊界處理
- 新增 `src/bindings.cpp`：以 pybind11 將 `Board`、`GameEngine` 暴露給 Python，支援觀測值（observation）擷取
- 更新 `CMakeLists.txt` 以同時建置 C++ 執行檔與 Python 模組
- 新增 `model.py`：引入 PyTorch，建立神經網路模型骨架

---

## 2026-03-25

### AI 模型與 MCTS 實作（aionyx）
- 實作 Monte Carlo Tree Search（MCTS）決策核心：UCB 選擇、展開、模擬、反向傳播
- 強化 MCTS：加入棋盤狀態管理與完整模擬邏輯
- 新增 `processTrainModel.py`：完整訓練流程，包含 Replay Buffer、自我對弈資料收集、批次梯度更新
- MCTS 每步預設 200 次模擬

---

## 2026-03-26

### 訓練效能調整（aionyx）
- 將 MCTS 每步模擬次數從 200 提升至 400，提高 AI 決策品質
