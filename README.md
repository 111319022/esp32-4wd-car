# 蠻橫衝直撞無理四驅車

> 機電整合實務應用 2026 Fall｜組員：徐丞睿、王宸祐、鍾尚哲

一台用 **ESP32** 控制的四驅車：可以用 **Switch 手把**藍牙遙控，也能靠車頭三顆**超音波**自動避障。名字很蠻橫，其實很講理：手動駕駛快撞到東西時，車子會自己踩煞車，手把也會震動提醒。

## 功能

| 功能 | 說明 |
|---|---|
| 手動模式 | 左搖桿前進後退、右搖桿轉向，按住 ZR 加速 |
| 自動避障模式 | 依左前、正前、右前三個距離，自動直行、轉向或後退 |
| 模式切換 | 按手把的「＋」 |
| 防撞輔助 | 手動時正前方小於 15 cm 禁止前進，手把震動 |
| 斷線保護 | 手把斷線立刻停車 |
| 電池保護 | 低電量震動提醒，電壓過低自動停車 |
| 選配 | 頭燈、煞車燈、喇叭 |

## 系統架構

```
Switch 手把 ──藍牙──┐
                    ▼
HC-SR04 ×3 ──測距──▶ ESP32-WROOM-32 ──控制訊號──▶ L298N ──▶ TT 馬達 ×4
                    ▲                               │
電池電壓 ──分壓─────┘        ◀──── 5 V 供電 ────────┤
                                                    │
                              18650 ×2 串聯（7.4 V）─┘
```

## 快速開始

1. 照 [`docs/setup.md`](docs/setup.md) 安裝 Arduino IDE 和 Bluepad32 開發板套件。
2. 照 [`docs/hardware.md`](docs/hardware.md) 接線，**第一次通電前先做「通電檢查」**。
3. 依序跑 [`firmware/tests/`](firmware/tests/) 的單項測試（步驟見 [`docs/testing.md`](docs/testing.md)）。
4. 打開 `firmware/car_main/car_main.ino` 上傳主程式。
5. 讓手把進入配對模式（Pro 手把按住頂部的小同步鍵），連上後就能開車。

## 文件

| 文件 | 內容 |
|---|---|
| [`docs/BOM.md`](docs/BOM.md) | 材料清單與採購狀態 |
| [`docs/hardware.md`](docs/hardware.md) | 接線、腳位、電源、機構配置 |
| [`docs/software.md`](docs/software.md) | 韌體架構、控制邏輯、參數調整 |
| [`docs/setup.md`](docs/setup.md) | 開發環境安裝與燒錄 |
| [`docs/testing.md`](docs/testing.md) | 測試步驟、驗收標準、測試紀錄 |
| [`docs/decisions.md`](docs/decisions.md) | 設計決策紀錄 |
| [`CLAUDE.md`](CLAUDE.md) | 專案記憶：給 AI 代理與組員快速掌握全貌 |

## 用 AI 代理協作

- **Claude Code**：在 repo 根目錄開啟就會自動讀取 `CLAUDE.md`。
- **其他代理**（Codex、Cursor 等）：會讀 `AGENTS.md`，再導向 `CLAUDE.md`。
- 有新的決定、腳位變更或測試結果時，請更新 `CLAUDE.md` 與對應的 `docs/` 文件，讓下一個人（或下一個 AI）接得上。

## 協作方式

- `main` 保持可以編譯的狀態。
- 各自開分支開發，例如 `chengrui/sonar-filter`，完成後開 Pull Request 請組員看過再合併。
- commit 訊息用中文寫清楚改了什麼，例如「調整防撞距離為 20 cm」。

## 安全提醒

- 5 V 只能接 ESP32 的 **VIN**，不能接 3V3。
- HC-SR04 的 Echo 一定要分壓。
- 測試馬達前先把車架高；燒錄程式時先關電池開關。
- 18650 沒有保護板：**用完一定要關開關**，不要在電池上焊錫，不要讓電池碰到金屬短路。
