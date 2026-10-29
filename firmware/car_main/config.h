// config.h — 全車設定的唯一來源：腳位、功能開關、可調參數
//
// 修改腳位時，請同步更新：
//   1. docs/hardware.md 的腳位表
//   2. firmware/tests/ 底下各測試程式開頭的腳位常數
#pragma once

// ===== 功能開關（1 = 啟用，0 = 停用）=====
#define FEATURE_BATTERY_MONITOR 1  // 電池電壓監測（GPIO 39，需 20kΩ + 10kΩ 分壓）
#define FEATURE_LIGHTS 0           // 選配：頭燈、煞車燈
#define FEATURE_BUZZER 0           // 選配：蜂鳴器喇叭
#define DEBUG_SERIAL 1             // 每 200 ms 在序列埠印出狀態

// ===== 腳位：L298N 馬達驅動 =====
// 左側兩顆馬達並聯接 A 通道（OUT1/OUT2），右側兩顆並聯接 B 通道（OUT3/OUT4）
constexpr int PIN_ENA = 25;  // 左側速度（PWM），需拔掉 ENA 跳線帽
constexpr int PIN_IN1 = 26;  // 左側方向
constexpr int PIN_IN2 = 27;
constexpr int PIN_ENB = 33;  // 右側速度（PWM），需拔掉 ENB 跳線帽
constexpr int PIN_IN3 = 32;  // 右側方向
constexpr int PIN_IN4 = 13;

// ===== 腳位：HC-SR04 超音波 ×3 =====
// Echo 輸出 5 V，必須經 1kΩ + 2kΩ 分壓後才能接 ESP32
// Echo 用 GPIO 34～36：只能輸入的腳位，剛好拿來讀訊號
constexpr int PIN_TRIG_LEFT = 16;
constexpr int PIN_ECHO_LEFT = 34;
constexpr int PIN_TRIG_CENTER = 17;
constexpr int PIN_ECHO_CENTER = 35;
constexpr int PIN_TRIG_RIGHT = 18;
constexpr int PIN_ECHO_RIGHT = 36;

// ===== 腳位：電池電壓 =====
// 電池正極 → 20kΩ → GPIO 39 → 10kΩ → GND（電壓除以 3，8.4 V → 2.8 V）
constexpr int PIN_BATTERY = 39;  // ADC1，只能輸入

// ===== 腳位：選配 =====
constexpr int PIN_HEADLIGHT = 19;   // 白色 LED ×2，經 S8050 從 5 V 供電
constexpr int PIN_BRAKELIGHT = 21;  // 紅色 LED ×2，各串 220Ω 直接驅動
constexpr int PIN_BUZZER = 22;      // 5 V 有源蜂鳴器，經 S8050 驅動

// ===== 馬達 =====
constexpr int PWM_FREQ_HZ = 1000;
constexpr int PWM_RESOLUTION_BITS = 8;
constexpr int PWM_MAX = 255;
// 低於這個 PWM，TT 馬達通常轉不動（待實測調整）
constexpr int MOTOR_MIN_PWM = 80;
// 接線後若某一側前後相反，改成 true 即可，不用重新接線
constexpr bool LEFT_MOTOR_INVERTED = false;
constexpr bool RIGHT_MOTOR_INVERTED = false;
// 走不直時微調：較快的那一側調小，例如 0.95
constexpr float LEFT_TRIM = 1.0f;
constexpr float RIGHT_TRIM = 1.0f;

// ===== 手動模式 =====
constexpr int SPEED_LIMIT_NORMAL_PCT = 60;  // 平常的速度上限
constexpr int SPEED_LIMIT_BOOST_PCT = 100;  // 按住 ZR 時的速度上限
constexpr int STICK_DEADZONE = 40;          // 搖桿死區（滿刻度約 512）

// ===== 防撞輔助（手動模式）=====
constexpr int ASSIST_STOP_CM = 15;       // 正前方小於此距離：禁止前進
constexpr int ASSIST_WARN_SIDE_CM = 10;  // 左前／右前小於此距離：只震動提醒
constexpr int RUMBLE_WARN_MS = 200;
constexpr int RUMBLE_COOLDOWN_MS = 600;  // 兩次震動之間至少間隔

// ===== 超音波 =====
constexpr int SONAR_INTERVAL_MS = 30;               // 三顆輪流觸發的間隔，避免互相干擾
constexpr unsigned long SONAR_TIMEOUT_US = 20000;   // 約 3.4 m，超過視為沒有障礙物
constexpr int SONAR_MAX_CM = 300;                   // 沒收到回波時回報的距離

// ===== 自動避障（距離單位 cm、速度單位 %）=====
constexpr int AUTO_EMERGENCY_CM = 10;  // 任一顆小於此距離：後退
constexpr int AUTO_FRONT_CM = 30;      // 正前方小於此距離：原地轉向
constexpr int AUTO_SIDE_CM = 20;       // 左前或右前小於此距離：往另一邊微調
constexpr int AUTO_CLEAR_MARGIN_CM = 10;  // 轉向時，正前方要比 AUTO_FRONT_CM 再多這些才恢復直行
constexpr int AUTO_CRUISE_PCT = 50;
constexpr int AUTO_TURN_PCT = 60;
constexpr int AUTO_NUDGE_PCT = 25;  // 微調時內側輪減速多少
constexpr int AUTO_BACKUP_PCT = 50;
constexpr unsigned long AUTO_BACKUP_MS = 500;
constexpr unsigned long AUTO_TURN_TIMEOUT_MS = 1500;  // 轉太久還沒脫困就再後退一次

// ===== 電池（2S 18650：滿電 8.4 V）=====
constexpr float BATTERY_DIVIDER_RATIO = 3.0f;  // (20k + 10k) / 10k
constexpr float BATTERY_LOW_V = 7.0f;          // 3.5 V／顆：手把震動提醒
constexpr float BATTERY_CRITICAL_V = 6.6f;     // 3.3 V／顆：停車保護電池（LG MJ1 沒有保護板）
constexpr unsigned long BATTERY_CRITICAL_HOLD_MS = 3000;  // 連續低於 CRITICAL 這麼久就鎖定停車，重開機才解除
constexpr float BATTERY_ABSENT_V = 5.0f;       // 低於此值視為沒接電池（只插 USB），不鎖定
