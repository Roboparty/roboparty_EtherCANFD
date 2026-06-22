# SSC 命令行工具

SSC (Slave Stack Code) 工具安装于:
```
C:\Program Files (x86)\Beckhoff\EtherCAT Slave Stack Code Tool\
```

## SSC Tool Cmd.exe

从 `.esp` 项目文件生成 C 源码和 ESI 文件。

```
SSC Tool Cmd.exe -o <输出目录> [-p <.esp>] [-c <.xml> [name]] [-d 宏 值] [-l]
```

| 参数 | 说明 |
|---|---|
| `-o folder` | **必选**。输出根目录。工具会在其下创建 `src/` 子目录存放 C/H 文件，并在根目录生成 `EtherCANFD.xml` (ESI) |
| `-p file` | 项目文件 (`.esp`)。不指定则使用当前已安装的工程 |
| `-c file [name]` | SSC 配置文件 (`.xml` / `.h`)。可指定配置名，默认使用文件中第一个配置 |
| `-co [allowfileimport]` | 配置选项。`allowfileimport` 允许导入配置中引用的文件 |
| `-d name value` | 覆盖宏定义 (可多次使用)，如 `-d VENDOR_ID 0x000011BC` |
| `-l` | 列出所有配置项到控制台 (不生成文件) |

**示例:**
```powershell
SSC Tool Cmd.exe -o C:\tmp\out -p .\EtherCANFD.esp
SSC Tool Cmd.exe -o .\Src -p .\EtherCANFD.esp -d FOE_SUPPORTED 0
```

## SSC OD Tool.exe

从对象字典 Excel 导出 ESI XML 或 C 源码片段。

```
SSC OD Tool.exe "ObjectDictionary File" [/esi "Esi File"] [/src "Source file folder"]
```

| 参数 | 说明 |
|---|---|
| `ObjectDictionary File` | **必选**。对象字典 Excel 文件 (`.xlsx`) |
| `/esi "Esi File"` | 导出 ESI XML 到指定路径 |
| `/src "Source file folder"` | 导出 C 源码 (`EtherCANFD.c/h/Objects.h`) 到指定目录 |

**示例:**
```powershell
SSC OD Tool.exe .\EtherCANFD.xlsx /esi .\EtherCANFD.xml /src .\Src
```

## 自动生成脚本 `generate_ssc.ps1`

```powershell
.\generate_ssc.ps1             # 全流程: xlsx → OD Tool → Tool Cmd
.\generate_ssc.ps1 -SkipOdt    # 跳过 OD Tool, 仅 Tool Cmd
.\generate_ssc.ps1 -SkipXlsx   # 跳过 generate_xlsx.py
.\generate_ssc.ps1 -SkipAll    # 仅检查工具路径
```

CMake 构建时会自动检测 `.esp` / `.xlsx` 变化并按需调用 `generate_ssc.ps1 -SkipOdt`。
