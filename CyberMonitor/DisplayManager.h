// 显示模块：
// 统一管理 LCD 2004 的初始化与页面切换，避免业务代码直接操作 LCD。
#ifndef CYBERMONITOR_DISPLAY_MANAGER_H
#define CYBERMONITOR_DISPLAY_MANAGER_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

class DisplayManager {
 public:
  void begin();
  void showBoot();
  void showSetupMode(const String &apName, const String &apIp);
  void showSetupWifiProgress(const String &message);
  void showSetupWifiResult(const String &ssid, const String &localIp, const String &subnetMask);
  void showWifiConnecting(const String &ssid);
  void showRunning(const String &localIp, const String &target, const String &instance);
  void showFetchStatus(bool success, uint32_t delayMs, int httpCode, const String &detail);
  void showRestarting();

 private:
  void printLine(uint8_t row, const String &text);
  String fitLine(const String &text) const;

  LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4);
};

#endif
