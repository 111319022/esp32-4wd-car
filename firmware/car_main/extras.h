// extras.h — 選配的車燈與喇叭（由 config.h 的 FEATURE_LIGHTS / FEATURE_BUZZER 開關）
#pragma once

void extrasBegin();

// lightTogglePressed：B 剛按下；hornHeld：按住 A
// stopped：左右輪都是 0；reversing：兩側都在後退
void extrasUpdate(bool lightTogglePressed, bool hornHeld, bool stopped, bool reversing);

// 立刻關掉所有燈和蜂鳴器（例如手把斷線時）
void extrasOff();
