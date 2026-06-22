# HPM Manufacturing Tool CLI 参考

## 工具位置

```
D:\env\HPMicro_Manufacturing_Tool_v0.6.0\hpm_manufacturing_cmd.exe
```

## 命令行参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `-p` | 串口连接，后跟串口名；仅单设备可省略参数 | `-p COM11` 或 `-p` |
| `-u` | USB-HID 连接，后跟 `vid,pid`；仅单设备可省略参数 | `-u "0x34b7,0x0001"` 或 `-u` |
| `-r` | 运行一条烧写命令，可多次指定实现连续执行 | `-r "write-memory ..."` |
| `-t` | 命令超时（毫秒），默认 10000 | `-t 30000` |
| `-f` | 烧写前加载 bootloader 固件，参数 `<SoC名>,<0=非签名/1=签名>` 或固件路径 | `-f "HPM6200,0"` |
| `-i` | 镜像编辑模式，仅 `-r` 参数生效，其余参数无效 | `-i -r "create-image imageUtil.json"` |

## 全部烧写命令

### query-rte — 查询 ROM 特性
```
query-rte <id> [<param>]
```
- `id=0`: ROM features (不需要 param)
- `id=4`: Memory properties, param: `0`=ILM, `1`=DLM, `0x10000`=XPI0, `0x10001`=XPI1, `0x20000`=OTP, `0x30000`=SDXC0, `0x30001`=SDXC1
- `id=5`: Supported command list

### config-memory — 配置 FLASH/eMMC
```
config-memory <memory_id> <config_address>
```
- memory_id: `0x10000`=XPI0 NOR, `0x10001`=XPI1 NOR, `0x30000`=SDXC0, `0x30001`=SDXC1

### write-memory — 写内存
```
write-memory <memory_id> <address/sector_index> [[hex_array]]/<file_path>
```
- memory_id: `0`=ILM, `1`=DLM, `0x10000`=XPI0, `0x10001`=XPI1, `0x20000`=OTP, `0x30000`=SDXC0, `0x30001`=SDXC1
- 数据源两种格式: `[[0xfcf90001,0x05,0x0,0x0]]` 或 `flash_xip.bin`

### read-memory — 读内存
```
read-memory <memory_id> <address/sector_index> <byte_length> [file_path]
```
- memory_id 同上
- 不指定 file_path 则输出到标准输出

### verify-checksum — 校验内存校验和 (v0.6 新增)
```
verify-checksum <memory_id> <address/sector_index> [[hex_array]]/<file_path>
```
- memory_id: `0`=ILM, `1`=DLM, `0x10000`=XPI0, `0x10001`=XPI1, `0x30000`=SDXC0, `0x30001`=SDXC1

### erase-region — 区域擦除
```
erase-region <memory_id> <address/sector_index> <byte_length>
```
- memory_id: `0x10000`=XPI0, `0x10001`=XPI1

### erase-chip — 整片擦除
```
erase-chip <memory_id>
```
- memory_id: `0x10000`=XPI0, `0x10001`=XPI1

### load-image — 加载镜像到 RAM 并执行
```
load-image <image_path>
```

### load-blfw — 加载 bootloader 固件
```
load-blfw <soc_type> <isSigned>
```
- soc_type: `HPM6700/HPM6400`, `HPM6300`, `HPM6200`, `HPM5300`, `HPM6800`, `HPM6E00`, `HPM5E00`, `HPM6P00`
- isSigned: `0`=未签名, `1`=签名
- 注意: 固件加载成功后设备会自动复位

### reset — 复位设备
```
reset [<reset_type>]
```
- reset_type: `0`=默认, `1`=热复位, `2`=冷复位; 默认 0

### create-image — 镜像编辑 (配合 -i 使用)
```
create-image <json_config_path>
```

## 典型烧写序列

### 单步 FLASH 烧写 (3条独立命令)
```powershell
.\hpm_manufacturing_cmd.exe -u "0x34b7,0x0001" -r "write-memory 0x0 0x200 [[0xfcf90001,0x05,0x0,0x0]]"
.\hpm_manufacturing_cmd.exe -u "0x34b7,0x0001" -r "config-memory 0x10000 0x200"
.\hpm_manufacturing_cmd.exe -u "0x34b7,0x0001" -r "write-memory 0x10000 0x80000400 flash_xip.bin"
```

### 连续 FLASH 烧写 (单次连接)
```powershell
.\hpm_manufacturing_cmd.exe -u "0x34b7,0x0001" -r "write-memory 0x0 0x200 [[0xfcf90001,0x05,0x0,0x0]]" -r "config-memory 0x10000 0x200" -r "write-memory 0x10000 0x80000400 flash_xip.bin"
```

### 烧写 + 校验
```powershell
.\hpm_manufacturing_cmd.exe -u "0x34b7,0x0001" -r "write-memory 0x0 0x200 [[0xfcf90001,0x05,0x0,0x0]]" -r "config-memory 0x10000 0x200" -r "write-memory 0x10000 0x80000400 flash_xip.bin" -r "verify-checksum 0x10000 0x80000400 flash_xip.bin"
```

### 串口连接 (单设备自动检测)
```powershell
.\hpm_manufacturing_cmd.exe -p -r "write-memory 0x10000 0x80000400 flash_xip.bin"
```

### 加载 bootloader 固件后查询
```powershell
.\hpm_manufacturing_cmd.exe -p -r "load-blfw HPM6200 0"
.\hpm_manufacturing_cmd.exe -p -f "HPM6200,0" -r "query-rte 0"
```

### 镜像编辑 (离线)
```powershell
.\hpm_manufacturing_cmd.exe -i -r "create-image imageUtil.json"
```

### 单设备自动检测 (USB)
```powershell
.\hpm_manufacturing_cmd.exe -u -r "query-rte 0"
```

## 注意事项

1. `-r` 可多次指定，命令按顺序连续执行，共用一次连接
2. `write-memory` 写 OTP 时 memory_id 为 `0x20000`
3. USB 设备路径在 PowerShell 中需双引号，CMD 中不需要
4. 加载固件 (`-f` / `load-blfw`) 后设备会自动复位重连
5. `-i` 镜像编辑模式是离线操作，不需要连接硬件

## 支持的 SoC 型号

`HPM6700/HPM6400`, `HPM6300`, `HPM6200`, `HPM5300`, `HPM6800`, `HPM6E00`, `HPM5E00`, `HPM6P00`

---

## ethercanfd 固件烧写 (HPM5E00, USB-HID)

### 设备信息

| 项目 | 值 |
|------|-----|
| SoC | HPM5E00 |
| USB VID | 0x34b7 |
| USB PID | 0x0008 |
| 连接方式 | USB-HID |
| Flash Option Words | `{0xfcf90002, 0x05, 0x1000, 0x0}` |
| Flash 基址 | 0x80000000 (XPI0 NOR) |
| 固件烧写地址 | 0x80000400 |
| 固件路径 | `D:\env\hpm\hpm_sdk\samples\ethercat\ethercanfd_firmware\build\output\ethercanfd.bin` |

### 查询设备

```powershell
& "D:\env\HPMicro_Manufacturing_Tool_v0.6.0\hpm_manufacturing_cmd.exe" -u "0x34b7,0x0008" -r "query-rte 0"
```

### 单次连接烧写 (推荐)

```powershell
& "D:\env\HPMicro_Manufacturing_Tool_v0.6.0\hpm_manufacturing_cmd.exe" -u "0x34b7,0x0008" -r "write-memory 0x0 0x200 [[0xfcf90002,0x05,0x1000,0x0]]" -r "config-memory 0x10000 0x200" -r "write-memory 0x10000 0x80000400 D:\env\hpm\hpm_sdk\samples\ethercat\ethercanfd_firmware\build\output\ethercanfd.bin"
```
