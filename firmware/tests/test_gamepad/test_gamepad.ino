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
//   - 按住該支手把的 a 鍵時會不會震動（Bluepad32 4.1.0 的 Joy-Con 可能不會震）

#include <Bluepad32.h>

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

void onConnected(ControllerPtr ctl) {
  for (int i = 0; i < MAX_PADS; i++) {
    if (pads[i] == nullptr) {
      pads[i] = ctl;
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
