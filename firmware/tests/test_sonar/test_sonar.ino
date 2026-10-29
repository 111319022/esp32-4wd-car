// test_sonar — 三顆 HC-SR04 單項測試
//
// 三顆輪流觸發（間隔 30 ms），在序列埠印出左前／正前／右前的距離
// 校正方法：用捲尺在 10、30、50、100 cm 放紙箱，記錄每顆的讀值與誤差到 docs/testing.md
// 「---」代表沒收到回波（前方沒東西、太遠，或接線有問題）
//
// ⚠️ Echo 一定要經 1kΩ + 2kΩ 分壓再接 ESP32
// 腳位必須與 firmware/car_main/config.h 相同

constexpr int TRIG[3] = {16, 17, 18};  // 左前、正前、右前
constexpr int ECHO[3] = {34, 35, 36};
const char* const NAMES[3] = {"左前", "正前", "右前"};

constexpr unsigned long TIMEOUT_US = 20000;

long measureCm(int i) {
  digitalWrite(TRIG[i], LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG[i], HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG[i], LOW);
  unsigned long us = pulseIn(ECHO[i], HIGH, TIMEOUT_US);
  return us == 0 ? -1 : static_cast<long>(us / 58);
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) {
    pinMode(TRIG[i], OUTPUT);
    digitalWrite(TRIG[i], LOW);
    pinMode(ECHO[i], INPUT);
  }
  Serial.println("test_sonar 開始");
}

void loop() {
  long cm[3];
  for (int i = 0; i < 3; i++) {
    cm[i] = measureCm(i);
    delay(30);
  }
  for (int i = 0; i < 3; i++) {
    if (cm[i] < 0) {
      Serial.printf("%s:  ---    ", NAMES[i]);
    } else {
      Serial.printf("%s: %4ld cm  ", NAMES[i], cm[i]);
    }
  }
  Serial.println();
  delay(200);
}
