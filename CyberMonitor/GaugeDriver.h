// 表盘驱动模块：
// 负责 8 路 LEDC PWM 初始化、数值归一化映射，以及机械表头的平滑步进。
#ifndef CYBERMONITOR_GAUGE_DRIVER_H
#define CYBERMONITOR_GAUGE_DRIVER_H

#include <Arduino.h>

#include "PrometheusClient.h"

class GaugeDriver {
 public:
  void begin();
  void update();
  void setTargetsFromSnapshot(const MetricsSnapshot &snapshot);
  void resetTargets();

 private:
  static uint8_t scaleToDuty(float value, float minValue, float maxValue);
  uint8_t mapMetricToDuty(GaugeChannel channel, float value) const;
  void writeDuty(uint8_t channel, uint8_t duty);

  uint8_t currentDuty[GAUGE_COUNT] = {0};
  uint8_t targetDuty[GAUGE_COUNT] = {0};
  uint32_t lastStepAtMs = 0;
};

#endif
