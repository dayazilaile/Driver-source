# 驱动源 · C 原生版 0.2.0

Windows 10 1809 及以上 x64 的厂商驱动检测、下载与匹配安装工具，使用 C17 和 Win32 系统 API。

本版改为独立的厂商包流程，可在 Windows Update 服务关闭时枚举硬件和匹配厂商目录。当前自动下载目录只有 **2 个乐鑫 ESP32 USB 接口驱动包**，显卡、普通网卡、声卡及数位板的厂商安装包尚未接入。现有 USB/PCI/数位板识别库和 51 个来源入口继续用于识别与查询。识别库中的设备数量不能作为可下载驱动数量。

## 使用

从 [0.2.0 发行版](https://github.com/dayazilaile/Driver-source/releases/tag/v0.2.0) 下载 `DriverSource-Windows-x64.zip`，完整解压，保留 EXE 旁的 `database` 文件夹。运行 `DriverSource.exe` 后由程序自动检测硬件、整机厂商和型号、Windows 内部版本、原生架构。

1. “可下载驱动”显示硬件 ID、架构和系统版本符合目录条件的厂商安装包。列表包含可重装包，需要结合本机当前版本选择。
2. “本机硬件”显示所有检测到的设备及其安装包收录状态。正常设备也可能无需额外驱动。没有收录的型号可查看来源建议。
3. 勾选驱动并点击“下载并安装”。默认在安装前备份当前第三方驱动，随后直接下载厂商包。取消“下载后自动安装”可以仅下载。
4. 每个下载包都核对预期大小和 SHA-256。安装时解压已校验的包，用 Windows PnPUtil 检查驱动签名并匹配安装。
5. 成功返回显示“已提交系统匹配安装”。Windows 根据驱动排名决定是否替换现有版本，具体结果保存在下载目录 `install.log`。程序不强制降级。
6. 系统要求重启时会暂停后续安装，并提示手动重启。

默认下载路径为 EXE 所在目录的 `下载`。每个包保存在独立的 `Vendor-{GUID}` 文件夹，内含 `package.zip`，自动安装时另有 `extracted`、`extract.log`、`install.log`。失败的 `.part` 会清理，已完成的包及处理日志会保留。

## 厂商安装包目录

运行时读取 `database/vendor-packages.tsv`。包记录包含标题、来源厂商、版本、完整硬件 ID、HTTPS 下载地址、SHA-256、文件大小、最低系统内部版本、架构和目标 INF。

当前支持 x64 ZIP/INF 包，使用 Windows 自带 `tar.exe` 解压和 `pnputil.exe` 匹配安装。自动列表仅按设备的硬件 ID 精确命中，不使用通用兼容 ID 或仅按厂商 VID 匹配。相同包被多个设备命中时只显示一次。ARM64 不使用 x64 包。

已核验的包来源为 [Espressif 官方发布](https://github.com/espressif/esp-win-usb-drivers/releases)。包校验、INF 原始 ID、版本和文件列表见 [来源记录](database/vendor-packages-provenance.json)。国内访问速度与具体设备安装效果需实测。

普通厂商支持网页仍在“驱动来源”中提供查询。来源入口的 HTTP 状态只能反映入口连通性。当前云环境的域名权限阻止访问 WCH、联想等站点，这些厂商的安装包尚未核验与接入。

维护者可以扩充 TSV，重启软件后生效，不需要重新编译 EXE。新增记录前必须核验真实包、x64 INF、完整硬件 ID、签名目录、版本和系统适用范围。共享 ID 的机型需要额外条件，现有格式不支持时暂不加入自动目录。例如三个 ESP 调试板共享 FTDI `VID_0403&PID_6010&MI_00`，本版未将其当作通用 FTDI 驱动自动匹配。

可选 Python 维护检查会下载目录中的原始包并核对哈希、大小、INF ID 和文件路径，不执行安装，也不修改目录：

```sh
python scripts/verify_vendor_packages.py
```

## 离线硬件识别库

本版打包 USB IDs、PCI IDs 和数位板机型资料，启动后可离线识别硬件标识并提供来源建议。

| 数据 | 数量 |
| --- | --- |
| USB 厂商 / 产品 | 3,427 / 20,537 |
| PCI 厂商 / 设备 / 子系统 | 2,526 / 21,915 / 18,163 |
| 数位板品牌分组 / 机型名称 / 标识映射 | 28 / 363 / 428 |

数位板资料包括 Wacom、Huion、XP-Pen、UGEE、GAOMON、VEIKK、Xencelabs、Parblo、Artisul、BOSTO 等。USB/PCI 资料还覆盖显卡、网卡、蓝牙、声卡、打印机、扫描仪、摄像头、键鼠、存储控制器、USB 串口等硬件的已知标识。

“本机硬件”会显示离线识别结果、设备类别、识别依据及来源建议。选择设备后点击“查看设备来源建议”，来源列表会按已知厂商或机型候选筛选。可用“显示全部来源”恢复完整列表，连通性检查针对当前列表执行。

识别会检查设备报告的多个硬件 ID 和兼容 ID，并优先保留更具体的 PCI 子系统结果。共享 VID/PID 的数位板显示候选数量和核对提示。设备标识命中用于确认候选与寻找来源，具体驱动仍需核对系统版本、架构及厂商支持条件。

USB 数据快照为 2026.06.26，PCI 数据快照为 2026.11.22。来源提交、原始配置、校验值和许可说明见 [database/README.md](database/README.md)。

构建脚本会自动将识别库复制到 EXE 输出目录。应用的编译与运行使用 C 和现有数据文件，维护者可以用可选脚本重建数位板索引。

## 进度、取消与诊断

直接按收到的下载字节统计百分比、已下载大小和估算速度。每半秒计算速度，短小文件可能在形成有效速度采样前下载完成。连接、校验、解压和安装有各自的等待提示。

取消下载会在当前网络请求返回后生效，单次请求设置了超时；取消解压可以终止当前解压进程。PnPUtil 安装和 DISM 备份会等待当前系统操作结束，以保留真实结果。任务取消后不会再开始下一项安装。

“系统诊断”展示整机型号、提权状态、系统版本、架构和厂商目录加载结果。日志保存在软件目录 `logs/DriverSource.log`，包含本机设备名称、硬件 ID、驱动版本、匹配状态及处理结果。硬件信息保存在本机，不上传硬件报告；下载请求只访问所选包的发布地址。

## 驱动备份与导入

默认备份位置为 EXE 所在目录下的 `驱动备份`。在“驱动备份”页可更改位置、手动备份、打开备份目录或导入已有备份。目录写入失败时，可选择具有写入权限的位置。

- 每次备份使用 Windows DISM 导出当前系统的第三方驱动包，生成独立的 `Backup-时间-GUID` 目录。
- 成功备份包含 `backup-complete.txt` 标记、驱动包及日志。失败任务保留在 `.partial` 目录内，便于查看 `output.log` 和 `dism.log`。
- 默认在一批驱动安装前备份一次。备份失败会暂停后续安装，可排查后重试，也可按需要调整“安装前备份现有驱动”选项。
- 导入时选择完整的 `Backup-时间-GUID` 文件夹。程序使用 Windows PnPUtil 导入 INF，并为兼容设备安装匹配项，处理结果保存在备份根目录的 `Restore-时间-GUID.log`。
- 备份导入遵循 Windows 的驱动排名规则。需要降级版本时，可在设备管理器中使用“回退驱动程序”或手动选择备份中的适配 INF。
- 备份内容为第三方驱动包。Windows 自带驱动由系统维护，厂商附带应用可从相应安装包重新安装。
- 导出和导入由系统工具完成。期间的取消和关闭请求会在当前系统操作结束后生效，已完成的备份会保留。

## 自行编译

程序链接 Windows 系统库。编译器可以沿用现有工具链，无需安装额外的运行库或包管理器。

### 一键编译

在 Windows 运行：

```bat
build-windows.bat
```

脚本优先识别 MinGW-w64 GCC，随后尝试 MSVC。输出为 `dist\DriverSource.exe`。

- **MinGW-w64** — 使用支持 C17 的 64 位 GCC，将其 `bin` 加入 PATH。也可将工具链放入项目内 `tools\mingw64` 或 `mingw64` 目录。需包含 GCC 和 windres。
- **MSVC** — 安装 Visual Studio 2022 或 Build Tools 的 MSVC 与 Windows SDK 组件。脚本通过 vswhere 自动初始化编译环境，也可从 x64 Native Tools 命令提示符运行。

可以明确指定编译器：

```bat
build-windows.bat mingw
build-windows.bat msvc
```

### IDE 与 CMake

支持 CMake 3.20+。可用支持 CMake 的 IDE 打开项目。Visual Studio 2022 的命令行构建方式为：

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

输出为 `build-vs\Release\DriverSource.exe`。

在已配置 MinGW-w64 和 Ninja 的命令行中，也可使用：

```bat
cmake -S . -B build-gcc -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_RC_COMPILER=windres
cmake --build build-gcc
```

输出为 `build-gcc\DriverSource.exe`。

## 验证范围

GitHub Actions 使用 MinGW-w64 和 MSVC 编译，并验证硬件识别、速度统计、厂商包精确匹配、系统架构和版本过滤、SHA-256、取消和篡改拒绝。Windows 下载测试会从厂商官方 GitHub 发布中下载两个实际包并解压，不执行驱动安装。

原生窗口和设备枚举检查：

```bat
DriverSource.exe --self-test
DriverSource.exe --smoke-test
```

厂商目录匹配可在普通 C 环境独立验证：

```sh
gcc -std=c17 -Wall -Wextra -Werror tests/test_vendor_catalog.c native/vendor_catalog.c -o test_vendor_catalog
./test_vendor_catalog
```

实际硬件安装、版本替换、国内网络速度、DISM 备份及恢复需 Windows 实机验证。当前目录覆盖范围仍很小，完整常用驱动库需要继续接入经核验的厂商安装包。
