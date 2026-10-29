# 韌體架構與控制邏輯

> 狀態：程式骨架已完成，**尚未在實機上編譯與測試**。Bluepad32 的 API 是對照官方範例寫的；第一次編譯若有錯誤，以實際錯誤訊息修正後更新本文件。

## 模組

| 檔案 | 負責 | 對外函式 |
|---|---|---|
| `car_main.ino` | setup／loop、模式切換、手動混控、防撞輔助、電池保護 | — |
| `config.h` | 腳位、功能開關、所有可調參數 | — |
| `motors.*` | L298N 坦克式驅動 | `motorsBegin`、`motorsSet`、`motorsSetPercent`、`motorsStop` |
| `sonar.*` | 三顆 HC-SR04 輪流測距 | `sonarBegin`、`sonarUpdate`、`sonarCm` |
| `gamepad.*` | Bluepad32 手把（只接第一支） | `gamepadBegin`、`gamepadUpdate`、`gamepadState`、`gamepadRumble` |
| `autopilot.*` | 自動避障狀態機 | `autopilotReset`、`autopilotStep`、`autopilotStateName` |
| `battery.*` | 電池電壓監測 | `batteryBegin`、`batteryUpdate`、`batteryVolts`、`batteryLow`、`batteryCritical` |
| `extras.*` | 選配車燈、喇叭 | `extrasBegin`、`extrasUpdate`、`extrasOff` |

## 主迴圈

每圈 loop 依序：

1. `gamepadUpdate()`：讀手把
2. `sonarUpdate()`：距離上次量測滿 30 ms 才量下一顆（左 → 中 → 右），每顆約每 90 ms 更新一次
3. `batteryUpdate()`：每 100 ms 取樣、平滑
4. 依模式算出左右輪速度（%）
5. 電池保護覆寫速度
6. `motorsSetPercent()` 輸出到 L298N
7. 每 200 ms 在序列埠印出狀態（`DEBUG_SERIAL`）

**不可長時間阻塞**：超音波的 `pulseIn` 最多阻塞約 20 ms，這已經是上限。需要計時的動作（例如後退 0.5 秒）一律用 `millis()` 寫成狀態機。

## 手把對應

| 按鍵 | 功能 | 程式 |
|---|---|---|
| 左搖桿上下 | 油門 T | `-axisY()`（往上推是負值，所以取負號） |
| 右搖桿左右 | 轉向 S | `axisRX()` |
| ZR（按住） | 速度上限 60% → 100% | `r2()` 或 `throttle() > 512` |
| ＋ | 切換手動／自動 | `miscButtons() & MISC_BUTTON_START` |
| A | 喇叭（選配） | `a()` |
| B | 車燈開關（選配） | `b()` |

⚠️ Bluepad32 依「按鍵位置」對應（Xbox 配置），Switch 手把上印的 A/B 可能和 `a()`/`b()` 相反。請用 `test_gamepad` 確認後更新本表。

## 模式

```
            按「＋」
  手動模式 ◀────────▶ 自動避障模式
     ▲                    │
     └── 手把斷線 ◀────────┘（任何模式下斷線：立刻停車並回到手動）
```

### 手動模式：坦克式混控

```
左輪 = T + S
右輪 = T − S
```

結果限制在 −100～100%，再乘上速度上限（平常 60%，按住 ZR 100%）。搖桿有死區 `STICK_DEADZONE`，避免放開搖桿時車子緩慢飄移。

### 防撞輔助（手動模式）

- 正前方 < `ASSIST_STOP_CM`（15 cm）且駕駛想前進：油門歸零、手把震動。後退與原地轉向仍可操作。
- 左前或右前 < `ASSIST_WARN_SIDE_CM`（10 cm）：手把震動提醒，不干預駕駛。
- 震動有冷卻時間 `RUMBLE_COOLDOWN_MS`，不會一直震。

### 自動避障：狀態機

```
        任一顆 < 10 cm
CRUISE ─────────────────▶ BACKUP（後退 0.5 秒）
  │  ▲                       │
  │  │ 正前 > 40 cm          │ 時間到，選較空的一側
  │  │                       ▼
  └──┼── 正前 < 30 cm ─────▶ TURN（原地轉向）
     └──────────────────────┘ │
                              └─ 轉超過 1.5 秒仍未脫困 → BACKUP
```

CRUISE 狀態的規則依優先順序判斷，符合一條就執行：

1. 任一顆 < `AUTO_EMERGENCY_CM`（10 cm）→ 進入 BACKUP
2. 正前 < `AUTO_FRONT_CM`（30 cm）→ 進入 TURN，轉向較空的一側
3. 左前 < `AUTO_SIDE_CM`（20 cm）→ 往右微調；右前 < 20 cm → 往左微調
4. 其他 → 直行（`AUTO_CRUISE_PCT`）

TURN 回到 CRUISE 的條件是正前 > `AUTO_FRONT_CM + AUTO_CLEAR_MARGIN_CM`（40 cm），多留的 10 cm 避免在門檻附近來回抖動。

### 安全保護（優先於所有模式）

| 情況 | 行為 |
|---|---|
| 手把斷線 | 立刻停車、關閉車燈喇叭、回到手動模式 |
| 電池 < `BATTERY_LOW_V`（7.0 V） | 手把震動提醒 |
| 電池 < `BATTERY_CRITICAL_V`（6.6 V） | 強制停車並震動 |
| 電池連續 < 6.6 V 達 `BATTERY_CRITICAL_HOLD_MS`（3 秒） | **鎖定停車**，重開機才解除 |

電池電壓有平滑處理，馬達起步時的瞬間壓降不會被誤判成沒電。

為什麼要鎖定：停車後電池不再出力，電壓會回升到 6.6 V 以上；不鎖定的話車子會「跑 → 壓降 → 停 → 回升 → 又跑」反覆循環，持續過度放電。鎖定後序列埠會印出 `[battery] ... 停車鎖定`。
電壓低於 `BATTERY_ABSENT_V`（5.0 V）視為沒接電池（例如燒錄時只插 USB、電池開關關著），這時不會開車但也不鎖定，打開電池開關就能正常使用。

## 參數調整指南

所有參數都在 `config.h`。調整後請把結果記到 `docs/testing.md`。

| 現象 | 調整 |
|---|---|
| 某一側前後相反 | `LEFT_MOTOR_INVERTED`／`RIGHT_MOTOR_INVERTED` 改 true |
| 車子走不直 | 較快那側的 `*_TRIM` 調小（例如 0.95） |
| 低速時馬達只嗡嗡叫不轉 | `MOTOR_MIN_PWM` 調大 |
| 放開搖桿車子還會動 | `STICK_DEADZONE` 調大 |
| 防撞煞車太晚撞到牆 | `ASSIST_STOP_CM` 調大，或 `SPEED_LIMIT_NORMAL_PCT` 調小 |
| 自動模式太早轉彎、在空曠處也一直轉 | `AUTO_FRONT_CM` 調小 |
| 自動模式原地轉不動 | `AUTO_TURN_PCT` 調大 |
| 超音波讀值亂跳 | 確認接線與分壓；之後可加中位數濾波（待辦） |

## 待辦

- [ ] 第一次在 Arduino IDE 編譯 `car_main`，修正編譯錯誤
- [ ] 用 `test_gamepad` 確認 A/B、ZR、「＋」的實際對應，更新上方「手把對應」
- [ ] 超音波加中位數濾波（取最近 3 次的中位數），減少偶發的錯誤讀值
- [ ] 自動模式加「手動接管」：自動模式中推搖桿就暫時改為手動
- [ ] （選配）啟用車燈、喇叭並實測
