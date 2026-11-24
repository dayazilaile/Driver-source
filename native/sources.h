#ifndef DRIVER_SOURCES_H
#define DRIVER_SOURCES_H

typedef struct {
    const WCHAR *name;
    const WCHAR *devices;
    const WCHAR *url;
    const WCHAR *mode;
    const char *key;
} DriverSource;

/* Public support entry points. Page reachability is checked separately from package availability. */
static const DriverSource driver_sources[] = {
    {L"联想机型驱动包目录", L"联想商用机型、部署驱动包", L"https://download.lenovo.com/cdrt/td/catalogv2.xml", L"机型目录，待自动接入", "lenovo-pack"},
    {L"戴尔机型驱动包目录", L"Dell 商用整机驱动包", L"https://downloads.dell.com/catalog/DriverPackCatalog.cab", L"机型目录，待自动接入", "dell-pack"},
    {L"惠普机型驱动包目录", L"HP 商用整机驱动包", L"https://hpia.hpcloud.hp.com/downloads/driverpackcatalog/HPClientDriverPackCatalog.cab", L"机型目录，待自动接入", "hp-pack"},
    {L"联想中国", L"整机、ThinkPad、主板", L"https://newsupport.lenovo.com.cn/", L"厂商支持页", "lenovo"},
    {L"华为中国", L"MateBook、整机", L"https://consumer.huawei.com/cn/support/driver-list/", L"厂商支持页", "huawei"},
    {L"华硕中国", L"整机、主板、显卡", L"https://www.asus.com.cn/support/download-center/", L"厂商支持页", "asus"},
    {L"戴尔中国", L"整机、服务标签查询", L"https://www.dell.com/support/home/zh-cn?app=drivers", L"厂商支持页", "dell"},
    {L"惠普中国", L"整机、打印机", L"https://support.hp.com/cn-zh/drivers", L"厂商支持页", "hp"},
    {L"宏碁中国", L"整机、显示器", L"https://www.acer.com/cn-zh/support/drivers-and-manuals", L"厂商支持页", "acer"},
    {L"英伟达中国", L"GeForce、RTX、专业显卡", L"https://www.nvidia.cn/Download/index.aspx?lang=cn", L"厂商支持页", "nvidia"},
    {L"AMD 中文站", L"Radeon、Ryzen、芯片组", L"https://www.amd.com/zh-cn/support/download/drivers.html", L"厂商支持页", "amd"},
    {L"英特尔中国", L"核显、Arc、网卡、蓝牙", L"https://www.intel.cn/content/www/cn/zh/download-center/home.html", L"厂商支持页", "intel"},
    {L"微星中国", L"主板、显卡、笔记本", L"https://www.msi.cn/support/download", L"厂商支持页", "msi"},
    {L"技嘉中国", L"主板、显卡", L"https://www.gigabyte.cn/Support", L"厂商支持页", "gigabyte"},
    {L"普联中国", L"无线、有线、USB 网卡", L"https://service.tp-link.com.cn/download", L"厂商支持页", "tplink"},
    {L"腾达中国", L"无线、有线、USB 网卡", L"https://www.tenda.com.cn/download/", L"厂商支持页", "tenda"},
    {L"瑞昱官方站", L"网卡、声卡、读卡器", L"https://www.realtek.com/Download/List", L"厂商支持页", "realtek"},
    {L"爱普生中国", L"打印机、扫描仪", L"https://www.epson.com.cn/services/download/", L"厂商支持页", "epson"},
    {L"佳能中国", L"打印机、扫描仪", L"https://www.canon.com.cn/supports/download/", L"厂商支持页", "canon"},
    {L"兄弟中国区", L"打印机、扫描仪", L"https://support.brother.com/g/b/countrytop.aspx?c=cn&lang=zh", L"厂商支持页", "brother"},
    {L"奔图中国", L"打印机、一体机", L"https://www.pantum.com.cn/service/download/", L"厂商支持页", "pantum"},
    {L"罗技中文站", L"键盘、鼠标、摄像头", L"https://support.logi.com/hc/zh-cn", L"厂商支持页", "logitech"},
    {L"荣耀中国", L"MagicBook、整机", L"https://www.honor.com/cn/support/", L"厂商支持页", "honor"},
    {L"三星中国", L"打印机、显示器、存储", L"https://www.samsung.com/cn/support/downloadcenter/", L"厂商支持页", "samsung"},
    {L"水星网络", L"无线、有线、USB 网卡", L"https://service.mercurycom.com.cn/download", L"厂商支持页", "mercury"},
    {L"迅捷网络", L"无线、有线、USB 网卡", L"https://service.fastcom.com.cn/download", L"厂商支持页", "fast"},
    {L"机械革命", L"笔记本、整机", L"https://www.mechrevo.com/service/", L"厂商支持页", "mechrevo"},
    {L"京瓷中国", L"打印机、复合机", L"https://www.kyoceradocumentsolutions.com.cn/support/mfp/download.html", L"厂商支持页", "kyocera"},
    {L"微软更新目录", L"按硬件 ID 搜索驱动", L"https://www.catalog.update.microsoft.com/", L"厂商支持页", "microsoft-catalog"},
    {L"Wacom 中国", L"数位板、手绘屏、数位笔", L"https://www.wacom.com/zh-cn/support/product-support/drivers", L"厂商支持入口", "wacom"},
    {L"绘王 Huion", L"数位板、手绘屏", L"https://www.huion.cn/download/", L"厂商支持入口", "huion"},
    {L"汉王友基 XP-Pen", L"数位板、手绘屏", L"https://www.xp-pen.com.cn/download/", L"厂商支持入口", "xppen"},
    {L"友基 UGEE", L"数位板、手绘屏", L"https://www.ugee.com.cn/download/", L"厂商支持入口", "ugee"},
    {L"高漫 GAOMON", L"数位板、手绘屏", L"https://www.gaomon.cn/download/", L"厂商支持入口", "gaomon"},
    {L"VEIKK", L"数位板、手绘屏", L"https://www.veikk.com.cn/download.html", L"厂商支持入口", "veikk"},
    {L"Xencelabs", L"数位板、手绘屏", L"https://www.xencelabs.com/cn/support/download-drivers/", L"厂商支持入口", "xencelabs"},
    {L"Parblo", L"数位板、手绘屏", L"https://www.parblo.com/pages/drivers", L"厂商支持入口", "parblo"},
    {L"Artisul", L"数位板、手绘屏", L"https://www.artisul.com/Support/download/", L"厂商支持入口", "artisul"},
    {L"BOSTO", L"数位板、手绘屏", L"https://www.bostotablet.com/download/", L"厂商支持入口", "bosto"},
    {L"汉王 Hanvon", L"手写输入、绘图外设", L"https://www.hanvon.com/", L"厂商支持入口", "hanvon"},
    {L"沁恒 WCH", L"USB 转串口、桥接芯片", L"https://www.wch.cn/downloads/category/67.html", L"厂商支持入口", "wch"},
    {L"Silicon Labs", L"CP210x USB 转串口", L"https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers", L"厂商支持入口", "silabs"},
    {L"FTDI", L"USB 转串口、桥接芯片", L"https://ftdichip.com/drivers/vcp-drivers/", L"厂商支持入口", "ftdi"},
    {L"Prolific", L"PL2303 USB 转串口", L"https://www.prolific.com.tw/US/ShowProduct.aspx?p_id=225&pcid=41", L"厂商支持入口", "prolific"},
    {L"Focusrite", L"USB 音频接口、声卡", L"https://downloads.focusrite.com/", L"厂商支持入口", "focusrite"},
    {L"Yamaha", L"USB 音频、MIDI、乐器", L"https://download.yamaha.com/", L"厂商支持入口", "yamaha"},
    {L"雷蛇 Razer", L"键盘、鼠标、耳机", L"https://mysupport.razer.com/", L"厂商支持入口", "razer"},
    {L"海盗船 Corsair", L"键鼠、耳机、控制器", L"https://www.corsair.com/cn/zh/s/downloads", L"厂商支持入口", "corsair"},
    {L"赛睿 SteelSeries", L"键鼠、耳机", L"https://cn.steelseries.com/gg", L"厂商支持入口", "steelseries"},
    {L"金士顿 Kingston", L"存储、SSD、读卡器", L"https://www.kingston.com/cn/support", L"厂商支持入口", "kingston"},
    {L"OpenTabletDriver", L"配置库支持的数位板、手绘屏", L"https://github.com/OpenTabletDriver/OpenTabletDriver/releases", L"开源驱动应用，需确认支持型号", "opentabletdriver"}
};
#endif
