// test_gamepad — Switch 手把連線測試（需要 esp32_bluepad32 開發板套件）
//
// 1. 上傳後打開序列埠監控視窗（115200）
// 2. 手把進入配對模式：Pro 手把按住頂部的小同步鍵；Joy-Con 按住側邊滑軌上的同步鍵
// 3. 連上後會印出搖桿與按鍵狀態
//
// 要確認的事（結果請記到 docs/testing.md）：
//   - 左搖桿往上推時 axisY 是正還是負
//   - 手把上印的 A、B 鍵，各自讓 a()、b() 哪一個變成 1（Bluepad32 依按鍵位置對應，可能相反）
//   - 按「＋」時 misc 欄位會出現 0x04
//   - 按 ZR 時是 r2 變成 1，還是 throttle 變大
//   - 按 X 鍵（或對應位置）時手把會不會震動

#include <Bluepad32.h>

ControllerPtr pad = nullptr;

void onConnected(ControllerPtr ctl) {
  if (pad == nullptr) {
    pad = ctl;
    Serial.println("手把已連線");
  }
}

void onDisconnected(ControllerPtr ctl) {
  if (ctl == pad) {
    pad = nullptr;
    Serial.println("手把斷線");
  }
}

void setup() {
  Serial.begin(115200);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);
  Serial.println("test_gamepad：請讓手把進入配對模式");
}

void loop() {
  bool updated = BP32.update();
  if (updated && pad && pad->isConnected() && pad->hasData()) {
    Serial.printf("LX=%4d LY=%4d RX=%4d RY=%4d | a=%d b=%d x=%d y=%d | l1=%d r1=%d l2=%d r2=%d | brake=%4d throttle=%4d | misc=0x%02x\n",
                  pad->axisX(), pad->axisY(), pad->axisRX(), pad->axisRY(), pad->a(), pad->b(), pad->x(), pad->y(),
                  pad->l1(), pad->r1(), pad->l2(), pad->r2(), pad->brake(), pad->throttle(), pad->miscButtons());
    if (pad->x()) {
      pad->playDualRumble(0, 250, 0x80, 0x40);
    }
  }
  delay(100);
}
