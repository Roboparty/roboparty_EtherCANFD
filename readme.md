# roboparty EtherCANFD —— gs_usb 固件编译指南

4 路 CAN FD ↔ USB 转换器固件，USB 侧实现 **gs_usb** 协议。
插到 Linux 上就是标准 SocketCAN 设备（can0 ~ can3），不需要任何厂商驱动。

---

## 1. 概览

基于 HPMicro **HPM5E31**（RISC-V rv32imac）的 USB-CANFD 转换固件。固件把 4 路独立
CAN FD 控制器经 USB 高速接口暴露给主机，协议为社区通用的 **gs_usb**（candleLight 兼容），
因此 Linux 内核自带的 `gs_usb` 驱动、`python-can`、cangaroo 等工具可直接使用。

| 项目 | 值 |
|---|---|
| SoC | HPM5E31（HPM5E00 系列） |
| 内核架构 | RISC-V rv32imac_zicsr_zifencei |
| Flash | XPI0 NOR，固件烧写地址 `0x80000400` |
| USB | High-Speed，VID `0x1209` / PID `0x2323` |
| 序列号 | `202604070001` |
| 端点 | EP1 IN（`0x81`）/ EP2 OUT（`0x02`），512 字节包 |
| CAN 通道 | 4 路独立 CAN FD |
| 固件版本 | 1.1.0 |

---

## 2. 环境准备

速率由主机动态配置，无需为每个速率重新编译固件。每路支持独立的仲裁段和 FD 数据段
位时序；固件报告实际 CAN 时钟和两段各自的参数范围，并根据数据段分频自动选择 TDC。
只能使用时钟、控制器、收发器及实际布线支持的速率，通信双方的速率须匹配。

ONE_SHOT 严格遵守主机设置，固件不再默认强制禁止自动重发。电机部署如果要求
禁止陈旧命令重发，须在接口停用时显式配置 `one-shot on`。

真正的 bus-off 允许通过主机 START 请求重新初始化；`restart-ms` 是否可用仍取决于
主机驱动的恢复支持和门控配置。软件发送取消超时单独报告 `CAN_ERR_TX_TIMEOUT`，
不伪装成 bus-off，通道保持停用直到主机重启。USB 提交失败按 1ms 间隔重试；
此重试只处理尚未成功提交的传输，不处理已提交后的 QTD 错误。没有 EP 自动恢复。

例如将已停用的接口配置为仲裁 500k、数据 500k（`canX` 替换为实际接口名）：

```sh
sudo ip link set canX type can bitrate 500000 sample-point 0.750 sjw 2 dbitrate 500000 dsample-point 0.750 dsjw 2 fd on
sudo ip link set canX up
```

### 2.1 依赖

| 依赖 | 版本 | 来源 |
|---|---|---|
| CMake | ≥ 3.13 | sdk_env 自带 |
| Ninja | 任意 | sdk_env 自带 |
| Python 3 | 3.x | sdk_env 自带 |
| RISC-V GNU 工具链 | `rv32imac_zicsr_zifencei_multilib_b_ext`（GCC 13.2.0） | sdk_env 自带 |
| hpm_sdk | 1.13.0（已验证） | 随 sdk_env 一并 clone |

### 2.2 安装 sdk_env

若本机尚无 `C:\sdk_env`，先安装 HPMicro 官方 SDK 环境（含工具链、CMake、Ninja、Python3）：

```powershell
git clone --recurse-submodules https://github.com/hpmicro/sdk_env.git C:\sdk_env
```

装到别的路径也可以，但需要同步修改 `env.ps1` 里的路径。

### 2.3 加载环境

工程根目录下的 `env.ps1` 会一次性设置好全部环境变量与工具路径，**每开一个新终端都要执行一次**：

```powershell
. .\env.ps1
```

> 注意开头的 `. `（点 + 空格）不能省略，它表示在当前会话中执行；
> 直接 `.\env.ps1` 在子作用域运行，环境变量不会留在当前终端里。

成功时输出：

```
环境已加载:
  HPM_SDK_BASE = C:\sdk_env\hpm_sdk
  GNURISCV_TOOLCHAIN_PATH = C:\sdk_env\toolchains\rv32imac_zicsr_zifencei_multilib_b_ext-win
```

`env.ps1` 会设置这些内容：

| 变量 | 值 |
|---|---|
| `HPM_SDK_BASE` | `C:\sdk_env\hpm_sdk` |
| `GNURISCV_TOOLCHAIN_PATH` | `C:\sdk_env\toolchains\rv32imac_zicsr_zifencei_multilib_b_ext-win` |
| `HPM_SDK_TOOLCHAIN_VARIANT` | 空 |
| `PATH` 追加 | `C:\sdk_env\tools\cmake\bin`、`C:\sdk_env\tools\ninja`、`C:\sdk_env\tools\python3` |
| `JLINK_PATH` | `C:\Program Files\SEGGER\JLink`（该目录存在时才设置） |

---

## 3. 编译

### 3.1 三条命令

在**工程根目录**打开 PowerShell：

```powershell
# 1) 加载环境
. .\env.ps1

# 2) 配置
cmake -B build -S . -G Ninja

# 3) 编译
cmake --build build --parallel
```

本工程**默认即构建 gs_usb 固件**，不需要任何额外开关；板卡（`ethercanfd`）与构建类型
（`flash_xip`）也都是默认值，所以配置命令可以写得这么短。想显式写全等价于：

```powershell
cmake -B build -S . -G Ninja -DBOARD=ethercanfd -DHPM_BUILD_TYPE=flash_xip
```

配置成功的标志：

```
-- hpm_sdk: 1.13.0 (C:\sdk_env\hpm_sdk)
-- GNU ld linker script: .../soc/HPM5E00/HPM5E31/toolchains/gcc/flash_xip.ld
-- Configuring done
-- Generating done
```

编译成功后末尾会打印内存占用：

```
Memory region         Used Size  Region Size  %age Used
           FLASH:      116128 B         1 MB     11.07%
             ILM:        2648 B       128 KB      2.02%
             DLM:         32 KB       128 KB     25.00%
        AXI_SRAM:       56112 B       192 KB     28.54%
AXI_SRAM_NONCACHEABLE:   14344 B        64 KB     21.89%
        AHB_SRAM:         10 KB        32 KB     31.25%
```

### 3.2 编译产物

全部在 `build/output/` 下：

| 文件 | 大小 | 说明 |
|---|---|---|
| `ethercanfd.bin` | 116256 B | **烧录用**，已含 HPM 引导头 |
| `ethercanfd_v1.1.0-<日期>.bin` | 116256 B | 带版本号的副本 |
| `ethercanfd.elf` | — | 调试用可执行文件 |
| `ethercanfd.map` | — | 内存与符号分布 |
| `ethercanfd.asm` | — | 反汇编 |

`ethercanfd.bin` 开头 16 字节为：

```
02 00 F9 FC 05 00 00 00 00 10 00 00 00 00 00 00
```

小端解读即 `0xfcf90002, 0x05, 0x1000, 0x0`，这正是烧录工具所需的引导头，
**无需再做任何镜像转换**，可直接烧写。

### 3.3 重新编译

Ninja 会增量编译，改一个源文件只重编受影响的部分。若改动了 CMake 选项或板级配置，
必须清掉构建目录，否则会残留旧对象：

```powershell
Remove-Item -Recurse -Force build
cmake -B build -S . -G Ninja
cmake --build build --parallel
```

只想强制重编应用代码（约 5 秒，SDK 库不动）：

```powershell
cmake --build build --target clean
cmake --build build --parallel
```





## 4. 目录速览

```
CMakeLists.txt          顶层构建脚本（板卡、版本号、源文件列表）
env.ps1                 环境变量：SDK 路径、工具链、cmake/ninja/python3 的 PATH
app.yaml                SDK 组件依赖声明
src/
  main.c                入口：LED 自检 + 启动 gs_usb 主循环
  gs_usb.c              gs_usb 协议实现（4 路 CAN FD ↔ USB）
  usb_config.h          USB 描述符配置
  version.h.in          版本号模板，由 CMake 生成到 build/generated/
boards/ethercanfd/      板级支持：时钟、引脚复用、CAN 收发器、LED
doc/dfu.md              HPM Manufacturing Tool 烧录参考
```
