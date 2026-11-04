// test_gamepad — Switch 手把連線測試（需要 esp32_bluepad32 開發板套件）
//
// 1. 上傳後打開序列埠監控視窗（115200）
// 2. 左右兩支 Joy-Con 各自按住側邊滑軌上的同步鍵（也可以測一支 Pro 手把：按住頂部的小同步鍵）
// 3. 連上後每支手把一行，印出種類與搖桿、按鍵狀態
//
// 要確認的事（結果請記到 docs/testing.md）：
//   - 連線時印出的 model：左 Joy-Con 應為 39、右 Joy-Con 應為 40、Pro 手把為 38
//   - Joy-Con 直握：左 Joy-Con 搖桿往上推時，axisX 是不是負值（程式假設是）
//   - Joy-Con 直握：右 Joy-Con 搖桿往右推時，axisY 是不是正值（程式假設是）
//   - 右 Joy-Con 上印的 A、B 鍵，各自讓 a/b/x/y 哪一個變成 1（程式假設 A → a、B → x）
//   - 右 Joy-Con 按 ZR 時 r2 會不會變成 1；按「＋」時 misc 會不會出現 0x04
//   - 按住該支手把的 a 鍵時會不會震動
//     Joy-Con 連上時會補送「開啟震動」指令（esp32_bluepad32 4.1.0 沒送），做法與 car_main/gamepad.cpp 相同
//     ENABLE_JOYCON_VIBRATION 改成 0 可以比較補送前後的差別

#include <Bluepad32.h>

extern "C" {
#include <btstack_run_loop.h>
#include <uni_hid_device.h>
}

#define ENABLE_JOYCON_VIBRATION 1

constexpr int MAX_PADS = 2;
ControllerPtr pads[MAX_PADS] = {nullptr, nullptr};

const char* modelName(int model) {
  switch (model) {
    case Controller::CONTROLLER_TYPE_SwitchJoyConLeft:
      return "左 Joy-Con";
    case Controller::CONTROLLER_TYPE_SwitchJoyConRight:
      return "右 Joy-Con";
    case Controller::CONTROLLER_TYPE_SwitchProController:
      return "Pro 手把";
    default:
      return "其他手把";
  }
}

// ===== 補送 Joy-Con「開啟震動」指令（說明見 car_main/gamepad.cpp）=====
struct VibrationEnableJob {
  btstack_context_callback_registration_t registration;
  uint8_t btaddr[6];
  volatile bool pending;
};
VibrationEnableJob vibrationJobs[MAX_PADS];

void sendVibrationEnableOnBtThread(void* context) {
  VibrationEnableJob* job = static_cast<VibrationEnableJob*>(context);
  uni_hid_device_t* d = uni_hid_device_get_instance_for_address(job->btaddr);
  if (d != nullptr) {
    static uint8_t packetNum = 0;
    uint8_t report[] = {0xA2, 0x01, packetNum, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x48, 0x01};
    packetNum = (packetNum + 1) & 0x0F;
    uni_hid_device_send_intr_report(d, report, sizeof(report));
  }
  job->pending = false;
}

void enableJoyConVibration(ControllerPtr ctl, int slot) {
  VibrationEnableJob& job = vibrationJobs[slot];
  if (job.pending) return;
  ControllerProperties props = ctl->getProperties();
  memcpy(job.btaddr, props.btaddr, sizeof(job.btaddr));
  job.registration = {};
  job.registration.callback = &sendVibrationEnableOnBtThread;
  job.registration.context = &job;
  job.pending = true;
  btstack_run_loop_execute_on_main_thread(&job.registration);
  Serial.printf("[%d] 已補送 Joy-Con 開啟震動指令\n", slot);
}

void onConnected(ControllerPtr ctl) {
  for (int i = 0; i < MAX_PADS; i++) {
    if (pads[i] == nullptr) {
      pads[i] = ctl;
#if ENABLE_JOYCON_VIBRATION
      int model = ctl->getModel();
      if (model == Controller::CONTROLLER_TYPE_SwitchJoyConLeft || model == Controller::CONTROLLER_TYPE_SwitchJoyConRight) {
        enableJoyConVibration(ctl, i);
      }
#endif
      Serial.printf("[%d] 已連線：%s（model=%d, %s）\n", i, modelName(ctl->getModel()), ctl->getModel(),
                    ctl->getModelName().c_str());
      return;
    }
  }
  Serial.println("已經有兩支手把，忽略這支");
}

void onDisconnected(ControllerPtr ctl) {
  for (int i = 0; i < MAX_PADS; i++) {
    if (pads[i] == ctl) {
      pads[i] = nullptr;
      Serial.printf("[%d] 斷線\n", i);
    }
  }
}

void setup() {
  Serial.begin(115200);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);
  Serial.println("test_gamepad：請讓手把進入配對模式（兩支 Joy-Con 要分別配對）");
}

void loop() {
  BP32.update();
  for (int i = 0; i < MAX_PADS; i++) {
    ControllerPtr pad = pads[i];
    if (!pad || !pad->isConnected() || !pad->hasData()) continue;
    Serial.printf("[%d] %-8s X=%4d Y=%4d RX=%4d RY=%4d | a=%d b=%d x=%d y=%d | l1=%d r1=%d l2=%d r2=%d | "
                  "brake=%4d throttle=%4d | misc=0x%02x\n",
                  i, modelName(pad->getModel()), pad->axisX(), pad->axisY(), pad->axisRX(), pad->axisRY(), pad->a(),
                  pad->b(), pad->x(), pad->y(), pad->l1(), pad->r1(), pad->l2(), pad->r2(), pad->brake(),
                  pad->throttle(), pad->miscButtons());
    if (pad->a()) {
      pad->playDualRumble(0, 250, 0x80, 0x40);
    }
  }
  delay(100);
}
