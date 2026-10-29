#include "motors.h"

#include <Arduino.h>

#include "config.h"

namespace {

constexpr int CH_LEFT = 0;
constexpr int CH_RIGHT = 1;

// arduino-esp32 2.x 和 3.x 的 LEDC（PWM）API 不同，這裡兩種都支援
void pwmAttach(int pin, int channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcAttach(pin, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
#else
  ledcSetup(channel, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(pin, channel);
#endif
}

void pwmWrite(int pin, int channel, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

void driveSide(int pinEn, int channel, int pinA, int pinB, int value, bool inverted, float trim) {
  if (inverted) value = -value;
  value = constrain(static_cast<int>(value * trim), -PWM_MAX, PWM_MAX);

  // 把 1～255 映射到 MOTOR_MIN_PWM～255，避免低速時馬達只嗡嗡叫不轉
  int duty = abs(value);
  if (duty > 0) duty = map(duty, 1, PWM_MAX, MOTOR_MIN_PWM, PWM_MAX);

  digitalWrite(pinA, value > 0 ? HIGH : LOW);
  digitalWrite(pinB, value < 0 ? HIGH : LOW);
  pwmWrite(pinEn, channel, duty);
}

}  // namespace

void motorsBegin() {
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pwmAttach(PIN_ENA, CH_LEFT);
  pwmAttach(PIN_ENB, CH_RIGHT);
  motorsStop();
}

void motorsSet(int left, int right) {
  driveSide(PIN_ENA, CH_LEFT, PIN_IN1, PIN_IN2, left, LEFT_MOTOR_INVERTED, LEFT_TRIM);
  driveSide(PIN_ENB, CH_RIGHT, PIN_IN3, PIN_IN4, right, RIGHT_MOTOR_INVERTED, RIGHT_TRIM);
}

void motorsSetPercent(int leftPct, int rightPct) {
  leftPct = constrain(leftPct, -100, 100);
  rightPct = constrain(rightPct, -100, 100);
  motorsSet(leftPct * PWM_MAX / 100, rightPct * PWM_MAX / 100);
}

void motorsStop() {
  motorsSet(0, 0);
}
