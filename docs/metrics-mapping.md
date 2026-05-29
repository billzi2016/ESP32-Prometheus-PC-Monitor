# CyberMonitor Metrics Mapping

## 1. 文档目标

本文件定义 CyberMonitor 首期 8 路物理表盘所对应的真实 Prometheus 查询口径、单位、量程、归一化规则和通道映射关系，用于统一固件、Prometheus 部署和联调口径。

## 2. 通道总表

| 通道 | 面板位置 | 指标显示名 | 数据来源 | 单位 |
| --- | --- | --- | --- | --- |
| CH0 | 上排 1 | CPU Load | `windows_cpu_processor_utility_total` + `windows_cpu_processor_rtc_total` | % |
| CH1 | 上排 2 | CPU Temp | `windows_thermalzone_temperature_celsius` | C |
| CH2 | 上排 3 | GPU Load | `windows_gpu_engine_time_seconds` | % |
| CH3 | 上排 4 | RAM Usage | `windows_memory_physical_free_bytes` + `windows_memory_physical_total_bytes` | % |
| CH4 | 下排 1 | Net Up | `windows_net_bytes_sent_total` | B/s |
| CH5 | 下排 2 | Net Down | `windows_net_bytes_received_total` | B/s |
| CH6 | 下排 3 | Disk Read | `windows_logical_disk_read_bytes_total` | B/s |
| CH7 | 下排 4 | Disk Write | `windows_logical_disk_write_bytes_total` | B/s |

## 3. 默认查询口径

首期固件通过单条组合 PromQL 查询真实 exporter 指标，而不是读取占位 metric。

默认环境：

1. Windows PC
2. `windows_exporter`
3. `gpu` collector 已启用
4. `thermalzone` collector 已启用

配置输入项：

1. `Prometheus Host`
2. `Prometheus Port`
3. `Prometheus Instance`
4. `GPU Physical Index`

固件把每一路查询都包装成 `channel` 标签，返回后按 `metric.channel` 完成表盘通道映射。

## 4. 归一化规则

### 4.1 百分比类

适用指标：

1. CPU Load
2. GPU Load
3. RAM Usage

归一化区间：

1. 输入范围 `0` 到 `100`
2. 输出范围 `0` 到 `255`

显示逻辑：

1. `0` 对应表盘起点
2. `100` 对应表盘满刻度

### 4.2 温度类

适用指标：

1. CPU Temp

归一化区间：

1. 输入范围 `0` 到 `100`
2. 输出范围 `0` 到 `255`

显示逻辑：

1. `0C` 对应表盘起点
2. `100C` 对应表盘满刻度

### 4.3 网络吞吐类

适用指标：

1. Net Up
2. Net Down

归一化区间：

1. 输入范围 `0` 到 `125000000`
2. 输出范围 `0` 到 `255`

说明：

1. `125000000 B/s` 约等于 `1 Gbps` 级别的理论上限
2. 对于桌面 PC 与局域网监控场景，这个量程适合作为首期默认值

### 4.4 磁盘吞吐类

适用指标：

1. Disk Read
2. Disk Write

归一化区间：

1. 输入范围 `0` 到 `500000000`
2. 输出范围 `0` 到 `255`

说明：

1. `500000000 B/s` 约为 `500 MB/s` 量级
2. 适合作为机械硬盘与普通 SSD 混合场景的首期默认量程

## 5. 采集方式

固件侧采用单条组合 Prometheus 查询语句批量抓取 8 路结果。固件通过 `metric.channel` 完成通道映射，并将对应的 `value` 数值送入表盘驱动模块。

## 6. 映射要求

1. Prometheus 返回结果中的 `channel` 标签必须稳定。
2. 每个 channel 只对应一个固定表盘通道。
3. 百分比类指标统一使用 `0-100`。
4. 温度类指标统一使用摄氏度。
5. 吞吐类指标统一使用 `B/s`。

## 7. 注意事项

1. CPU Temp 依赖 `thermalzone` collector 可用。
2. GPU Load 依赖 `gpu` collector 与显卡驱动计数器可用。
3. 某一路查询没有返回结果时，对应表盘不会伪造数据。

## 8. 后续扩展方向

1. 增加 GPU Temp 通道方案
2. 增加自定义量程配置
3. 增加按主机标签区分的查询映射
4. 增加不同硬件平台的 metric 兼容模板
