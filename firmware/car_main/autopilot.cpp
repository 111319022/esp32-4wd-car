#include "autopilot.h"

#include <Arduino.h>

#include "config.h"

namespace {

AutoState state = AutoState::CRUISE;
unsigned long stateStartMs = 0;
int turnDir = 1;  // +1 原地右轉，-1 原地左轉

void enter(AutoState next) {
  state = next;
  stateStartMs = millis();
}

// 往比較空的那一側轉
int chooseTurnDir(int leftCm, int rightCm) {
  return leftCm > rightCm ? -1 : 1;
}

}  // namespace

void autopilotReset() {
  enter(AutoState::CRUISE);
}

void autopilotStep(int leftCm, int centerCm, int rightCm, int& leftPct, int& rightPct) {
  unsigned long elapsed = millis() - stateStartMs;
  int nearest = min(leftCm, min(centerCm, rightCm));

  switch (state) {
    case AutoState::BACKUP:
      leftPct = -AUTO_BACKUP_PCT;
      rightPct = -AUTO_BACKUP_PCT;
      if (elapsed >= AUTO_BACKUP_MS) {
        turnDir = chooseTurnDir(leftCm, rightCm);
        enter(AutoState::TURN);
      }
      return;

    case AutoState::TURN:
      leftPct = turnDir * AUTO_TURN_PCT;
      rightPct = -turnDir * AUTO_TURN_PCT;
      if (centerCm > AUTO_FRONT_CM + AUTO_CLEAR_MARGIN_CM && nearest > AUTO_EMERGENCY_CM) {
        enter(AutoState::CRUISE);
      } else if (elapsed >= AUTO_TURN_TIMEOUT_MS) {
        enter(AutoState::BACKUP);  // 轉太久還沒脫困，再退一次
      }
      return;

    case AutoState::CRUISE:
      // 規則依優先順序：符合一條就執行，不再往下看
      if (nearest < AUTO_EMERGENCY_CM) {  // 規則 1
        enter(AutoState::BACKUP);
        leftPct = rightPct = 0;
      } else if (centerCm < AUTO_FRONT_CM) {  // 規則 2
        turnDir = chooseTurnDir(leftCm, rightCm);
        enter(AutoState::TURN);
        leftPct = rightPct = 0;
      } else if (leftCm < AUTO_SIDE_CM) {  // 規則 3：左前太近，往右偏
        leftPct = AUTO_CRUISE_PCT;
        rightPct = AUTO_CRUISE_PCT - AUTO_NUDGE_PCT;
      } else if (rightCm < AUTO_SIDE_CM) {  // 規則 3：右前太近，往左偏
        leftPct = AUTO_CRUISE_PCT - AUTO_NUDGE_PCT;
        rightPct = AUTO_CRUISE_PCT;
      } else {  // 規則 4：直行
        leftPct = AUTO_CRUISE_PCT;
        rightPct = AUTO_CRUISE_PCT;
      }
      return;
  }
}

AutoState autopilotState() {
  return state;
}

const char* autopilotStateName() {
  switch (state) {
    case AutoState::CRUISE:
      return "CRUISE";
    case AutoState::BACKUP:
      return "BACKUP";
    case AutoState::TURN:
      return "TURN";
  }
  return "?";
}
