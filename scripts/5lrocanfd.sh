#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 wentywenty

# ================= 配置区域 =================
SKIP_INIT=true      # true=跳过接口初始化和配置, 使用现有状态
# INTERFACES=("can0" "can1" "can2" "can3")
INTERFACES=("can0")
# INTERFACES=("can4" "can5" "can6" "can7")
BITRATE=1000000
DBITRATE=5000000
SAMPLEPOINT=0.800
SJW=4
DSAMPLEPOINT=0.750
DSJW=2

# MIT 控制帧参数
CANID=00008080      # LRO 一拖多 MIT CAN ID (扩展帧, 8位十六进制)
NUM_MOTORS=5        # 控制电机数量 (1~8)
SEND_COUNT=10000    # 连发次数
MIT_HZ=750          # MIT 发送频率 (Hz)

# LRO MIT 编码: mode(3) | kp(12) | kd(9) | pos(16) | vel(12) | torque(12) = 64 bits
# 全零: 0x00 | 0x000 | 0x000(9bit) | 0x7FFF(0rad) | 0x7FF(0rad/s) | 0x7FF(0Nm)
# 字节序: big-endian -> 00 00 7F FF 07 FF 07 FF (kd=0, kp=0, 扭矩零位=0x7FF)
MOTOR_ZERO_DATA="00007FFF07FF07FF"

ENABLE_RESCUE=false
PARSE_ERROR=0        # 0=不解析电机错误反馈, 1=启动 candump 解析
# [SET] 日志文件路径
RESCUE_LOG="can_rescue_mit.log"
ERROR_LOG="/tmp/lro_error_$$.log"

# LRO 错误码映射 (data[0] & 0x1F)
declare -A LRO_ERR_MAP=(
    [01]="MOTOR_OVERHEAT"
    [02]="OVER_CURRENT"
    [03]="UNDER_VOLTAGE"
    [04]="ENCODER_ERROR"
    [06]="BRAKE_OVERVOLT"
    [07]="DRV_ERROR"
)
# ============================================

if [ "$EUID" -ne 0 ]; then
  echo "请使用 sudo 运行"
  exit
fi

if [ "$SKIP_INIT" = false ]; then
echo ">>> 配置 restart-ms + txqueuelen..."
for IF in "${INTERFACES[@]}"; do
    ip link set "$IF" down 2>/dev/null
    if ip link set "$IF" type can bitrate $BITRATE sample-point $SAMPLEPOINT sjw $SJW \
            dbitrate $DBITRATE dsample-point $DSAMPLEPOINT dsjw $DSJW fd on restart-ms 100 2>/dev/null; then
        ip link set "$IF" mtu 72
        ip link set "$IF" txqueuelen 10000
    fi
    ip link set "$IF" up 2>/dev/null
    echo "  [$IF] ${BITRATE}/$DBITRATE ds${DSAMPLEPOINT} restart-ms=100 qlen=10000"
done
echo ""
fi

# LRO: 电机控制指令 ID=0x7FF, 4字节帧: ID_H ID_L 0x00 CMD
#   使能=0x06, 失能=0x07, 标零=0x03
CMD_ENABLE="06"
CMD_ZERO="03"

echo "=========================================================================================="
echo ">>> 逐接口 逐电机 使能 + 标零 (LRO 0x7FF 设置指令)"
echo "=========================================================================================="
for IF in "${INTERFACES[@]}"; do
    echo "  [$IF] ${NUM_MOTORS} 个电机..."
    for ((m=1; m<=NUM_MOTORS; m++)); do
        MID=$(printf '%04X' $m)
        echo -n "    [Motor $m] 使能..."
        cansend "$IF" "7FF#${MID}00${CMD_ENABLE}"
        sleep 0.3
        echo -n " 标零..."
        cansend "$IF" "7FF#${MID}00${CMD_ZERO}"
        sleep 0.2
        echo -n " 再使能..."
        cansend "$IF" "7FF#${MID}00${CMD_ENABLE}"
        sleep 0.3
        echo " 完成"
    done
done
echo ""

# 构建 64 字节 CANFD 一拖多数据帧
# LRO 一拖多: 扩展帧 0x8080, 每电机 8 字节, 最多 8 电机
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
> "$ERROR_LOG"

# ---------- 后台 candump 抓包 + 解析 LRO CANFD 错误 ----------
start_error_monitor() {
    local ifs=$(IFS=,; echo "${INTERFACES[*]}")
    # LRO 回复帧: can_id = motor_id (0x001-0x007), 标准帧, len >= 8
    stdbuf -oL candump -L "$ifs" 2>/dev/null | stdbuf -oL grep -E '^\([0-9.]+\)\s+can[0-9]+\s+00[1-7]#' | while read -r line; do
        local ts iface midhex data
        ts=$(echo "$line" | awk '{print $1}' | tr -d '()')
        iface=$(echo "$line" | awk '{print $2}')
        midhex=$(echo "$line" | awk '{print $3}' | cut -d'#' -f1)
        data=$(echo "$line" | awk '{print $3}' | cut -d'#' -f2)
        # LRO: error = data[0] & 0x1F (低5位)
        local err_byte=$((16#${data:0:2}))
        local err_code=$(( err_byte & 0x1F ))
        if [[ $err_code -ne 0 ]]; then
            local mid=$((16#$midhex))
            local err_hex=$(printf '%02X' $err_code)
            local err_name="${LRO_ERR_MAP[$err_hex]:-UNKNOWN}"
            echo "$ts|$iface|motor_$mid|$err_code|$err_name|$data"
        fi
    done >> "$ERROR_LOG" 2>/dev/null &
    CANDUMP_PID=$!
}

stop_error_monitor() {
    [[ -n "${CANDUMP_PID:-}" ]] && kill "$CANDUMP_PID" 2>/dev/null
    rm -f "$ERROR_LOG"
}

parse_error_summary() {
    declare -gA LRO_ERR_LAST
    declare -gA LRO_ERR_LAST_TS
    while IFS='|' read -r ts iface motor err_code err_name data; do
        LRO_ERR_LAST["$iface:$motor"]="$err_name(0x$(printf '%02X' ${err_code:-0}))"
        LRO_ERR_LAST_TS["$iface:$motor"]="$ts"
    done < "$ERROR_LOG"
}

cleanup() {
    echo -e "\n[!] 正在停止所有测试进程..."
    stop_error_monitor
    kill $(jobs -p) 2>/dev/null
    exit
}
trap cleanup INT

# 启动错误监控
if [[ $PARSE_ERROR -eq 1 ]]; then
    start_error_monitor
fi

# ---------- 构建 MIT 连发帧 ----------
echo ">>> 构建帧数据: CANID=0x${CANID}, $NUM_MOTORS 电机, MIT 全零 (LRO 一拖多)..."
FRAME_DATA=$(build_frame)
echo "    数据: $FRAME_DATA"
echo ""

declare -A last_rx last_tx
INTERVAL_SEC=1

while true; do
    clear
    echo "=========================================================================================="
    echo "    LRO CANFD MIT FLOOD [RESCUE ENABLED] - $(date +%T)"
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
    if [[ $PARSE_ERROR -eq 1 ]]; then
    echo "  电机错误反馈:"
    parse_error_summary
    local err_count=0
    for IF in "${INTERFACES[@]}"; do
        for ((m=1; m<=NUM_MOTORS; m++)); do
            local key="$IF:motor_$m"
            if [[ -n "${LRO_ERR_LAST[$key]:-}" ]]; then
                printf "    \033[31m[%s motor_%d] %s\033[0m (ts %s)\n" \
                    "$IF" "$m" "${LRO_ERR_LAST[$key]}" "${LRO_ERR_LAST_TS[$key]}"
                ((err_count++))
            fi
        done
    done
    if [[ $err_count -eq 0 ]]; then
        echo "    \033[32m全部正常\033[0m"
    fi
    fi
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
