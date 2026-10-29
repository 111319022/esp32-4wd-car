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
4. **工具 → 開發板 → esp32_bluepad32 → ESP32 Dev Board**。
   不要選到 Espressif 官方 esp32 套件裡的「ESP32 Dev Module」，那個沒有 Bluepad32。

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

燒錄時請先**關掉車上的電池開關**，只用 USB 供電。

## 5. 配對 Switch 手把

| 手把 | 進入配對模式 |
|---|---|
| Switch Pro 手把 | 按住頂部 USB-C 旁的小同步鍵，直到燈號來回跑 |
| Joy-Con | 按住側邊滑軌上的小同步鍵 |

ESP32 開機後會自動搜尋並連線，序列埠會印出「手把已連線」。之後重新開機通常會自動重連。

## 常見問題

| 狀況 | 處理 |
|---|---|
| 上傳時出現 `Failed to connect to ESP32` | 上傳開始時按住板子上的 **BOOT** 鍵，看到「Connecting...」後放開 |
| 找不到連接埠 | 換一條能傳資料的 USB 線；安裝上面的 USB 驅動 |
| 編譯錯誤說找不到 `Bluepad32.h` | 開發板沒有選到 esp32_bluepad32 套件裡的板子 |
| 手把一直連不上或重連失敗 | 在 `gamepad.cpp` 暫時打開 `BP32.forgetBluetoothKeys();`，上傳一次後再關掉；手把也重新配對 |
| ESP32 一直重開機 | 通常是電源不穩：檢查共地、電容、電池電量 |
| 車子完全不動，序列埠顯示 bat 很低 | 電池分壓還沒接：先把 `FEATURE_BATTERY_MONITOR` 設為 0 |
