// Prometheus 客户端模块：
// 负责把配置转换成真实 PromQL，请求 Prometheus HTTP API，
// 再把返回的 JSON 解析成 8 路表盘可直接消费的数据结构。
#ifndef CYBERMONITOR_PROMETHEUS_CLIENT_H
#define CYBERMONITOR_PROMETHEUS_CLIENT_H

#include <Arduino.h>

#include "Config.h"

// 8 路表盘的固定通道定义。
enum GaugeChannel : uint8_t {
  GAUGE_CPU_LOAD = 0,
  GAUGE_CPU_TEMP,
  GAUGE_GPU_LOAD,
  GAUGE_RAM_USAGE,
  GAUGE_NET_UP,
  GAUGE_NET_DOWN,
  GAUGE_DISK_READ,
  GAUGE_DISK_WRITE,
  GAUGE_COUNT
};

struct MetricPoint {
  const char *name = "";
  float value = 0.0f;
  bool valid = false;
};

struct MetricsSnapshot {
  MetricPoint points[GAUGE_COUNT];
  bool success = false;
  int httpCode = 0;
  uint32_t latencyMs = 0;
  String errorMessage;
};

class PrometheusClient {
 public:
  MetricsSnapshot fetchMetrics(const AppConfig &config);

 private:
  String buildQueryUrl(const AppConfig &config) const;
  String buildWindowsQuery(const AppConfig &config) const;
  static String buildLabelMatcher(const AppConfig &config, const String &extraMatchers);
  static String wrapChannelQuery(const String &channelName, const String &expression);
  static String escapePrometheusString(const String &value);
  static String urlEncode(const String &value);
  static int channelNameToIndex(const String &channelName);
};

#endif
