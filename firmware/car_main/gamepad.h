// gamepad.h — 透過 Bluepad32 讀取手把：一次只用一支（見 docs/decisions.md D15）
// 可以是一支 Joy-Con（左或右，直握），或一支大手把（Switch Pro、Xbox、PS4／PS5 等）
#pragma once

struct GamepadState {
  bool connected = false;  // 可以開車：手把在線、歸零完成，且連上後搖桿已回到中間
  int throttle = 0;  // 油門，-512～512，正值前進（Joy-Con：搖桿上下；大手把：左搖桿上下）
  int steer = 0;     // 轉向，-512～512，正值右轉（Joy-Con：同一支搖桿左右；大手把：右搖桿左右）
  bool boost = false;              // 按住加速鍵（右 Joy-Con ZR、左 Joy-Con ZL、大手把右扳機）
  bool modeTogglePressed = false;  // 切換模式鍵剛按下（只在按下那一次為 true）
  bool hornHeld = false;           // 喇叭鍵（選配）
  bool lightTogglePressed = false; // 車燈鍵剛按下（選配）
  bool plusHeld = false;           // 切換模式鍵按住中（給除錯輸出用）
  bool lightHeld = false;          // 車燈鍵按住中（給除錯輸出用）
};

// 目前那一支手把的狀態（給除錯輸出用）；同一時間最多只有一種不是 PAD_ABSENT
enum GamepadSlotStatus { PAD_ABSENT = 0, PAD_CALIBRATING = 1, PAD_READY = 2 };
enum GamepadSlotId { PAD_SLOT_LEFT = 0, PAD_SLOT_RIGHT = 1, PAD_SLOT_PRO = 2 };  // PRO = 大手把
GamepadSlotStatus gamepadSlotStatus(GamepadSlotId slot);

// 目前手把的簡稱（給 LOG 用）："JL"、"JR"、"STD"，沒有手把是 "-"
const char* gamepadShortName();

// 從上次呼叫到現在，手把送來幾次新資料（給 LOG 算每秒更新次數，用來抓延遲）
unsigned long gamepadTakeUpdateCount();

void gamepadBegin();

// 每次 loop 呼叫一次
void gamepadUpdate();

const GamepadState& gamepadState();

// 讓手把震動；內建冷卻時間 RUMBLE_COOLDOWN_MS，連續呼叫不會一直震
void gamepadRumble(int durationMs);
