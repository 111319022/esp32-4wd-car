# 韌體架構與控制邏輯

> 狀態：程式骨架已完成，**尚未在實機上編譯與測試**。Bluepad32 的 API 是對照官方範例寫的；第一次編譯若有錯誤，以實際錯誤訊息修正後更新本文件。

## 模組

| 檔案 | 負責 | 對外函式 |
|---|---|---|
| `car_main.ino` | setup／loop、模式切換、手動混控、防撞輔助、電池保護 | — |
| `config.h` | 腳位、功能開關、所有可調參數 | — |
| `motors.*` | L298N 坦克式驅動 | `motorsBegin`、`motorsSet`、`motorsSetPercent`、`motorsStop` |
| `sonar.*` | 三顆 HC-SR04 輪流測距 | `sonarBegin`、`sonarUpdate`、`sonarCm` |
| `gamepad.*` | Bluepad32 手把（左右兩支 Joy-Con，或一支 Pro 手把當備用） | `gamepadBegin`、`gamepadUpdate`、`gamepadState`、`gamepadRumble` |
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

主要用**左右兩支 Joy-Con 直握**（一手一支）。兩支都連上才算「手把已連線」；**任何一支斷線就停車**並回到手動。也可以改用一支 Pro 手把（備用）；已經連上 Joy-Con 時 Pro 手把會被拒絕，反之亦然。

### 雙 Joy-Con（主要）

Bluepad32 把單支 Joy-Con 當成「橫握」的小手把，軸向和按鍵都轉了 90 度，所以程式讀的軸和直覺不同：

| 按鍵 | 功能 | 程式 |
|---|---|---|
| 左 Joy-Con 搖桿上下 | 油門 T | `-joyLeft->axisX()`（往上推是負值） |
| 右 Joy-Con 搖桿左右 | 轉向 S | `joyRight->axisY()`（往右推是正值） |
| 右 ZR（按住） | 速度上限 60% → 100% | `joyRight->r2()` |
| ＋（右 Joy-Con） | 切換手動／自動 | `joyRight->miscButtons() & MISC_BUTTON_START` |
| 右 Joy-Con 的 A | 喇叭（選配） | `joyRight->a()` |
| 右 Joy-Con 的 B | 車燈開關（選配） | `joyRight->x()` |

方向相反時改 `config.h` 的 `JOYCON_THROTTLE_INVERTED`／`JOYCON_STEER_INVERTED`（2026-11-04 實測方向正確，維持 false）。

**連線時自動歸零**：實測右 Joy-Con 放開搖桿時轉向讀值停在約 +60，超過死區，車子會自己偏（4.1.0 讀右 Joy-Con 校正資料有誤，4.2.0 才修正）。所以每支手把連上後，`gamepad.cpp` 先取 `STICK_CENTER_SAMPLE_MS`（500 ms）的平均當中心點：

- 歸零完成前這支手把不算可用，`connected = false`，車子不會動。
- 取樣期間變化超過 `STICK_CENTER_MAX_SPREAD`，或中心偏超過 `STICK_CENTER_MAX_OFFSET`，判定有人在推搖桿，序列埠印「歸零失敗：請放開搖桿」並重新取樣。
- 之後的讀值扣掉中心點，兩側再各自拉回 ±512 的滿刻度，最後才套死區 `STICK_DEADZONE`。
- **連線時不要碰搖桿。** Pro 手把也一樣會歸零。

上表已在 2026-11-04 用 `test_gamepad` 實測確認（見 `docs/testing.md`）。

**Joy-Con 震動**：Joy-Con 要先收到子指令 `0x48`（開啟震動）才會震，esp32_bluepad32 4.1.0 初始化時沒有送（Bluepad32 5.0 才補上）。`gamepad.cpp` 的 `enableJoyConVibration()` 會在 Joy-Con 連上時補送一次：

- 用 Bluepad32 內部的 `uni_hid_device_get_instance_for_address()` 找到裝置，`uni_hid_device_send_intr_report()` 送出。
- 藍牙函式只能在 BTstack 執行緒呼叫，所以透過 `btstack_run_loop_execute_on_main_thread()` 轉交。
- 只能在手把初始化完成後送（4.1.0 初始化期間收到任何子指令回覆都會跳到下一步）；`onConnected()` 是在初始化完成後才呼叫，所以時機是安全的。
- 序列埠可能會出現 Bluepad32 的「unexpected subcmd_id=0x48」訊息，這是它不認得這個回覆，無害。
- 換成 Bluepad32 5.0 以上的開發板套件後可以拿掉這段。

2026-11-04 實測：補送後 Joy-Con 會震。

### Pro 手把（備用）

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
| 手把斷線（雙 Joy-Con 時任何一支） | 立刻停車、關閉車燈喇叭、回到手動模式 |
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
