#include "logger.h"

#include <Arduino.h>
#include <esp_system.h>
#include <stdarg.h>

#include "config.h"

#if FEATURE_FLASH_LOG
#include <LittleFS.h>
#include <Preferences.h>
#endif

namespace {

constexpr size_t MSG_MAX = 200;  // 單筆訊息最多幾個字元（超過會被截斷）

bool dumping = false;
unsigned long bootId = 0;

char cmd[40];
size_t cmdLen = 0;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON（開電源、按 EN 鍵或燒錄後重開）";
    case ESP_RST_EXT:
      return "EXT（外部重置腳）";
    case ESP_RST_SW:
      return "SW（程式要求重開機）";
    case ESP_RST_PANIC:
      return "PANIC（程式當機）";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "WDT（看門狗：程式卡住太久）";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP（睡眠喚醒）";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT（電壓不足：電池快沒電、接觸不良，或馬達啟動時 5V 掉下去）";
    default:
      return "UNKNOWN";
  }
}

#if FEATURE_FLASH_LOG

const char* const LOG_PATH = "/log.txt";
const char* const OLD_PATH = "/log_old.txt";

bool fsReady = false;
char* ram = nullptr;  // RAM 暫存：還沒寫進 Flash 的內容（都是完整的行）
size_t ramCap = 0;
size_t ramLen = 0;
size_t curSize = 0;  // /log.txt 目前大小
unsigned long droppedBytes = 0;
bool eventPending = false;  // 暫存裡有事件（不是只有快照）：停車後要盡快寫入
bool prevStopped = false;
unsigned long stoppedSinceMs = 0;
unsigned long lastFlushMs = 0;

enum class DumpStage { WAIT_STOP, OLD_FILE, CUR_FILE };
DumpStage dumpStage = DumpStage::WAIT_STOP;
File dumpFile;
size_t dumpSent = 0;

void appendLine(char kind, const char* text) {
  if (!ram) return;
  char line[MSG_MAX + 64];
  unsigned long ms = millis();
  int n = snprintf(line, sizeof(line), "#%lu %lu.%03lu %c %s\n", bootId, ms / 1000, ms % 1000, kind, text);
  if (n <= 0) return;
  if ((size_t)n >= sizeof(line)) {
    n = sizeof(line) - 1;
    line[n - 1] = '\n';
  }
  if (ramLen + n > ramCap) {
    droppedBytes += n;  // 暫存滿了（一直沒停車又寫不進 Flash）：丟掉，之後記一筆丟了多少
    return;
  }
  memcpy(ram + ramLen, line, n);
  ramLen += n;
}

size_t fileSize(const char* path) {
  if (!LittleFS.exists(path)) return 0;
  File f = LittleFS.open(path, FILE_READ);
  size_t size = f ? f.size() : 0;
  f.close();
  return size;
}

// 把暫存最前面最多 maxBytes 寫進 Flash（只寫完整的行）。這裡會卡住數 ms～數十 ms
void flushChunk(size_t maxBytes) {
  if (!fsReady || ramLen == 0) return;
  size_t n = min(ramLen, maxBytes);
  if (n < ramLen) {
    while (n > 0 && ram[n - 1] != '\n') n--;
    if (n == 0) n = min(ramLen, maxBytes);
  }

  // 檔案太大就換新檔：舊的 /log_old.txt 刪掉，/log.txt 改名成 /log_old.txt
  if (curSize > 0 && curSize + n > LOG_FILE_MAX_BYTES) {
    LittleFS.remove(OLD_PATH);
    LittleFS.rename(LOG_PATH, OLD_PATH);
    curSize = 0;
  }

  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) {
    if (!dumping) Serial.println("[log] 寫入 Flash 失敗：打不開 /log.txt");
    lastFlushMs = millis();
    return;
  }
  size_t written = f.write(reinterpret_cast<const uint8_t*>(ram), n);
  f.close();
  if (written != n && !dumping) Serial.printf("[log] 寫入 Flash 不完整：%u／%u bytes\n", written, n);
  curSize += written;

  memmove(ram, ram + n, ramLen - n);
  ramLen -= n;
  lastFlushMs = millis();
  if (ramLen == 0) eventPending = false;

  if (droppedBytes > 0) {
    char note[80];
    snprintf(note, sizeof(note), "[log] RAM 暫存滿了，丟掉 %lu bytes（行駛太久沒停車）", droppedBytes);
    droppedBytes = 0;
    appendLine('E', note);
    eventPending = true;
  }
}

void startDump() {
  if (dumping) return;
  if (!fsReady) {
    Serial.println("[log] Flash LOG 沒有掛載成功，沒有東西可以送");
    return;
  }
  // 先讓主程式停車（logDumping() 變成 true），等車子停住再把暫存寫進 Flash、開始送
  dumping = true;
  dumpStage = DumpStage::WAIT_STOP;
}

void finishDump() {
  dumpFile.close();
  dumpFile = File();
  Serial.printf("=== LOG DUMP END bytes=%u ===\n", dumpSent);
  dumping = false;
}

// 依序送出 /log_old.txt、/log.txt；一次最多送序列埠緩衝區放得下的量，不會卡住 loop
void pumpDump(bool vehicleStopped) {
  if (dumpStage == DumpStage::WAIT_STOP) {
    if (!vehicleStopped) return;
    flushChunk(ramCap);  // 暫存全部寫進 Flash（車子已經停了，卡一下沒關係）
    size_t total = fileSize(OLD_PATH) + curSize;
    Serial.printf("\n=== LOG DUMP BEGIN boot=%lu bytes=%u ===\n", bootId, total);
    dumpSent = 0;
    dumpStage = DumpStage::OLD_FILE;
    dumpFile = LittleFS.exists(OLD_PATH) ? LittleFS.open(OLD_PATH, FILE_READ) : File();
  }

  size_t budget = 2048;  // 每圈 loop 最多送這麼多
  while (budget > 0) {
    if (!dumpFile) {
      if (dumpStage == DumpStage::OLD_FILE) {
        dumpStage = DumpStage::CUR_FILE;
        dumpFile = LittleFS.exists(LOG_PATH) ? LittleFS.open(LOG_PATH, FILE_READ) : File();
        if (dumpFile) continue;
      }
      finishDump();
      return;
    }
    int room = Serial.availableForWrite();
    if (room < 64) return;  // 序列埠緩衝區滿了，下一圈再送
    uint8_t buf[256];
    size_t want = min(min((size_t)room, sizeof(buf)), budget);
    size_t got = dumpFile.read(buf, want);
    if (got == 0) {
      dumpFile.close();
      dumpFile = File();
      continue;
    }
    Serial.write(buf, got);
    dumpSent += got;
    budget -= got;
  }
}

void clearLog() {
  if (dumping) {
    Serial.println("[log] 正在送 LOG，不能清除");
    return;
  }
  if (fsReady) {
    LittleFS.remove(LOG_PATH);
    LittleFS.remove(OLD_PATH);
  }
  curSize = 0;
  ramLen = 0;
  droppedBytes = 0;
  eventPending = false;
  logEvent("log", "LOG 已清除");
}

void printInfo() {
  Serial.printf("[log] 開機 #%lu；Flash %s；log_old.txt %u B、log.txt %u B（每檔上限 %u B）；RAM 暫存 %u／%u B\n", bootId,
                fsReady ? "正常" : "沒有掛載", fsReady ? fileSize(OLD_PATH) : 0, curSize, LOG_FILE_MAX_BYTES, ramLen,
                ramCap);
}

#endif  // FEATURE_FLASH_LOG

void handleCommand(char* line) {
  // 去掉前後空白、轉小寫
  while (*line == ' ') line++;
  size_t len = strlen(line);
  while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
  for (char* p = line; *p; p++) *p = tolower(*p);
  if (strncmp(line, "log", 3) != 0) return;  // 不是給 LOG 的指令就不理

#if FEATURE_FLASH_LOG
  if (strcmp(line, "log dump") == 0) {
    startDump();
  } else if (strcmp(line, "log clear") == 0) {
    clearLog();
  } else if (strcmp(line, "log info") == 0) {
    printInfo();
  } else {
    Serial.println("[log] 指令：log dump（送出 LOG）、log clear（清除）、log info（大小）");
  }
#else
  Serial.println("[log] Flash LOG 沒有啟用（config.h 的 FEATURE_FLASH_LOG）");
#endif
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmd[cmdLen] = '\0';
        handleCommand(cmd);
        cmdLen = 0;
      }
    } else if (cmdLen < sizeof(cmd) - 1) {
      cmd[cmdLen++] = c;
    }
  }
}

}  // namespace

void logBegin() {
#if FEATURE_FLASH_LOG
  ram = static_cast<char*>(malloc(LOG_RAM_BUFFER_BYTES));
  ramCap = ram ? LOG_RAM_BUFFER_BYTES : 0;

  Preferences prefs;
  if (prefs.begin("carlog", false)) {
    bootId = prefs.getULong("boot", 0) + 1;
    prefs.putULong("boot", bootId);
    prefs.end();
  }

  fsReady = LittleFS.begin(false);
  if (!fsReady) {
    Serial.println("[log] 第一次使用 Flash LOG（或內容壞掉），正在格式化，可能要等幾十秒...");
    fsReady = LittleFS.begin(true);
  }
  if (fsReady) curSize = fileSize(LOG_PATH);

  logEvent("log", "開機 #%lu，重開機原因：%s；Flash LOG %s，已用 %u KB；RAM 暫存 %u B；可用記憶體 %u B", bootId,
           resetReasonName(esp_reset_reason()), fsReady ? "正常" : "掛載失敗（只印序列埠）",
           fsReady ? (fileSize(OLD_PATH) + curSize) / 1024 : 0, ramCap, ESP.getFreeHeap());
#else
  logEvent("log", "開機，重開機原因：%s（Flash LOG 未啟用）", resetReasonName(esp_reset_reason()));
#endif
}

void logEvent(const char* tag, const char* fmt, ...) {
  char msg[MSG_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (!dumping) Serial.printf("[%s] %s\n", tag, msg);
#if FEATURE_FLASH_LOG
  char text[MSG_MAX + 24];
  snprintf(text, sizeof(text), "[%s] %s", tag, msg);
  appendLine('E', text);
  eventPending = true;
#endif
}

void logSnapshot(const char* fmt, ...) {
#if FEATURE_FLASH_LOG
  char msg[MSG_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  appendLine('S', msg);
#else
  (void)fmt;
#endif
}

void logUpdate(bool vehicleStopped) {
  pollSerial();
#if FEATURE_FLASH_LOG
  if (dumping) {
    pumpDump(vehicleStopped);
    return;
  }

  unsigned long now = millis();
  if (vehicleStopped && !prevStopped) stoppedSinceMs = now;
  prevStopped = vehicleStopped;
  if (ramLen == 0) return;

  bool stoppedLongEnough = vehicleStopped && now - stoppedSinceMs >= LOG_FLUSH_STOPPED_MS;
  bool worthWriting = eventPending || ramLen >= LOG_FLUSH_MIN_BYTES || now - lastFlushMs >= LOG_FLUSH_MAX_WAIT_MS;
  if ((stoppedLongEnough && worthWriting) || ramLen >= LOG_FORCE_FLUSH_BYTES) {
    flushChunk(LOG_FLUSH_CHUNK_BYTES);
  }
#else
  (void)vehicleStopped;
#endif
}

bool logDumping() {
  return dumping;
}
