// test_battery — 電池電壓量測測試
//
// 接線：電池正極 → 20kΩ → GPIO 39 → 10kΩ → GND
// 每 0.5 秒印出 GPIO 39 的電壓與換算後的電池電壓
// 校正方法：同時用三用電表量電池盒兩端，若誤差超過 0.1 V，調整 car_main/config.h 的 BATTERY_DIVIDER_RATIO
//
// 腳位必須與 firmware/car_main/config.h 相同

constexpr int PIN_BATTERY = 39;
constexpr float DIVIDER_RATIO = 3.0f;  // (20k + 10k) / 10k

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BATTERY, INPUT);
  Serial.println("test_battery 開始");
}

void loop() {
  uint32_t mv = analogReadMilliVolts(PIN_BATTERY);
  float volts = mv / 1000.0f * DIVIDER_RATIO;
  Serial.printf("GPIO39 = %4u mV  →  電池 = %.2f V（每顆約 %.2f V）\n", static_cast<unsigned>(mv), volts, volts / 2);
  delay(500);
}
