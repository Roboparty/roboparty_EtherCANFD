EtherCAT 转 CANFD 运控网关设计方案
1. 系统概述与整体拓扑
本方案旨在设计一款高性能工业级通信网关，实现上位机（RK3588）经由 EtherCAT 总线对末端四路独立 CANFD 节点的超低延迟控制。
2. 硬件架构设计
网关硬件以 HPM5E 芯片为核心，发挥其内置 EtherCAT 从站控制器（ESC）与多路高吞吐 CANFD 外设的优势。
•核心处理器：HPM5E（具备强劲的内部总线带宽与中断响应能力）。
•EtherCAT 链路：
￮PHY 芯片：对接双网口（IN/OUT），保障百兆工业以太网物理层信号完整性。
￮I2C EEPROM：用于持久化存储 EtherCAT 从站信息（ESI 文件配置）。
•CANFD 链路：
￮收发器：配置支持至少 8Mbps 速率的高速 CANFD 收发器。
￮接口与端子：四组独立的工业级隔离 CAN 接口端子，分别连接四条总线。
3. 交互帧分类
针对雷赛电机一拖多机制（一个 CANFD 帧控制总线上所有电机），系统的分类通信策略按 EtherCAT 数据模式定义如下：
3.1 基于 PDO (Process Data Object) 的周期性实时通信
核心定位：处理对时间极度敏感的运控指令，必须内核态全速运行，HPM5E 侧不作任何业务解析，纯粹执行内存拷贝与物理层转发。四路 CANFD 严格保持完全一致的控制频率，与 EtherCAT 的通信周期深度绑定。
•PTM控制帧（全速周期任务）：上位机 RK3588 在高速 PTM 模式控制状态时，按照统一的全局控制频率生成四路控制报文，并将其映射为对应通道的 PDO 负载。HPM5E 收到该 PDO 数据后，全速打入对应的物理总线，实现极低延迟的电机闭环控制。
•读取/调试帧（单电机参数读取/标零）：针对总线上的单台电机标定，网关保持透传逻辑不变，不增加 HPM5E 的解析负担。由 RK3588 侧的运控程序/标定工具负责空帧构造。上位机直接组装一帧完整的 64 字节 CANFD 报文，HPM5E 视其为普通数据帧，仅负责将这个长帧原封不动地广播出去。
•电机反馈帧（反馈电机状态）：在每一个严格定时的 EtherCAT 通信周期（如 1ms 或 500us）中，IgH 将包含着四路电机复合反馈数据的 TxPDO 接收入物理内存中。Motor 层无需进行任何形式的内存数据拷贝或协议解包重组，直接依据预先定义好的地址偏移量，精准提取出对应 CAN0 至 CAN3 物理通道上的电机状态数据（如实际位置、实际速度）。
3.2 基于 SDO (Service Data Object) 与邮箱通信的非周期异步通信
核心定位：处理网关自身的行为配置、参数下发以及固件更新。这些任务按需触发，走 EtherCAT 的邮箱通道，绝对不挤占 PDO 的高频实时带宽。
•CANFD配置帧（总线控制）：对于 CANFD 接口的开启、关闭，以及 2M 或 8M 波特率的动态切换，将控制指令定义为 EtherCAT 的 SDO 对象。RK3588通过向特定的 SDO 字典索引发起写请求，直接触发 HPM5E 内部的寄存器配置逻辑，从而改变物理总线的工作状态。
•CANFD查询帧（查询总线状态）：为弥补完全剥离网络子系统后标准命令工具的缺失，必须利用 SDO 机制设计了总线硬件诊断机制（例如发送/接收错误计数器 TEC/REC、Bus-Off 节点离线状态、错误主动/被动模式等）。主站在系统排查故障时，可通过 IgH 命令行直接拉取这些关键诊断指标。
•OTA 升级机制（基于 FoE）：采用 EtherCAT 标准的 FoE（File Access over EtherCAT）协议，这属于邮箱通信的高级应用。上位机直接使用原生的 ssctool 推送固件文件，数据通过邮箱通道分块传输。HPM5E 固件层实现 FoE 的写操作回调函数，接收完毕后自动完成内部 Flash 的擦写与系统重启。
•日志功能（排查通信故障）：主站可通过 IgH 命令行或非实时服务按需读取，获取带有精确时间戳的结构化错误码，便于上层运控算法做出故障降级决策。
4. 软件架构与内核空间设计
由主站（RK3588）承担复杂的协议组装，从站（HPM5E）专注于时序同步与高速转发。
4.1 主站侧核心功能实现（RK3588 / Linux 环境） 
主站侧彻底摒弃了传统的网络协议栈开销，软件核心聚焦于 EtherCAT 栈的精细化管控与交互。
需重点实现以下功能模块：
•主站部署：加载专有的 i210 网卡实时驱动程序，初始化 IgH。
•数据内存映射：向 IgH 协议栈注册从站的 PDO 字典结构，申请并锁定一块连续的物理共享内存，将其精准划分为对应四路 CANFD 通道的收发偏移槽位。
•数据交换（ethercat-canfd层）：在实时核环境下运行核心控制循环。该线程直接操作 PDO 共享内存指针，按指定偏移量写入控制帧数据，并零延迟读取反馈帧数据。
•协议封包与解包(motor层）：主站软件接管雷赛电机的专属协议组装工作。包括一拖多控制字的字节拼接、标定空帧占位符的构造，以及对反馈长帧的位域解析。
•非实时服务（调试配置层）：运行于非实时空间的守护进程或命令行脚本，负责调用 IgH 的 API 发起 SDO 读写请求（执行 CANFD 接口配置与硬件状态查询），推送 FoE 固件文件进行 OTA，并定期读取系统的结构化诊断日志。
4.2 从站侧核心功能实现（HPM5E 固件） 
从站软件设计的核心原则是极简与绝对的确定性，专注于数据交换及同步，不涉及任何电机的业务控制逻辑。
需重点实现以下功能模块：
•硬件外设初始化：配置百兆以太网 PHY 芯片寄存器，初始化 I2C EEPROM 加载 ESI 节点配置信息，并完成四路 CANFD 控制器及高速收发器的底层引脚与时钟树初始化。
•同步中断服务（数据解析/获取层）：精确捕获并响应 EtherCAT 中断。在中断服务函数中，立刻将获取到的控制报文打入四路 CANFD的缓冲区，确保四路物理总线发送起始位的绝对并发。
•内存搬运（数据传输层）：建立 CANFD 缓冲区与 ESC 控制器内部 RAM 之间的映射。利用 DMA 或 CPU 高效指令，高效完成电机-主控数据搬运。
•SDO/FoE事件处理（调试配置层）：编写对象字典的回调响应函数。当收到主站的特定 SDO 写入时，动态调整 CAN 控制器寄存器（如切换波特率或启停总线）；当收到 SDO 读取时，返回底层的错误计数器；实现 FoE 协议的 Flash 扇区擦写逻辑以支持系统级 OTA 更新，并实时维护系统的诊断日志环形缓冲区。
5. ECANT 通信协议规范
ECANT 通信协议专为基于 EtherCAT 工业以太网与 CANFD 现场总线的异构网关系统设计。本协议旨在实现上位机（主站）与末端多路 CANFD 节点（如雷赛电机）之间的高实时、低延迟、零拷贝数据交互。协议严格分离高频的运动控制数据与低频的诊断配置数据，通过 PDO 与 SDO 双通道并行机制，保障系统在极端高负载工况下的通信确定性与鲁棒性。
5.1 基础报文帧结构定义
为确保上位机（64位架构）与底层网关（32位架构）在内存映射过程中的绝对对齐，消除跨平台解析偏差，协议规定单路 CANFD 的数据交换基础单元强制进行 4 字节对齐，单路占用固定 72 字节。
•CAN ID (4 字节)：报文标识符，包含标准帧/扩展帧标志。
•DLC (1 字节)：数据长度代码，指示负载有效长度（最大 64 字节）。
•Flags (1 字节)：通信控制标志位（涵盖 FDF 与 BRS 波特率切换使能）。
•Reserved (2 字节)：保留位，强制补齐以实现严格的 32 位内存对齐。
•Payload (64 字节)：CANFD 数据负载核心区域。
5.2 周期性实时通信 (PDO 通道)
ECANT 协议的 PDO 通道为固定 576 字节的实时周期通信通道，采用 4 路发送与 4 路接收的对称结构，单路 CANFD 帧长度为 72 字节，可同时承载电机运动控制帧与参数查询帧两类数据，两类帧共用同一套通信结构与传输通路，协议本身不做格式区分与解析处理，仅以透明透传的方式完成数据交互，既满足多轴电机同步运动控制的实时性要求，也支持参数读取、设备校验等查询指令的下发传递，实现控制指令与查询指令的统一承载。
为解决 EtherCAT 周期通信带来的脏数据与帧延迟问题，ECANT 协议通过 SDO 配置寄存器实现 PDO 通道的忽略控制机制，上位机可通过 IGNORE_RX 与 IGNORE_TX 两个标志位分别屏蔽上行反馈数据与下行发送数据，在初始化阶段开启双忽略以清空历史残留数据，在通信启动首帧忽略上行数据以丢弃旧反馈，在退出阶段忽略下行数据以避免残留指令执行，整套机制可有效过滤无效数据与脏帧，确保电机控制与参数查询过程中数据的有效性与可靠性。
5.3 非周期异步通信与控制 (SDO 通道)
ECANT 协议的 SDO 通道采用 EtherCAT CoE 标准规范实现，主要用于 CANFD 总线的全局配置与运行控制，支持对总线使能、CANFD 功能开关、通信波特率等关键参数进行读写配置，波特率区间严格限定在 2Mbps 及以上，同时提供总线状态、发送错误计数、接收错误计数等实时状态反馈，保证上下位机能够对总线运行情况进行完整监控与管理。
针对 EtherCAT 周期通信固有的数据延迟与残留问题，协议通过专用 SDO 寄存器实现上行数据与下行数据的独立忽略控制，上位机可通过标志位分别屏蔽上行脏数据与下行残留指令，在初始化、启动、停止、查询交互等不同阶段灵活配置忽略策略，从通信层面彻底消除历史脏数据对控制流程与查询结果的干扰。
SDO 通道同时提供设备基础信息查询与调试日志功能，支持读取硬件版本、固件版本、设备类型等基础信息，日志系统采用类 dmesg 的环形缓存结构，仅保留最新的 50 条日志信息，通过 SDO 接口可实现日志逐条读取与缓存清空操作，满足设备调试与故障定位的轻量化需求。
5.4 固件升级与维护 (FoE 机制)
协议的固件升级功能不使用自定义 SDO 实现，而是遵循 EtherCAT 标准 FoE (File Access over EtherCAT) 文件传输机制完成，该方式为主流上位机平台通用的升级方式，具备传输稳定、通用性强、无需私有协议解析等优势，可实现固件的安全、可靠、标准化升级。

6.协议
好！我来像 Wireshark 一样，在字节流层面逐字节拆解一个 CANFD 扩展帧。



场景设定

我们发一帧：
•扩展帧，ID = 0x00000601，加上 CAN_EFF_FLAG
•DLC = 8
•flags = FDF | BRS = 0x03
•payload = 40 60 60 00 00 00 00 00（CANopen 读位置指令）



字节流逐字节分析

Plain Text
Offset  Hex                    解析
──────────────────────────────────────────────────────────────────
/* ── can_id：4字节，小端序 ── */
0x00    01                     can_id byte0 (LSB)
0x01    06                     can_id byte1
0x02    00                     can_id byte2
0x03    20                     can_id byte3 (MSB) ← bit29=1 扩展帧标志

                               拼合：0x20000601
                               bit29 = 1        → EFF=1 扩展帧 ✓
                               低29位 = 0x601   → 实际ID = 0x601 ✓

/* ── dlc：1字节 ── */
0x04    08                     DLC = 8，本帧有效数据8字节

/* ── flags：1字节 ── */
0x05    03                     bit0=1 → FDF=1 CANFD帧 ✓
                               bit1=1 → BRS=1 波特率切换使能 ✓

/* ── reserved：2字节 ── */
0x06    00                     reserved[0] 填充对齐
0x07    00                     reserved[1] 填充对齐

/* ── payload：64字节 ── */
0x08    40                     payload[0]  CANopen SDO读请求命令字
0x09    60                     payload[1]  对象字典索引低字节 0x6060
0x0A    60                     payload[2]  对象字典索引高字节
0x0B    00                     payload[3]  子索引 = 0x00
0x0C    00                     payload[4]  数据区（读请求填0）
0x0D    00                     payload[5]
0x0E    00                     payload[6]
0x0F    00                     payload[7]
0x10    00 00 00 00 00 00 00   payload[8~14]  ┐
0x17    00 00 00 00 00 00 00   payload[15~21] │
0x1E    00 00 00 00 00 00 00   payload[22~28] │ 剩余56字节填0
0x25    00 00 00 00 00 00 00   payload[29~35] │
0x2C    00 00 00 00 00 00 00   payload[36~42] │
0x33    00 00 00 00 00 00 00   payload[43~49] │
0x3A    00 00 00 00 00 00 00   payload[50~56] │
0x41    00 00 00 00 00 00 00   payload[57~63] ┘

/* ── 帧结束 ── */
0x47    (end)                  总计 72 字节 ✓



关键字节重点看这里

Plain Text
Offset 0x00~0x03：
┌────┬────┬────┬────┐
│ 01 │ 06 │ 00 │ 20 │
└────┴────┴────┴────┘
  ↑              ↑
 LSB            MSB
                bit29=1 → 扩展帧标志位就藏在这个字节的 bit1

Plain Text
Offset 0x05（flags）：
┌──────────────────────┐
│ 0000 0011            │
└──────────────────────┘
         ││
         │└── bit0 = FDF = 1（CANFD格式）
         └─── bit1 = BRS = 1（高速段使能）



如果是标准帧对比看

同样 ID=0x601，但不加 CAN_EFF_FLAG：

Plain Text
Offset  Hex
0x00    01
0x01    06
0x02    00
0x03    00   ← bit29=0，标准帧，低11位取ID
             拼合：0x00000601 → 实际ID = 0x601

唯一区别就是 byte3 的 bit29，其他字节流完全一样。



对应到你的 ECANT PDO 映射

4路TX打包进去就是这样：

Plain Text
PDO字节流（288字节TX区）：

[0x000 ~ 0x047]  第1路 CANFD帧（72字节）
[0x048 ~ 0x08F]  第2路 CANFD帧（72字节）
[0x090 ~ 0x0D7]  第3路 CANFD帧（72字节）
[0x0D8 ~ 0x11F]  第4路 CANFD帧（72字节）

Wireshark 抓到的 EtherCAT PDO 原始字节流就长这样，每隔72字节就是一路新的 CANFD 帧。



清楚了吗？要不要我再拆一帧接收回来的电机反馈帧，或者把 PDO 576字节的完整布局也画出来？

好！我把整个 576 字节完整画出来：



576 字节 PDO 完整结构

Plain Text
总布局：
┌─────────────────────────────────┐  offset 0x000
│         TX 区（288字节）         │  下行：主站 → 网关 → 电机
└─────────────────────────────────┘
┌─────────────────────────────────┐  offset 0x120
│         RX 区（288字节）         │  上行：电机 → 网关 → 主站
└─────────────────────────────────┘  offset 0x240



TX 区逐字节展开（288字节）

Plain Text
┌──────────────────────────────────────────────────────┐
│ 第1路 TX（offset 0x000 ~ 0x047，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x000  │ can_id byte0 (LSB)                           │
│ 0x001  │ can_id byte1                                 │
│ 0x002  │ can_id byte2                                 │
│ 0x003  │ can_id byte3 (MSB) ← bit29=扩展/标准帧标志   │
│ 0x004  │ dlc                ← 有效数据长度 0~64        │
│ 0x005  │ flags              ← bit0=FDF  bit1=BRS      │
│ 0x006  │ reserved[0]                                  │
│ 0x007  │ reserved[1]                                  │
│ 0x008  │ payload[0]                                   │
│ 0x009  │ payload[1]                                   │
│  ...   │ ...                                          │
│ 0x047  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第2路 TX（offset 0x048 ~ 0x08F，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x048  │ can_id byte0                                 │
│ 0x049  │ can_id byte1                                 │
│ 0x04A  │ can_id byte2                                 │
│ 0x04B  │ can_id byte3                                 │
│ 0x04C  │ dlc                                          │
│ 0x04D  │ flags                                        │
│ 0x04E  │ reserved[0]                                  │
│ 0x04F  │ reserved[1]                                  │
│ 0x050  │ payload[0]                                   │
│  ...   │ ...                                          │
│ 0x08F  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第3路 TX（offset 0x090 ~ 0x0D7，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x090  │ can_id byte0                                 │
│  ...   │ ...                                          │
│ 0x0D7  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第4路 TX（offset 0x0D8 ~ 0x11F，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x0D8  │ can_id byte0                                 │
│  ...   │ ...                                          │
│ 0x11F  │ payload[63]                                  │
└──────────────────────────────────────────────────────┘



RX 区逐字节展开（288字节）

Plain Text
┌──────────────────────────────────────────────────────┐
│ 第1路 RX（offset 0x120 ~ 0x167，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x120  │ can_id byte0                                 │
│ 0x121  │ can_id byte1                                 │
│ 0x122  │ can_id byte2                                 │
│ 0x123  │ can_id byte3 ← bit29 电机回来的帧类型        │
│ 0x124  │ dlc                                          │
│ 0x125  │ flags                                        │
│ 0x126  │ reserved[0]                                  │
│ 0x127  │ reserved[1]                                  │
│ 0x128  │ payload[0]                                   │
│  ...   │ ...                                          │
│ 0x167  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第2路 RX（offset 0x168 ~ 0x1AF，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x168  │ can_id byte0                                 │
│  ...   │ ...                                          │
│ 0x1AF  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第3路 RX（offset 0x1B0 ~ 0x1F7，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x1B0  │ can_id byte0                                 │
│  ...   │ ...                                          │
│ 0x1F7  │ payload[63]                                  │
├──────────────────────────────────────────────────────┤
│ 第4路 RX（offset 0x1F8 ~ 0x23F，72字节）              │
├────────┬──────────────────────────────────────────────┤
│ 0x1F8  │ can_id byte0                                 │
│  ...   │ ...                                          │
│ 0x23F  │ payload[63]                                  │
└──────────────────────────────────────────────────────┘



整体一张图

Plain Text
offset  0x000                    0x120                    0x240
        │<────── TX 288字节 ──────>│<────── RX 288字节 ──────>│

        │  ch1  │  ch2  │  ch3  │  ch4  │  ch1  │  ch2  │  ch3  │  ch4  │
        │  72B  │  72B  │  72B  │  72B  │  72B  │  72B  │  72B  │  72B  │

        0x000  0x048  0x090  0x0D8  0x120  0x168  0x1B0  0x1F8  0x240



每一路72字节内部结构

Plain Text
+0   +1   +2   +3   +4   +5   +6   +7   +8 ........... +71
│     can_id    │ dlc │flag │ res │ res │   payload[64]   │
│   4字节        │ 1B  │ 1B  │ 1B  │ 1B  │   64字节        │



这就是完整的 576 字节。你上位机 memcpy 整块映射进去，网关按偏移量拆，每隔 72 字节就是一路新的 CANFD 帧，TX 和 RX 各4路完全对称。

还有问题吗？
好！把这个双向流程完整梳理一下：



整体双向数据流

Plain Text
上位机
  │  ↓ 写PDO（TX区）          ↑ 读PDO（RX区）
  │
EtherCAT总线
  │  ↓ EtherCAT帧下行         ↑ EtherCAT帧上行
  │
网关
  │  ↓ 解包→发CANFD            ↑ 收CANFD→打包
  │
CANFD总线
  │
电机



网关处理逻辑（每个EtherCAT周期）

第一步：收到 EtherCAT 帧，解包TX区

Plain Text
EtherCAT帧到达
        ↓
取 PDO offset 0x000 开始的 288 字节（TX区）
        ↓
按72字节步长遍历4路：

for i in 0..3:
    base = i * 72

    // 读 can_id
    can_id = bytes[base+0]
           | bytes[base+1] << 8
           | bytes[base+2] << 16
           | bytes[base+3] << 24

    // 判断帧类型
    if can_id & CAN_EFF_FLAG:        // bit29=1
        real_id = can_id & 0x1FFFFFFF  // 取低29位
        frame_type = 扩展帧
    else:
        real_id = can_id & 0x7FF       // 取低11位
        frame_type = 标准帧

    // 读其他字段
    dlc   = bytes[base+4]
    flags = bytes[base+5]
    payload = bytes[base+8 .. base+71]

    // 判断BRS/FD
    if flags & FDF:
        用CANFD格式发送
    if flags & BRS:
        数据段切换高速

    // 发到CANFD总线
    canfd_send(real_id, frame_type, dlc, payload)



第二步：从 CANFD 总线收帧，打包进RX区

Plain Text
CANFD总线收到电机回包
        ↓
// 同样逐帧判断
if 收到的帧 can_id & CAN_EFF_FLAG:
    打包时 bit29 置1   → 扩展帧
else:
    打包时 bit29 不动  → 标准帧

// 按路数填入RX区
base = 0x120 + i * 72

bytes[base+0] = can_id & 0xFF
bytes[base+1] = (can_id >> 8)  & 0xFF
bytes[base+2] = (can_id >> 16) & 0xFF
bytes[base+3] = (can_id >> 24) & 0xFF   ← bit29原样保留
bytes[base+4] = dlc
bytes[base+5] = flags
bytes[base+6] = 0x00   // reserved
bytes[base+7] = 0x00   // reserved
bytes[base+8 .. base+71] = payload
        ↓
// 等下一个EtherCAT周期上行
打包进 EtherCAT 帧 RX区
        ↓
发回上位机



第三步：时序对齐（关键）

Plain Text
EtherCAT周期T：
├── 收到下行帧 → 解包TX → 发4路CANFD
│                              ↓
│                         电机处理
│                              ↓
│                         电机回包
├── 收到CANFD回包 → 打包RX
└── 上行帧 → 带着RX区数据回上位机

EtherCAT周期T+1：
上位机读到上一周期的电机反馈



扩展帧/标准帧识别汇总

Plain Text
方向          识别点              处理
──────────────────────────────────────────
下行解包      can_id bit29        =1取低29位
                                  =0取低11位
上行打包      CANFD收到的帧       bit29原样透传
                                  上位机自己判断



对应你的 IGNORE 机制

Plain Text
初始化阶段：
IGNORE_TX=1 → 网关收到TX区不发CANFD（忽略脏数据）
IGNORE_RX=1 → 网关不填RX区（上位机读到的全是0）

正常运行：
IGNORE_TX=0 → 正常解包发送
IGNORE_RX=0 → 正常打包上行

退出阶段：
IGNORE_TX=1 → 停止发CANFD（防残留指令）
IGNORE_RX=0 → 继续上行（让上位机看到最后状态）



整个流程核心就一句话：

网关就是一个透明转换器，EtherCAT帧↔CANFD帧，帧类型由bit29自描述，双向原样透传，不做任何内容解析。

# EtherCANFD 固件架构

HPM5E31 从站固件，实现 4 通道 CAN FD ↔ EtherCAT 双向网关。也支持 GS_USB（USB 转 CAN）模式。

## 启动流程

```
main()
 ├─ board_init()                  — 时钟、引脚、控制台、PMP
 ├─ 打印 banner: "=== ethercanfd v<version> ==="
 ├─ 读取 WKUP 引脚状态
 └─ 分支:
     WKUP_GS_USB_MODE=0 (默认):
       WKUP 按下 → ecat_run()     — EtherCAT 模式
       WKUP 释放 → gs_usb_run()   — GS_USB USB-CAN 模式
     WKUP_GS_USB_MODE=1 (反转):
       WKUP 按下 → GS_USB
       WKUP 释放 → EtherCAT
```

**没有 bootloader、没有 OTA-apply-on-boot、没有固件槽位检查。** 上电直接进入选择的应用模式，`ecat_run()` / `gs_usb_run()` 均不返回。

### ecat_run() 初始化

```
1. board_init_ethercat()     — ESC 引脚/PHY 初始化
2. board_init_switch_led()
3. ecat_hardware_init()      — ESC + EEPROM + PHY 硬件初始化
4. MainInit()                — SSC 初始化，重置所有函数指针为 NULL
5. 安装 CAN ISR（4 通道）
6. ecant_canfd_init()        — 配置 4 路 CAN-FD 控制器
7. APPL_GenerateMapping()    — PDO 映射
8. bRunApplication = TRUE    — 进入主循环
```

## 主循环数据流

```
forever:
  forward_can_to_ecat()    ── CAN FIFO 轮询 → TOBJ6xxx (CoE 输入对象)
  PDO_OutputMapping()      ── ESC SM2 → TOBJ7xxx (显式调用)
  PDO_InputMapping()       ── TOBJ6xxx → aPdInputData → ESC SM3 (显式调用)
  MainLoop()               ── SSC 状态机 (ECAT_Main, COE_Main)
  forward_ecat_to_can()    ── TOBJ7xxx → CAN TX FIFO
```

### 状态门控

| 条件 | 说明 |
|---|---|
| AL State = SAFEOP 或 OP | PDO 映射生效；CAN→ECAT 转发开启 |
| AL State = OP | TX 帧写入 `subindex005` 作为触发器 |
| 退出 OP | 取消所有 CAN TX 缓冲区、清零触发计数器 |
| 进入 OP | 清空 RX 队列、写 `ESC_PD_WD_TIME` |
| TX 超时 (20ms) | 调用 `mcan_cancel_tx_buf_send_request()` 取消发送 |

## 关键设计决策

### 显式 PDO_OutputMapping / PDO_InputMapping

`MainLoop()` 的 FreeRun 路径仅首帧可见 AL Event（寄存器读后清零）。后续 ECAT 周期中 `PDO_OutputMapping` 不再被调。修复：在 `ecat_run()` 主循环中每轮显式调 `PDO_OutputMapping()` + `PDO_InputMapping()`。

### SubIndex005 不清零

`APPL_InputMapping()` 中不再清零 `SubIndex005`。seq 仅由 `forward_can_to_ecat()` 设置（CAN RX 帧到达时递增）。主站按 seq 变化去重，重复读不计入 `rx_total`。

### CAN RX 轮询

`mcan_read_rxfifo()` 每轮只读一帧（`if` 非 `while`）。ISR 仅清中断标志，不读 FIFO。避免 ISR→队列→dequeue 路径的延迟和同步问题。

### 看门狗

- 进 OP 时写 `ESC_PD_WD_TIME = 0` 关闭硬件看门狗
- 主循环中 `EcatWdValue = 0` 放在 `MainLoop()` 之后

## FoE / OTA 固件更新

**已移除。** 固件更新统一使用 HPM DFU 模式（USB）或 J-Link。

移除的文件：`src/ecat_foe_support.c`、`src/ecat_foe_support.h`

移除的宏（`ecat_config.h`）：`FLASH_OTA_SLOT_ADDR`、`FLASH_OTA_SLOT_SIZE`、`FLASH_OTA_META_ADDR`、`FLASH_OTA_MAGIC`

移除的代码（`ethercanfd.c`）：`extern bool foe_reset_request`、主循环中 `foe_reset_request` 检查 + `foe_support_soc_reset()`

## 为什么不用 FoE / OTA 自擦除

HPM5E31 是单 Bank 内部 flash。运行 Bank 0 中的代码时无法擦除 Bank 0。FoE write 到 OTA 槽位（Bank 1，`0x90000`）可成功，但要从 Bank 0 中 apply（擦除 `0x00000` 起始段）必然挂死。

尝试过的方法（均失败）：
- `ATTR_RAMFUNC` + 关 Cache
- 扇区逐个擦除
- 双 Bank 链接脚本（破坏了 SSC 绝对地址引用）

## 对象字典

| 索引 | 名称 | 用途 |
|---|---|---|
| 0x6000 | Can0RxFrame | CH0 CAN→ECAT 输入帧 (72B) |
| 0x6010 | Can1RxFrame | CH1 |
| 0x6020 | Can2RxFrame | CH2 |
| 0x6030 | Can3RxFrame | CH3 |
| 0x7000 | Can0TxFrame | CH0 ECAT→CAN 输出帧 (72B) |
| 0x7010 | Can1TxFrame | CH1 |
| 0x7020 | Can2TxFrame | CH2 |
| 0x7030 | Can3TxFrame | CH3 |
| 0x8000 | Can0Config | CH0 通道使能/速率/FD 配置 |
| 0x8010 | Can1Config | CH1 |
| 0x8020 | Can2Config | CH2 |
| 0x8030 | Can3Config | CH3 |
| 0x9000 | Can0Status | CH0 TEC/REC/总线状态 |
| 0x9010 | Can1Status | CH1 |
| 0x9020 | Can2Status | CH2 |
| 0x9030 | Can3Status | CH3 |
| 0xA000 | DataControl | 全局 ignore_tx/ignore_rx |

## TOBJ6000 帧结构 (72 字节)

```
Offset  0-1:  SubIndex001  (UINT16) ── 未映射到 PDO
Offset  2-3:  CanIdLo      (UINT16) ── CAN ID 低 16 位
Offset  4-5:  CanIdHi      (UINT16) ── CAN ID 高 16 位 (bit13=1: 扩展帧)
Offset    6:  Dlc          (UINT8)  ── DLC (0-15)
Offset    7:  Flags        (UINT8)  ── bit0=FDF, bit1=BRS
Offset  8-9:  SubIndex005  (UINT16) ── 序列号/触发器
Offset 10-39: Payload1     (CHAR[30])
Offset 40-69: Payload2     (CHAR[30])
Offset 70-73: Payload3     (CHAR[4])
```

PDO 映射跳过 SubIndex001（≠2 字节），从 CanIdLo 开始复制 72 字节。

## CAN 配置

全通道：仲裁 1Mbps，数据 5Mbps，FD+BRS，TDC 使能，40MHz 时钟。

## 构建文件

| 来源 | 文件 | 说明 |
|---|---|---|
| port/ | `hpm_ecat_hw.c` | ESC 硬件初始化 |
| port/ | `hpm_ecat_phy.c` | PHY 配置 |
| port/ | `hpm_ecat_e2p_emulation.c` | EEPROM 模拟 (条件: `CONFIG_EEPROM_EMULATION=1`) |
| src/ | `main.c` | 入口，启动模式选择 |
| src/ | `ethercanfd.c` | EtherCAT/CAN-FD 网关主逻辑 |
| src/ | `gs_usb.c` | GS_USB USB-CAN 模式 |
| SSC/ | `ecatappl.c`, `ecatslv.c`, `ecatfoe.c`, `ecatcoe.c`, `coeappl.c`, `foeappl.c`, `mailbox.c`, `objdef.c`, `sdoserv.c`, `bootmode.c` | SSC 协议栈 |

**未编译：** `src/ecat_foe_support.c`（OTA flash 操作）不在 CMakeLists.txt 中。

## 状态机转换

| 转换 | 动作 |
|---|---|
| OP → 非 OP | 取消全部 4 通道 CAN TX 缓冲区、清零 TX 运行时状态 |
| 非 OP → OP | 清空 RX 队列（4 通道 head=tail）、写 `ESC_PD_WD_TIME` |
| 任意 AL 变化 | 日志输出当前状态和 RX 轮询计数 |
