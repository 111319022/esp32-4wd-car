// gamepad.h — 透過 Bluepad32 讀取 Switch 手把（只接受第一支連上的手把）
#pragma once

struct GamepadState {
  bool connected = false;
  int throttle = 0;  // 油門，-512～512，正值前進（左搖桿上下）
  int steer = 0;     // 轉向，-512～512，正值右轉（右搖桿左右）
  bool boost = false;              // 按住 ZR：加速
  bool modeTogglePressed = false;  // 「＋」剛按下（只在按下那一次為 true）
  bool hornHeld = false;           // A：喇叭（選配）
  bool lightTogglePressed = false; // B 剛按下：車燈開關（選配）
};

void gamepadBegin();

// 每次 loop 呼叫一次
void gamepadUpdate();

const GamepadState& gamepadState();

// 讓手把震動；內建冷卻時間 RUMBLE_COOLDOWN_MS，連續呼叫不會一直震
void gamepadRumble(int durationMs);
