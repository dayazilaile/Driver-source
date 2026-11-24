# 驱动源 · C 原生版 0.1.3

Windows 10/11 x64 驱动检测、下载与安装软件。使用 C17 和 Windows 系统 API 编写。

## 使用

下载 `releases/DriverSource-0.1.3-x64.zip`，解压并运行 `DriverSource.exe`。Windows 会请求管理员权限，用于安装驱动。压缩包包含可编译的完整项目源码。

1. 启动后自动检测本机硬件，并按当前系统版本在线查询适用驱动。
2. 在“可下载驱动”中勾选需要的项目。默认保存目录为 EXE 所在文件夹下的 `下载`，也可选择其他位置。
3. 默认勾选“下载后自动安装”和“安装前备份现有驱动”。点击“下载并安装”，先备份当前第三方驱动，随后逐项下载并安装。取消自动安装选项可以仅下载。
4. 如果驱动有尚未接受的许可协议，阅读并确认后继续。
5. 列表显示每项下载、保存、安装结果及错误码。成功安装的项目会取消勾选，当前列表再次执行时跳过这些项目。
6. Windows 要求重启时，程序提示手动重启并停止处理后续项目。保存工作并重启后，可重新扫描。
7. “取消”会请求更新服务停止任务。安装结果以更新服务返回的实际结果为准，已完成安装的项目仍会显示成功。

“本机硬件”可查看设备名称、硬件 ID、当前驱动版本及设备问题。“厂商下载来源”包含 29 个候选官方入口，可以在本机检查入口的 HTTP 连通性并打开所选官方来源。

## 下载进度和速度

下载区显示当前驱动的百分比、已下载大小、总大小以及估算速度（KiB/s 或 MiB/s）。速度使用 Windows 更新服务报告的字节数增量计算，首次采样先建立基线，约每半秒更新。缓存命中和更新服务的统计刷新节奏可能导致速度为零或间歇变化。更新服务暂未提供字节统计时会显示等待提示。

下载后的保存、校验和安装阶段有各自的进度提示。显示的速度用于估算该项更新的传输情况。

## 驱动备份与导入

默认备份位置为 EXE 所在目录下的 `驱动备份`。在“驱动备份”页可更改位置、手动备份、打开备份目录或导入已有备份。目录写入失败时，可选择具有写入权限的位置。

- 每次备份使用 Windows DISM 导出当前系统的第三方驱动包，生成独立的 `Backup-时间-GUID` 目录。
- 成功备份包含 `backup-complete.txt` 标记、驱动包及日志。失败任务保留在 `.partial` 目录内，便于查看 `output.log` 和 `dism.log`。
- 默认在一批驱动安装前备份一次。备份失败会暂停后续安装，可排查后重试，也可按需要调整“安装前备份现有驱动”选项。
- 导入时选择完整的 `Backup-时间-GUID` 文件夹。程序使用 Windows PnPUtil 导入 INF，并为兼容设备安装匹配项，处理结果保存在备份根目录的 `Restore-时间-GUID.log`。
- 备份导入遵循 Windows 的驱动排名规则。需要降级版本时，可在设备管理器中使用“回退驱动程序”或手动选择备份中的适配 INF。
- 备份内容为第三方驱动包。Windows 自带驱动由系统维护，厂商附带应用可从相应安装包重新安装。
- 导出和导入由系统工具完成。期间的取消和关闭请求会在当前系统操作结束后生效，已完成的备份会保留。

## 驱动来源

自动匹配、下载、签名与包校验、安装使用 Windows Update Agent API，支持系统配置的更新服务（包括内网 WSUS）及微软 Windows Update。查询范围为服务判定适用、尚未安装且未隐藏的驱动。

厂商来源清单包括联想、华为、华硕、戴尔、惠普、英伟达、AMD、英特尔、微星、技嘉、普联等，另收录联想 XML、戴尔 CAB、惠普 CAB 机型驱动包目录。清单及核查结果见 [SOURCES.md](SOURCES.md)。

厂商来源当前提供入口查询和连通性检查，自动匹配及安装链路使用 Windows 更新服务。机型驱动包目录仍需实现各厂商的机型、系统匹配和包解析。实际下载节点地域由来源服务决定。

当前云环境的白名单拦截厂商站点请求，候选入口的实际安装包下载尚未在线验证。HTTP 200 仅表明入口返回成功响应，具体型号、版本和驱动包还需核对。

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

## 下载、安装与取消结果

驱动先下载至 Windows 更新缓存，再导出到保存目录下独立的 `Driver-{GUID}` 子目录。下载包保存失败会单独记录，自动安装仍可使用更新缓存中的已下载包。仅下载模式下，导出失败会计为该项失败。

失败或取消的程序临时导出目录会清理，Windows 更新缓存由系统管理。安装取消由 Windows 更新服务处理，可能需要等待当前步骤完成。关闭程序时也会请求结束任务。

安装失败会显示 HRESULT 错误码。可检查 Windows Update 服务、网络以及单位更新策略后重试。未匹配到驱动表示当前服务未返回适用的新驱动，设备仍可在硬件列表查看。

在线查询所需的设备信息由 Windows 更新服务处理，遵循系统的更新服务与策略。

## 验证范围

Linux 环境已通过 MinGW-w64 警告检查、GCC 静态分析及 CMake 交叉构建，生成 Windows x64 EXE。速度计算的缓存、空闲和计数重置回归检查已通过。真实 Windows 电脑的下载速度、驱动导出、备份导入和安装结果仍需实机验证。

提供两个 Windows 检查入口，退出码 0 表示通过：

```bat
DriverSource.exe --self-test
DriverSource.exe --smoke-test
```

前者检查设备枚举和 Windows Update 安装器初始化，后者检查原生窗口、新控件、默认下载/备份路径及安装前备份默认选项。这些检查不会执行驱动安装。

GitHub Actions 分别使用 MSVC 和 MinGW 编译，并运行上述检查。版本标签的 MSVC 构建成功后发布到 Releases。

速度计算回归检查可用 C 编译器独立运行：

```sh
gcc -std=c17 -Wall -Wextra -Werror tests/test_transfer.c -o test_transfer
./test_transfer
```
