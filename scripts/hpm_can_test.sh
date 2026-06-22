#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

# ================= 配置区域 =================
INTERFACES=("can0")
BITRATE=1000000
DBITRATE=5000000
MIT_HZ=750            # MIT 发送频率
MIT_COUNT=10000       # 每轮连发帧数
SAMPLEPOINT=0.800
DSAMPLEPOINT=0.750
SKIP_INIT=true        # true=跳过接口初始化

# MIT 控制帧参数 (64 字节, 扩展帧 0x8080, BRS)
CANID=00008080
NUM_MOTORS=5
MOTOR_ZERO_DATA="00007FFF07FF07FF"
# ============================================

if [ "$EUID" -ne 0 ]; then echo "请使用 sudo 运行"; exit; fi

build_frame() {
    local frame=""
    for ((i=0; i<NUM_MOTORS; i++)); do frame+="$MOTOR_ZERO_DATA"; done
    local remaining=$((8 - NUM_MOTORS))
    for ((i=0; i<remaining; i++)); do frame+="0000000000000000"; done
    echo "$frame"
}

if [ "$SKIP_INIT" = false ]; then
    echo ">>> 初始化接口..."
    for IF in "${INTERFACES[@]}"; do
        ip link set "$IF" down 2>/dev/null
        ip link set "$IF" type can bitrate $BITRATE sample-point $SAMPLEPOINT \
            dbitrate $DBITRATE dsample-point $DSAMPLEPOINT fd on restart-ms 100 2>/dev/null
        ip link set "$IF" mtu 72 txqueuelen 10000
        ip link set "$IF" up 2>/dev/null
        echo "  [$IF] ${BITRATE}/${DBITRATE} fd on"
    done
    echo ""
fi

FRAME_DATA=$(build_frame)

cleanup() {
    echo -e "\n[!] 退出"
    kill $(jobs -p) 2>/dev/null
    exit
}
trap cleanup INT

declare -A last_rx last_tx

while true; do
    clear
    echo "=========================================================================================="
    echo "    HPM MIT CANFD TEST - $(date +%T)"
    echo "    CANID: 0x${CANID} | Motors: $NUM_MOTORS | Payload: 64B | Rate: ${MIT_HZ}Hz"
    echo "=========================================================================================="
    printf "%-5s %-12s %-8s %-10s %-10s %-8s %-8s %-10s\n" "IF" "State" "TEC/REC" "RX-PPS" "TX-PPS" "BusErr" "ArbLst" "Action"
    echo "------------------------------------------------------------------------------------"

    for IF in "${INTERFACES[@]}"; do
        ip_info=$(ip -d -s link show $IF)
        state=$(echo "$ip_info" | grep -oE "ERROR-(ACTIVE|WARNING|PASSIVE)|BUS-OFF|STOPPED" | head -1)
        tec_rec=$(echo "$ip_info" | grep -A 1 "bus-errors" | tail -n 1 | awk '{print $1"/"$2}')
        bus_err=$(echo "$ip_info" | grep -oP "bus-errors \K[0-9]+")
        arb_lst=$(echo "$ip_info" | grep -oP "arbitration-lost \K[0-9]+")
        sp=$(echo "$ip_info" | grep "sample-point" | head -1 | awk '{print $2}')
        dsp=$(echo "$ip_info" | grep "sample-point" | tail -n 1 | awk '{print $2}')
        err_details=$(echo "$ip_info" | grep -E "ack|crc|stuff|form" | xargs | sed 's/  */ /g')

        curr_rx=$(cat /sys/class/net/$IF/statistics/rx_packets 2>/dev/null || echo 0)
        curr_tx=$(cat /sys/class/net/$IF/statistics/tx_packets 2>/dev/null || echo 0)
        rx_rate=$((curr_rx - ${last_rx[$IF]:-$curr_rx}))
        tx_rate=$((curr_tx - ${last_tx[$IF]:-$curr_tx}))
        last_rx[$IF]=$curr_rx
        last_tx[$IF]=$curr_tx

        color="\033[32m"
        [[ "$state" != "ERROR-ACTIVE" ]] && color="\033[33m"
        [[ "$state" == "BUS-OFF" ]] && color="\033[31m"

        printf "%-5s ${color}%-12s\033[0m %-8s %-10s %-10s %-8s %-8s %-10s\n" \
                "$IF" "$state" "$tec_rec" "$rx_rate" "$tx_rate" "$bus_err" "$arb_lst" ""
        echo -e "      \033[90m└─ SP: ${sp}/${dsp} | Errors: ${err_details}\033[0m"
    done

    echo ""
    echo "  [r] 连发 ${MIT_COUNT} 帧 | [q] 退出 | 其他=刷新"
    read -rs -t 1 -N 1 key
    if [[ "$key" == "q" ]]; then echo ""; exit 0; fi
    if [[ "$key" == "r" ]]; then
        echo ""
        echo ">>> 连发 ${MIT_COUNT} 帧 MIT 指令 @ ${MIT_HZ}Hz..."
        for IF in "${INTERFACES[@]}"; do
            OK=0; FAIL=0
            SLEEP_US=$(( 1000000 / MIT_HZ ))
            for ((i=1; i<=MIT_COUNT; i++)); do
                if cansend "$IF" "${CANID}##1${FRAME_DATA}" 2>/dev/null; then
                    ((OK++))
                else
                    ((FAIL++))
                fi
                usleep "$SLEEP_US" 2>/dev/null || sleep 0.001333
            done
            echo "  [$IF] 成功=$OK  失败=$FAIL"
        done
        echo "  [完成] 等待下一轮..."
    fi
done
