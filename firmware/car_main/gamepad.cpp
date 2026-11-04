#include "gamepad.h"

#include <Arduino.h>
#include <Bluepad32.h>

#include "config.h"

// Bluepad32 內部介面（C 語言），只用來補送 Joy-Con 的「開啟震動」指令，見 enableJoyConVibration()
extern "C" {
#include <btstack_run_loop.h>
#include <uni_hid_device.h>
}

// 手把組合（擇一）：
//   1. 左右兩支 Joy-Con（主要設計）：左 Joy-Con 搖桿上下 = 油門，右 Joy-Con 搖桿左右 = 轉向
//   2. 一支 Switch Pro 手把（備用）：左搖桿上下 = 油門，右搖桿左右 = 轉向
// 兩支 Joy-Con 都連上才算 connected；任何一支斷線就視為斷線（主程式會停車）。
// 已經用 Joy-Con 時 Pro 手把會被拒絕，反之亦然；多出來的手把一律斷開。

namespace {

ControllerPtr joyLeft = nullptr;
ControllerPtr joyRight = nullptr;
ControllerPtr pro = nullptr;

enum Slot { SLOT_LEFT, SLOT_RIGHT, SLOT_PRO, SLOT_COUNT };
const char* const SLOT_NAMES[SLOT_COUNT] = {"左 Joy-Con", "右 Joy-Con", "Pro 手把"};
void resetCalibration(Slot slot);  // 定義在下方「連線時自動歸零」

GamepadState state;
bool prevPlus = false;
bool prevB = false;
unsigned long lastRumbleMs = 0;

bool isUsable(ControllerPtr ctl) {
  return ctl && ctl->isConnected() && ctl->isGamepad();
}

// ===== 補送 Joy-Con「開啟震動」指令 =====
// Joy-Con 要先收到子指令 0x48（enable vibration）才會理會震動訊號。esp32_bluepad32 4.1.0
// 初始化時沒有送（Bluepad32 5.0 才補上），所以 Joy-Con 連上後由我們補送一次。
// - 必須在手把初始化完成後才送：4.1.0 初始化期間收到任何子指令回覆都會跳到下一步。
//   onConnected() 是在 Bluepad32 完成初始化（STATE_READY）之後才被呼叫，所以在這裡送是安全的。
// - 藍牙相關函式只能在 BTstack 的執行緒呼叫，所以用 btstack_run_loop_execute_on_main_thread() 轉交。
// - 換成 Bluepad32 5.0 以上的開發板套件後，這段就不需要了（重送一次也無害）。
struct VibrationEnableJob {
  btstack_context_callback_registration_t registration;
  uint8_t btaddr[6];
  volatile bool pending;
};
VibrationEnableJob vibrationJobs[2];  // [0] 左 Joy-Con、[1] 右 Joy-Con

void sendVibrationEnableOnBtThread(void* context) {
  VibrationEnableJob* job = static_cast<VibrationEnableJob*>(context);
  uni_hid_device_t* d = uni_hid_device_get_instance_for_address(job->btaddr);
  if (d != nullptr) {
    static uint8_t packetNum = 0;
    // 0xA2：HID DATA | OUTPUT；0x01：子指令報告；接著 8 bytes 中性震動資料；0x48 0x01：開啟震動
    uint8_t report[] = {0xA2, 0x01, packetNum, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x48, 0x01};
    packetNum = (packetNum + 1) & 0x0F;
    uni_hid_device_send_intr_report(d, report, sizeof(report));
  }
  job->pending = false;
}

void enableJoyConVibration(ControllerPtr ctl, int slot) {
  VibrationEnableJob& job = vibrationJobs[slot];
  if (job.pending) return;  // 上一次還沒送出，不能重複登記同一個 registration
  ControllerProperties props = ctl->getProperties();
  memcpy(job.btaddr, props.btaddr, sizeof(job.btaddr));
  job.registration = {};
  job.registration.callback = &sendVibrationEnableOnBtThread;
  job.registration.context = &job;
  job.pending = true;
  btstack_run_loop_execute_on_main_thread(&job.registration);
}

void onConnected(ControllerPtr ctl) {
  int model = ctl->getModel();
  bool usingJoyCon = joyLeft || joyRight;

  if (model == Controller::CONTROLLER_TYPE_SwitchJoyConLeft && !joyLeft && !pro) {
    joyLeft = ctl;
    resetCalibration(SLOT_LEFT);
    enableJoyConVibration(ctl, 0);
    Serial.println("[gamepad] 左 Joy-Con 已連線");
  } else if (model == Controller::CONTROLLER_TYPE_SwitchJoyConRight && !joyRight && !pro) {
    joyRight = ctl;
    resetCalibration(SLOT_RIGHT);
    enableJoyConVibration(ctl, 1);
    Serial.println("[gamepad] 右 Joy-Con 已連線");
  } else if (model == Controller::CONTROLLER_TYPE_SwitchProController && !pro && !usingJoyCon) {
    pro = ctl;
    resetCalibration(SLOT_PRO);
    Serial.println("[gamepad] Pro 手把已連線");
  } else {
    Serial.printf("[gamepad] 拒絕這支手把（model=%d），已經有對應的手把或組合不符\n", model);
    ctl->disconnect();
    return;
  }

  if (joyLeft && joyRight) {
    Serial.println("[gamepad] 兩支 Joy-Con 都到齊，歸零完成後就能開車（連線時先不要碰搖桿）");
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

// ===== 原始搖桿讀值：[0] = 油門（正值前進）、[1] = 轉向（正值右轉），沒用到的軸填 0 =====
// Joy-Con 直握時，Bluepad32 把單支 Joy-Con 當成橫握的小手把，軸向轉了 90 度（2026-11-04 實測確認）：
//   左 Joy-Con：axisX = -(直握時的上下)，往上推 axisX 是負值
//   右 Joy-Con：axisY = 直握時的左右，往右推是正值
void rawAxes(Slot slot, ControllerPtr ctl, int out[2]) {
  out[0] = 0;
  out[1] = 0;
  if (slot == SLOT_LEFT) {
    out[0] = JOYCON_THROTTLE_INVERTED ? ctl->axisX() : -ctl->axisX();
  } else if (slot == SLOT_RIGHT) {
    out[1] = JOYCON_STEER_INVERTED ? -ctl->axisY() : ctl->axisY();
  } else {
    out[0] = -ctl->axisY();  // Pro 手把：搖桿往上推時 axisY 是負值
    out[1] = ctl->axisRX();
  }
}

// ===== 連線時自動歸零 =====
// 4.1.0 讀右 Joy-Con 校正資料有誤，放開搖桿時轉向讀值停在約 +60，超過死區，車子會自己偏。
// 所以手把連上後先取 STICK_CENTER_SAMPLE_MS 的平均當作中心點；歸零完成前這支手把不算可用（車子不會動）。
// 取樣期間搖桿有在動（變化太大）或中心偏太多，就判定有人在推搖桿，重新取樣。
struct StickCalibration {
  bool started = false;
  bool done = false;
  unsigned long startMs = 0;
  long sum[2] = {0, 0};
  int count = 0;
  int minV[2] = {0, 0};
  int maxV[2] = {0, 0};
  int center[2] = {0, 0};
};
StickCalibration calibrations[SLOT_COUNT];

void resetCalibration(Slot slot) {
  calibrations[slot] = StickCalibration();
}

void updateCalibration(Slot slot, ControllerPtr ctl) {
  StickCalibration& c = calibrations[slot];
  if (c.done) return;

  int v[2];
  rawAxes(slot, ctl, v);
  unsigned long now = millis();
  if (!c.started) {
    c = StickCalibration();
    c.started = true;
    c.startMs = now;
    for (int i = 0; i < 2; i++) c.minV[i] = c.maxV[i] = v[i];
  }
  for (int i = 0; i < 2; i++) {
    c.sum[i] += v[i];
    c.minV[i] = min(c.minV[i], v[i]);
    c.maxV[i] = max(c.maxV[i], v[i]);
  }
  c.count++;
  if (now - c.startMs < STICK_CENTER_SAMPLE_MS) return;

  bool ok = true;
  int avg[2];
  for (int i = 0; i < 2; i++) {
    avg[i] = c.sum[i] / c.count;
    if (c.maxV[i] - c.minV[i] > STICK_CENTER_MAX_SPREAD || abs(avg[i]) > STICK_CENTER_MAX_OFFSET) ok = false;
  }
  if (!ok) {
    Serial.printf("[gamepad] %s 歸零失敗：請放開搖桿（平均 %d／%d），重新取樣\n", SLOT_NAMES[slot], avg[0], avg[1]);
    c.started = false;
    return;
  }
  c.center[0] = avg[0];
  c.center[1] = avg[1];
  c.done = true;
  Serial.printf("[gamepad] %s 搖桿歸零完成：油門中心 %d、轉向中心 %d\n", SLOT_NAMES[slot], avg[0], avg[1]);
}

// 扣掉中心點，並把兩側各自拉回 ±512 的滿刻度（不然偏移那一側會推不到底）
int applyCenter(int value, int center) {
  long scaled;
  if (value >= center) {
    scaled = (long)(value - center) * 512 / (512 - center);
  } else {
    scaled = (long)(value - center) * 512 / (512 + center);
  }
  return constrain((int)scaled, -512, 512);
}

int calibratedAxis(Slot slot, ControllerPtr ctl, int axis) {
  int v[2];
  rawAxes(slot, ctl, v);
  return applyDeadzone(applyCenter(v[axis], calibrations[slot].center[axis]));
}

bool isReady(Slot slot, ControllerPtr ctl) {
  return isUsable(ctl) && calibrations[slot].done;
}

void readJoyCons(GamepadState& next, bool& plus, bool& b) {
  next.throttle = calibratedAxis(SLOT_LEFT, joyLeft, 0);
  next.steer = calibratedAxis(SLOT_RIGHT, joyRight, 1);

  // 右 Joy-Con（2026-11-04 實測）：ZR → r2()、「＋」→ MISC_BUTTON_START、手把上印的 A → a()、B → x()
  next.boost = joyRight->r2();
  next.hornHeld = joyRight->a();
  plus = joyRight->miscButtons() & MISC_BUTTON_START;
  b = joyRight->x();
}

void readPro(GamepadState& next, bool& plus, bool& b) {
  next.throttle = calibratedAxis(SLOT_PRO, pro, 0);
  next.steer = calibratedAxis(SLOT_PRO, pro, 1);
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

  if (isUsable(joyLeft)) updateCalibration(SLOT_LEFT, joyLeft);
  if (isUsable(joyRight)) updateCalibration(SLOT_RIGHT, joyRight);
  if (isUsable(pro)) updateCalibration(SLOT_PRO, pro);

  GamepadState next;
  bool plus = false;
  bool b = false;

  if (isReady(SLOT_LEFT, joyLeft) && isReady(SLOT_RIGHT, joyRight)) {
    next.connected = true;
    readJoyCons(next, plus, b);
  } else if (isReady(SLOT_PRO, pro)) {
    next.connected = true;
    readPro(next, plus, b);
  }
  // 其他情況（只連上一支 Joy-Con、還在歸零）：next 維持預設值，connected = false → 主程式停車

  if (next.connected) {
    next.modeTogglePressed = plus && !prevPlus;
    next.lightTogglePressed = b && !prevB;
    next.plusHeld = plus;
    next.lightHeld = b;
  }
  prevPlus = plus;
  prevB = b;
  state = next;
}

const GamepadState& gamepadState() {
  return state;
}

GamepadSlotStatus gamepadSlotStatus(GamepadSlotId slotId) {
  Slot slot = static_cast<Slot>(slotId);
  ControllerPtr ctl = slot == SLOT_LEFT ? joyLeft : slot == SLOT_RIGHT ? joyRight : pro;
  if (!isUsable(ctl)) return PAD_ABSENT;
  return calibrations[slot].done ? PAD_READY : PAD_CALIBRATING;
}

void gamepadRumble(int durationMs) {
  unsigned long now = millis();
  if (now - lastRumbleMs < RUMBLE_COOLDOWN_MS) return;
  lastRumbleMs = now;
  // Joy-Con 要先送過「開啟震動」指令才會震（連線時已由 enableJoyConVibration() 補送）
  rumbleOne(pro, durationMs);
  rumbleOne(joyLeft, durationMs);
  rumbleOne(joyRight, durationMs);
}
