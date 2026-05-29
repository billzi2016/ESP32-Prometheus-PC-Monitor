# Prometheus 联调准备与排查指南

## 1. 文档目标

本文件用于在 ESP32 烧录和实机联调之前，先把 Prometheus、`windows_exporter`、`instance` 标签和 8 路指标链路逐项排通。目标是把问题尽量留在 PC 侧和 Prometheus 侧解决，不把联调压力全部堆到 ESP32 上。

## 2. 适用范围

当前指南对应仓库中的首版固件实现，默认场景如下：

1. 被监控主机为 Windows PC
2. 指标来源为 `windows_exporter`
3. 数据查询入口为 `Prometheus HTTP API`
4. ESP32 固件使用内置 HTML 配网页与 8 路组合 PromQL

## 3. 联调总原则

联调顺序建议固定为：

1. 先确认 `windows_exporter /metrics` 正常
2. 再确认 Prometheus 已成功抓取目标
3. 再确认 PromQL 查询能返回 8 路有效结果
4. 最后再烧录 ESP32 并填写参数

不要一上来就把 ESP32、Wi-Fi、Prometheus、Exporter 一起排。那样问题来源会混在一起，效率很低。

## 4. 先决条件

开始之前，应具备以下条件：

1. Windows 主机已安装 `windows_exporter`
2. Prometheus 已启动并能访问 Web 页面
3. ESP32 与 Prometheus 位于互通的局域网
4. 你知道目标 Windows 主机的局域网地址
5. 你知道 Prometheus 主机的局域网地址

## 5. Windows 主机侧检查

### 5.1 `windows_exporter` 是否运行

在浏览器中访问：

```text
http://<WINDOWS_HOST>:9182/metrics
```

如果页面能返回大量纯文本 metric，说明 exporter 进程正常。

如果访问失败，优先检查：

1. `windows_exporter` 服务是否启动
2. Windows 防火墙是否放行 `9182`
3. 主机局域网 IP 是否变化

### 5.2 首版必须关注的原始 metric

当前固件默认依赖以下原始 metric：

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

其中最容易缺失的是：

1. `windows_thermalzone_temperature_celsius`
2. `windows_gpu_engine_time_seconds`

### 5.3 Collector 建议

建议启用：

1. `[defaults]`
2. `gpu`
3. `thermalzone`

如果你发现 CPU 温度或 GPU 利用率一直没有数据，先回头看 collector 是否真的启用了，而不是先怀疑 ESP32。

## 6. Prometheus 抓取检查

### 6.1 Web 页面检查

打开：

```text
http://<PROM_HOST>:9090/targets
```

重点确认：

1. 目标主机处于 `UP`
2. 抓取地址与你预期一致
3. 没有持续报错

### 6.2 `instance` 标签确认

当前 ESP32 固件会要求填写：

1. `Prometheus Host`
2. `Prometheus Port`
3. `Prometheus Instance`
4. `GPU Physical Index`

这里最容易填错的是 `Prometheus Instance`。

它不是 Prometheus 自己的地址，而是目标主机在 Prometheus 中的 `instance` 标签值。通常类似：

```text
192.168.1.50:9182
```

确认方法：

1. 打开 Prometheus Graph 页面
2. 查询任意一个已存在的 Windows metric，例如：

```promql
windows_memory_physical_total_bytes
```

3. 展开返回结果，查看其中的 `instance` 标签
4. 把这个值原样填进 ESP32 配网页

## 7. 当前固件的 8 路查询口径

### 7.1 CPU Load

```promql
sum by (instance) (
  clamp_max(
    (
      rate(windows_cpu_processor_utility_total{instance="<instance>"}[1m]) /
      rate(windows_cpu_processor_rtc_total{instance="<instance>"}[1m])
    ),
    100
  )
) / count by (instance) (
  windows_cpu_processor_utility_total{instance="<instance>"}
)
```

### 7.2 CPU Temp

```promql
max by (instance) (
  windows_thermalzone_temperature_celsius{instance="<instance>"}
)
```

### 7.3 GPU Load

```promql
clamp_max(
  (
    sum by (instance) (
      rate(
        windows_gpu_engine_time_seconds{
          instance="<instance>",
          phys="<gpu_phys>",
          engtype="3D"
        }[1m]
      )
    ) * 100
  ),
  100
)
```

### 7.4 RAM Usage

```promql
100 - 100 *
windows_memory_physical_free_bytes{instance="<instance>"} /
windows_memory_physical_total_bytes{instance="<instance>"}
```

### 7.5 Net Up

```promql
sum by (instance) (
  rate(
    windows_net_bytes_sent_total{
      instance="<instance>",
      nic!~"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*"
    }[1m]
  )
)
```

### 7.6 Net Down

```promql
sum by (instance) (
  rate(
    windows_net_bytes_received_total{
      instance="<instance>",
      nic!~"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*"
    }[1m]
  )
)
```

### 7.7 Disk Read

```promql
sum by (instance) (
  rate(
    windows_logical_disk_read_bytes_total{
      instance="<instance>",
      volume=~"[A-Z]:"
    }[1m]
  )
)
```

### 7.8 Disk Write

```promql
sum by (instance) (
  rate(
    windows_logical_disk_write_bytes_total{
      instance="<instance>",
      volume=~"[A-Z]:"
    }[1m]
  )
)
```

## 8. `gpu phys` 如何确认

GPU 查询里用到了：

```text
phys="<gpu_phys>"
```

默认值通常可以先试 `0`。

如果 `gpu` collector 有数据但 GPU 表盘没有反应，检查方法：

1. 在 Prometheus Graph 页面查询：

```promql
windows_gpu_engine_time_seconds{instance="<instance>"}
```

2. 看返回结果里的 `phys` 标签有哪些值
3. 把存在的 `phys` 值填到 ESP32 配网页的 `GPU Physical Index`

## 9. 烧录前建议执行的本地脚本

仓库已经提供两个联调脚本：

1. [check_exporter_metrics.sh](/Users/bizi/Desktop/GitHub/ESP32-Prometheus-PC-Monitor/tools/check_exporter_metrics.sh)
2. [check_prometheus_queries.sh](/Users/bizi/Desktop/GitHub/ESP32-Prometheus-PC-Monitor/tools/check_prometheus_queries.sh)

建议顺序：

1. 先跑 `check_exporter_metrics.sh`
2. 再跑 `check_prometheus_queries.sh`

## 10. ESP32 配网页应该怎么填

当你打开 `192.168.4.1` 后：

### 第一步

填写或选择：

1. Wi-Fi SSID
2. Wi-Fi Password

然后让 ESP32 去连接局域网。

### 第二步

ESP32 连上后，HTML 页面会自动给出局域网前缀，例如：

```text
192.168.1.
```

你再填写：

1. `Prometheus Host`
   - 例如：`192.168.1.10`
2. `Prometheus Port`
   - 通常：`9090`
3. `Prometheus Instance`
   - 例如：`192.168.1.50:9182`
4. `Exporter Instance Port`
   - 通常：`9182`
5. `GPU Physical Index`
   - 通常先填：`0`

## 11. 常见故障排查顺序

### 11.1 页面能打开，但 ESP32 连不上 Wi-Fi

优先检查：

1. SSID 是否正确
2. 密码是否正确
3. 是否是 2.4GHz Wi-Fi
4. 路由器是否限制新设备接入

### 11.2 Prometheus 连通，但所有表盘都没反应

优先检查：

1. `Prometheus Host` 是否正确
2. `Prometheus Instance` 是否和 Prometheus 标签完全一致
3. Prometheus 查询是否真有返回值

### 11.3 只有 CPU/GPU 温度没反应

优先检查：

1. `thermalzone` collector 是否启用
2. Windows 主机是否真的暴露了温度数据

### 11.4 只有 GPU 表盘没反应

优先检查：

1. `gpu` collector 是否启用
2. `phys` 标签值是否填对
3. 该主机显卡驱动是否暴露了对应计数器

### 11.5 网络和磁盘值始终很小

这是正常现象之一，尤其在空闲主机上会很明显。  
先通过 Prometheus Graph 页面确认查询值本身是否很小，再判断是否需要后续调整量程。

## 12. 建议联调流程

建议实际联调按下面顺序执行：

1. 访问 `windows_exporter /metrics`
2. 确认关键原始 metric 存在
3. 打开 Prometheus `Targets`
4. 确认目标主机 `UP`
5. 确认 `instance` 标签值
6. 运行 `tools/check_exporter_metrics.sh`
7. 运行 `tools/check_prometheus_queries.sh`
8. 烧录 ESP32
9. 通过 HTML 页面完成 Wi-Fi 和 Prometheus 配置
10. 观察 LCD 和 8 路表盘行为

## 13. 结论

只要先把 exporter、Prometheus、`instance`、`gpu phys` 这几层排通，ESP32 侧的联调难度会低很多。这个项目最关键的不是“能否请求到 Prometheus”，而是“查询口径和标签值是否与真实环境完全一致”。
