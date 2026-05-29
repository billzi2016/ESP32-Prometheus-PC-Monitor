// 配置模块：
// 负责定义需要持久化保存的参数，以及 Preferences 的读写入口。
#ifndef CYBERMONITOR_CONFIG_H
#define CYBERMONITOR_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String prometheusHost;
  String prometheusInstance;
  String gpuPhysicalIndex = "0";
  uint16_t prometheusPort = 9090;
  uint32_t fetchIntervalMs = 2000;

  // 只有当运行模式所需的关键配置齐全时，设备才会直接进入正常模式。
  bool isValid() const;
};

class ConfigManager {
 public:
  bool begin();
  AppConfig load();
  bool save(const AppConfig &config);
  void end();

 private:
  Preferences preferences;
  bool opened = false;
};

#endif
