# EtherCAT 分支说明

本文档只记录 EtherCAT 分支（PDO/SDO/FoE）开发与联调流程。
gs_usb 分支已全部完成，不在此文档中展开。

## 0. 硬件 LED 指示灯

板载 6 个 LED，分两种引脚布局，由 PF27 (ADC0.IN00) 电压自动识别：

| 电压 | 引脚布局 | CAN0 | CAN1 | CAN2 | CAN3 | USB/ECAT | MODE |
|------|---------|------|------|------|------|----------|------|
| >1.2V (默认) | PF02-07 | PF02 绿 | PF03 绿 | PF04 绿 | PF05 绿 | PF06 蓝 | PF07 黄 |
| <1.2V (alt)  | PB26-31 | PB26 绿 | PB27 绿 | PB28 绿 | PB29 绿 | PB30 蓝 | PB31 黄 |

### EtherCAT 模式

| LED | 行为 |
|-----|------|
| 绿灯 CAN0~3 | 对应通道有 CAN 帧收发时闪（持续约 50~200ms） |
| 蓝灯 ECAT | INIT → 灭，PREOP → 慢闪，SAFEOP → 快闪，OP → 常亮 |
| 黄灯 MODE | OP 状态 → 常亮，其他状态 → 灭 |

### GS_USB 模式

| LED | 行为 |
|-----|------|
| 绿灯 CAN0~3 | 对应通道有 CAN 帧收发时闪（持续约 50~200ms） |
| 蓝灯 USB | 未连接 → 灭，已连接未配置 → 慢闪，已配置 → 快闪 |
| 黄灯 MODE | 持续慢闪（约 500k 主循环 tick 翻转） |

## 1. 环境准备

1. 准备 HPM SDK 环境：
	https://github.com/hpmicro/sdk_env
2. 在当前工程目录加载环境：

```powershell
. .\env.ps1
```

3. 确认关键环境变量：

```powershell
$env:HPM_SDK_BASE
$env:JLINK_PATH
```

## 2. EtherCAT 固件构建

```powershell


cmake -B build -S . -G Ninja 

```

说明：
1. 当前文档默认构建 EtherCAT 固件（即 `BUILD_GS_USB=OFF` 默认路径）。
2. 如需切板卡，请在 `CMakeLists.txt` 中调整 `BOARD`。

## 3. 烧录

```powershell
cmake --build build --target jlink_flash
```

## 4. EtherCAT 联调与验收

1. 主从站 PDO 576 字节映射联调（4 路 TX + 4 路 RX）。
2. SDO 配置与状态查询联调（`0x8000` 配置、`0x9000` 状态）。
3. FoE 升级流程验证（写入、校验、重启回读版本）。
4. 四路 CANFD 独立通道验证（并发收发、错误注入、恢复）。

## 5. 压力测试项（EtherCAT）

1. 周期负载压测：持续高频 PDO 收发，统计丢帧与抖动。
2. 极限吞吐压测：多通道满载报文，检查 CPU 占用与错误计数。
3. 异常恢复压测：Bus-Off、Error Passive、链路抖动后的恢复能力。

移植指南：在board文件夹移植指定的hpm芯片，注意hpm6e系列需要运用patch文件：https://github.com/hpmicro/sdk_env/blob/main/hpm_sdk/samples/ethercat/ecat_io/README_zh.rst


# roboto_usb4can CAN FD 1M/5M (gs_usb)
SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="2323", ATTRS{serial}=="202604070001", KERNEL=="can*", RUN+="/sbin/ip link set %k type can bitrate 1000000 sample-point 0.800 sjw 4 dbitrate 5000000 dsample-point 0.750 dsjw 2 fd on", RUN+="/sbin/ip link set %k mtu 72", RUN+="/sbin/ip link set %k txqueuelen 10000", RUN+="/sbin/ip link set %k up"
