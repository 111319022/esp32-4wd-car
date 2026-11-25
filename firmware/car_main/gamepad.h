// gamepad.h — 透過 Bluepad32 讀取手把（Joy-Con 與標準手把）
// 主要用左右兩支 Joy-Con，只連上一支 Joy-Con（左或右）也能開，
// 也接受一支標準手把（Switch Pro、Xbox、PS4／PS5 等）
#pragma once

struct GamepadState {
  bool connected = false;  // 可以開車：有可用的手把組合、歸零完成，且組合改變後搖桿已回中
  int throttle = 0;  // 油門，-512～512，正值前進（雙 Joy-Con 時是左搖桿上下，單支時是那支搖桿上下）
  int steer = 0;     // 轉向，-512～512，正值右轉（雙 Joy-Con 時是右搖桿左右，單支時是那支搖桿左右）
  bool boost = false;              // 按住 ZR：加速
  bool modeTogglePressed = false;  // 「＋」剛按下（只在按下那一次為 true）
  bool hornHeld = false;           // A：喇叭（選配）
  bool lightTogglePressed = false; // B 剛按下：車燈開關（選配）
  bool plusHeld = false;           // 「＋」按住中（給除錯輸出用）
  bool lightHeld = false;          // B 按住中（給除錯輸出用）
};

// 個別手把的狀態（給除錯輸出用）
enum GamepadSlotStatus { PAD_ABSENT = 0, PAD_CALIBRATING = 1, PAD_READY = 2 };
enum GamepadSlotId { PAD_SLOT_LEFT = 0, PAD_SLOT_RIGHT = 1, PAD_SLOT_PRO = 2 };
GamepadSlotStatus gamepadSlotStatus(GamepadSlotId slot);

void gamepadBegin();

// 每次 loop 呼叫一次
void gamepadUpdate();

const GamepadState& gamepadState();

// 讓手把震動；內建冷卻時間 RUMBLE_COOLDOWN_MS，連續呼叫不會一直震
void gamepadRumble(int durationMs);
