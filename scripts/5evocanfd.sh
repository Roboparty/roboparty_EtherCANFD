#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

# ================= 配置区域 =================
# INTERFACES=("can0" "can1" "can2" "can3")
INTERFACES=("can7")
# INTERFACES=("can4" "can5" "can6" "can7")
BITRATE=1000000
DBITRATE=5000000
SAMPLEPOINT=0.800
SJW=4
DSAMPLEPOINT=0.750
DSJW=2

# MIT 控制帧参数
CANID=020          # MIT 模式 CAN ID
NUM_MOTORS=5        # 控制电机数量 (1~8)
SEND_COUNT=10000    # 连发次数
MIT_HZ=750          # MIT 发送频率 (Hz)

# 单电机全部为0的 MIT 数据: Kp=0, Kd=0, θ=0, V=0, T=0
# 协议定义: Byte0-1=位置(0x7FFF=0rad), Byte2-3=速度(0x7FF=0rad/s),
#           Byte3-4=Kp(0x000=0), Byte5-6=Kd(0x000=0), Byte6-7=扭矩(0x7FF=0Nm)
MOTOR_ZERO_DATA="7FFF7FF0000017FF"  # 7F FF 7F F0 00 00 17 FF (Kd=1)

ENABLE_RESCUE=false
# [SET] 日志文件路径
RESCUE_LOG="can_rescue_mit.log"
# ============================================

if [ "$EUID" -ne 0 ]; then
  echo "请使用 sudo 运行"
  exit
fi

echo ">>> 配置 restart-ms + txqueuelen..."
for IF in "${INTERFACES[@]}"; do
    ip link set "$IF" down
    ip link set "$IF" type can bitrate $BITRATE sample-point $SAMPLEPOINT sjw $SJW \
            dbitrate $DBITRATE dsample-point $DSAMPLEPOINT dsjw $DSJW fd on restart-ms 100
    ip link set "$IF" mtu 72
    ip link set "$IF" txqueuelen 10000
    ip link set "$IF" up
    echo "  [$IF] ${BITRATE}/$DBITRATE ds${DSAMPLEPOINT} restart-ms=100 qlen=10000"
done
echo ""

# 构建 64 字节 CANFD 数据帧
# 前 NUM_MOTORS 个槽位填入 MOTOR_ZERO_DATA，剩余填 00
build_frame() {
    local frame=""
    for ((i=0; i<NUM_MOTORS; i++)); do
        frame+="$MOTOR_ZERO_DATA"
    done
    local remaining=$((8 - NUM_MOTORS))
    for ((i=0; i<remaining; i++)); do
        frame+="0000000000000000"
    done
    echo "$frame"
}

# 初始清理并创建/清空日志
echo "--- CANFD MIT Flood Log Start: $(date) ---" > "$RESCUE_LOG"

cleanup() {
    echo -e "\n[!] 正在停止所有测试进程..."
    kill $(jobs -p) 2>/dev/null
    exit
}
trap cleanup INT


# ---------- 逐电机使能 + 标零 (8字节经典CAN帧) ----------
# CANID = Motor ID, 8B, 经典CAN格式
CMD_ENABLE="FFFFFFFFFFFFFFFC"  # 使能: FF FF FF FF FF FF FF FC
CMD_ZERO="FFFFFFFFFFFFFFFE"    # 标零: FF FF FF FF FF FF FF FE

echo "=========================================================================================="
echo ">>> 逐接口 逐电机 使能 + 标零"
echo "=========================================================================================="
for IF in "${INTERFACES[@]}"; do
    echo "  [$IF] ${NUM_MOTORS} 个电机..."
    for ((m=1; m<=NUM_MOTORS; m++)); do
        MID=$(printf '%03X' $m)
        echo -n "    [Motor $m] 使能..."
        cansend "$IF" "${MID}#${CMD_ENABLE}"
        sleep 0.3
        echo -n " 标零..."
        cansend "$IF" "${MID}#${CMD_ZERO}"
        sleep 0.2
        echo -n " 再使能..."
        cansend "$IF" "${MID}#${CMD_ENABLE}"
        sleep 0.3
        echo " 完成"
    done
done
echo ""

# ---------- 构建 MIT 连发帧 ----------
echo ">>> 构建帧数据: CANID=0x${CANID}, $NUM_MOTORS 电机, MIT 全零..."
FRAME_DATA=$(build_frame)
echo "    数据: $FRAME_DATA"
echo ""

declare -A last_rx last_tx
INTERVAL_SEC=1

while true; do
    clear
    echo "=========================================================================================="
    echo "    HPM5E31 CANFD MIT FLOOD [RESCUE ENABLED] - $(date +%T)"
    echo "    CANID: 0x${CANID} | Motors: $NUM_MOTORS | Payload: 64B | Flood: ${SEND_COUNT}x"
    echo "    Log: $RESCUE_LOG | Config: 1M/5M BRS"
    echo "=========================================================================================="
    printf "%-5s %-12s %-8s %-10s %-10s %-8s %-8s %-10s\n" "IF" "State" "TEC/REC" "RX-PPS" "TX-PPS" "BusErr" "ArbLst" "Action"
    echo "------------------------------------------------------------------------------------------"

    for IF in "${INTERFACES[@]}"; do
        ip_info=$(ip -d -s link show $IF)
        state=$(echo "$ip_info" | grep -oE "ERROR-(ACTIVE|WARNING|PASSIVE)|BUS-OFF|STOPPED" | head -1)
        tec_rec=$(echo "$ip_info" | grep -A 1 "bus-errors" | tail -n 1 | awk '{print $1"/"$2}')
        bus_err=$(echo "$ip_info" | grep -oP "bus-errors \K[0-9]+")
        arb_lst=$(echo "$ip_info" | grep -oP "arbitration-lost \K[0-9]+")
        sp=$(echo "$ip_info" | grep "sample-point" | head -1 | awk '{print $2}')
        dsp=$(echo "$ip_info" | grep "sample-point" | tail -1 | awk '{print $2}')
        err_details=$(echo "$ip_info" | grep -E "ack|crc|stuff|form" | xargs | sed 's/  */ /g')

        action="IDLE"
        if [[ "$state" == "BUS-OFF" ]] || [[ "$state" == "STOPPED" ]]; then
            if [ "$ENABLE_RESCUE" = true ]; then
                action="RESTARTING"
                log_msg="[$(date +%T)] [$IF] Event: $state | TEC/REC: $tec_rec | Stats: $err_details | SP: $sp/$dsp"
                echo "$log_msg" >> "$RESCUE_LOG"
                ip link set $IF type can restart 2>/dev/null
            fi
        fi

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
                "$IF" "$state" "$tec_rec" "$rx_rate" "$tx_rate" "$bus_err" "$arb_lst" "$action"
        echo -e "      \033[90m└─ SP: ${sp}/${dsp} | Errors: ${err_details}\033[0m"
    done

    echo ""
    echo "------------------------------------------------------------------------------------------"
    printf "  [INPUT] r=开始连发 | q=退出 | 其他=刷新状态\n"
    echo "------------------------------------------------------------------------------------------"

    read -rs -t $INTERVAL_SEC -N 1 key
    if [[ "$key" == "q" ]]; then
        echo ""
        echo "[!] 退出。"
        exit 0
    elif [[ "$key" == "r" ]]; then
        echo ""
        echo ">>> 开始连发 ${SEND_COUNT} 帧 MIT 全零指令..."
        START_TIME=$(date +%s%N)
        SUCCESS=0
        FAIL=0
        for IF in "${INTERFACES[@]}"; do
            IF_OK=0
            IF_FAIL=0
            SLEEP_US=$(( 1000000 / MIT_HZ ))
            for ((i=1; i<=SEND_COUNT; i++)); do
                if cansend "$IF" "${CANID}##1${FRAME_DATA}"; then
                    ((IF_OK++))
                else
                    ((IF_FAIL++))
                fi
                usleep "$SLEEP_US" 2>/dev/null || sleep 0.001333
            done
            echo "  [$IF] 完成: 成功=$IF_OK, 失败=$IF_FAIL"
            ((SUCCESS += IF_OK))
            ((FAIL += IF_FAIL))
        done
        END_TIME=$(date +%s%N)
        ELAPSED=$(( (END_TIME - START_TIME) / 1000000 ))
        TOTAL=$((SUCCESS + FAIL))
        if [[ $ELAPSED -gt 0 ]]; then
            echo "  汇总: ${TOTAL} 帧 / ${ELAPSED}ms = $(echo "scale=1; $TOTAL * 1000 / $ELAPSED" | bc) 帧/s"
        fi
        echo "  日志: $RESCUE_LOG"
    fi
done
