#include "DisplayManager.h"

#include <Wire.h>

namespace {
// LCD 2004 固定为 20x4，这里统一收口，避免魔法数字散落在代码里。
const uint8_t kLcdColumns = 20;
const uint8_t kSdaPin = 21;
const uint8_t kSclPin = 22;
}

void DisplayManager::begin() {
  // LCD 通过 I2C 转接板连接，因此先初始化 I2C，再初始化液晶本体。
  Wire.begin(kSdaPin, kSclPin);
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void DisplayManager::showBoot() {
  // 开机页面只负责告诉用户设备正在初始化，不展示细节状态。
  printLine(0, "SYS: Booting");
  printLine(1, "Init modules...");
  printLine(2, "WiFi / LCD / PWM");
  printLine(3, "Please wait...");
}

void DisplayManager::showSetupMode(const String &apName, const String &apIp) {
  printLine(0, "SYS: Setup Mode");
  printLine(1, "Connect AP:");
  printLine(2, apName);
  printLine(3, "IP: " + apIp);
}

void DisplayManager::showSetupWifiProgress(const String &message) {
  // 配网向导中，用户提交 SSID 和密码后，LCD 会进入等待连网状态。
  printLine(0, "SYS: Setup Wizard");
  printLine(1, "WiFi connecting...");
  printLine(2, "Back to 192.168.4.1");
  printLine(3, message);
}

void DisplayManager::showSetupWifiResult(
    const String &ssid, const String &localIp, const String &subnetMask) {
  // 当 ESP32 在 AP+STA 模式下成功连上家庭 Wi-Fi 后，
  // LCD 给出最关键的信息：已连上的 SSID、本机 IP 和子网信息。
  printLine(0, "SYS: WiFi Ready");
  printLine(1, "SSID: " + ssid);
  printLine(2, "IP: " + localIp);
  printLine(3, "Mask: " + subnetMask);
}

void DisplayManager::showWifiConnecting(const String &ssid) {
  printLine(0, "SYS: WiFi Connect");
  printLine(1, "SSID:");
  printLine(2, ssid);
  printLine(3, "Please wait...");
}

void DisplayManager::showRunning(
    const String &localIp, const String &target, const String &instance) {
  // 正常运行模式下，LCD 前三行固定显示运行状态、本机 IP、Prometheus 目标。
  // 第四行留给抓取状态，避免频繁刷新整屏导致观感跳动。
  // instance 在 Web 配网向导里展示更合适，这里保留参数是为了接口一致性。
  (void)instance;
  printLine(0, "SYS: Running");
  printLine(1, "IP: " + localIp);
  printLine(2, "Tar: " + target);
}

void DisplayManager::showFetchStatus(
    bool success, uint32_t delayMs, int httpCode, const String &detail) {
  String line = "Fetch: ";

  if (detail.length() > 0 && httpCode == 0 && delayMs == 0) {
    line += detail;
    printLine(3, line);
    return;
  }

  if (success) {
    line += "OK ";
    line += String(delayMs);
    line += "ms";
  } else {
    line += "ERR ";
    if (httpCode != 0) {
      line += String(httpCode);
    } else if (detail.length() > 0) {
      line += detail;
    } else {
      line += "NoData";
    }
  }

  printLine(3, line);
}

void DisplayManager::showRestarting() {
  printLine(0, "SYS: Config Saved");
  printLine(1, "Restarting...");
  printLine(2, "Reconnect soon");
  printLine(3, "Please wait...");
}

void DisplayManager::printLine(uint8_t row, const String &text) {
  // 每次写行时都补齐空格，确保旧字符不会残留在 LCD 尾部。
  lcd.setCursor(0, row);
  lcd.print(fitLine(text));
}

String DisplayManager::fitLine(const String &text) const {
  String result = text;
  if (result.length() > kLcdColumns) {
    result = result.substring(0, kLcdColumns);
  }

  while (result.length() < kLcdColumns) {
    result += ' ';
  }

  return result;
}
