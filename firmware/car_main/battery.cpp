#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace {

float smoothedVolts = 0.0f;
unsigned long lastSampleMs = 0;

float readVolts() {
  // analogReadMilliVolts 會套用 ESP32 出廠校正，比 analogRead 準
  return analogReadMilliVolts(PIN_BATTERY) / 1000.0f * BATTERY_DIVIDER_RATIO;
}

}  // namespace

void batteryBegin() {
#if FEATURE_BATTERY_MONITOR
  pinMode(PIN_BATTERY, INPUT);
  smoothedVolts = readVolts();
#endif
}

void batteryUpdate() {
#if FEATURE_BATTERY_MONITOR
  unsigned long now = millis();
  if (now - lastSampleMs < 100) return;
  lastSampleMs = now;
  // 馬達起步時電壓會瞬間下降，用平滑值判斷，避免誤判成沒電
  smoothedVolts = smoothedVolts * 0.9f + readVolts() * 0.1f;
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
  return smoothedVolts < BATTERY_CRITICAL_V;
#else
  return false;
#endif
}
