// motors.h — L298N 坦克式驅動（左側一組、右側一組）
#pragma once

void motorsBegin();

// 速度 -255～255，正值前進、負值後退、0 停止（滑行）
void motorsSet(int left, int right);

// 速度 -100～100（%），內部換算成 PWM
void motorsSetPercent(int leftPct, int rightPct);

void motorsStop();
