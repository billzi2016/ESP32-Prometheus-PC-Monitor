#!/usr/bin/env bash

# 这个脚本直接按当前 ESP32 固件的真实查询口径去请求 Prometheus。
# 它用于在烧录前验证：
# 1. Prometheus API 是否可访问
# 2. instance 标签是否填写正确
# 3. gpu phys 标签是否可用
# 4. 8 路查询是否都能返回结果

set -euo pipefail

PROM_HOST="${1:-}"
PROM_PORT="${2:-9090}"
INSTANCE="${3:-}"
GPU_PHYS="${4:-0}"

if [[ -z "${PROM_HOST}" || -z "${INSTANCE}" ]]; then
  echo "用法: $0 <PROM_HOST> [PROM_PORT] <INSTANCE> [GPU_PHYS]"
  echo "示例: $0 192.168.1.10 9090 192.168.1.50:9182 0"
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "错误: 当前环境没有 curl。"
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "错误: 当前环境没有 python3，无法做 URL 编码和 JSON 简析。"
  exit 1
fi

BASE_URL="http://${PROM_HOST}:${PROM_PORT}/api/v1/query"

run_query() {
  local label="$1"
  local query="$2"

  echo "------------------------------------------------------------"
  echo "检查: ${label}"

  local encoded_query
  encoded_query="$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "${query}")"

  local response
  if ! response="$(curl -fsS "${BASE_URL}?query=${encoded_query}")"; then
    echo "[ERR] Prometheus API 请求失败"
    return 1
  fi

  python3 - "$label" <<'PY' <<<"${response}"
import json
import sys

label = sys.argv[1]
raw = sys.stdin.read()

try:
    payload = json.loads(raw)
except Exception as exc:
    print(f"[ERR] {label}: JSON 解析失败: {exc}")
    sys.exit(1)

if payload.get("status") != "success":
    print(f"[ERR] {label}: Prometheus 返回非 success 状态")
    sys.exit(1)

result = payload.get("data", {}).get("result", [])
if not result:
    print(f"[MISS] {label}: 没有返回结果")
    sys.exit(0)

for item in result:
    metric = item.get("metric", {})
    value = item.get("value", [])
    metric_desc = ", ".join(f"{k}={v}" for k, v in metric.items())
    if len(value) >= 2:
      print(f"[OK] {label}: value={value[1]} | {metric_desc}")
    else:
      print(f"[OK] {label}: 返回结果存在，但 value 字段不完整 | {metric_desc}")
PY
}

CPU_LOAD_QUERY="sum by (instance) (clamp_max((rate(windows_cpu_processor_utility_total{instance=\"${INSTANCE}\"}[1m]) / rate(windows_cpu_processor_rtc_total{instance=\"${INSTANCE}\"}[1m])), 100)) / count by (instance) (windows_cpu_processor_utility_total{instance=\"${INSTANCE}\"})"
CPU_TEMP_QUERY="max by (instance) (windows_thermalzone_temperature_celsius{instance=\"${INSTANCE}\"})"
GPU_LOAD_QUERY="clamp_max((sum by (instance) (rate(windows_gpu_engine_time_seconds{instance=\"${INSTANCE}\", phys=\"${GPU_PHYS}\", engtype=\"3D\"}[1m])) * 100), 100)"
RAM_USAGE_QUERY="100 - 100 * windows_memory_physical_free_bytes{instance=\"${INSTANCE}\"} / windows_memory_physical_total_bytes{instance=\"${INSTANCE}\"}"
NET_UP_QUERY="sum by (instance) (rate(windows_net_bytes_sent_total{instance=\"${INSTANCE}\", nic!~\"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*\"}[1m]))"
NET_DOWN_QUERY="sum by (instance) (rate(windows_net_bytes_received_total{instance=\"${INSTANCE}\", nic!~\"isatap.*|Teredo.*|Loopback.*|Bluetooth.*|Npcap.*|vEthernet.*\"}[1m]))"
DISK_READ_QUERY="sum by (instance) (rate(windows_logical_disk_read_bytes_total{instance=\"${INSTANCE}\", volume=~\"[A-Z]:\"}[1m]))"
DISK_WRITE_QUERY="sum by (instance) (rate(windows_logical_disk_write_bytes_total{instance=\"${INSTANCE}\", volume=~\"[A-Z]:\"}[1m]))"

run_query "CPU Load" "${CPU_LOAD_QUERY}"
run_query "CPU Temp" "${CPU_TEMP_QUERY}"
run_query "GPU Load" "${GPU_LOAD_QUERY}"
run_query "RAM Usage" "${RAM_USAGE_QUERY}"
run_query "Net Up" "${NET_UP_QUERY}"
run_query "Net Down" "${NET_DOWN_QUERY}"
run_query "Disk Read" "${DISK_READ_QUERY}"
run_query "Disk Write" "${DISK_WRITE_QUERY}"

echo "------------------------------------------------------------"
echo "检查完成。"
echo "如果只有 GPU 或温度缺失，优先回头检查 windows_exporter collector 和标签值。"
