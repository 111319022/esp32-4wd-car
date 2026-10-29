#include "extras.h"

#include <Arduino.h>

#include "config.h"

namespace {
bool headlightOn = false;
}

void extrasBegin() {
#if FEATURE_LIGHTS
  pinMode(PIN_HEADLIGHT, OUTPUT);
  pinMode(PIN_BRAKELIGHT, OUTPUT);
#endif
#if FEATURE_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
#endif
  extrasOff();
}

void extrasUpdate(bool lightTogglePressed, bool hornHeld, bool stopped, bool reversing) {
#if FEATURE_LIGHTS
  if (lightTogglePressed) headlightOn = !headlightOn;
  digitalWrite(PIN_HEADLIGHT, headlightOn ? HIGH : LOW);
  digitalWrite(PIN_BRAKELIGHT, (stopped || reversing) ? HIGH : LOW);
#else
  (void)lightTogglePressed;
  (void)stopped;
#endif

#if FEATURE_BUZZER
  // 後退時每 0.5 秒嗶一聲
  bool reverseBeep = reversing && (millis() % 500 < 250);
  digitalWrite(PIN_BUZZER, (hornHeld || reverseBeep) ? HIGH : LOW);
#else
  (void)hornHeld;
  (void)reversing;
#endif
}

void extrasOff() {
  headlightOn = false;
#if FEATURE_LIGHTS
  digitalWrite(PIN_HEADLIGHT, LOW);
  digitalWrite(PIN_BRAKELIGHT, LOW);
#endif
#if FEATURE_BUZZER
  digitalWrite(PIN_BUZZER, LOW);
#endif
}
