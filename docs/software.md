# 韌體架構與控制邏輯

> 狀態（2026-11-25）：`car_main` 已用 esp32_bluepad32 4.1.0 編譯。2026-11-25 改成一次只用一支手把（D15）並加入 Flash LOG（D16），這兩項只有編譯通過、還沒燒錄實測。手把的右 Joy-Con 按鍵、震動、搖桿歸零在 2026-11-04 實測通過；馬達方向 2026-11-20 實測通過。

## 模組

| 檔案 | 負責 | 對外函式 |
|---|---|---|
| `car_main.ino` | setup／loop、模式切換、手動混控、防撞輔助、電池保護、除錯輸出 | — |
| `config.h` | 腳位、功能開關、所有可調參數 | — |
| `motors.*` | L298N 坦克式驅動 | `motorsBegin`、`motorsSet`、`motorsSetPercent`、`motorsStop` |
| `sonar.*` | 三顆 HC-SR04 輪流測距 | `sonarBegin`、`sonarUpdate`、`sonarCm` |
| `gamepad.*` | Bluepad32 手把：一次只用一支（一支 Joy-Con 或一支大手把）、連上後停止搜尋新手把、搖桿歸零、補送 Joy-Con 震動指令 | `gamepadBegin`、`gamepadUpdate`、`gamepadState`、`gamepadRumble`、`gamepadSlotStatus`、`gamepadShortName`、`gamepadTakeUpdateCount` |
| `autopilot.*` | 自動避障狀態機 | `autopilotReset`、`autopilotStep`、`autopilotStateName` |
| `battery.*` | 電池電壓監測 | `batteryBegin`、`batteryUpdate`、`batteryVolts`、`batteryLow`、`batteryCritical` |
| `extras.*` | 選配車燈、喇叭 | `extrasBegin`、`extrasUpdate`、`extrasOff` |
| `logger.*` | 事件與狀態快照 LOG：存在 Flash（LittleFS），序列埠指令 `log dump`／`log clear`／`log info` | `logBegin`、`logEvent`、`logSnapshot`、`logUpdate`、`logDumping` |

## 主迴圈

每圈 loop 依序：

1. `gamepadUpdate()`：讀手把
2. `sonarUpdate()`：距離上次量測滿 30 ms 才量下一顆（左 → 中 → 右），每顆約每 90 ms 更新一次
3. `batteryUpdate()`：每 100 ms 取樣、平滑
4. 依模式算出左右輪速度（%）
5. 電池保護覆寫速度
6. `motorsSetPercent()` 輸出到 L298N
7. 記 LOG：第一次出現的電池警告、狀態快照（可以開車時每 1 秒、沒手把時每 10 秒）
8. 每 200 ms 在序列埠印出給人看的狀態（`DEBUG_SERIAL`），每 100 ms 印一行給監看網頁讀的 `GP ...`（`DEBUG_STREAM`），見下方「除錯輸出」；正在送 LOG 時不印
9. `logUpdate()`：處理序列埠指令；車子停住 1 秒後把 LOG 暫存寫進 Flash；分段送出 LOG

**不可長時間阻塞**：超音波的 `pulseIn` 最多阻塞約 20 ms，這已經是上限。需要計時的動作（例如後退 0.5 秒）一律用 `millis()` 寫成狀態機。

## 手把對應

**一次只用一支手把**（見 decisions.md D15）：一支 Joy-Con（左或右都可以，直握），或一支**大手把**（Switch Pro、Xbox、PS4／PS5 等雙搖桿手把，程式裡沿用 `pro`／`PAD_SLOT_PRO` 這個名字）。

- 已經有一支手把時，第二支一律斷開（LOG 會記「拒絕 …一次只能用一支手把」）。
- 連上後呼叫 `BP32.enableNewBluetoothConnections(false)`，停止搜尋新手把，藍牙連線比較穩、延遲比較小；那支斷線後再打開搜尋。
- 要換手把：先關掉目前這支，再讓新的進入配對模式。
- 連上後先**自動歸零**（見下方），再等**搖桿回到中間**（T、S 都是 0）才能開車，避免連線瞬間搖桿剛好被推著就暴衝。
- 斷線就立刻停車並回到手動。

### Joy-Con（一支，直握）

同一支搖桿上下是油門、左右是轉向。Bluepad32 把單支 Joy-Con 當成「橫握」的小手把，軸向和按鍵都轉了 90 度，所以程式讀的軸和直覺不同：

| 功能 | 右 Joy-Con | 左 Joy-Con |
|---|---|---|
| 油門 T（往上推前進） | `axisX()`（推算，待實測） | `-axisX()`（2026-11-04 實測） |
| 轉向 S（往右推右轉） | `axisY()`（2026-11-04 實測） | `-axisY()`（推算，待實測） |
| 加速（按住，85% → 100%） | ZR：`r2()`（實測） | ZL：`r2()` 或 L：`l2()`（2026-11-25 實測） |
| 切換手動／自動 | 「＋」：`miscStart()`（實測） | 「−」：`miscSelect()`（待實測） |
| 喇叭（選配） | A：`a()`（實測） | `a()`（對應哪顆鍵待實測） |
| 車燈開關（選配） | B：`x()`（實測） | `x()`（對應哪顆鍵待實測） |

方向相反就改 `config.h` 的 `JOYCON_LEFT_THROTTLE_INVERTED`、`JOYCON_LEFT_STEER_INVERTED`、`JOYCON_RIGHT_THROTTLE_INVERTED`、`JOYCON_RIGHT_STEER_INVERTED`。「推算」的兩個方向是依 Bluepad32 把 Joy-Con 轉 90 度的關係推出來的。

**Joy-Con 震動**：Joy-Con 要先收到子指令 `0x48`（開啟震動）才會震，esp32_bluepad32 4.1.0 初始化時沒有送（Bluepad32 5.0 才補上）。`gamepad.cpp` 的 `enableJoyConVibration()` 會在 Joy-Con 連上時補送一次：

- 用 Bluepad32 內部的 `uni_hid_device_get_instance_for_address()` 找到裝置，`uni_hid_device_send_intr_report()` 送出。
- 藍牙函式只能在 BTstack 執行緒呼叫，所以透過 `btstack_run_loop_execute_on_main_thread()` 轉交。
- 只能在手把初始化完成後送（4.1.0 初始化期間收到任何子指令回覆都會跳到下一步）；`onConnected()` 是在初始化完成後才呼叫，所以時機是安全的。
- 序列埠可能會出現 Bluepad32 的「unexpected subcmd_id=0x48」訊息，這是它不認得這個回覆，無害。
- 換成 Bluepad32 5.0 以上的開發板套件後可以拿掉這段。

2026-11-04 實測：補送後 Joy-Con 會震。

### 大手把（Switch Pro、Xbox、PS4／PS5）

| 功能 | Switch Pro | Xbox | PS4／PS5 | 程式 |
|---|---|---|---|---|
| 油門 T | 左搖桿上下 | 左搖桿上下 | 左搖桿上下 | `-axisY()`（往上推是負值，所以取負號） |
| 轉向 S | 右搖桿左右 | 右搖桿左右 | 右搖桿左右 | `axisRX()` |
| 加速（按住，85% → 100%） | ZR | RT | R2 | `r2()` 或 `throttle() > 512` |
| 切換手動／自動 | ＋ | ≡ 選單鍵 | OPTIONS | `miscStart()` |
| 喇叭（選配） | 位置在下方的鍵（印 B） | A | ✕ | `a()` |
| 車燈開關（選配） | 位置在右方的鍵（印 A） | B | ○ | `b()` |

Bluepad32 依 Xbox 的「按鍵位置」對應，所以 Xbox 手把的 A/B 就是 `a()`/`b()`；其他手把以位置為準。以上對應都還沒實測，請用 `test_gamepad` 確認後更新本表。新款 Xbox 手把（Series X|S、Elite 2、韌體更新過的 One S）用 BLE 連線，esp32_bluepad32 4.1.0 預設有開啟 BLE。

### 連線時自動歸零

實測右 Joy-Con 放開搖桿時轉向讀值停在約 +60，超過死區，車子會自己偏（4.1.0 讀右 Joy-Con 校正資料有誤，4.2.0 才修正）。所以手把連上後，`gamepad.cpp` 先取 `STICK_CENTER_SAMPLE_MS`（500 ms）的平均當中心點：

- 歸零完成前不能開車（`connected = false`）。
- 取樣期間變化超過 `STICK_CENTER_MAX_SPREAD`，或中心偏超過 `STICK_CENTER_MAX_OFFSET`，判定有人在推搖桿，序列埠印「歸零失敗：請放開搖桿」並重新取樣。
- 之後的讀值扣掉中心點；離中心小於死區 `STICK_DEADZONE`（60）當作 0；超過死區後，在「死區～推到底」之間線性放大到 ±512。「推到底」是 `STICK_FULL_TRAVEL_JOYCON`（340）或 `STICK_FULL_TRAVEL_STANDARD`（480）：實測 Joy-Con 推到底離中心只有約 ±380，以前假設 ±512，所以永遠推不到全速（往前 90%、往後 64%）。

### 失聯保護（D17）

Joy-Con、Switch Pro 連線時大約每秒送 60 次資料。超過 `PAD_DATA_TIMEOUT_MS`（500 ms）沒收到新資料，就當作失聯、停車（`connected = false`），LOG 記「超過 500 ms 沒有送資料」；恢復送資料後要等搖桿回中才能再開。實測遇過手把資料停了 70 秒、Bluepad32 卻沒有回報斷線，這時讀到的是最後一筆搖桿值，不停車的話車子會照最後的指令一直跑。Xbox、PS 手把可能只在按鍵變化時才送資料，所以不套用。
- **連線時不要碰搖桿。** 大手把也一樣會歸零。

## 模式

```
         按切換鍵（＋／−／選單鍵）
  手動模式 ◀────────▶ 自動避障模式
     ▲                    │
     └── 手把斷線 ◀────────┘（任何模式下斷線：立刻停車並回到手動）
```

### 手動模式：坦克式混控

```
左輪 = T + S
右輪 = T − S
```

T、S 是歸零、死區之後的搖桿值（−512～512），先換算成 −100～100%，混控結果限制在 −100～100%，再乘上速度上限（平常 `SPEED_LIMIT_NORMAL_PCT` 85%，按住加速鍵 100%）。連線時的自動歸零加上死區 `STICK_DEADZONE`，避免放開搖桿時車子緩慢飄移。

### 防撞輔助（手動模式）

- 正前方 < `ASSIST_STOP_CM`（25 cm；速度上限調高後由 15 加大）且駕駛想前進：油門歸零、手把震動。後退與原地轉向仍可操作。
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
| 手把斷線、還在歸零，或剛連上搖桿還沒回中 | 立刻停車、關閉車燈喇叭、回到手動模式 |
| 正在把 LOG 送到電腦（`log dump`） | 停車、回到手動，送完才恢復 |
| 電池 < `BATTERY_LOW_V`（7.0 V） | 手把震動提醒 |
| 電池 < `BATTERY_CRITICAL_V`（6.6 V） | 強制停車並震動 |
| 電池連續 < 6.6 V 達 `BATTERY_CRITICAL_HOLD_MS`（3 秒） | **鎖定停車**，重開機才解除 |

電池電壓有平滑處理，馬達起步時的瞬間壓降不會被誤判成沒電。

為什麼要鎖定：停車後電池不再出力，電壓會回升到 6.6 V 以上；不鎖定的話車子會「跑 → 壓降 → 停 → 回升 → 又跑」反覆循環，持續過度放電。鎖定後序列埠會印出 `[battery] ... 停車鎖定`。
電壓低於 `BATTERY_ABSENT_V`（5.0 V）視為沒接電池（例如燒錄時只插 USB、電池開關關著），這時不會開車但也不鎖定，打開電池開關就能正常使用。

## 除錯輸出與手把監看網頁

`tools/gamepad_monitor.html` 是測試用的網頁：用電腦版 **Chrome 或 Edge** 直接打開檔案，按「連接 ESP32」選序列埠，就會即時顯示手把的狀態、搖桿值、按鍵、判讀結果（向前、原地右轉……）、左右輪速度、超音波距離與電池，並可下載 LOG。沒有 ESP32 時可以按「示範模式」看畫面。Safari、Firefox 不支援 Web Serial。使用網頁時，Arduino IDE 的序列埠監控視窗要關掉（同一個序列埠只能被一個程式打開）。

`car_main` 每 `DEBUG_STREAM_MS`（100 ms）印一行，空白分隔的 `key=value`：

```
GP pad=1 jl=2 jr=2 pro=0 mode=MAN auto=CRUISE T=0 S=0 zr=0 a=0 b=0 plus=0 cl=0 cr=0 ol=0 or=0 dl=300 dc=300 dr=300 bat=0.43 low=1 crit=1
```

| 欄位 | 意義 |
|---|---|
| `pad` | 1 = 可以開車（手把就緒，且連上後搖桿已回中） |
| `jl`／`jr`／`pro` | 左 Joy-Con／右 Joy-Con／大手把（Switch Pro、Xbox、PS）：0 未連線、1 歸零中、2 就緒；同一時間最多只有一個不是 0 |
| `mode`／`auto` | `MAN` 手動／`AUTO` 自動；自動避障狀態機目前狀態 |
| `T`／`S` | 歸零、死區之後的油門／轉向（−512～512） |
| `zr`／`a`／`b`／`plus` | 加速鍵、喇叭鍵、車燈鍵、切換鍵是否按住（欄位名稱沿用右 Joy-Con 的按鍵） |
| `cl`／`cr` | 左右輪速度指令（%），電池保護覆寫**之前** |
| `ol`／`or` | 實際輸出到 L298N 的速度（%） |
| `dl`／`dc`／`dr` | 左前／正前／右前距離（cm），300 = 沒有回波 |
| `bat`／`low`／`crit` | 電池電壓；是否低於 `BATTERY_LOW_V`；是否觸發停車保護 |

只插 USB 沒接電池時 `crit=1`，`ol`／`or` 會一直是 0，看 `cl`／`cr` 就知道車子本來要怎麼走。要改欄位時，`car_main.ino` 和網頁的 `parseGp()` 要一起改。

## Flash LOG（`logger.*`，見 decisions.md D16）

車子用電池跑的時候沒接電腦，所以 `car_main` 把事件和狀態快照存在 ESP32 的 Flash（LittleFS），斷電也不會消失，事後插 USB 下載。操作步驟見 `docs/operation.md`。

每行格式：`#開機次數 開機後秒數 種類 內容`，種類 `E` = 事件、`S` = 狀態快照。

```
#12 0.418 E [log] 開機 #12，重開機原因：BROWNOUT（電壓不足：…）；Flash LOG 正常，已用 37 KB；…
#12 6.902 E [gamepad] 右 Joy-Con 已連線：Switch JoyCon Right（model=40）；停止搜尋其他手把。連線時先不要碰搖桿
#12 7.410 E [gamepad] 右 Joy-Con 搖桿歸零完成：油門中心 3、轉向中心 55
#12 9.001 S pad=1 c=JR m=MAN a=CRUISE T=312 S=0 zr=0 o=36/36 d=120/85/300 b=7.62 hz=66 lp=21
#12 12.733 E [assist] 正前 14 cm，禁止前進
```

| 事件標籤 | 什麼時候記 |
|---|---|
| `[log]` | 開機（含**重開機原因**：POWERON 開電源／EN、BROWNOUT 電壓不足、PANIC 當機、WDT 卡住）、LOG 清除、RAM 暫存滿了丟掉多少 |
| `[gamepad]` | 手把連線／斷線／被拒絕、歸零完成或失敗、搖桿回中可以開車 |
| `[mode]` | 手動／自動切換 |
| `[assist]` | 防撞輔助開始禁止前進（含正前距離） |
| `[auto]` | 自動避障狀態變化（CRUISE／BACKUP／TURN，含三個距離） |
| `[battery]` | 開機後第一次出現的電池狀態（沒接電池／低電量／停車保護），以及停車鎖定 |

快照欄位：`pad` 可以開車、`c` 手把（`JL`／`JR`／`STD`，`-` = 沒有）、`m` 模式、`a` 避障狀態、`T`／`S` 搖桿、`zr` 加速、`o` 左／右輪實際輸出 %、`d` 左前／正前／右前 cm、`b` 電池 V、**`hz` 手把每秒送來幾次新資料**（明顯變低 = 藍牙不穩、延遲）、**`lp` 這段時間 loop 最久跑了幾 ms**（大於 30 代表有東西卡住，寫 Flash 的那一秒會比較高）。

寫入規則（參數在 `config.h` 的「Flash LOG」）：

- 先存在 RAM 暫存（`LOG_RAM_BUFFER_BYTES` 16 KB）。車子停住 `LOG_FLUSH_STOPPED_MS`（1 秒）後，有事件、或暫存超過 512 B、或距上次寫入超過 5 秒，就分段寫進 Flash（每圈最多 `LOG_FLUSH_CHUNK_BYTES` 1 KB）。
- 行駛中不寫（寫 Flash 會卡住數 ms～數十 ms）；暫存超過 `LOG_FORCE_FLUSH_BYTES`（14 KB，約 3～4 分鐘不停車）才強制寫。真的滿了就丟掉新的，寫入時記一筆丟了多少。
- `/log.txt` 超過 `LOG_FILE_MAX_BYTES`（128 KB）就改名成 `/log_old.txt`，舊的刪掉；最多約 256 KB，可以開車時約 1 小時。
- 還沒寫進 Flash 的會在斷電時不見，所以**停車後等 1～2 秒再關電源**。

序列埠指令（115200，Arduino IDE 序列埠監控視窗也能打，結尾要換行）：

| 指令 | 作用 |
|---|---|
| `log dump` | 先停車、把暫存寫進 Flash，再送出 `=== LOG DUMP BEGIN boot=… bytes=… ===`、整份 LOG、`=== LOG DUMP END … ===`。送的期間車子強制停車、其他輸出暫停；分段送，不會卡住 loop |
| `log info` | 顯示開機次數、兩個檔案大小、RAM 暫存用量 |
| `log clear` | 刪除兩個檔案與暫存 |

監看網頁的「下載車上 LOG」就是送 `log dump`，收完存成 `car_flash_log_<時間>.txt`。

## 參數調整指南

所有參數都在 `config.h`。調整後請把結果記到 `docs/testing.md`。

| 現象 | 調整 |
|---|---|
| 某一側前後相反 | `LEFT_MOTOR_INVERTED`／`RIGHT_MOTOR_INVERTED` 改 true |
| 車子走不直 | 較快那側的 `*_TRIM` 調小（例如 0.95） |
| 低速時馬達只嗡嗡叫不轉 | `MOTOR_MIN_PWM` 調大 |
| 放開搖桿車子還會動 | 先確認連線時沒碰搖桿（看監看網頁或序列埠的歸零中心）；仍會動再把 `STICK_DEADZONE` 調大 |
| 一直「歸零失敗」 | 手把放桌上別碰；仍失敗就把 `STICK_CENTER_MAX_SPREAD` 調大 |
| 防撞煞車太晚撞到牆 | `ASSIST_STOP_CM` 調大，或 `SPEED_LIMIT_NORMAL_PCT` 調小 |
| 搖桿推到底還不是全速（監看網頁的 T／S 到不了 ±512） | `STICK_FULL_TRAVEL_JOYCON`／`STICK_FULL_TRAVEL_STANDARD` 調小 |
| 車子整體太慢 | 先看電池（低於 7.4 V 就充電；L298N 本身會吃掉約 2 V），再把 `SPEED_LIMIT_NORMAL_PCT` 調高 |
| 自動模式太早轉彎、在空曠處也一直轉 | `AUTO_FRONT_CM` 調小 |
| 自動模式原地轉不動 | `AUTO_TURN_PCT` 調大 |
| 超音波讀值亂跳 | 確認接線與分壓；之後可加中位數濾波（待辦） |
| 手把操作有延遲 | 下載 Flash LOG 看快照的 `hz`（手把更新次數）和 `lp`（loop 最長耗時）：`hz` 低是藍牙問題（距離、干擾、電量），`lp` 高是程式卡住 |
| LOG 一直出現「RAM 暫存滿了」 | 自動模式跑很久沒停車：`LOG_RAM_BUFFER_BYTES` 調大，或 `LOG_SNAPSHOT_DRIVE_MS` 調長 |

## 待辦

- [x] 第一次編譯 `car_main`（2026-11-04，無錯誤）
- [x] 用 `test_gamepad` 確認雙 Joy-Con 的 A/B、ZR、「＋」、搖桿方向與震動（2026-11-04）
- [ ] 燒錄 D15／D16 版 `car_main`：單支 Joy-Con 與大手把能開、第二支被拒絕、延遲有沒有改善；Flash LOG 能下載
- [ ] 用 `test_gamepad` 確認左 Joy-Con、大手把（Xbox、PS、Switch Pro）的搖桿方向與按鍵對應
- [ ] 超音波加中位數濾波（取最近 3 次的中位數），減少偶發的錯誤讀值
- [ ] 自動模式加「手動接管」：自動模式中推搖桿就暫時改為手動
- [ ] （選配）啟用車燈、喇叭並實測
