# Prometheus-Analog-Matrix

Bridging Cloud-Native Observability with Retro-Industrial Hardware.

Prometheus-Analog-Matrix 是一个基于 ESP32 与 Prometheus 的独立物理监控终端项目。系统通过 Wi-Fi 接入局域网，向 Prometheus 拉取主机运行指标，再用 8 个机械模拟电压表组成的 2x4 物理矩阵进行实时显示。设备同时集成 LCD 2004 状态屏和本地 Web 配网页面，形成从接线、配网、抓取到显示的完整闭环。

当前固件默认面向 Windows PC 场景，直接查询 `windows_exporter` 的真实指标。

## 项目定位

1. 以 ESP32 为主控构建独立硬件监控终端。
2. 以 Prometheus HTTP API 作为统一数据输入接口。
3. 以 8 路物理表盘替代传统屏幕式监控 UI。
4. 以本地 Web 配网和 LCD 状态回显支撑独立运行。

## 核心特性

1. 8 路物理模拟仪表矩阵，实时展示核心负载与吞吐状态。
2. 标准 Prometheus API 对接方式，适配常见 exporter 架构。
3. ESP32 LEDC 直驱 8 路表盘，不依赖额外 PWM 扩展板。
4. AP 配网模式与本地配置页面，适合手机直接部署。
5. LCD 2004 实时显示网络状态、目标地址、抓取结果与延迟。
6. 每路表盘并联续流二极管，匹配线圈型负载的保护需求。

## 面板布局

上排：

1. CPU Load
2. CPU Temp
3. GPU Load
4. RAM Usage

下排：

1. Net Up
2. Net Down
3. Disk Read
4. Disk Write

## 硬件组成

| 模块 | 规格 |
| --- | --- |
| 主控 | ESP32 开发板 |
| 表盘 | 8 个 3V 或 3.3V 机械直流电压表 |
| 保护 | 8 个续流二极管，推荐 1N4148 或 1N4007 |
| 状态显示 | LCD 2004，带 I2C 转接板 |
| 供电 | 5V 2A 或以上电源适配器 |

## 接线摘要

1. LCD I2C 使用 `GPIO 21` 连接 `SDA`，`GPIO 22` 连接 `SCL`。
2. 8 路表盘正极默认连接 `13, 14, 15, 25, 26, 27, 32, 33`。
3. 8 路表盘负极统一接系统 `GND`。
4. 每个表盘两端并联一个续流二极管，白环端接表盘正极。
5. ESP32、LCD、表盘共地，系统统一由 5V 电源供电。

## 运行流程

1. 首次上电或 Wi-Fi 连接失败时，设备进入 AP 配网模式。
2. 用户连接 `CyberMonitor-Setup` 热点并访问 `192.168.4.1`。
3. 用户先在 ESP32 内置 HTML 页面中选择或输入 Wi-Fi SSID 与密码。
4. ESP32 连上局域网后，页面自动读取本机 IP 和子网掩码，并推导出地址前缀。
5. 用户只需补 Prometheus 主机和 exporter instance 的最后一段，或直接填写完整地址。
6. 保存后设备重启并进入正常运行模式。
7. 系统按固定周期向 Prometheus 发起组合查询。
8. 设备解析 8 路指标并更新 LCD 状态与物理表盘。

## Arduino IDE 工程

Arduino IDE 直接打开 [CyberMonitor](/Users/bizi/Desktop/GitHub/ESP32-Prometheus-PC-Monitor/CyberMonitor) 文件夹即可。

模块拆分如下：

1. `CyberMonitor.ino`：主流程与状态切换
2. `Config.*`：Preferences 配置读写
3. `DisplayManager.*`：LCD 2004 显示
4. `PortalManager.*`：AP 配网、HTML 向导、Wi-Fi 连接
5. `PrometheusClient.*`：Prometheus 查询与 JSON 解析
6. `GaugeDriver.*`：8 路 LEDC PWM 驱动

## 依赖

需要以下 Arduino 依赖：

1. `esp32 by Espressif Systems`
2. `ArduinoJson`
3. `LiquidCrystal I2C`

本地已使用 `arduino-cli` 对 `esp32:esp32:nodemcu-32s` 完成干净编译检查。

## 当前默认查询口径

当前固件默认使用以下真实指标：

1. `windows_cpu_processor_utility_total`
2. `windows_cpu_processor_rtc_total`
3. `windows_thermalzone_temperature_celsius`
4. `windows_gpu_engine_time_seconds`
5. `windows_memory_physical_free_bytes`
6. `windows_memory_physical_total_bytes`
7. `windows_net_bytes_sent_total`
8. `windows_net_bytes_received_total`
9. `windows_logical_disk_read_bytes_total`
10. `windows_logical_disk_write_bytes_total`

因此首版推荐目标环境为：

1. `Prometheus`
2. `windows_exporter`
3. `gpu` collector
4. `thermalzone` collector

## 文档索引

1. [产品需求文档与规格书](docs/PRD.md)
2. [开发任务拆解](docs/tasks.md)
3. [硬件接线说明](docs/connections.md)
4. [Prometheus 部署方案](docs/Prometheus-deployment.md)
5. [Metrics Mapping](docs/metrics-mapping.md)
6. [测试计划](docs/test-plan.md)
7. [Prometheus 联调准备与排查指南](docs/prometheus-debug-guide.md)

## 联调脚本

仓库内提供两个联调辅助脚本：

1. [check_exporter_metrics.sh](/Users/bizi/Desktop/GitHub/ESP32-Prometheus-PC-Monitor/tools/check_exporter_metrics.sh)
2. [check_prometheus_queries.sh](/Users/bizi/Desktop/GitHub/ESP32-Prometheus-PC-Monitor/tools/check_prometheus_queries.sh)

推荐顺序：

1. 先检查 `windows_exporter` 原始 metric 是否存在
2. 再检查 Prometheus 侧 8 路真实查询是否都能返回结果

## 当前交付范围

当前仓库已完成文档层面的项目定义，包括：

1. 产品规格
2. 硬件连接
3. 开发任务拆解
4. Prometheus 部署规划
5. Metrics Mapping
6. 测试计划

同时已落地 Arduino IDE 形态的多文件固件，并完成过一轮真实编译检查。当前重点是依照实际 exporter、真实主机 instance 和表盘量程继续联调、校准和实机验证。

## 项目目标

这个项目的目标是把 Prometheus 这套云原生监控标准，落到纯物理、可触达、可长期运行的桌面观测终端上，形成一套兼具展示性、工程性与可扩展性的硬件监控控制台。
