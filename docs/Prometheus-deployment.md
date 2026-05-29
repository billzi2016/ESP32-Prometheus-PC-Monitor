# Prometheus 部署方案

## 1. 文档目标

本文件定义 CyberMonitor 项目所依赖的 Prometheus 部署结构、组件组成、抓取路径、目标端要求与接入方式，用于支撑 ESP32 侧的数据获取与后续联调。

当前首版固件默认面向 Windows PC，直接查询 `windows_exporter` 的真实指标。

## 2. 部署目标

1. 在局域网内提供一个稳定可访问的 Prometheus 服务。
2. 将目标主机的运行指标暴露为 Prometheus 可抓取数据。
3. 为 ESP32 提供统一、固定、可重复调用的 HTTP API 查询入口。
4. 保持部署结构简洁，适合家庭实验室、桌面主机和小型局域网环境。

## 3. 部署架构

系统由以下部分组成：

| 组件 | 作用 |
| --- | --- |
| Prometheus | 负责抓取、存储与查询监控指标 |
| Exporter | 负责从目标系统导出可抓取指标 |
| Target Host | 被监控主机，例如 Windows PC、Linux 主机或 NAS |
| CyberMonitor | 使用 HTTP API 从 Prometheus 拉取指标的 ESP32 终端 |

整体数据流如下：

1. Target Host 运行 Exporter。
2. Exporter 暴露 `/metrics` 接口。
3. Prometheus 周期性抓取 Exporter 指标。
4. CyberMonitor 通过 Prometheus HTTP API 查询所需指标。

## 4. 推荐部署方式

### 4.1 方案 A：单机部署

Prometheus 与 Exporter 部署在同一台被监控主机上。

适用场景：

1. 单台 Windows PC
2. 单台 Linux 工作站
3. 初期联调与快速验证

特点：

1. 部署简单
2. 网络路径短
3. 管理成本低

### 4.2 方案 B：局域网集中部署

Prometheus 部署在独立常驻主机上，多个目标端各自运行 Exporter。

适用场景：

1. 多台设备统一监控
2. 家庭实验室
3. 小型工作室网络

特点：

1. 监控中心统一
2. 目标扩展方便
3. 适合后续增加更多仪表映射

本项目优先推荐方案 A 作为首期部署结构。

## 5. Exporter 规划

### 5.1 Windows 主机

推荐组件：

1. `windows_exporter`

导出内容可覆盖：

1. CPU 使用率
2. 内存使用率
3. 磁盘读写
4. 网络吞吐
5. 温度相关指标，前提是 `thermalzone` collector 可用

首版建议启用：

1. `[defaults]`
2. `gpu`
3. `thermalzone`

### 5.2 Linux 主机

推荐组件：

1. `node_exporter`

导出内容可覆盖：

1. CPU 使用率
2. 内存使用率
3. 磁盘读写
4. 网络吞吐
5. 温度相关指标，前提是系统暴露相关传感器数据

### 5.3 GPU 指标

Windows 首版直接依赖 `windows_exporter` 的 `gpu` collector。

终端默认读取：

1. `windows_gpu_engine_time_seconds`

## 6. Prometheus 主机要求

| 项目 | 要求 |
| --- | --- |
| 网络位置 | 与 ESP32 位于同一局域网，或二者之间可路由互通 |
| HTTP 访问 | Prometheus Web API 可从 ESP32 访问 |
| 端口 | 默认使用 `9090` |
| 稳定性 | 建议常驻运行，避免频繁重启 |
| 地址形式 | 优先使用固定局域网 IP |

## 7. Prometheus 配置要求

Prometheus 需要具备以下能力：

1. 已完成基础启动。
2. 已注册目标 Exporter 为抓取对象。
3. 已可通过 Web 页面或 API 查询目标指标。
4. 抓取间隔适合终端展示型场景。

### 7.1 抓取示例

```yaml
global:
  scrape_interval: 2s

scrape_configs:
  - job_name: windows-pc
    static_configs:
      - targets:
          - 192.168.1.50:9182
```

说明：

1. `192.168.1.50:9182` 是目标主机上的 `windows_exporter`
2. ESP32 配网页里的 `Prometheus Instance` 应与 Prometheus 中该目标的 `instance` 标签一致

### 7.2 抓取对象要求

每个 Target Host 应至少满足以下条件：

1. Exporter 进程处于运行状态。
2. `/metrics` 可被 Prometheus 抓取。
3. Prometheus 目标页面显示为可用状态。

### 7.3 抓取周期建议

建议 Prometheus 抓取周期与终端刷新目标保持协调。

建议区间：

1. 1 秒到 5 秒

推荐值：

1. 2 秒

该值与终端默认 2000ms 抓取周期一致，便于形成稳定更新节奏。

## 8. CyberMonitor 对 Prometheus 的接入要求

ESP32 终端侧需要配置以下参数：

| 参数 | 说明 |
| --- | --- |
| Prometheus Host | Prometheus 服务器 IP 或域名 |
| Prometheus Port | Prometheus 服务端口，默认 9090 |
| Prometheus Instance | 目标主机在 Prometheus 中的 `instance` 标签值 |
| GPU Physical Index | GPU 查询使用的 `phys` 标签值，默认 `0` |
| Fetch Interval | 终端发起查询的时间间隔 |

接入条件如下：

1. ESP32 能连接到目标 Wi-Fi。
2. ESP32 能访问 Prometheus HTTP API。
3. Prometheus 返回的查询结果包含目标指标数据。

## 9. 指标接入策略

### 9.1 首期目标指标

首期推荐对接以下 8 路指标：

1. CPU Load
2. CPU Temp
3. GPU Load
4. RAM Usage
5. Net Up
6. Net Down
7. Disk Read
8. Disk Write

### 9.2 查询组织策略

推荐使用单条组合查询承载 8 路结果。固件会通过 `label_replace` 为每一路结果附加 `channel` 标签，再由 ESP32 侧解析。

策略目标：

1. 减少 HTTP 请求次数
2. 降低 ESP32 端网络与内存开销
3. 简化抓取状态管理

### 9.3 命名与映射要求

每路结果在终端侧应有稳定的映射关系。

映射要求：

1. 查询结果中的 `channel` 标签稳定
2. 指标与通道绑定明确
3. 单位换算规则固定

## 10. 网络部署建议

### 10.1 IP 规划

建议为 Prometheus 主机分配固定局域网 IP。

理由：

1. 便于 ESP32 长期运行
2. 避免 DHCP 变化导致终端失联
3. 降低配网后重复修改配置的概率

### 10.2 路由要求

1. ESP32 所在 Wi-Fi 网络可访问 Prometheus 主机
2. 防火墙放行 Prometheus 服务端口
3. Exporter 端口对 Prometheus 抓取主机可访问

## 11. 部署步骤定义

### 11.1 单机部署步骤

1. 在目标主机安装 Prometheus。
2. 在目标主机安装 `windows_exporter`。
3. 启用 `[defaults],gpu,thermalzone` collector。
4. 配置 Prometheus 抓取本机 `windows_exporter`。
5. 启动并确认 Prometheus 抓取正常。
6. 记录 Prometheus 地址以及目标主机的 `instance` 标签值。
7. 将这些信息填写到 CyberMonitor 配置页面。

### 11.2 集中部署步骤

1. 在独立主机安装 Prometheus。
2. 在每台目标主机安装 Exporter。
3. 在 Prometheus 中注册多个抓取目标。
4. 验证各目标状态正常。
5. 规划 CyberMonitor 所需的指标来源。
6. 将 Prometheus 地址配置给终端。

## 12. 验收条件

Prometheus 部署完成后，应满足以下验收条件：

1. 浏览器可访问 Prometheus Web 页面。
2. Prometheus Target 页面可见目标主机状态正常。
3. 目标 Exporter 已连续稳定暴露指标。
4. Prometheus 查询接口可返回首期 8 路目标数据。
5. ESP32 所在网络可以访问 Prometheus。
6. 终端配置完成后可进入正常抓取流程。

## 13. 风险与约束

| 风险点 | 说明 |
| --- | --- |
| Prometheus 地址变化 | 会导致终端抓取失败 |
| Exporter 缺失温度或 GPU 指标 | 会导致部分表盘无有效数据 |
| 抓取周期过长 | 会降低仪表动态感 |
| 抓取周期过短 | 会增加网络与解析压力 |
| 局域网隔离 | 会阻断 ESP32 对 Prometheus 的访问 |

## 14. 建议结论

本项目首期推荐采用以下部署方式：

1. Prometheus 采用局域网固定 IP 部署。
2. 单台目标主机先完成基础联调。
3. Windows 环境优先使用 `windows_exporter`。
4. Linux 环境优先使用 `node_exporter`。
5. 终端侧默认抓取周期设置为 2000ms。

该方案结构简单、维护成本低，适合作为 CyberMonitor 的首版落地方案，并能为后续多目标扩展保留足够空间。
