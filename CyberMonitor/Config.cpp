#include "Config.h"

namespace {
const char *kPreferencesNamespace = "config";
const char *kWifiSsidKey = "wifi_ssid";
const char *kWifiPasswordKey = "wifi_pass";
const char *kPrometheusHostKey = "prom_host";
const char *kPrometheusInstanceKey = "prom_inst";
const char *kGpuPhysicalIndexKey = "gpu_phys";
const char *kPrometheusPortKey = "prom_port";
const char *kFetchIntervalKey = "fetch_int";
const uint16_t kDefaultPrometheusPort = 9090;
const uint32_t kDefaultFetchIntervalMs = 2000;
const char *kDefaultGpuPhysicalIndex = "0";
}

bool AppConfig::isValid() const {
  return wifiSsid.length() > 0 && prometheusHost.length() > 0 &&
         prometheusInstance.length() > 0 && prometheusPort > 0;
}

bool ConfigManager::begin() {
  if (opened) {
    return true;
  }

  opened = preferences.begin(kPreferencesNamespace, false);
  return opened;
}

AppConfig ConfigManager::load() {
  AppConfig config;
  if (!begin()) {
    return config;
  }

  config.wifiSsid = preferences.getString(kWifiSsidKey, "");
  config.wifiPassword = preferences.getString(kWifiPasswordKey, "");
  config.prometheusHost = preferences.getString(kPrometheusHostKey, "");
  config.prometheusInstance = preferences.getString(kPrometheusInstanceKey, "");
  config.gpuPhysicalIndex =
      preferences.getString(kGpuPhysicalIndexKey, kDefaultGpuPhysicalIndex);
  config.prometheusPort = preferences.getUShort(kPrometheusPortKey, kDefaultPrometheusPort);
  config.fetchIntervalMs = preferences.getULong(kFetchIntervalKey, kDefaultFetchIntervalMs);

  if (config.fetchIntervalMs < 500) {
    config.fetchIntervalMs = 500;
  }

  if (config.gpuPhysicalIndex.length() == 0) {
    config.gpuPhysicalIndex = kDefaultGpuPhysicalIndex;
  }

  return config;
}

bool ConfigManager::save(const AppConfig &config) {
  if (!begin()) {
    return false;
  }

  // Preferences 的 put* 返回写入字节数，但空字符串写入时返回值并不适合
  // 作为统一的成功判定条件，因此这里以“完成写入调用”为保存成功标准。
  preferences.putString(kWifiSsidKey, config.wifiSsid);
  preferences.putString(kWifiPasswordKey, config.wifiPassword);
  preferences.putString(kPrometheusHostKey, config.prometheusHost);
  preferences.putString(kPrometheusInstanceKey, config.prometheusInstance);
  preferences.putString(kGpuPhysicalIndexKey, config.gpuPhysicalIndex);
  preferences.putUShort(kPrometheusPortKey, config.prometheusPort);
  preferences.putULong(kFetchIntervalKey, config.fetchIntervalMs);

  return true;
}

void ConfigManager::end() {
  if (!opened) {
    return;
  }

  preferences.end();
  opened = false;
}
