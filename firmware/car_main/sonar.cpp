#include "sonar.h"

#include <Arduino.h>

#include "config.h"

namespace {

const int kTrigPins[SONAR_COUNT] = {PIN_TRIG_LEFT, PIN_TRIG_CENTER, PIN_TRIG_RIGHT};
const int kEchoPins[SONAR_COUNT] = {PIN_ECHO_LEFT, PIN_ECHO_CENTER, PIN_ECHO_RIGHT};

int distancesCm[SONAR_COUNT] = {SONAR_MAX_CM, SONAR_MAX_CM, SONAR_MAX_CM};
int nextIndex = 0;
unsigned long lastPingMs = 0;

int measureCm(int i) {
  digitalWrite(kTrigPins[i], LOW);
  delayMicroseconds(2);
  digitalWrite(kTrigPins[i], HIGH);
  delayMicroseconds(10);
  digitalWrite(kTrigPins[i], LOW);

  unsigned long echoUs = pulseIn(kEchoPins[i], HIGH, SONAR_TIMEOUT_US);
  if (echoUs == 0) return SONAR_MAX_CM;  // 逾時：前方沒有東西，或東西太遠

  // 聲速約 343 m/s，來回距離 → d(cm) ≈ t(µs) / 58
  int cm = static_cast<int>(echoUs / 58);
  return min(cm, SONAR_MAX_CM);
}

}  // namespace

void sonarBegin() {
  for (int i = 0; i < SONAR_COUNT; i++) {
    pinMode(kTrigPins[i], OUTPUT);
    digitalWrite(kTrigPins[i], LOW);
    pinMode(kEchoPins[i], INPUT);
  }
}

bool sonarUpdate() {
  unsigned long now = millis();
  if (now - lastPingMs < SONAR_INTERVAL_MS) return false;
  lastPingMs = now;

  distancesCm[nextIndex] = measureCm(nextIndex);
  nextIndex = (nextIndex + 1) % SONAR_COUNT;
  return true;
}

int sonarCm(SonarId id) {
  return distancesCm[id];
}
