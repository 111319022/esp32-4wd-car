// logger.h — 事件與狀態 LOG：存在 ESP32 的 Flash（LittleFS），事後插 USB 下載（見 docs/decisions.md D16）
//
// - 每一行開頭是「#開機次數 開機後秒數」，例如「#12 83.415 E [gamepad] 右 Joy-Con 斷線，停車」
//   E = 事件（也會印到序列埠）、S = 狀態快照（只存 LOG，不印）
// - 寫 Flash 會讓程式卡住數 ms～數十 ms，所以先存在 RAM，等車子停住 LOG_FLUSH_STOPPED_MS 才寫入；
//   行駛中暫存快滿（LOG_FORCE_FLUSH_BYTES）才分段寫入。斷電前還沒寫入的那幾秒會不見。
// - 兩個檔案輪流用：/log.txt 超過 LOG_FILE_MAX_BYTES 就改名成 /log_old.txt（舊的刪掉），再開新檔
//
// 序列埠指令（115200，結尾換行；tools/gamepad_monitor.html 的按鈕就是送這些）：
//   log dump   把整份 LOG 送到電腦（送的期間車子強制停車）
//   log clear  清除 LOG
//   log info   顯示 LOG 大小
#pragma once

#include <stddef.h>

// 在 setup() 裡、Serial.begin() 與 motorsBegin() 之後呼叫：掛載 Flash、記下開機原因
void logBegin();

// 記一筆事件：印到序列埠（「[tag] 訊息」，和以前的 Serial.printf 一樣），同時存進 LOG
void logEvent(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// 記一行狀態快照：只存進 LOG，不印到序列埠
void logSnapshot(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// 每次 loop 呼叫一次：處理序列埠指令、停車時寫入 Flash、分段把 LOG 送到電腦
// vehicleStopped：左右輪輸出都是 0
void logUpdate(bool vehicleStopped);

// 正在把 LOG 送到電腦：主程式要停車，並暫停其他序列埠輸出（不然會混進 LOG 裡）
bool logDumping();
