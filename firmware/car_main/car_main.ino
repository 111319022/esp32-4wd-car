// 蠻橫衝直撞無理四驅車 — 主程式
// 機電整合實務應用 2026 Fall
//
// 開發環境：Arduino IDE + esp32_bluepad32 開發板套件，開發板選 esp32_bluepad32 底下的「ESP32 Dev Module」
// 安裝方式見 docs/setup.md；腳位與參數都在 config.h
//
// 每圈 loop 依序做：讀手把 → 量一顆超音波 → 讀電池 → 依模式算左右輪速度 → 輸出到 L298N → 記 LOG

#include "autopilot.h"
#include "battery.h"
#include "config.h"
#include "extras.h"
#include "gamepad.h"
#include "logger.h"
#include "motors.h"
#include "sonar.h"

enum class DriveMode { MANUAL, AUTO };

DriveMode mode = DriveMode::MANUAL;
unsigned long lastDebugMs = 0;
unsigned long lastStreamMs = 0;

// LOG 用：狀態快照計時、loop 最長耗時、事件的前一次狀態（只在變化時記一筆）
unsigned long lastSnapshotMs = 0;
unsigned long maxLoopUs = 0;
bool prevAssistBlock = false;
AutoState prevAutoState = AutoState::CRUISE;
bool batteryNoticeLogged = false;

void setup() {
  Serial.setTxBufferSize(4096);  // 加大傳送緩衝：印東西時不會卡住 loop，也讓 LOG 能分段送出
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== 蠻橫衝直撞無理四驅車 ===");

  motorsBegin();  // 最先初始化，確保馬達一開機就是停止狀態
  logBegin();     // 第一次使用要格式化 Flash，可能要等幾十秒（馬達已經停住）
  sonarBegin();
  batteryBegin();
  extrasBegin();
  gamepadBegin();

  Serial.println("等待手把連線（一次只用一支）：Joy-Con 按住側邊同步鍵，或一支 Switch Pro／Xbox／PS 手把進入配對...");
}

// 手動模式：搖桿 → 左右輪速度（%），含防撞輔助
void manualDrive(const GamepadState& gp, int leftCm, int centerCm, int rightCm, int& leftPct, int& rightPct) {
  int limit = gp.boost ? SPEED_LIMIT_BOOST_PCT : SPEED_LIMIT_NORMAL_PCT;
  int throttle = gp.throttle * 100 / 512;
  int steer = gp.steer * 100 / 512;

  // 防撞輔助：正前方太近時不准前進，但可以後退和原地轉向
  bool assistBlock = centerCm < ASSIST_STOP_CM && throttle > 0;
  if (assistBlock && !prevAssistBlock) logEvent("assist", "正前 %d cm，禁止前進", centerCm);
  prevAssistBlock = assistBlock;
  if (assistBlock) {
    throttle = 0;
    gamepadRumble(RUMBLE_WARN_MS);
  } else if (min(leftCm, rightCm) < ASSIST_WARN_SIDE_CM) {
    gamepadRumble(RUMBLE_WARN_MS);  // 側邊只提醒，不干預
  }

  // 坦克式混控：左輪 = T + S，右輪 = T − S
  leftPct = constrain(throttle + steer, -100, 100) * limit / 100;
  rightPct = constrain(throttle - steer, -100, 100) * limit / 100;
}

// 電池狀態只在開機後第一次發生時記一筆（電壓在門檻附近會一直跳，每次都記會塞滿 LOG）
void logBatteryNotice() {
  if (batteryNoticeLogged || !FEATURE_BATTERY_MONITOR) return;
  float v = batteryVolts();
  if (v < BATTERY_ABSENT_V) {
    logEvent("battery", "電壓 %.2f V：沒接電池（只插 USB），低電量警告會一直震，屬正常", v);
  } else if (batteryCritical()) {
    logEvent("battery", "電壓 %.2f V 低於 %.1f V：停車保護電池", v, BATTERY_CRITICAL_V);
  } else if (batteryLow()) {
    logEvent("battery", "電壓 %.2f V 低於 %.1f V：低電量提醒", v, BATTERY_LOW_V);
  } else {
    return;
  }
  batteryNoticeLogged = true;
}

void logSnapshotIfDue(const GamepadState& gp, int leftCm, int centerCm, int rightCm, int leftPct, int rightPct) {
  unsigned long now = millis();
  unsigned long interval = gp.connected ? LOG_SNAPSHOT_DRIVE_MS : LOG_SNAPSHOT_IDLE_MS;
  unsigned long elapsed = now - lastSnapshotMs;
  if (elapsed < interval) return;
  lastSnapshotMs = now;
  // hz = 手把每秒送來幾次新資料（太低代表藍牙不穩）；lp = 這段時間內 loop 最久跑了幾 ms
  unsigned long hz = gamepadTakeUpdateCount() * 1000 / max(elapsed, 1UL);
  logSnapshot("pad=%d c=%s m=%s a=%s T=%d S=%d zr=%d o=%d/%d d=%d/%d/%d b=%.2f hz=%lu lp=%lu", gp.connected,
              gamepadShortName(), mode == DriveMode::AUTO ? "AUTO" : "MAN", autopilotStateName(), gp.throttle,
              gp.steer, gp.boost, leftPct, rightPct, leftCm, centerCm, rightCm, batteryVolts(), hz,
              maxLoopUs / 1000);
  maxLoopUs = 0;
}

void loop() {
  unsigned long loopStartUs = micros();
  gamepadUpdate();
  sonarUpdate();
  batteryUpdate();

  const GamepadState& gp = gamepadState();
  int leftCm = sonarCm(SONAR_LEFT);
  int centerCm = sonarCm(SONAR_CENTER);
  int rightCm = sonarCm(SONAR_RIGHT);
  int leftPct = 0;
  int rightPct = 0;
  int cmdLeftPct = 0;   // 電池保護覆寫之前的速度指令（除錯用：只插 USB 時看得到車子「本來要怎麼走」）
  int cmdRightPct = 0;

  if (!gp.connected || logDumping()) {
    // 斷線保護：沒有手把就一律停車，回到手動模式（把 LOG 送到電腦的期間也停車）
    if (mode == DriveMode::AUTO) logEvent("mode", "手動（手把斷線或正在送 LOG）");
    mode = DriveMode::MANUAL;
    prevAssistBlock = false;
    motorsStop();
    extrasOff();
  } else {
    if (gp.modeTogglePressed) {
      mode = (mode == DriveMode::MANUAL) ? DriveMode::AUTO : DriveMode::MANUAL;
      autopilotReset();
      prevAutoState = autopilotState();
      logEvent("mode", "%s", mode == DriveMode::AUTO ? "自動避障" : "手動");
    }

    if (mode == DriveMode::MANUAL) {
      manualDrive(gp, leftCm, centerCm, rightCm, leftPct, rightPct);
    } else {
      autopilotStep(leftCm, centerCm, rightCm, leftPct, rightPct);
      if (autopilotState() != prevAutoState) {
        prevAutoState = autopilotState();
        logEvent("auto", "%s（左前／正前／右前 %d／%d／%d cm）", autopilotStateName(), leftCm, centerCm, rightCm);
      }
    }

    cmdLeftPct = leftPct;
    cmdRightPct = rightPct;

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

  logBatteryNotice();
  logSnapshotIfDue(gp, leftCm, centerCm, rightCm, leftPct, rightPct);

  unsigned long now = millis();
  bool quiet = logDumping();  // 送 LOG 的期間不印其他東西，不然會混進下載的 LOG
#if DEBUG_SERIAL
  if (!quiet && now - lastDebugMs >= DEBUG_SERIAL_MS) {
    lastDebugMs = now;
    // T／S 是歸零、死區之後的搖桿值（-512～512），只插 USB 時電池保護會讓 out 一直是 0，看 T／S 就好
    Serial.printf("pad=%d mode=%s auto=%s | T=%4d S=%4d%s | L=%3d C=%3d R=%3d cm | out L=%4d R=%4d %% | bat=%.2fV\n",
                  gp.connected, mode == DriveMode::AUTO ? "AUTO" : "MAN", autopilotStateName(), gp.throttle,
                  gp.steer, gp.boost ? " ZR" : "", leftCm, centerCm, rightCm, leftPct, rightPct, batteryVolts());
  }
#endif

#if DEBUG_STREAM
  // 給 tools/gamepad_monitor.html 讀的機器格式：「GP」開頭、空白分隔的 key=value，欄位說明見 docs/software.md
  if (!quiet && now - lastStreamMs >= DEBUG_STREAM_MS) {
    lastStreamMs = now;
    Serial.printf("GP pad=%d jl=%d jr=%d pro=%d mode=%s auto=%s T=%d S=%d zr=%d a=%d b=%d plus=%d "
                  "cl=%d cr=%d ol=%d or=%d dl=%d dc=%d dr=%d bat=%.2f low=%d crit=%d\n",
                  gp.connected, gamepadSlotStatus(PAD_SLOT_LEFT), gamepadSlotStatus(PAD_SLOT_RIGHT),
                  gamepadSlotStatus(PAD_SLOT_PRO), mode == DriveMode::AUTO ? "AUTO" : "MAN", autopilotStateName(),
                  gp.throttle, gp.steer, gp.boost, gp.hornHeld, gp.lightHeld, gp.plusHeld, cmdLeftPct, cmdRightPct,
                  leftPct, rightPct, leftCm, centerCm, rightCm, batteryVolts(), batteryLow(), batteryCritical());
  }
#endif

  logUpdate(leftPct == 0 && rightPct == 0);

  unsigned long loopUs = micros() - loopStartUs;
  if (loopUs > maxLoopUs) maxLoopUs = loopUs;
  delay(1);  // 讓出 CPU 給 Bluepad32 和系統任務，避免看門狗重開機
}
