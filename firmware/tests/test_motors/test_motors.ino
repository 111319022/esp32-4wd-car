// test_motors — 馬達與 L298N 單項測試
//
// ⚠️ 測試前先把車架高，讓四個輪子懸空！
//
// 依序：左側前進 → 左側後退 → 右側前進 → 右側後退 → 全部前進 → 原地右轉，每段 2 秒，然後重複
// 用來確認：(1) 接線正確 (2) 前進方向對不對；方向相反就改 car_main/config.h 的 *_MOTOR_INVERTED
//
// 腳位必須與 firmware/car_main/config.h 相同

constexpr int PIN_ENA = 25;
constexpr int PIN_IN1 = 26;
constexpr int PIN_IN2 = 27;
constexpr int PIN_ENB = 33;
constexpr int PIN_IN3 = 32;
constexpr int PIN_IN4 = 13;

constexpr int TEST_DUTY = 180;  // 0～255
constexpr int CH_LEFT = 0;
constexpr int CH_RIGHT = 1;

void pwmAttach(int pin, int channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcAttach(pin, 1000, 8);
#else
  ledcSetup(channel, 1000, 8);
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

// value：-255～255
void side(int pinEn, int channel, int pinA, int pinB, int value) {
  digitalWrite(pinA, value > 0 ? HIGH : LOW);
  digitalWrite(pinB, value < 0 ? HIGH : LOW);
  pwmWrite(pinEn, channel, abs(value));
}

void drive(const char* label, int left, int right) {
  Serial.printf("%-12s 左=%4d 右=%4d\n", label, left, right);
  side(PIN_ENA, CH_LEFT, PIN_IN1, PIN_IN2, left);
  side(PIN_ENB, CH_RIGHT, PIN_IN3, PIN_IN4, right);
  delay(2000);
  side(PIN_ENA, CH_LEFT, PIN_IN1, PIN_IN2, 0);
  side(PIN_ENB, CH_RIGHT, PIN_IN3, PIN_IN4, 0);
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  pwmAttach(PIN_ENA, CH_LEFT);
  pwmAttach(PIN_ENB, CH_RIGHT);
  Serial.println("test_motors：3 秒後開始，請確認輪子已懸空");
  delay(3000);
}

void loop() {
  drive("左側前進", TEST_DUTY, 0);
  drive("左側後退", -TEST_DUTY, 0);
  drive("右側前進", 0, TEST_DUTY);
  drive("右側後退", 0, -TEST_DUTY);
  drive("全部前進", TEST_DUTY, TEST_DUTY);
  drive("原地右轉", TEST_DUTY, -TEST_DUTY);
}
