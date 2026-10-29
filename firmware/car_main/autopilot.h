// autopilot.h — 自動避障（非阻塞的狀態機）
#pragma once

enum class AutoState { CRUISE, BACKUP, TURN };

// 進入自動模式時呼叫，從「直行」狀態開始
void autopilotReset();

// 依三顆超音波的距離（cm）算出左右輪速度（%，-100～100）
void autopilotStep(int leftCm, int centerCm, int rightCm, int& leftPct, int& rightPct);

AutoState autopilotState();
const char* autopilotStateName();
