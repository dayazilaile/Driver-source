# 驱动来源核查

核查日期为 2026-11-24。软件收录 29 个候选官方入口。自动匹配与安装目前使用系统更新服务和微软 Windows Update。

云环境网络白名单拦截厂商域名，已观察到的请求错误为 CONNECT 403。以下入口的实际驱动包可用性和国内下载速度尚未验证。软件提供本机连通性检查，检查结果展示 HTTP 状态或 Windows 网络错误码。

## 机型驱动包目录

以下三个目录地址在 OSDeploy/OSD 的目录获取实现中有明确引用，包含整机部署驱动包的机型索引。厂商目录当前未接入自动匹配、下载与安装。

- 联想目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogLenovo.ps1)。
- 戴尔目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogDell.ps1)。
- 惠普目录地址的引用见 [上游代码](https://github.com/OSDeploy/OSD/blob/1bf395f2b673b2874992716a776c0be4a8c98c6e/Private/core-driverpack/Get-OSDCoreDriverPackCatalogHP.ps1)。

## 候选入口清单

“入口待核验”表示尚未验证该入口或具体安装包的当前可用性。区域信息按厂商网站的中国区或中文路径标注，实际 CDN 地域由厂商决定。

| 来源 | 覆盖范围 | 接入方式 | 状态 |
| --- | --- | --- | --- |
| [联想机型驱动包目录](https://download.lenovo.com/cdrt/td/catalogv2.xml) | 联想商用机型、部署驱动包 | 机型目录，待自动接入 | 目录地址有上游引用，直连被代理拦截 |
| [戴尔机型驱动包目录](https://downloads.dell.com/catalog/DriverPackCatalog.cab) | Dell 商用整机驱动包 | 机型目录，待自动接入 | 目录地址有上游引用，直连被代理拦截 |
| [惠普机型驱动包目录](https://hpia.hpcloud.hp.com/downloads/driverpackcatalog/HPClientDriverPackCatalog.cab) | HP 商用整机驱动包 | 机型目录，待自动接入 | 目录地址有上游引用，直连被代理拦截 |
| [联想中国](https://newsupport.lenovo.com.cn/) | 整机、ThinkPad、主板 | 厂商支持页 | 入口待核验 |
| [华为中国](https://consumer.huawei.com/cn/support/driver-list/) | MateBook、整机 | 厂商支持页 | 入口待核验 |
| [华硕中国](https://www.asus.com.cn/support/download-center/) | 整机、主板、显卡 | 厂商支持页 | 入口待核验 |
| [戴尔中国](https://www.dell.com/support/home/zh-cn?app=drivers) | 整机、服务标签查询 | 厂商支持页 | 入口待核验 |
| [惠普中国](https://support.hp.com/cn-zh/drivers) | 整机、打印机 | 厂商支持页 | 入口待核验 |
| [宏碁中国](https://www.acer.com/cn-zh/support/drivers-and-manuals) | 整机、显示器 | 厂商支持页 | 入口待核验 |
| [英伟达中国](https://www.nvidia.cn/Download/index.aspx?lang=cn) | GeForce、RTX、专业显卡 | 厂商支持页 | 入口待核验 |
| [AMD 中文站](https://www.amd.com/zh-cn/support/download/drivers.html) | Radeon、Ryzen、芯片组 | 厂商支持页 | 入口待核验 |
| [英特尔中国](https://www.intel.cn/content/www/cn/zh/download-center/home.html) | 核显、Arc、网卡、蓝牙 | 厂商支持页 | 入口待核验 |
| [微星中国](https://www.msi.cn/support/download) | 主板、显卡、笔记本 | 厂商支持页 | 入口待核验 |
| [技嘉中国](https://www.gigabyte.cn/Support) | 主板、显卡 | 厂商支持页 | 入口待核验 |
| [普联中国](https://service.tp-link.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待核验 |
| [腾达中国](https://www.tenda.com.cn/download/) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待核验 |
| [瑞昱官方站](https://www.realtek.com/Download/List) | 网卡、声卡、读卡器 | 厂商支持页 | 入口待核验 |
| [爱普生中国](https://www.epson.com.cn/services/download/) | 打印机、扫描仪 | 厂商支持页 | 入口待核验 |
| [佳能中国](https://www.canon.com.cn/supports/download/) | 打印机、扫描仪 | 厂商支持页 | 入口待核验 |
| [兄弟中国区](https://support.brother.com/g/b/countrytop.aspx?c=cn&lang=zh) | 打印机、扫描仪 | 厂商支持页 | 入口待核验 |
| [奔图中国](https://www.pantum.com.cn/service/download/) | 打印机、一体机 | 厂商支持页 | 入口待核验 |
| [罗技中文站](https://support.logi.com/hc/zh-cn) | 键盘、鼠标、摄像头 | 厂商支持页 | 入口待核验 |
| [荣耀中国](https://www.honor.com/cn/support/) | MagicBook、整机 | 厂商支持页 | 入口待核验 |
| [三星中国](https://www.samsung.com/cn/support/downloadcenter/) | 打印机、显示器、存储 | 厂商支持页 | 入口待核验 |
| [水星网络](https://service.mercurycom.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待核验 |
| [迅捷网络](https://service.fastcom.com.cn/download) | 无线、有线、USB 网卡 | 厂商支持页 | 入口待核验 |
| [机械革命](https://www.mechrevo.com/service/) | 笔记本、整机 | 厂商支持页 | 入口待核验 |
| [京瓷中国](https://www.kyoceradocumentsolutions.com.cn/support/mfp/download.html) | 打印机、复合机 | 厂商支持页 | 入口待核验 |
| [微软更新目录](https://www.catalog.update.microsoft.com/) | 按硬件 ID 搜索驱动 | 厂商支持页 | 入口待核验 |

## 后续核查所需网络访问

访问上述域名及学习微软 API 文档所需的域名已保存到环境网络配置草稿。需在环境设置审阅、保存并发布后，再核验目录内容、重定向下载地址、签名和具体设备适配情况。
