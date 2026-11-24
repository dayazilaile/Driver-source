# 驱动来源核查

核查日期为 2026-11-24。软件收录 51 个候选来源入口。0.3.0 的自动匹配与安装使用本地厂商包目录及独立 HTTPS 下载。

云环境网络白名单拦截厂商域名，已观察到的请求错误为 CONNECT 403。以下入口的实际驱动包可用性和国内下载速度尚未验证。软件提供本机连通性检查，检查结果展示 HTTP 状态或 Windows 网络错误码。

## 已接入的整机目录

新增 `database/oem-packages.tsv` 包含 3,777 条机型与系统记录，来自 OSDeploy/OSD 保存的联想及戴尔官方目录快照。约覆盖 500 个联想型号名称、428 个戴尔型号名称；同型号不同系统和标识别名会产生多条记录。

原始 XML、GPL-3.0 许可、上游提交和文件 SHA-256 见 [来源记录](database/oem-snapshots/README.md)。新增包的实际可下载性、厂商节点地域和实机安装仍需验证。运行时对所有包执行 SHA-256 校验，下载 EXE 不会自动执行。

## 已在线核验的单设备安装包

| 包 | 精确硬件 ID | 版本 | 原始发布 |
| --- | --- | --- | --- |
| ESP32-S2 USB | `USB\VID_303A&PID_0002&MI_02` | 1.0.0.0 | [Espressif](https://github.com/espressif/esp-win-usb-drivers/releases/tag/ESP32-S2_v1.0.0.0) |
| ESP32 USB JTAG | `USB\VID_303A&PID_1001&MI_02` | 6.1.7600.16386 | [Espressif](https://github.com/espressif/esp-win-usb-drivers/releases/tag/ESP32-USB-JTAG_v6.1.7600.16386) |

已下载核对大小、SHA-256、x64 INF、完整硬件 ID、CAT 文件和归档成员路径。CAT 中的签名证书主体为 Espressif Systems (Shanghai) Co., Ltd.；系统实际信任和安装结果由 Windows 检查。该来源的国内下载速度尚未验证。程序不内置这些二进制驱动包，只保存匹配目录，运行时从原始发布下载。

其他支持入口的机型、系统和安装包仍需逐项接入。共有三个开发板包使用 FTDI 的共享接口 ID，本版未加入自动下载目录，以免匹配到其他 FTDI 设备。

## 机型驱动包目录

以下三个目录地址在 OSDeploy/OSD 的目录获取实现中有明确引用，包含整机部署驱动包的机型索引。联想、戴尔目录快照已接入自动整机匹配。下载仍使用源厂商地址，EXE 包仅下载，戴尔 CAB 可提交 INF 匹配安装。惠普目录尚未接入。

- 联想目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogLenovo.ps1)。
- 戴尔目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogDell.ps1)。
- 惠普目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogHP.ps1)。

## 候选入口清单

“入口待核验”表示尚未验证该入口或具体安装包的当前可用性。区域信息按厂商网站的中国区或中文路径标注，实际 CDN 地域由厂商决定。

| 来源 | 覆盖范围 | 接入方式 | 状态 |
| --- | --- | --- | --- |
| [联想机型驱动包目录](https://download.lenovo.com/cdrt/td/catalogv2.xml) | 联想商用机型、部署驱动包 | 整机目录快照匹配，EXE 仅下载 | 原始目录快照已核验，实际包直连被代理拦截 |
| [戴尔机型驱动包目录](https://downloads.dell.com/catalog/DriverPackCatalog.cab) | Dell 商用整机驱动包 | 整机目录快照匹配，CAB 支持 INF 安装 | 原始目录快照已核验，实际包直连被代理拦截 |
| [惠普机型驱动包目录](https://hpia.hpcloud.hp.com/downloads/driverpackcatalog/HPClientDriverPackCatalog.cab) | HP 商用整机驱动包 | 机型目录，待自动接入 | 目录地址有上游引用，直连被代理拦截 |
| [联想中国](https://newsupport.lenovo.com.cn/) | 整机、ThinkPad、主板 | 厂商支持页 | 入口待设备与系统适配核验 |
| [华为中国](https://consumer.huawei.com/cn/support/driver-list/) | MateBook、整机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [华硕中国](https://www.asus.com.cn/support/download-center/) | 整机、主板、显卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [戴尔中国](https://www.dell.com/support/home/zh-cn?app=drivers) | 整机、服务标签查询 | 厂商支持页 | 入口待设备与系统适配核验 |
| [惠普中国](https://support.hp.com/cn-zh/drivers) | 整机、打印机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [宏碁中国](https://www.acer.com/cn-zh/support/drivers-and-manuals) | 整机、显示器 | 厂商支持页 | 入口待设备与系统适配核验 |
| [英伟达中国](https://www.nvidia.cn/Download/index.aspx?lang=cn) | GeForce、RTX、专业显卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [AMD 中文站](https://www.amd.com/zh-cn/support/download/drivers.html) | Radeon、Ryzen、芯片组 | 厂商支持页 | 入口待设备与系统适配核验 |
| [英特尔中国](https://www.intel.cn/content/www/cn/zh/download-center/home.html) | 核显、Arc、网卡、蓝牙 | 厂商支持页 | 入口待设备与系统适配核验 |
| [微星中国](https://www.msi.cn/support/download) | 主板、显卡、笔记本 | 厂商支持页 | 入口待设备与系统适配核验 |
| [技嘉中国](https://www.gigabyte.cn/Support) | 主板、显卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [普联中国](https://service.tp-link.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [腾达中国](https://www.tenda.com.cn/download/) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [瑞昱官方站](https://www.realtek.com/Download/List) | 网卡、声卡、读卡器 | 厂商支持页 | 入口待设备与系统适配核验 |
| [爱普生中国](https://www.epson.com.cn/services/download/) | 打印机、扫描仪 | 厂商支持页 | 入口待设备与系统适配核验 |
| [佳能中国](https://www.canon.com.cn/supports/download/) | 打印机、扫描仪 | 厂商支持页 | 入口待设备与系统适配核验 |
| [兄弟中国区](https://support.brother.com/g/b/countrytop.aspx?c=cn&lang=zh) | 打印机、扫描仪 | 厂商支持页 | 入口待设备与系统适配核验 |
| [奔图中国](https://www.pantum.com.cn/service/download/) | 打印机、一体机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [罗技中文站](https://support.logi.com/hc/zh-cn) | 键盘、鼠标、摄像头 | 厂商支持页 | 入口待设备与系统适配核验 |
| [荣耀中国](https://www.honor.com/cn/support/) | MagicBook、整机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [三星中国](https://www.samsung.com/cn/support/downloadcenter/) | 打印机、显示器、存储 | 厂商支持页 | 入口待设备与系统适配核验 |
| [水星网络](https://service.mercurycom.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [迅捷网络](https://service.fastcom.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待设备与系统适配核验 |
| [机械革命](https://www.mechrevo.com/service/) | 笔记本、整机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [京瓷中国](https://www.kyoceradocumentsolutions.com.cn/support/mfp/download.html) | 打印机、复合机 | 厂商支持页 | 入口待设备与系统适配核验 |
| [乐鑫 Espressif](https://github.com/espressif/esp-win-usb-drivers/releases) | ESP32 USB、JTAG 接口 | 2 个精确 ID 安装包 | 原始包和 INF 已核验 |
| [Wacom 中国](https://www.wacom.com/zh-cn/support/product-support/drivers) | 数位板、手绘屏、数位笔 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [绘王 Huion](https://www.huion.cn/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [汉王友基 XP-Pen](https://www.xp-pen.com.cn/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [友基 UGEE](https://www.ugee.com.cn/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [高漫 GAOMON](https://www.gaomon.cn/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [VEIKK](https://www.veikk.com.cn/download.html) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Xencelabs](https://www.xencelabs.com/cn/support/download-drivers/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Parblo](https://www.parblo.com/pages/drivers) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Artisul](https://www.artisul.com/Support/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [BOSTO](https://www.bostotablet.com/download/) | 数位板、手绘屏 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [汉王 Hanvon](https://www.hanvon.com/) | 手写输入、绘图外设 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [沁恒 WCH](https://www.wch.cn/downloads/category/67.html) | USB 转串口、桥接芯片 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Silicon Labs](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers) | CP210x USB 转串口 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [FTDI](https://ftdichip.com/drivers/vcp-drivers/) | USB 转串口、桥接芯片 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Prolific](https://www.prolific.com.tw/US/ShowProduct.aspx?p_id=225&pcid=41) | PL2303 USB 转串口 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Focusrite](https://downloads.focusrite.com/) | USB 音频接口、声卡 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [Yamaha](https://download.yamaha.com/) | USB 音频、MIDI、乐器 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [雷蛇 Razer](https://mysupport.razer.com/) | 键盘、鼠标、耳机 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [海盗船 Corsair](https://www.corsair.com/cn/zh/s/downloads) | 键鼠、耳机、控制器 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [赛睿 SteelSeries](https://cn.steelseries.com/gg) | 键鼠、耳机 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [金士顿 Kingston](https://www.kingston.com/cn/support) | 存储、SSD、读卡器 | 厂商支持入口 | 云环境代理拦截，入口待核验 |
| [OpenTabletDriver](https://github.com/OpenTabletDriver/OpenTabletDriver/releases) | 配置库支持的数位板、手绘屏 | 开源驱动应用，需确认支持型号 | 公开 Releases 页面 HTTP 200，具体包待核验 |

## 硬件与来源绑定

来源建议使用 USB/PCI 独立命名空间、PCI 子系统厂商、数位板 VID/PID 候选资料和明确的厂商名称提示。共享数位板标识会保留多个来源候选。自动驱动匹配及安装当前使用 Windows 更新服务，厂商来源需要按型号与系统版本进一步接入。

离线数据、上游快照、机型分组和许可见 [数据库说明](database/README.md)。

## 网络核查

当前云环境的白名单拦截厂商站点，新增来源的请求证据保存在 database/source-checks.json。OpenTabletDriver 的公开 Releases 页面可访问，具体安装包、版本及设备适配还需逐项验证。
