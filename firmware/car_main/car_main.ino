// 蠻橫衝直撞無理四驅車 — 主程式
// 機電整合實務應用 2026 Fall
//
// 開發環境：Arduino IDE + esp32_bluepad32 開發板套件，開發板選「ESP32 Dev Board」
// 安裝方式見 docs/setup.md；腳位與參數都在 config.h
//
// 每圈 loop 依序做：讀手把 → 量一顆超音波 → 讀電池 → 依模式算左右輪速度 → 輸出到 L298N

#include "autopilot.h"
#include "battery.h"
#include "config.h"
#include "extras.h"
#include "gamepad.h"
#include "motors.h"
#include "sonar.h"

enum class DriveMode { MANUAL, AUTO };

DriveMode mode = DriveMode::MANUAL;
unsigned long lastDebugMs = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== 蠻橫衝直撞無理四驅車 ===");

  motorsBegin();  // 最先初始化，確保馬達一開機就是停止狀態
  sonarBegin();
  batteryBegin();
  extrasBegin();
  gamepadBegin();

  Serial.println("等待 Switch 手把連線（按住手把上的配對鍵）...");
}

// 手動模式：搖桿 → 左右輪速度（%），含防撞輔助
void manualDrive(const GamepadState& gp, int leftCm, int centerCm, int rightCm, int& leftPct, int& rightPct) {
  int limit = gp.boost ? SPEED_LIMIT_BOOST_PCT : SPEED_LIMIT_NORMAL_PCT;
  int throttle = gp.throttle * 100 / 512;
  int steer = gp.steer * 100 / 512;

  // 防撞輔助：正前方太近時不准前進，但可以後退和原地轉向
  if (centerCm < ASSIST_STOP_CM && throttle > 0) {
    throttle = 0;
    gamepadRumble(RUMBLE_WARN_MS);
  } else if (min(leftCm, rightCm) < ASSIST_WARN_SIDE_CM) {
    gamepadRumble(RUMBLE_WARN_MS);  // 側邊只提醒，不干預
  }

  // 坦克式混控：左輪 = T + S，右輪 = T − S
  leftPct = constrain(throttle + steer, -100, 100) * limit / 100;
  rightPct = constrain(throttle - steer, -100, 100) * limit / 100;
}

void loop() {
  gamepadUpdate();
  sonarUpdate();
  batteryUpdate();

  const GamepadState& gp = gamepadState();
  int leftCm = sonarCm(SONAR_LEFT);
  int centerCm = sonarCm(SONAR_CENTER);
  int rightCm = sonarCm(SONAR_RIGHT);
  int leftPct = 0;
  int rightPct = 0;

  if (!gp.connected) {
    // 斷線保護：沒有手把就一律停車，回到手動模式
    mode = DriveMode::MANUAL;
    motorsStop();
    extrasOff();
  } else {
    if (gp.modeTogglePressed) {
      mode = (mode == DriveMode::MANUAL) ? DriveMode::AUTO : DriveMode::MANUAL;
      autopilotReset();
      Serial.println(mode == DriveMode::AUTO ? "[mode] 自動避障" : "[mode] 手動");
    }

    if (mode == DriveMode::MANUAL) {
      manualDrive(gp, leftCm, centerCm, rightCm, leftPct, rightPct);
    } else {
      autopilotStep(leftCm, centerCm, rightCm, leftPct, rightPct);
    }

    if (batteryCritical()) {
      // LG MJ1 沒有保護板，電壓太低一定要停車，避免電池過度放電
      leftPct = rightPct = 0;
      gamepadRumble(RUMBLE_WARN_MS);
    } else if (batteryLow()) {
      gamepadRumble(RUMBLE_WARN_MS);
    }

    motorsSetPercent(leftPct, rightPct);
    extrasUpdate(gp.lightTogglePressed, gp.hornHeld, leftPct == 0 && rightPct == 0, leftPct < 0 && rightPct < 0);
  }

#if DEBUG_SERIAL
  unsigned long now = millis();
  if (now - lastDebugMs >= 200) {
    lastDebugMs = now;
    Serial.printf("pad=%d mode=%s auto=%s | L=%3d C=%3d R=%3d cm | out L=%4d R=%4d %% | bat=%.2fV\n",
                  gp.connected, mode == DriveMode::AUTO ? "AUTO" : "MAN", autopilotStateName(), leftCm, centerCm,
                  rightCm, leftPct, rightPct, batteryVolts());
  }
#endif

  delay(1);  // 讓出 CPU 給 Bluepad32 和系統任務，避免看門狗重開機
}
