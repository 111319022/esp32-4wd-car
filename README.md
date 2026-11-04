# 蠻橫衝直撞無理四驅車

> 機電整合實務應用 2026 Fall｜組員：徐丞睿、王宸祐、鍾尚哲

一台用 **ESP32** 控制的四驅車：可以用 **Switch 手把**藍牙遙控，也能靠車頭三顆**超音波**自動避障。名字很蠻橫，其實很講理：手動駕駛快撞到東西時，車子會自己踩煞車，手把也會震動提醒。

## 功能

| 功能 | 說明 |
|---|---|
| 手動模式 | 左右兩支 Joy-Con 直握：左搖桿前進後退、右搖桿轉向，按住 ZR 加速 |
| 自動避障模式 | 依左前、正前、右前三個距離，自動直行、轉向或後退 |
| 模式切換 | 按手把的「＋」 |
| 防撞輔助 | 手動時正前方小於 15 cm 禁止前進，手把震動 |
| 斷線保護 | 任一支手把斷線立刻停車 |
| 電池保護 | 低電量震動提醒，電壓過低自動停車 |
| 選配 | 頭燈、煞車燈、喇叭 |

## 系統架構

```
Joy-Con ×2 ──藍牙──┐
                    ▼
HC-SR04 ×3 ──測距──▶ ESP32-WROOM-32 ──控制訊號──▶ L298N ──▶ TT 馬達 ×4
                    ▲                               │
電池電壓 ──分壓─────┘        ◀──── 5 V 供電 ────────┤
                                                    │
                              18650 ×2 串聯（7.4 V）─┘
```

## 目前進度（2026-11-04）

| 項目 | 狀態 |
|---|---|
| 規劃、材料清單、接線設計 | ✅ 完成（期中報告） |
| 韌體 `car_main` | ✅ 可編譯、已燒錄到 ESP32 |
| 手把：雙 Joy-Con 連線、按鍵對應、震動、搖桿歸零 | ✅ 已實測 |
| 手把監看網頁 | ✅ 完成 |
| 材料採購、組裝底盤 | 進行中 |
| 馬達、超音波、電池實測與整合測試 | 待材料到齊 |

詳細測試結果見 [`docs/testing.md`](docs/testing.md)。

## 程式碼導覽

韌體是 Arduino sketch，每個功能一個模組，參數全部集中在 `config.h`。主迴圈不會長時間阻塞，需要計時的動作都用 `millis()` 寫成狀態機。

| 檔案 | 內容 | 建議先看 |
|---|---|---|
| [`firmware/car_main/car_main.ino`](firmware/car_main/car_main.ino) | 主迴圈：讀手把 → 測距 → 讀電池 → 依模式算左右輪速度 → 輸出；手動混控、防撞輔助、斷線保護 | ⭐ |
| [`firmware/car_main/config.h`](firmware/car_main/config.h) | 腳位、功能開關、所有可調參數 | ⭐ |
| [`firmware/car_main/gamepad.cpp`](firmware/car_main/gamepad.cpp) | 合併左右兩支 Joy-Con、搖桿歸零、補送 Joy-Con 震動指令 | ⭐ |
| [`firmware/car_main/autopilot.cpp`](firmware/car_main/autopilot.cpp) | 自動避障狀態機（直行／後退／轉向） | ⭐ |
| [`firmware/car_main/motors.cpp`](firmware/car_main/motors.cpp) | L298N 坦克式驅動、PWM | |
| [`firmware/car_main/sonar.cpp`](firmware/car_main/sonar.cpp) | 三顆 HC-SR04 輪流測距 | |
| [`firmware/car_main/battery.cpp`](firmware/car_main/battery.cpp) | 電池電壓監測、低電壓停車鎖定 | |
| [`firmware/car_main/extras.cpp`](firmware/car_main/extras.cpp) | 選配車燈、喇叭 | |
| [`firmware/tests/`](firmware/tests/) | 單項測試程式：馬達、手把、超音波、電池 | |
| [`tools/gamepad_monitor.html`](tools/gamepad_monitor.html) | 測試用網頁：用 Web Serial 讀 ESP32，即時顯示手把與判讀結果 | |

控制邏輯與設計理由分別見 [`docs/software.md`](docs/software.md) 與 [`docs/decisions.md`](docs/decisions.md)。

## 快速開始

1. 照 [`docs/setup.md`](docs/setup.md) 安裝 Arduino IDE 和 Bluepad32 開發板套件。
2. 照 [`docs/hardware.md`](docs/hardware.md) 接線，**第一次通電前先做「通電檢查」**。
3. 依序跑 [`firmware/tests/`](firmware/tests/) 的單項測試（步驟見 [`docs/testing.md`](docs/testing.md)）。
4. 打開 `firmware/car_main/car_main.ino` 上傳主程式。用 Chrome 打開 [`tools/gamepad_monitor.html`](tools/gamepad_monitor.html) 可以即時看手把讀值與判讀結果。
5. 左右兩支 Joy-Con 各按住側邊滑軌上的同步鍵配對，兩支都連上、搖桿歸零完成後就能開車（連線時先不要碰搖桿）。

## 文件

| 文件 | 內容 |
|---|---|
| [`docs/BOM.md`](docs/BOM.md) | 材料清單與採購狀態 |
| [`docs/hardware.md`](docs/hardware.md) | 接線、腳位、電源、機構配置 |
| [`docs/software.md`](docs/software.md) | 韌體架構、控制邏輯、參數調整 |
| [`docs/setup.md`](docs/setup.md) | 開發環境安裝與燒錄 |
| [`docs/testing.md`](docs/testing.md) | 測試步驟、驗收標準、測試紀錄 |
| [`docs/decisions.md`](docs/decisions.md) | 設計決策紀錄 |

## 用 AI 代理協作

- AI 代理（Codex、Cursor 等）會讀 [`AGENTS.md`](AGENTS.md)，裡面有本專案的規則。
- 有新的決定、腳位變更或測試結果時，請更新對應的 `docs/` 文件，讓下一個人（或下一個 AI）接得上。

## 協作方式

- `main` 保持可以編譯的狀態。
- 各自開分支開發，例如 `chengrui/sonar-filter`，完成後開 Pull Request 請組員看過再合併。
- commit 訊息用中文寫清楚改了什麼，例如「調整防撞距離為 20 cm」。

## 安全提醒

- 5 V 只能接 ESP32 的 **VIN**，不能接 3V3。
- HC-SR04 的 Echo 一定要分壓。
- 測試馬達前先把車架高。
- 插 USB 時，USB 和 L298N 的 5 V 不可以同時接到 ESP32：關電池開關，或拔掉 VIN 線（見 `docs/hardware.md`）。
- 18650 沒有保護板：**用完一定要關開關**，不要在電池上焊錫，不要讓電池碰到金屬短路。
