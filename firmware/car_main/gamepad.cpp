#include "gamepad.h"

#include <Arduino.h>
#include <Bluepad32.h>

#include "config.h"
#include "logger.h"

// Bluepad32 內部介面（C 語言），只用來補送 Joy-Con 的「開啟震動」指令，見 enableJoyConVibration()
extern "C" {
#include <btstack_run_loop.h>
#include <uni_hid_device.h>
}

// 一次只用一支手把（見 docs/decisions.md D15）：
//   - 一支 Joy-Con（左或右）直握：同一支搖桿上下 = 油門、左右 = 轉向
//   - 一支大手把（Switch Pro、Xbox、PS4／PS5 等雙搖桿手把）：左搖桿上下 = 油門、右搖桿左右 = 轉向
// 同時連兩支會讓藍牙連線不穩、延遲變大，所以：
//   - 已經有一支手把時，其他手把一律斷開
//   - 連上之後關掉「接受新配對」（Bluepad32 不再搜尋新手把），斷線後再打開
// 連上後要先歸零、再等搖桿回到中間才能開車；斷線就停車。

namespace {

enum Slot { SLOT_LEFT, SLOT_RIGHT, SLOT_STANDARD, SLOT_COUNT };  // 順序與 GamepadSlotId 相同
const char* const SLOT_NAMES[SLOT_COUNT] = {"左 Joy-Con", "右 Joy-Con", "大手把"};
const char* const SLOT_SHORT_NAMES[SLOT_COUNT] = {"JL", "JR", "STD"};

ControllerPtr active = nullptr;  // 目前使用中的那一支手把
Slot activeSlot = SLOT_STANDARD;
bool activeStreams = false;      // Switch 手把：連線時會持續送資料，可以用「多久沒收到資料」判斷失聯
unsigned long lastDataMs = 0;    // 最近一次收到新資料的時間
bool dataStalled = false;        // 超過 PAD_DATA_TIMEOUT_MS 沒收到資料

GamepadState state;
bool prevPlus = false;
bool prevB = false;
unsigned long lastRumbleMs = 0;
unsigned long updateCount = 0;
// 連上（歸零完成）後要等搖桿回到中間才能開，避免連線瞬間搖桿剛好被推著就暴衝
bool waitNeutral = false;
bool prevReady = false;

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
VibrationEnableJob vibrationJob;

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

void enableJoyConVibration(ControllerPtr ctl) {
  if (vibrationJob.pending) return;  // 上一次還沒送出，不能重複登記同一個 registration
  ControllerProperties props = ctl->getProperties();
  memcpy(vibrationJob.btaddr, props.btaddr, sizeof(vibrationJob.btaddr));
  vibrationJob.registration = {};
  vibrationJob.registration.callback = &sendVibrationEnableOnBtThread;
  vibrationJob.registration.context = &vibrationJob;
  vibrationJob.pending = true;
  btstack_run_loop_execute_on_main_thread(&vibrationJob.registration);
}

// ===== 連線時自動歸零 =====
// 4.1.0 讀右 Joy-Con 校正資料有誤，放開搖桿時轉向讀值停在約 +60，超過死區，車子會自己偏。
// 所以手把連上後先取 STICK_CENTER_SAMPLE_MS 的平均當作中心點；歸零完成前不能開車。
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
StickCalibration calibration;

void onConnected(ControllerPtr ctl) {
  int model = ctl->getModel();
  String name = ctl->getModelName();

  if (active) {
    logEvent("gamepad", "拒絕 %s（model=%d）：一次只能用一支手把；要換手把請先關掉目前這支", name.c_str(), model);
    ctl->disconnect();
    return;
  }

  Slot slot;
  if (model == Controller::CONTROLLER_TYPE_SwitchJoyConLeft) {
    slot = SLOT_LEFT;
  } else if (model == Controller::CONTROLLER_TYPE_SwitchJoyConRight) {
    slot = SLOT_RIGHT;
  } else if (ctl->isGamepad()) {
    slot = SLOT_STANDARD;  // Switch Pro、Xbox、PS4／PS5 等雙搖桿手把都走這裡
  } else {
    logEvent("gamepad", "拒絕 %s（model=%d）：不是手把（滑鼠、鍵盤等）", name.c_str(), model);
    ctl->disconnect();
    return;
  }

  active = ctl;
  activeSlot = slot;
  activeStreams = slot != SLOT_STANDARD || model == Controller::CONTROLLER_TYPE_SwitchProController;
  lastDataMs = millis();
  dataStalled = false;
  calibration = StickCalibration();
  if (slot != SLOT_STANDARD) enableJoyConVibration(ctl);
  // 已經有手把了，停止搜尋新手把：藍牙不用一直掃描，連線比較穩、延遲比較小
  BP32.enableNewBluetoothConnections(false);
  logEvent("gamepad", "%s 已連線：%s（model=%d）；停止搜尋其他手把。連線時先不要碰搖桿", SLOT_NAMES[slot],
           name.c_str(), model);
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl != active) return;  // 被拒絕的那支斷線，不用處理
  active = nullptr;
  BP32.enableNewBluetoothConnections(true);
  logEvent("gamepad", "%s 斷線，停車；重新開始搜尋手把", SLOT_NAMES[activeSlot]);
}

// ===== 原始搖桿讀值：[0] = 油門（正值前進）、[1] = 轉向（正值右轉）=====
// Joy-Con 直握時，Bluepad32 把單支 Joy-Con 當成橫握的小手把，軸向轉了 90 度：
//   左 Joy-Con：往上推 axisX 是負值（2026-11-04 實測）；往右推 axisY 是負值（由轉向關係推算，待實測）
//   右 Joy-Con：往右推 axisY 是正值（2026-11-04 實測）；往上推 axisX 是正值（由轉向關係推算，待實測）
void rawAxes(ControllerPtr ctl, int out[2]) {
  if (activeSlot == SLOT_LEFT) {
    out[0] = JOYCON_LEFT_THROTTLE_INVERTED ? ctl->axisX() : -ctl->axisX();
    out[1] = JOYCON_LEFT_STEER_INVERTED ? ctl->axisY() : -ctl->axisY();
  } else if (activeSlot == SLOT_RIGHT) {
    out[0] = JOYCON_RIGHT_THROTTLE_INVERTED ? -ctl->axisX() : ctl->axisX();
    out[1] = JOYCON_RIGHT_STEER_INVERTED ? -ctl->axisY() : ctl->axisY();
  } else {
    out[0] = -ctl->axisY();  // 大手把：左搖桿往上推時 axisY 是負值
    out[1] = ctl->axisRX();  // 大手把：右搖桿左右
  }
}

void updateCalibration(ControllerPtr ctl) {
  StickCalibration& c = calibration;
  if (c.done) return;

  int v[2];
  rawAxes(ctl, v);
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
    logEvent("gamepad", "%s 歸零失敗：請放開搖桿（平均 %d／%d），重新取樣", SLOT_NAMES[activeSlot], avg[0], avg[1]);
    c.started = false;
    return;
  }
  c.center[0] = avg[0];
  c.center[1] = avg[1];
  c.done = true;
  logEvent("gamepad", "%s 搖桿歸零完成：油門中心 %d、轉向中心 %d", SLOT_NAMES[activeSlot], avg[0], avg[1]);
}

// 扣掉中心點、套死區，再把「死區～推到底」線性放大到 0～±512：
//   離中心 < STICK_DEADZONE → 0；離中心 ≥ fullTravel → ±512（100%）
// 實測 Joy-Con 推到底離中心只有約 ±380，所以不能假設 ±512，不然永遠推不到全速
int shapeAxis(int value, int center, int fullTravel) {
  int delta = value - center;
  int magnitude = abs(delta);
  if (magnitude < STICK_DEADZONE) return 0;
  long scaled = (long)(magnitude - STICK_DEADZONE) * 512 / (fullTravel - STICK_DEADZONE);
  if (scaled > 512) scaled = 512;
  return delta > 0 ? (int)scaled : -(int)scaled;
}

void readSticks(GamepadState& next) {
  int v[2];
  rawAxes(active, v);
  int fullTravel = activeSlot == SLOT_STANDARD ? STICK_FULL_TRAVEL_STANDARD : STICK_FULL_TRAVEL_JOYCON;
  next.throttle = shapeAxis(v[0], calibration.center[0], fullTravel);
  next.steer = shapeAxis(v[1], calibration.center[1], fullTravel);
}

// 按鍵：plus = 切換模式鍵、b = 車燈鍵
void readButtons(GamepadState& next, bool& plus, bool& b) {
  ControllerPtr ctl = active;
  if (activeSlot == SLOT_RIGHT) {
    // 右 Joy-Con（2026-11-04 實測）：ZR → r2()、「＋」→ MISC_BUTTON_START、手把上印的 A → a()、B → x()
    next.boost = ctl->r2();
    next.hornHeld = ctl->a();
    plus = ctl->miscStart();
    b = ctl->x();
  } else if (activeSlot == SLOT_LEFT) {
    // 左 Joy-Con（2026-11-25 實測）：ZL → r2()、L → l2()、SL → l1()、SR → r1()、「−」→ misc 0x02
    // 左 Joy-Con 沒有「＋」，改用「−」切換模式；ZL 或 L 加速
    next.boost = ctl->r2() || ctl->l2();
    next.hornHeld = ctl->a();     // 選配喇叭（對應哪顆鍵待實測）
    plus = ctl->miscSelect();     // 「−」
    b = ctl->x();                 // 選配車燈
  } else {
    // 右扳機（Switch 的 ZR、Xbox 的 RT、PS 的 R2）可能回報成 r2() 或類比的 throttle()，兩個都看
    next.boost = ctl->r2() || ctl->throttle() > 512;
    // Bluepad32 依 Xbox 的「按鍵位置」對應：Xbox 手把的 A/B 就是 a()/b()；
    // Switch Pro 手把上印的 A/B 位置和 Xbox 相反，PS 手把是 ✕／○，請用 test_gamepad 確認
    next.hornHeld = ctl->a();
    plus = ctl->miscStart();  // Switch 的「＋」、Xbox 的 ≡ 選單鍵、PS 的 OPTIONS
    b = ctl->b();
  }
}

}  // namespace

void gamepadBegin() {
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);
  BP32.enableNewBluetoothConnections(true);
  // 手把一直連不上或重連失敗時，可以暫時打開下面這行清除配對紀錄
  // BP32.forgetBluetoothKeys();
}

void gamepadUpdate() {
  BP32.update();

  bool usable = isUsable(active);
  unsigned long now = millis();
  if (usable && active->hasData()) {
    updateCount++;
    lastDataMs = now;
    if (dataStalled) {
      dataStalled = false;
      logEvent("gamepad", "%s 恢復送資料", SLOT_NAMES[activeSlot]);
    }
  }
  // 失聯保護：Switch 手把超過 PAD_DATA_TIMEOUT_MS 沒送資料就停車（Bluepad32 不一定會回報斷線，
  // 這時讀到的會是最後一筆搖桿值，不停車的話車子會照最後的指令一直跑）
  if (usable && activeStreams && !dataStalled && now - lastDataMs > PAD_DATA_TIMEOUT_MS) {
    dataStalled = true;
    logEvent("gamepad", "%s 超過 %lu ms 沒有送資料，當作失聯、停車", SLOT_NAMES[activeSlot], PAD_DATA_TIMEOUT_MS);
  }
  if (usable) updateCalibration(active);
  bool ready = usable && calibration.done && !dataStalled;

  GamepadState next;
  bool plus = false;
  bool b = false;
  if (ready) {
    readSticks(next);
    readButtons(next, plus, b);
  }

  if (ready && !prevReady) {
    waitNeutral = true;
    logEvent("gamepad", "使用 %s；放開搖桿（回到中間）後就能開車", SLOT_NAMES[activeSlot]);
  }
  if (!ready) waitNeutral = false;
  prevReady = ready;
  if (waitNeutral && next.throttle == 0 && next.steer == 0) {
    waitNeutral = false;
    logEvent("gamepad", "搖桿已回中，可以開車");
  }

  // connected = false → 主程式停車並回到手動模式（T／S 仍保留，監看網頁看得到搖桿被推著）
  next.connected = ready && !waitNeutral;
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
  if (!isUsable(active) || static_cast<Slot>(slotId) != activeSlot) return PAD_ABSENT;
  return calibration.done ? PAD_READY : PAD_CALIBRATING;
}

const char* gamepadShortName() {
  return isUsable(active) ? SLOT_SHORT_NAMES[activeSlot] : "-";
}

unsigned long gamepadTakeUpdateCount() {
  unsigned long n = updateCount;
  updateCount = 0;
  return n;
}

void gamepadRumble(int durationMs) {
  unsigned long now = millis();
  if (now - lastRumbleMs < RUMBLE_COOLDOWN_MS) return;
  lastRumbleMs = now;
  // Joy-Con 要先送過「開啟震動」指令才會震（連線時已由 enableJoyConVibration() 補送）
  if (isUsable(active)) active->playDualRumble(0, durationMs, 0x80, 0x40);
}
