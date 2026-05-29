// 网络与配网模块：
// 1. 正常运行模式下负责连接已保存的 Wi-Fi
// 2. 配网模式下负责启动 ESP32 热点和本地 Web 向导
// 3. 在 AP+STA 双模式下完成“先选 Wi-Fi，再自动推导局域网前缀”的两段式配置流程
#ifndef CYBERMONITOR_NETWORK_MANAGER_H
#define CYBERMONITOR_NETWORK_MANAGER_H

#include <Arduino.h>
#include <IPAddress.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Config.h"
#include "DisplayManager.h"

// 配网向导状态：
// Idle        设备刚进入热点模式，等待用户输入 Wi-Fi
// Connecting  已收到 Wi-Fi 凭据，正在尝试连入家庭网络
// Connected   已成功拿到 STA IP，可继续填写 Prometheus 信息
// Failed      Wi-Fi 连接失败，等待用户重新输入
enum class ProvisioningState : uint8_t {
  Idle = 0,
  Connecting,
  Connected,
  Failed,
};

class PortalManager {
 public:
  PortalManager(ConfigManager &configManager, DisplayManager &displayManager);

  void begin();
  bool connectToWifi(const AppConfig &config, uint32_t timeoutMs, String &localIp);
  void startAccessPoint(const AppConfig &currentConfig);
  void handleClient();
  bool consumeRestartRequested();
  String getApIp() const;

 private:
  void configureRoutes();
  void handleProvisioningLoop();
  void startWifiProvisioning(const String &ssid, const String &password);

  String buildConfigPage(const AppConfig &currentConfig) const;
  String buildResultPage(const String &title, const String &message) const;
  String buildStatusJson() const;
  String buildScanJson();
  String buildSubnetPrefix(IPAddress localIp, IPAddress subnetMask) const;
  static String htmlEscape(const String &value);
  static String jsonEscape(const String &value);

  ConfigManager &configManager;
  DisplayManager &displayManager;
  WebServer server;

  AppConfig lastShownConfig;
  String portalWifiSsid;
  String portalWifiPassword;
  String portalLocalIp;
  String portalSubnetMask;
  String portalGateway;
  String portalSubnetPrefix;
  String portalErrorMessage;

  ProvisioningState provisioningState = ProvisioningState::Idle;
  bool routesConfigured = false;
  bool serverStarted = false;
  bool restartRequested = false;
  bool provisioningKickoffPending = false;
  uint32_t provisioningStartedAtMs = 0;
};

#endif
