# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

#!/usr/bin/env bash
set -euo pipefail

# HPM 最终压力测试脚本
# 目标：
# 1. 4 路 CAN FD 同时发压
# 2. 每个通道 1 秒 1 包，避免节奏过急
# 3. 实时显示仪表盘
# 4. 自动守护 cangen 进程，并在 BUS-OFF / STOPPED 时尝试恢复

INTERFACES=("can6" "can7" "can8" "can9")
BITRATE=1000000
DBITRATE=5000000
GAP_MS=1000
PAYLOAD_LEN=64
REFRESH_SEC=1
TX_QUEUE_LEN=10000
RESTART_MS=100

if [[ "${EUID}" -ne 0 ]]; then
    echo "请使用 root 运行该脚本。" >&2
    exit 1
fi

if ! command -v ip >/dev/null 2>&1; then
    echo "缺少 ip 命令。" >&2
    exit 1
fi

if ! command -v cangen >/dev/null 2>&1; then
    echo "缺少 cangen 命令，请先安装 can-utils。" >&2
    exit 1
fi

declare -A LAST_RX_PKTS
declare -A LAST_TX_PKTS
declare -A LAST_RX_BYTES
declare -A LAST_TX_BYTES
declare -A LAST_RESTARTS
declare -A LAST_RESTART_TS

cleanup() {
    echo
    echo "[!] 停止 HPM CAN FD 压测..."
    for if_name in "${INTERFACES[@]}"; do
        pkill -f "cangen ${if_name} " 2>/dev/null || true
    done
}

trap cleanup EXIT INT TERM

init_interface() {
    local if_name="$1"

    ip link set "${if_name}" down 2>/dev/null || true
    ip link set "${if_name}" type can \
        bitrate "${BITRATE}" \
        dbitrate "${DBITRATE}" \
        fd on \
        restart-ms "${RESTART_MS}"
    ip link set "${if_name}" txqueuelen "${TX_QUEUE_LEN}"
    ip link set "${if_name}" up
}

start_generator() {
    local if_name="$1"

    if pgrep -f "cangen ${if_name} " >/dev/null 2>&1; then
        return 1
    fi

    cangen "${if_name}" -g "${GAP_MS}" -I r -L "${PAYLOAD_LEN}" -f -b >/dev/null 2>&1 &
    return 0
}

extract_bus_state() {
    local ip_info="$1"
    local state

    state=$(grep -oE 'ERROR-(ACTIVE|WARNING|PASSIVE)|BUS-OFF|STOPPED' <<<"${ip_info}" | head -1 || true)
    if [[ -z "${state}" ]]; then
        state="UNKNOWN"
    fi
    printf '%s' "${state}"
}

extract_berr_counter() {
    local ip_info="$1"
    local berr

    berr=$(grep -oP 'berr-counter tx \K[0-9]+' <<<"${ip_info}" | head -1 || true)
    printf '%s' "${berr:-0}"
}

extract_rec_counter() {
    local ip_info="$1"
    local rec

    rec=$(grep -oP 'berr-counter tx [0-9]+ rx \K[0-9]+' <<<"${ip_info}" | head -1 || true)
    printf '%s' "${rec:-0}"
}

extract_restarts() {
    local ip_info="$1"
    local restarts

    restarts=$(grep -oP 're-started \K[0-9]+' <<<"${ip_info}" | head -1 || true)
    printf '%s' "${restarts:-0}"
}

extract_pkt_stat() {
    local if_name="$1"
    local kind="$2"
    cat "/sys/class/net/${if_name}/statistics/${kind}"
}

color_for_state() {
    local state="$1"
    local action="$2"

    if [[ "${action}" == "RECOVERED" || "${action}" == "RESTARTING" ]]; then
        printf '\033[1;36m'
        return
    fi
    if [[ "${state}" == "BUS-OFF" || "${state}" == "STOPPED" ]]; then
        printf '\033[31m'
        return
    fi
    if [[ "${state}" == "ERROR-WARNING" || "${state}" == "ERROR-PASSIVE" || "${state}" == "UNKNOWN" ]]; then
        printf '\033[33m'
        return
    fi
    printf '\033[32m'
}

prime_baseline() {
    for if_name in "${INTERFACES[@]}"; do
        LAST_RX_PKTS["${if_name}"]=$(extract_pkt_stat "${if_name}" rx_packets)
        LAST_TX_PKTS["${if_name}"]=$(extract_pkt_stat "${if_name}" tx_packets)
        LAST_RX_BYTES["${if_name}"]=$(extract_pkt_stat "${if_name}" rx_bytes)
        LAST_TX_BYTES["${if_name}"]=$(extract_pkt_stat "${if_name}" tx_bytes)
        LAST_RESTARTS["${if_name}"]=0
        LAST_RESTART_TS["${if_name}"]="-"
    done
}

echo ">>> 初始化 HPM 4 路 CAN FD 压测环境..."
for if_name in "${INTERFACES[@]}"; do
    init_interface "${if_name}"
    start_generator "${if_name}" || true
done

prime_baseline

while true; do
    printf '\033[2J\033[H'
    echo "==============================================================================="
    echo " HPM 4-CH CAN FD FINAL STRESS DASHBOARD - $(date '+%F %T')"
    echo " Config: 1M/5M BRS | Gap=${GAP_MS}ms | Payload=${PAYLOAD_LEN}B | Refresh=${REFRESH_SEC}s"
    echo "==============================================================================="
    printf '%-6s %-14s %-13s %-12s %-12s %-12s %-14s\n' "IF" "State" "TEC/REC" "TX pps" "RX pps" "TX/RX KBs" "Action"
    echo "-------------------------------------------------------------------------------"

    for if_name in "${INTERFACES[@]}"; do
        ip_info=$(ip -d -s link show "${if_name}" 2>/dev/null || true)
        state=$(extract_bus_state "${ip_info}")
        tec=$(extract_berr_counter "${ip_info}")
        rec=$(extract_rec_counter "${ip_info}")
        restarts=$(extract_restarts "${ip_info}")
        action="RUNNING"

        if [[ "${state}" == "BUS-OFF" || "${state}" == "STOPPED" ]]; then
            action="RESTARTING"
            if ! ip link set "${if_name}" type can restart >/dev/null 2>&1; then
                init_interface "${if_name}"
            fi
            LAST_RESTART_TS["${if_name}"]=$(date '+%T')
        fi

        if start_generator "${if_name}"; then
            if [[ "${action}" == "RUNNING" ]]; then
                action="RECOVERED"
                LAST_RESTART_TS["${if_name}"]=$(date '+%T')
            fi
        fi

        if [[ "${restarts}" != "${LAST_RESTARTS[${if_name}]}" ]]; then
            LAST_RESTART_TS["${if_name}"]=$(date '+%T')
            LAST_RESTARTS["${if_name}"]="${restarts}"
        fi

        curr_rx_pkts=$(extract_pkt_stat "${if_name}" rx_packets)
        curr_tx_pkts=$(extract_pkt_stat "${if_name}" tx_packets)
        curr_rx_bytes=$(extract_pkt_stat "${if_name}" rx_bytes)
        curr_tx_bytes=$(extract_pkt_stat "${if_name}" tx_bytes)

        rx_pps=$((curr_rx_pkts - LAST_RX_PKTS[${if_name}]))
        tx_pps=$((curr_tx_pkts - LAST_TX_PKTS[${if_name}]))
        rx_kbs=$(((curr_rx_bytes - LAST_RX_BYTES[${if_name}]) / 1024))
        tx_kbs=$(((curr_tx_bytes - LAST_TX_BYTES[${if_name}]) / 1024))

        LAST_RX_PKTS["${if_name}"]=${curr_rx_pkts}
        LAST_TX_PKTS["${if_name}"]=${curr_tx_pkts}
        LAST_RX_BYTES["${if_name}"]=${curr_rx_bytes}
        LAST_TX_BYTES["${if_name}"]=${curr_tx_bytes}

        color=$(color_for_state "${state}" "${action}")
        printf '%-6s %b%-14s\033[0m %-13s %-12s %-12s %-12s %-14s\n' \
            "${if_name}" \
            "${color}" \
            "${state}" \
            "${tec}/${rec}" \
            "${tx_pps}" \
            "${rx_pps}" \
            "${tx_kbs}/${rx_kbs}" \
            "${action}"
    done

    echo "==============================================================================="
    echo " Note: 每通道通过 cangen 以 ${GAP_MS}ms 周期发送 1 帧 64B CAN FD+BRS 报文。"
    echo " Hint: 如果某通道长期 TX pps=0 且反复 RESTARTING，优先检查 ACK、终端和物理连线。"
    echo " Stop: 按 Ctrl+C 结束测试。"
    sleep "${REFRESH_SEC}"
done