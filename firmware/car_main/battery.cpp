#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace {

float smoothedVolts = 0.0f;
unsigned long lastSampleMs = 0;
// 一旦鎖定就不再解除，必須重開機（停車後電壓會回升，不鎖定的話車子會停停走走）
bool criticalLatched = false;
unsigned long notCriticalSinceMs = 0;  // 最近一次「不在危險電壓範圍」的時間

float readVolts() {
  // analogReadMilliVolts 會套用 ESP32 出廠校正，比 analogRead 準
  return analogReadMilliVolts(PIN_BATTERY) / 1000.0f * BATTERY_DIVIDER_RATIO;
}

}  // namespace

void batteryBegin() {
#if FEATURE_BATTERY_MONITOR
  pinMode(PIN_BATTERY, INPUT);
  smoothedVolts = readVolts();
  notCriticalSinceMs = millis();
#endif
}

void batteryUpdate() {
#if FEATURE_BATTERY_MONITOR
  unsigned long now = millis();
  if (now - lastSampleMs < 100) return;
  lastSampleMs = now;
  // 馬達起步時電壓會瞬間下降，用平滑值判斷，避免誤判成沒電
  smoothedVolts = smoothedVolts * 0.9f + readVolts() * 0.1f;

  // 低於 BATTERY_ABSENT_V 視為沒接電池（例如只插 USB、電池開關關著），不鎖定；
  // 打開電池開關時平滑值會從 0 慢慢爬升，要連續低於門檻 BATTERY_CRITICAL_HOLD_MS 才鎖定，避免誤判
  bool inCriticalRange = smoothedVolts >= BATTERY_ABSENT_V && smoothedVolts < BATTERY_CRITICAL_V;
  if (!inCriticalRange) {
    notCriticalSinceMs = now;
  } else if (!criticalLatched && now - notCriticalSinceMs >= BATTERY_CRITICAL_HOLD_MS) {
    criticalLatched = true;
    Serial.printf("[battery] 電壓 %.2f V 過低，停車鎖定；請關機充電，重開機才會解除\n", smoothedVolts);
  }
#endif
}

float batteryVolts() {
  return smoothedVolts;
}

bool batteryLow() {
#if FEATURE_BATTERY_MONITOR
  return smoothedVolts < BATTERY_LOW_V;
#else
  return false;
#endif
}

bool batteryCritical() {
#if FEATURE_BATTERY_MONITOR
  return criticalLatched || smoothedVolts < BATTERY_CRITICAL_V;
#else
  return false;
#endif
}
