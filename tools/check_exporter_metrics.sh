#!/usr/bin/env bash

# 这个脚本用于在 ESP32 联调前，先直接检查 windows_exporter 的 /metrics 输出。
# 目标是回答两个问题：
# 1. Exporter 本身是否可访问
# 2. 当前固件依赖的关键原始 metric 是否真的存在

set -euo pipefail

EXPORTER_HOST="${1:-}"
EXPORTER_PORT="${2:-9182}"

if [[ -z "${EXPORTER_HOST}" ]]; then
  echo "用法: $0 <EXPORTER_HOST> [EXPORTER_PORT]"
  echo "示例: $0 192.168.1.50 9182"
  exit 1
fi

METRICS_URL="http://${EXPORTER_HOST}:${EXPORTER_PORT}/metrics"

echo "检查 exporter 地址: ${METRICS_URL}"

if ! command -v curl >/dev/null 2>&1; then
  echo "错误: 当前环境没有 curl，无法继续检查。"
  exit 1
fi

TMP_FILE="$(mktemp)"
trap 'rm -f "${TMP_FILE}"' EXIT

if ! curl -fsS "${METRICS_URL}" -o "${TMP_FILE}"; then
  echo "错误: 无法访问 exporter。"
  echo "请检查服务是否启动、防火墙是否放行、地址和端口是否正确。"
  exit 1
fi

echo "Exporter 可访问，开始检查关键 metric..."

check_metric() {
  local metric_name="$1"
  local label="$2"

  if grep -q "^${metric_name}" "${TMP_FILE}"; then
    echo "[OK] ${label}: ${metric_name}"
  else
    echo "[MISS] ${label}: ${metric_name}"
  fi
}

check_metric "windows_cpu_processor_utility_total" "CPU 负载"
check_metric "windows_cpu_processor_rtc_total" "CPU RTC 基准"
check_metric "windows_thermalzone_temperature_celsius" "CPU 温度"
check_metric "windows_gpu_engine_time_seconds" "GPU 利用率"
check_metric "windows_memory_physical_free_bytes" "空闲内存"
check_metric "windows_memory_physical_total_bytes" "总内存"
check_metric "windows_net_bytes_sent_total" "网络上传"
check_metric "windows_net_bytes_received_total" "网络下载"
check_metric "windows_logical_disk_read_bytes_total" "磁盘读取"
check_metric "windows_logical_disk_write_bytes_total" "磁盘写入"

echo
echo "如果 CPU 温度或 GPU 利用率缺失，优先检查 windows_exporter 的 collector 配置。"
