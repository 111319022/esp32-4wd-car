// test_sonar_diag — 超音波接線診斷
//
// test_sonar 只會在「自己的」Echo 腳等回波；這支程式每觸發一顆 Trig，就同時監看三支 Echo 腳，
// 用來找出：哪一顆沒反應、Echo 接到了別顆的腳位，或 Echo 腳一直卡在 HIGH。
//
// 每行輸出範例：
//   Trig16(左前) 待機=0/0/0 | Echo34: 23 cm   Echo35: ---   Echo36: ---
//   - 「待機」是觸發前三支 Echo 腳（34/35/36）的電位，正常都是 0；是 1 代表那支腳一直被拉高（接線有問題）
//   - 正常：Trig16 只有 Echo34 有數字、Trig17 只有 Echo35、Trig18 只有 Echo36
//
// 用法和 test_sonar 一樣：USB 除錯接法（VIN 拔掉、GND 接著），上傳後再開電池，序列埠 115200
// ⚠️ Echo 一定要經 1kΩ + 2kΩ 分壓再接 ESP32
// 腳位必須與 firmware/car_main/config.h 相同

constexpr int TRIG[3] = {16, 17, 18};  // 左前、正前、右前
constexpr int ECHO[3] = {34, 35, 36};
const char* const NAMES[3] = {"左前", "正前", "右前"};

constexpr unsigned long LISTEN_US = 25000;  // 每次觸發後監看 25 ms（約 4 m）

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) {
    pinMode(TRIG[i], OUTPUT);
    digitalWrite(TRIG[i], LOW);
    pinMode(ECHO[i], INPUT);
  }
  Serial.println("test_sonar_diag 開始");
}

void probe(int t) {
  int idle[3];
  for (int j = 0; j < 3; j++) idle[j] = digitalRead(ECHO[j]);

  digitalWrite(TRIG[t], LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG[t], HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG[t], LOW);

  // 同時監看三支 Echo 腳：記錄每支第一次變 HIGH、再變回 LOW 的時間
  unsigned long riseUs[3] = {0, 0, 0};
  unsigned long fallUs[3] = {0, 0, 0};
  int prev[3];
  for (int j = 0; j < 3; j++) prev[j] = digitalRead(ECHO[j]);
  unsigned long t0 = micros();
  while (micros() - t0 < LISTEN_US) {
    for (int j = 0; j < 3; j++) {
      int v = digitalRead(ECHO[j]);
      if (v && !prev[j] && riseUs[j] == 0) riseUs[j] = micros();
      if (!v && prev[j] && riseUs[j] != 0 && fallUs[j] == 0) fallUs[j] = micros();
      prev[j] = v;
    }
  }

  Serial.printf("Trig%d(%s) 待機=%d/%d/%d |", TRIG[t], NAMES[t], idle[0], idle[1], idle[2]);
  for (int j = 0; j < 3; j++) {
    if (riseUs[j] == 0) {
      Serial.printf("  Echo%d: ---   ", ECHO[j]);
    } else if (fallUs[j] == 0) {
      Serial.printf("  Echo%d: 卡HIGH ", ECHO[j]);
    } else {
      Serial.printf("  Echo%d: %3lu cm ", ECHO[j], (fallUs[j] - riseUs[j]) / 58);
    }
  }
  Serial.println();
}

// 印出三支 Echo 腳和電池腳（GPIO 39）的實際電壓，用來判斷某支腳是空接、還是被別的東西拉高
//   待機時 Echo 腳正常約 0 mV；和 GPIO39 差不多（約 2000～2800 mV）→ 那支腳跟電池分壓接在一起了；
//   約 3100 mV 以上 → 接到了 3.3 V（例如 3V3 或 EN 那一行）；數字亂跳 → 腳位空接，線沒接觸到
void printPinVoltages() {
  const int pins[4] = {34, 35, 36, 39};
  Serial.print("電壓(mV)");
  for (int k = 0; k < 4; k++) {
    Serial.printf("  GPIO%d=%4u", pins[k], static_cast<unsigned>(analogReadMilliVolts(pins[k])));
  }
  Serial.println();
  // analogRead 會把腳位切到類比模式，切回數位輸入，下一輪的 digitalRead 才會正常
  for (int j = 0; j < 3; j++) pinMode(ECHO[j], INPUT);
}

void loop() {
  for (int t = 0; t < 3; t++) {
    probe(t);
    delay(60);  // 等上一顆的回音散掉，避免互相干擾
  }
  printPinVoltages();
  Serial.println();
  delay(500);
}
