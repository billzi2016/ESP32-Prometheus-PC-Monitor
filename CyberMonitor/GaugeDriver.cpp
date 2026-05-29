#include "GaugeDriver.h"

#include <esp32-hal-ledc.h>

namespace {
// 8 路机械表盘的默认 GPIO 分配，与文档中的接线方案保持一致。
const uint8_t kGaugePins[GAUGE_COUNT] = {13, 14, 15, 25, 26, 27, 32, 33};
const uint8_t kPwmChannels[GAUGE_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};
const uint32_t kPwmFrequency = 1000;
const uint8_t kPwmResolution = 8;
// 步进周期越短，指针越丝滑；越长，阻尼感越重。
const uint32_t kSmoothingStepIntervalMs = 8;

const float kPercentMin = 0.0f;
const float kPercentMax = 100.0f;
const float kCpuTempMin = 0.0f;
const float kCpuTempMax = 100.0f;
const float kNetBytesPerSecondMax = 125000000.0f;
const float kDiskBytesPerSecondMax = 500000000.0f;
}

void GaugeDriver::begin() {
  // 每一路表盘都映射到独立的 LEDC 通道，避免互相抢占 PWM 资源。
  for (uint8_t i = 0; i < GAUGE_COUNT; ++i) {
    ledcAttachChannel(kGaugePins[i], kPwmFrequency, kPwmResolution, kPwmChannels[i]);
    writeDuty(i, 0);
  }
}

void GaugeDriver::update() {
  // 平滑算法核心：
  // 当前值每次只向目标值逼近 1 个 duty 单位，
  // 让机械指针表现出类似阻尼的“缓慢跟随”效果。
  const uint32_t now = millis();
  if (now - lastStepAtMs < kSmoothingStepIntervalMs) {
    return;
  }

  lastStepAtMs = now;

  for (uint8_t i = 0; i < GAUGE_COUNT; ++i) {
    if (currentDuty[i] == targetDuty[i]) {
      continue;
    }

    if (currentDuty[i] < targetDuty[i]) {
      currentDuty[i]++;
    } else {
      currentDuty[i]--;
    }

    writeDuty(i, currentDuty[i]);
  }
}

void GaugeDriver::setTargetsFromSnapshot(const MetricsSnapshot &snapshot) {
  // 只更新本轮返回有效值的通道，缺失通道保持原目标值，避免异常响应抖动。
  for (uint8_t i = 0; i < GAUGE_COUNT; ++i) {
    if (!snapshot.points[i].valid) {
      continue;
    }

    targetDuty[i] =
        mapMetricToDuty(static_cast<GaugeChannel>(i), snapshot.points[i].value);
  }
}

void GaugeDriver::resetTargets() {
  // 进入配网模式时，不再展示旧运行数据，因此目标值全部回零。
  for (uint8_t i = 0; i < GAUGE_COUNT; ++i) {
    targetDuty[i] = 0;
  }
}

uint8_t GaugeDriver::scaleToDuty(float value, float minValue, float maxValue) {
  // 统一把不同量纲的指标压缩成 0-255 的 8-bit PWM 占空比。
  if (value <= minValue) {
    return 0;
  }

  if (value >= maxValue) {
    return 255;
  }

  const float normalized = (value - minValue) / (maxValue - minValue);
  return static_cast<uint8_t>(normalized * 255.0f);
}

uint8_t GaugeDriver::mapMetricToDuty(GaugeChannel channel, float value) const {
  // 不同指标的量程不同，因此按通道分别映射。
  switch (channel) {
    case GAUGE_CPU_LOAD:
    case GAUGE_GPU_LOAD:
    case GAUGE_RAM_USAGE:
      return scaleToDuty(value, kPercentMin, kPercentMax);
    case GAUGE_CPU_TEMP:
      return scaleToDuty(value, kCpuTempMin, kCpuTempMax);
    case GAUGE_NET_UP:
    case GAUGE_NET_DOWN:
      return scaleToDuty(value, 0.0f, kNetBytesPerSecondMax);
    case GAUGE_DISK_READ:
    case GAUGE_DISK_WRITE:
      return scaleToDuty(value, 0.0f, kDiskBytesPerSecondMax);
    case GAUGE_COUNT:
    default:
      return 0;
  }
}

void GaugeDriver::writeDuty(uint8_t channel, uint8_t duty) {
  // 真正写入硬件 PWM 的地方统一收口到这里，便于后续做校准或反向控制。
  ledcWriteChannel(kPwmChannels[channel], duty);
}
