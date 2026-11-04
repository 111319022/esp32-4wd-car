# 開發環境安裝與燒錄

## 1. 安裝 Arduino IDE

到 Arduino 官網下載 **Arduino IDE 2.x** 並安裝。

## 2. 安裝 Bluepad32 開發板套件

Bluepad32 不是一般的函式庫，而是一整套 ESP32 開發板套件，所以安裝方式和一般函式庫不同。

1. 開啟 Arduino IDE → **檔案 → 偏好設定**（macOS：**Arduino IDE → Settings**）。
2. 在「**額外的開發板管理員網址**」貼上（已有其他網址就用逗號分隔）：

   ```
   https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
   ```

3. 開啟 **工具 → 開發板 → 開發板管理員**，搜尋 `bluepad32`，安裝 **esp32_bluepad32**（本專案使用 4.1.0 版）。套件約 200 MB，下載需要一點時間。
4. **工具 → 開發板 → esp32_bluepad32 → ESP32 Dev Module**（FQBN：`esp32-bluepad32:esp32:esp32`）。
   兩個套件裡都有叫「ESP32 Dev Module」的板子，一定要選 **esp32_bluepad32** 選單底下的那個；選到 Espressif 官方 esp32 套件裡的會沒有 Bluepad32。

## 3. 安裝 USB 驅動（電腦認不到板子時）

ESP32 板子上的 USB 轉序列晶片通常是下列其中一種，看板子上的晶片字樣：

| 晶片 | 驅動 |
|---|---|
| CP2102／CP2104 | Silicon Labs「CP210x USB to UART Bridge VCP」驅動 |
| CH340／CH9102 | WCH「CH340／CH341」驅動 |

## 4. 燒錄

1. 用**能傳資料**的 USB 線接上 ESP32（只能充電的線會認不到板子）。
2. **工具 → 連接埠**，選 ESP32 的序列埠。
3. 打開要上傳的 sketch：
   - 主程式：`firmware/car_main/car_main.ino`
   - 測試：`firmware/tests/<測試名稱>/<測試名稱>.ino`
4. 按「上傳」。
5. **工具 → 序列埠監控視窗**，鮑率設 **115200**。

插 USB 時，USB 和 L298N 的 5 V 不可以同時接在 ESP32 上：

- 只燒錄、不需要馬達或感測器：**關掉車上的電池開關**，只用 USB 供電。
- 需要電池通電又要看序列埠（`test_motors`、`test_sonar`、`test_battery`、整合測試）：先**拔掉「L298N 5V → ESP32 VIN」那條線**（GND 保持接著）再開電池。切換順序見 `docs/hardware.md` 的「插 USB 時的兩種接法」。

## 5. 配對 Switch 手把

本車主要用**左右兩支 Joy-Con**（一手一支直握），也可以只用一支 Pro 手把當備用。

| 手把 | 進入配對模式 |
|---|---|
| Joy-Con（左、右各做一次） | 從 Switch 主機或握把上拆下來，按住側邊滑軌上的小同步鍵，直到燈號來回跑 |
| Switch Pro 手把（備用） | 按住頂部 USB-C 旁的小同步鍵，直到燈號來回跑 |

ESP32 開機後會自動搜尋並連線。兩支 Joy-Con 先後配對，序列埠會依序印出「右 Joy-Con 已連線」「左 Joy-Con 已連線」「兩支 Joy-Con 都到齊」，以及每支的「搖桿歸零完成」。**只連上一支，或還在歸零時，車子不會動。**之後重新開機通常按任一鍵就會自動重連。

配對前先讓 Switch 主機關機或開飛航模式，不然 Joy-Con 可能優先連回主機。

**配對順序（實測）**：左 Joy-Con 已經連上時，右 Joy-Con 常常配不上。請**先關掉左 Joy-Con**（按住同步鍵約 3 秒到燈全滅），讓**右 Joy-Con 先單獨配對**，連上後再配左邊。

**每支連上後放著 5 秒不要碰搖桿**：程式會在這段時間自動歸零搖桿中心。序列埠出現「歸零失敗：請放開搖桿」就是有碰到，放開後會自動重試。

## 6. 手把監看網頁

`tools/gamepad_monitor.html` 可以即時看手把讀值與車子的判讀結果，方便測試：

1. 燒錄 `car_main`，**關掉 Arduino IDE 的序列埠監控視窗**（序列埠同時只能給一個程式用）。
2. 用電腦版 **Chrome 或 Edge** 打開這個檔案（Safari、Firefox 不支援 Web Serial）。
3. 按「連接 ESP32」，選 ESP32 的序列埠（macOS 上是 `cu.usbserial-xxxx`）；需要看開機訊息時按「重開機 ESP32」。
4. 配對手把後，畫面會顯示每支手把的狀態（未連線／歸零中／就緒）、油門 T 與轉向 S、按鍵、判讀結果（向前、原地右轉……）、左右輪速度、距離與電池；頁面下方有每個按鍵的功能對照表，LOG 可以下載。

沒有 ESP32 時可以按「示範模式」用假資料看畫面。資料格式見 `docs/software.md` 的「除錯輸出」。

## 常見問題

| 狀況 | 處理 |
|---|---|
| 上傳時出現 `Failed to connect to ESP32` | 上傳開始時按住板子上的 **BOOT** 鍵，看到「Connecting...」後放開 |
| 上傳時出現 `Invalid head of packet` | **工具 → Upload Speed** 改成 115200 |
| 監看網頁連上了但數值不動 | 看「手把」卡片：兩支 Joy-Con 都要是「就緒」才會開始讀搖桿 |
| 右 Joy-Con 一直配不上 | 先關掉左 Joy-Con、按板子 EN 鍵重開機，讓右邊單獨配對，再配左邊 |
| 只插 USB 時 Joy-Con 一直小震動 | 沒接電池，低電量警告在震，屬正常 |
| 找不到連接埠 | 換一條能傳資料的 USB 線；安裝上面的 USB 驅動 |
| 編譯錯誤說找不到 `Bluepad32.h` | 開發板沒有選到 esp32_bluepad32 套件裡的板子 |
| 手把一直連不上或重連失敗 | 在 `gamepad.cpp` 暫時打開 `BP32.forgetBluetoothKeys();`，上傳一次後再關掉；手把也重新配對 |
| ESP32 一直重開機 | 通常是電源不穩：檢查共地、電容、電池電量 |
| 車子完全不動，序列埠顯示 bat 很低 | 電池分壓還沒接：先把 `FEATURE_BATTERY_MONITOR` 設為 0 |
