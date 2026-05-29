#include <WiFi.h>

#include "Config.h"
#include "DisplayManager.h"
#include "GaugeDriver.h"
#include "PortalManager.h"
#include "PrometheusClient.h"

namespace {
// 设备首次启动或配网失败时，会广播这个热点名称。
const char *kAccessPointName = "CyberMonitor-Setup";
// 正常运行模式下，Wi-Fi 初次连接的最长等待时间。
const uint32_t kWifiConnectTimeoutMs = 15000;
// 运行过程中掉线后的重连重试间隔。
const uint32_t kWifiRetryIntervalMs = 5000;
}

// 这些对象在整个设备生命周期中只初始化一次，形成全局单例式结构。
ConfigManager configManager;
DisplayManager displayManager;
PortalManager portalManager(configManager, displayManager);
PrometheusClient prometheusClient;
GaugeDriver gaugeDriver;

// currentConfig 保存当前生效配置。
// isSetupMode 用于区分设备当前处于配网向导还是正常运行模式。
AppConfig currentConfig;
bool isSetupMode = false;
uint32_t lastFetchAtMs = 0;
uint32_t lastReconnectAttemptAtMs = 0;

void enterSetupMode() {
  // 进入配网模式时，将表盘目标全部清零，避免保留上一次运行时的指针位置。
  isSetupMode = true;
  gaugeDriver.resetTargets();
  portalManager.startAccessPoint(currentConfig);
  displayManager.showSetupMode(kAccessPointName, portalManager.getApIp());
}

bool connectToConfiguredWifi() {
  if (!currentConfig.isValid()) {
    return false;
  }

  // 运行模式下直接按已保存的参数连网，不再走配网页向导。
  displayManager.showWifiConnecting(currentConfig.wifiSsid);

  String localIp;
  const bool connected =
      portalManager.connectToWifi(currentConfig, kWifiConnectTimeoutMs, localIp);

  if (!connected) {
    return false;
  }

  isSetupMode = false;
  lastReconnectAttemptAtMs = 0;
  lastFetchAtMs = 0;

  displayManager.showRunning(
      localIp,
      currentConfig.prometheusHost + ":" + String(currentConfig.prometheusPort),
      currentConfig.prometheusInstance);
  return true;
}

void bootSystemMode() {
  // 启动时优先读取 Flash 配置：
  // 1. 配置缺失 -> 进入 AP 配网
  // 2. 配置存在但 Wi-Fi 连接失败 -> 也进入 AP 配网
  currentConfig = configManager.load();

  if (!currentConfig.isValid()) {
    enterSetupMode();
    return;
  }

  if (!connectToConfiguredWifi()) {
    enterSetupMode();
  }
}

void handlePendingRestart() {
  // 配网页保存成功后，不在 HTTP 回调里直接重启，
  // 而是把“需要重启”这个动作延后到主循环里执行，逻辑更稳定。
  if (!portalManager.consumeRestartRequested()) {
    return;
  }

  displayManager.showRestarting();
  delay(1000);
  ESP.restart();
}

void handleRuntimeReconnect() {
  // 运行模式掉线时，按固定时间间隔发起重连，避免在 loop 中暴力重试。
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastReconnectAttemptAtMs < kWifiRetryIntervalMs) {
    return;
  }

  lastReconnectAttemptAtMs = now;
  if (!connectToConfiguredWifi()) {
    enterSetupMode();
  }
}

void handleMetricsFetch() {
  // 配网模式下不访问 Prometheus。
  if (isSetupMode || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (lastFetchAtMs != 0 && now - lastFetchAtMs < currentConfig.fetchIntervalMs) {
    return;
  }

  lastFetchAtMs = now;

  MetricsSnapshot snapshot = prometheusClient.fetchMetrics(currentConfig);
  displayManager.showRunning(
      WiFi.localIP().toString(),
      currentConfig.prometheusHost + ":" + String(currentConfig.prometheusPort),
      currentConfig.prometheusInstance);
  displayManager.showFetchStatus(
      snapshot.success, snapshot.latencyMs, snapshot.httpCode, snapshot.errorMessage);

  if (snapshot.success) {
    // 只有在解析到有效结果时才更新目标值，避免错误响应把表盘全部打回零位。
    gaugeDriver.setTargetsFromSnapshot(snapshot);
  }
}

void setup() {
  // 串口日志主要用于开发和联调，不作为最终用户界面的一部分。
  Serial.begin(115200);
  delay(200);

  configManager.begin();
  displayManager.begin();
  displayManager.showBoot();

  gaugeDriver.begin();
  portalManager.begin();

  bootSystemMode();
}

void loop() {
  // 无论是否在配网模式，WebServer 都要持续处理 HTTP 请求。
  portalManager.handleClient();
  // 表盘平滑更新必须持续运行，不能只在抓取数据时才更新。
  gaugeDriver.update();

  handlePendingRestart();

  if (isSetupMode) {
    return;
  }

  handleRuntimeReconnect();
  handleMetricsFetch();
}
