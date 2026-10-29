#include "gamepad.h"

#include <Arduino.h>
#include <Bluepad32.h>

#include "config.h"

namespace {

ControllerPtr activeController = nullptr;
GamepadState state;
bool prevPlus = false;
bool prevB = false;
unsigned long lastRumbleMs = 0;

void onConnected(ControllerPtr ctl) {
  if (activeController == nullptr) {
    activeController = ctl;
    Serial.println("[gamepad] 手把已連線");
  } else {
    Serial.println("[gamepad] 已有手把連線，忽略第二支");
  }
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl == activeController) {
    activeController = nullptr;
    Serial.println("[gamepad] 手把斷線");
  }
}

int applyDeadzone(int value) {
  return abs(value) < STICK_DEADZONE ? 0 : value;
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
  ControllerPtr ctl = activeController;
  if (ctl && ctl->isConnected() && ctl->isGamepad()) {
    next.connected = true;
    next.throttle = applyDeadzone(-ctl->axisY());  // 搖桿往上推時 axisY 是負值
    next.steer = applyDeadzone(ctl->axisRX());
    // Switch 的 ZR 是數位按鍵，Bluepad32 可能回報成 r2() 或 throttle()，兩個都看
    next.boost = ctl->r2() || ctl->throttle() > 512;

    // 注意：Bluepad32 依「按鍵位置」對應，Switch 手把上印的 A/B 可能和 a()/b() 相反，
    // 實際對應請用 firmware/tests/test_gamepad 確認
    next.hornHeld = ctl->a();
    bool plus = ctl->miscButtons() & MISC_BUTTON_START;  // Switch 的「＋」
    bool b = ctl->b();
    next.modeTogglePressed = plus && !prevPlus;
    next.lightTogglePressed = b && !prevB;
    prevPlus = plus;
    prevB = b;
  } else {
    prevPlus = false;
    prevB = false;
  }
  state = next;
}

const GamepadState& gamepadState() {
  return state;
}

void gamepadRumble(int durationMs) {
  ControllerPtr ctl = activeController;
  if (!ctl || !ctl->isConnected()) return;
  unsigned long now = millis();
  if (now - lastRumbleMs < RUMBLE_COOLDOWN_MS) return;
  lastRumbleMs = now;
  ctl->playDualRumble(0, durationMs, 0x80, 0x40);
}
