// battery.h — 2S 18650 電池電壓監測（GPIO 39，20kΩ + 10kΩ 分壓）
#pragma once

void batteryBegin();

// 每次 loop 呼叫；內部每 100 ms 取樣一次並做平滑
void batteryUpdate();

float batteryVolts();
bool batteryLow();       // 低於 BATTERY_LOW_V：該充電了
bool batteryCritical();  // 低於 BATTERY_CRITICAL_V：必須停車；持續過低會鎖定，重開機才解除
