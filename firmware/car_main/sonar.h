// sonar.h — 三顆 HC-SR04 輪流測距（左 → 中 → 右）
#pragma once

enum SonarId { SONAR_LEFT = 0, SONAR_CENTER = 1, SONAR_RIGHT = 2, SONAR_COUNT = 3 };

void sonarBegin();

// 每次呼叫最多量一顆；距離上次量測未滿 SONAR_INTERVAL_MS 就直接返回 false
// 量測時會阻塞最多 SONAR_TIMEOUT_US（約 20 ms）
bool sonarUpdate();

// 最近一次量到的距離（cm）；沒收到回波時是 SONAR_MAX_CM
int sonarCm(SonarId id);
