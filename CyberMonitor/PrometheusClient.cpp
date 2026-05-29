#include "PrometheusClient.h"

#include <ctype.h>

#include <ArduinoJson.h>
#include <HTTPClient.h>

namespace {
// 终端内部使用的 8 路逻辑通道名。
// 它们不是 Prometheus 原始 metric 名，而是组合查询后由 label_replace 注入的 channel 标签值。
const char *kChannelNames[GAUGE_COUNT] = {"cpu_load",
                                          "cpu_temp",
                                          "gpu_load",
                                          "ram_usage",
                                          "net_up",
                                          "net_down",
                                          "disk_read",
                                          "disk_write"};
}

MetricsSnapshot PrometheusClient::fetchMetrics(const AppConfig &config) {
  MetricsSnapshot snapshot;
  for (size_t i = 0; i < GAUGE_COUNT; ++i) {
    snapshot.points[i].name = kChannelNames[i];
  }

  if (!config.isValid()) {
    snapshot.errorMessage = "Invalid config";
    return snapshot;
  }

  HTTPClient http;
  http.setTimeout(3000);

  // 这里走单次 HTTP 请求，而不是 8 次逐路查询。
  // 原因是 ESP32 内存和网络开销都有限，组合查询更符合这个项目的资源条件。
  const String url = buildQueryUrl(config);
  if (!http.begin(url)) {
    snapshot.errorMessage = "HTTP begin failed";
    return snapshot;
  }

  const uint32_t startedAt = millis();
  snapshot.httpCode = http.GET();
  snapshot.latencyMs = millis() - startedAt;

  if (snapshot.httpCode != HTTP_CODE_OK) {
    snapshot.errorMessage = "HTTP " + String(snapshot.httpCode);
    http.end();
    return snapshot;
  }

  const String payload = http.getString();
  http.end();

  StaticJsonDocument<256> filter;
  // 只解析 channel 和 value，主动丢弃 Prometheus 返回的其余字段，降低内存占用。
  filter["data"]["result"][0]["metric"]["channel"] = true;
  filter["data"]["result"][0]["value"][1] = true;

  StaticJsonDocument<4096> document;
  DeserializationError error =
      deserializeJson(document, payload, DeserializationOption::Filter(filter));

  if (error) {
    snapshot.errorMessage = error.c_str();
    return snapshot;
  }

  JsonArray results = document["data"]["result"].as<JsonArray>();
  if (results.isNull()) {
    snapshot.errorMessage = "No results";
    return snapshot;
  }

  uint8_t validCount = 0;
  for (JsonObject item : results) {
    const String channelName = item["metric"]["channel"] | "";
    const char *valueText = item["value"][1] | nullptr;
    if (channelName.length() == 0 || valueText == nullptr) {
      continue;
    }

    const int channel = channelNameToIndex(channelName);
    if (channel < 0 || channel >= GAUGE_COUNT) {
      continue;
    }

    snapshot.points[channel].value = String(valueText).toFloat();
    snapshot.points[channel].valid = true;
    validCount++;
  }

  if (validCount == 0) {
    snapshot.errorMessage = "No mapped metrics";
    return snapshot;
  }

  snapshot.success = true;
  return snapshot;
}

String PrometheusClient::buildQueryUrl(const AppConfig &config) const {
  String url = "http://";
  url += config.prometheusHost;
  url += ":";
  url += String(config.prometheusPort);
  url += "/api/v1/query?query=";
  url += urlEncode(buildWindowsQuery(config));
  return url;
}

String PrometheusClient::buildWindowsQuery(const AppConfig &config) const {
  // 真实 Prometheus 查询策略：
  // 1. 直接使用 windows_exporter 的实际指标
  // 2. 每一路查询最终都收敛成一条 instant vector
  // 3. 再用 label_replace 统一追加 channel 标签，便于终端按通道解析
  const String cpuMatcher = buildLabelMatcher(config, "");
  const String cpuLoadExpression =
      "sum by (instance) (clamp_max((rate(windows_cpu_processor_utility_total" +
      cpuMatcher + "[1m]) / rate(windows_cpu_processor_rtc_total" + cpuMatcher +
      "[1m])), 100)) / count by (instance) (windows_cpu_processor_utility_total" +
      cpuMatcher + ")";

  const String cpuTempExpression =
      "max by (instance) (windows_thermalzone_temperature_celsius" +
      buildLabelMatcher(config, "") + ")";

  const String gpuExpression =
      "clamp_max((sum by (instance) (rate(windows_gpu_engine_time_seconds" +
      buildLabelMatcher(
          config, ", phys=\"" + escapePrometheusString(config.gpuPhysicalIndex) +
                      "\", engtype=\"3D\"") +
      "[1m])) * 100), 100)";

  const String ramExpression =
      "100 - 100 * windows_memory_physical_free_bytes" + buildLabelMatcher(config, "") +
      " / windows_memory_physical_total_bytes" + buildLabelMatcher(config, "");

  const String netUpExpression =
      "sum by (instance) (rate(windows_net_bytes_sent_total" +
      buildLabelMatcher(
          config,
          ", nic!~\"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*\"") +
      "[1m]))";

  const String netDownExpression =
      "sum by (instance) (rate(windows_net_bytes_received_total" +
      buildLabelMatcher(
          config,
          ", nic!~\"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*\"") +
      "[1m]))";

  const String diskReadExpression =
      "sum by (instance) (rate(windows_logical_disk_read_bytes_total" +
      buildLabelMatcher(config, ", volume=~\"[A-Z]:\"") + "[1m]))";

  const String diskWriteExpression =
      "sum by (instance) (rate(windows_logical_disk_write_bytes_total" +
      buildLabelMatcher(config, ", volume=~\"[A-Z]:\"") + "[1m]))";

  String query;
  query.reserve(2600);
  query += wrapChannelQuery("cpu_load", cpuLoadExpression);
  query += " or ";
  query += wrapChannelQuery("cpu_temp", cpuTempExpression);
  query += " or ";
  query += wrapChannelQuery("gpu_load", gpuExpression);
  query += " or ";
  query += wrapChannelQuery("ram_usage", ramExpression);
  query += " or ";
  query += wrapChannelQuery("net_up", netUpExpression);
  query += " or ";
  query += wrapChannelQuery("net_down", netDownExpression);
  query += " or ";
  query += wrapChannelQuery("disk_read", diskReadExpression);
  query += " or ";
  query += wrapChannelQuery("disk_write", diskWriteExpression);
  return query;
}

String PrometheusClient::buildLabelMatcher(
    const AppConfig &config, const String &extraMatchers) {
  return "{instance=\"" + escapePrometheusString(config.prometheusInstance) + "\"" +
         extraMatchers + "}";
}

String PrometheusClient::wrapChannelQuery(
    const String &channelName, const String &expression) {
  return "label_replace((" + expression + "), \"channel\", \"" +
         escapePrometheusString(channelName) + "\", \"instance\", \".*\")";
}

String PrometheusClient::escapePrometheusString(const String &value) {
  String escaped = value;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  return escaped;
}

String PrometheusClient::urlEncode(const String &value) {
  String encoded;
  encoded.reserve(value.length() * 3);

  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char character = static_cast<unsigned char>(value[i]);

    if (isalnum(character) || character == '-' || character == '_' ||
        character == '.' || character == '~') {
      encoded += static_cast<char>(character);
      continue;
    }

    encoded += '%';
    encoded += hex[(character >> 4) & 0x0F];
    encoded += hex[character & 0x0F];
  }

  return encoded;
}

int PrometheusClient::channelNameToIndex(const String &name) {
  for (int i = 0; i < GAUGE_COUNT; ++i) {
    if (name == kChannelNames[i]) {
      return i;
    }
  }

  return -1;
}
