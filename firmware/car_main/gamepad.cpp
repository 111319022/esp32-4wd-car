#include "gamepad.h"

#include <Arduino.h>
#include <Bluepad32.h>

#include "config.h"

// 手把組合（擇一）：
//   1. 左右兩支 Joy-Con（主要設計）：左 Joy-Con 搖桿上下 = 油門，右 Joy-Con 搖桿左右 = 轉向
//   2. 一支 Switch Pro 手把（備用）：左搖桿上下 = 油門，右搖桿左右 = 轉向
// 兩支 Joy-Con 都連上才算 connected；任何一支斷線就視為斷線（主程式會停車）。
// 已經用 Joy-Con 時 Pro 手把會被拒絕，反之亦然；多出來的手把一律斷開。

namespace {

ControllerPtr joyLeft = nullptr;
ControllerPtr joyRight = nullptr;
ControllerPtr pro = nullptr;

GamepadState state;
bool prevPlus = false;
bool prevB = false;
unsigned long lastRumbleMs = 0;

bool isUsable(ControllerPtr ctl) {
  return ctl && ctl->isConnected() && ctl->isGamepad();
}

void onConnected(ControllerPtr ctl) {
  int model = ctl->getModel();
  bool usingJoyCon = joyLeft || joyRight;

  if (model == Controller::CONTROLLER_TYPE_SwitchJoyConLeft && !joyLeft && !pro) {
    joyLeft = ctl;
    Serial.println("[gamepad] 左 Joy-Con 已連線");
  } else if (model == Controller::CONTROLLER_TYPE_SwitchJoyConRight && !joyRight && !pro) {
    joyRight = ctl;
    Serial.println("[gamepad] 右 Joy-Con 已連線");
  } else if (model == Controller::CONTROLLER_TYPE_SwitchProController && !pro && !usingJoyCon) {
    pro = ctl;
    Serial.println("[gamepad] Pro 手把已連線");
  } else {
    Serial.printf("[gamepad] 拒絕這支手把（model=%d），已經有對應的手把或組合不符\n", model);
    ctl->disconnect();
    return;
  }

  if (joyLeft && joyRight) {
    Serial.println("[gamepad] 兩支 Joy-Con 都到齊，可以開車");
  } else if (!pro) {
    Serial.println("[gamepad] 等另一支 Joy-Con 連線...");
  }
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl == joyLeft) {
    joyLeft = nullptr;
    Serial.println("[gamepad] 左 Joy-Con 斷線");
  } else if (ctl == joyRight) {
    joyRight = nullptr;
    Serial.println("[gamepad] 右 Joy-Con 斷線");
  } else if (ctl == pro) {
    pro = nullptr;
    Serial.println("[gamepad] Pro 手把斷線");
  }
}

int applyDeadzone(int value) {
  return abs(value) < STICK_DEADZONE ? 0 : value;
}

// Joy-Con 直握時的讀值。Bluepad32 把單支 Joy-Con 當成橫握的小手把，軸向轉了 90 度：
//   左 Joy-Con：axisX = -(直握時的上下)，所以往上推 axisX 是負值
//   右 Joy-Con：axisY = 直握時的左右，往右推是正值
// 以上依 Bluepad32 4.1.0 原始碼推得，方向不對就改 config.h 的 JOYCON_*_INVERTED
void readJoyCons(GamepadState& next, bool& plus, bool& b) {
  int throttle = -joyLeft->axisX();
  int steer = joyRight->axisY();
  next.throttle = applyDeadzone(JOYCON_THROTTLE_INVERTED ? -throttle : throttle);
  next.steer = applyDeadzone(JOYCON_STEER_INVERTED ? -steer : steer);

  // 右 Joy-Con：ZR → r2()；「＋」→ MISC_BUTTON_START
  // 手把上印的 A → a()、B → x()（Bluepad32 4.1.0 原始碼的對應，待 test_gamepad 確認）
  next.boost = joyRight->r2();
  next.hornHeld = joyRight->a();
  plus = joyRight->miscButtons() & MISC_BUTTON_START;
  b = joyRight->x();
}

void readPro(GamepadState& next, bool& plus, bool& b) {
  next.throttle = applyDeadzone(-pro->axisY());  // 搖桿往上推時 axisY 是負值
  next.steer = applyDeadzone(pro->axisRX());
  // ZR 可能回報成 r2() 或 throttle()，兩個都看
  next.boost = pro->r2() || pro->throttle() > 512;
  // Bluepad32 依「按鍵位置」對應，手把上印的 A/B 可能和 a()/b() 相反，請用 test_gamepad 確認
  next.hornHeld = pro->a();
  plus = pro->miscButtons() & MISC_BUTTON_START;  // Switch 的「＋」
  b = pro->b();
}

void rumbleOne(ControllerPtr ctl, int durationMs) {
  if (isUsable(ctl)) ctl->playDualRumble(0, durationMs, 0x80, 0x40);
}

}  // namespace

void gamepadBegin() {
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);
  // 手把一直連不上或重連失敗時，可以暫時打開下面這行清除配對紀錄
  // BP32.forgetBluetoothKeys();
}

void gamepadUpdate() {
  BP32.update();

  GamepadState next;
  bool plus = false;
  bool b = false;

  if (isUsable(joyLeft) && isUsable(joyRight)) {
    next.connected = true;
    readJoyCons(next, plus, b);
  } else if (isUsable(pro)) {
    next.connected = true;
    readPro(next, plus, b);
  }
  // 其他情況（例如只連上一支 Joy-Con）：next 維持預設值，connected = false → 主程式停車

  if (next.connected) {
    next.modeTogglePressed = plus && !prevPlus;
    next.lightTogglePressed = b && !prevB;
  }
  prevPlus = plus;
  prevB = b;
  state = next;
}

const GamepadState& gamepadState() {
  return state;
}

void gamepadRumble(int durationMs) {
  unsigned long now = millis();
  if (now - lastRumbleMs < RUMBLE_COOLDOWN_MS) return;
  lastRumbleMs = now;
  // 注意：esp32_bluepad32 4.1.0 沒有啟用 Joy-Con 的震動馬達（Bluepad32 5.0 才修正），
  // Joy-Con 可能完全不會震；Pro 手把正常
  rumbleOne(pro, durationMs);
  rumbleOne(joyLeft, durationMs);
  rumbleOne(joyRight, durationMs);
}
