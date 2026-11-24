# 离线硬件识别数据

本目录随程序分发。将整个 database 文件夹保留在 EXE 同目录，程序启动时读取 USB、PCI 和数位板索引。构建脚本会自动复制此目录到输出位置。

## 数据范围

| 数据 | 快照 | 条目 |
| --- | --- | --- |
| USB IDs | 2026.06.26 | 3,427 个厂商、20,537 个产品 |
| PCI IDs | 2026.11.22 | 2,526 个厂商、21,915 个设备、18,163 个子系统 |
| 数位板资料 | 2026-11-24 提取 | 28 个品牌分组、363 个机型名称、428 条 VID/PID 映射 |

硬件标识用于识别和提供来源建议。驱动安装包还需核对型号、系统版本、架构及厂商支持条件。多个品牌或机型共用 VID/PID 时，程序显示型号候选数量和核对提示。数位板资料有些机型还依赖设备字符串或报告特征，当前资料采用标识匹配。

## 数位板分组

| 分组 | 机型名称数量 |
| --- | --- |
| Acepen | 2 |
| Adesso | 1 |
| Artisul | 8 |
| Bosto | 1 |
| Fansjoy | 1 |
| FlooGoo | 1 |
| Gaomon | 28 |
| Genius | 4 |
| Huion | 81 |
| KENTING | 1 |
| LetSketch | 2 |
| Lifetec | 1 |
| Monoprice | 3 |
| Parblo | 16 |
| RobotPen | 1 |
| Silvercrest | 1 |
| Trust | 1 |
| Turcom | 1 |
| UC-Logic | 3 |
| UGEE | 10 |
| VEIKK | 18 |
| ViewSonic | 2 |
| Wacom | 95 |
| Waltop | 1 |
| XENX | 3 |
| XOPPOX | 1 |
| XP-Pen | 74 |
| XenceLabs | 2 |

## 来源与许可

USB 数据由 hwdata 快照提供，保留 Stephen J. Gowdy 和贡献者的原始文件头，按该仓库提供的 GPL-2.0-or-later 许可分发。许可声明及全文位于 licenses/hwdata-LICENSE.txt 与 licenses/GPL-2.0.txt。 [上游数据](https://github.com/vcrhonek/hwdata/blob/1cf7032160ad57e2eaad36c4f23eede1edd8203b/usb.ids)

PCI 数据由 PCI ID Project 提供，原文件保留 Martin Mares、Albert Pool 等维护者的说明，按上游提供的 GPL-2.0-or-later 许可分发。 [上游数据](https://github.com/pciutils/pciids/blob/7b231cfd76ccfeb2d465575bb95993bbfbc291a7/pci.ids)

数位板索引由 OpenTabletDriver 贡献者的配置资料提取为 VID/PID、来源键与机型名称。原始 JSON 位于 tablet-configs，派生索引为 tablets.tsv，提取脚本位于 scripts/build_tablet_database.py。保留 LGPL-3.0 与其引用的 GPL-3.0 全文。 [上游配置](https://github.com/OpenTabletDriver/OpenTabletDriver/tree/3a9bedb786d6d5b5c8728479f7fc57f1e4174a8e/OpenTabletDriver.Configurations/Configurations)

provenance.json 记录固定提交、统计和 SHA-256。source-checks.json 记录本次新增来源入口的网络核查结果。各入口的 HTTP 连通性与具体安装包可用性需要分别核验。

## 维护

可维护原始机型 JSON，再使用可选的维护脚本 python scripts/build_tablet_database.py 重建 tablets.tsv。脚本使用 Python 标准库，应用的 C 编译和运行直接使用现有数据文件。替换数据库文件后重新启动程序。修改上游资料时需保留相应来源与许可声明。

## 0.2.0 厂商安装包目录

`vendor-packages.tsv` 是独立下载引擎读取的 UTF-8 TSV。每行 11 列，依次为唯一 key、标题、厂商、版本、以分号分隔的完整硬件 ID、HTTPS 下载地址、SHA-256、文件字节数、最低 Windows 内部版本、架构（当前仅 x64）、归档内根目录 INF 文件名。`#` 开头的行作为注释。

本版支持平面目标 INF 的 ZIP 包，全部归档内容解压到独立目录，供 INF 引用配套文件。扩充目录前必须核验所有成员路径、INF、CAT 和系统条件。当前包含 2 个乐鑫官方发布包，原始核查记录见 `vendor-packages-provenance.json`；其中 JTAG 的 `%DeviceID%` 替换为 INF Strings 节的 `VID_303A&PID_1001&MI_02` 后匹配。

维护检查 `python scripts/verify_vendor_packages.py` 不修改目录、不安装驱动。USB/PCI/数位板资料仍用于设备识别，其记录数不代表已接入的安装包数量。

## 0.3.0 整机型号目录

`oem-packages.tsv` 每行 11 列，依次为唯一 key、厂商（lenovo/dell）、型号名称、以分号分隔的机型代码/System ID、系统家族（win10/win11）、源目录系统版本或 `*`、发布日期、厂商 HTTPS 地址、SHA-256、大小（0 表示未知）、格式（cab/exe-download）。`exe-download` 不自动执行。

可选维护命令 `python scripts/build_oem_catalog.py` 根据 `oem-snapshots` 中的固定 XML 重建目录，完整来源及 GPL-3.0 数据许可见该文件夹说明。
