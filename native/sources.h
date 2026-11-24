#ifndef DRIVER_SOURCES_H
#define DRIVER_SOURCES_H

typedef struct {
    const WCHAR *name;
    const WCHAR *devices;
    const WCHAR *url;
    const WCHAR *mode;
} DriverSource;

/* Public support entry points. Page reachability is checked separately from package availability. */
static const DriverSource driver_sources[] = {
    {L"联想机型驱动包目录", L"联想商用机型、部署驱动包", L"https://download.lenovo.com/cdrt/td/catalogv2.xml", L"机型目录，待自动接入"},
    {L"戴尔机型驱动包目录", L"Dell 商用整机驱动包", L"https://downloads.dell.com/catalog/DriverPackCatalog.cab", L"机型目录，待自动接入"},
    {L"惠普机型驱动包目录", L"HP 商用整机驱动包", L"https://hpia.hpcloud.hp.com/downloads/driverpackcatalog/HPClientDriverPackCatalog.cab", L"机型目录，待自动接入"},
    {L"联想中国", L"整机、ThinkPad、主板", L"https://newsupport.lenovo.com.cn/", L"厂商支持页"},
    {L"华为中国", L"MateBook、整机", L"https://consumer.huawei.com/cn/support/driver-list/", L"厂商支持页"},
    {L"华硕中国", L"整机、主板、显卡", L"https://www.asus.com.cn/support/download-center/", L"厂商支持页"},
    {L"戴尔中国", L"整机、服务标签查询", L"https://www.dell.com/support/home/zh-cn?app=drivers", L"厂商支持页"},
    {L"惠普中国", L"整机、打印机", L"https://support.hp.com/cn-zh/drivers", L"厂商支持页"},
    {L"宏碁中国", L"整机、显示器", L"https://www.acer.com/cn-zh/support/drivers-and-manuals", L"厂商支持页"},
    {L"英伟达中国", L"GeForce、RTX、专业显卡", L"https://www.nvidia.cn/Download/index.aspx?lang=cn", L"厂商支持页"},
    {L"AMD 中文站", L"Radeon、Ryzen、芯片组", L"https://www.amd.com/zh-cn/support/download/drivers.html", L"厂商支持页"},
    {L"英特尔中国", L"核显、Arc、网卡、蓝牙", L"https://www.intel.cn/content/www/cn/zh/download-center/home.html", L"厂商支持页"},
    {L"微星中国", L"主板、显卡、笔记本", L"https://www.msi.cn/support/download", L"厂商支持页"},
    {L"技嘉中国", L"主板、显卡", L"https://www.gigabyte.cn/Support", L"厂商支持页"},
    {L"普联中国", L"无线、有线、USB 网卡", L"https://service.tp-link.com.cn/download", L"厂商支持页"},
    {L"腾达中国", L"无线、有线、USB 网卡", L"https://www.tenda.com.cn/download/", L"厂商支持页"},
    {L"瑞昱官方站", L"网卡、声卡、读卡器", L"https://www.realtek.com/Download/List", L"厂商支持页"},
    {L"爱普生中国", L"打印机、扫描仪", L"https://www.epson.com.cn/services/download/", L"厂商支持页"},
    {L"佳能中国", L"打印机、扫描仪", L"https://www.canon.com.cn/supports/download/", L"厂商支持页"},
    {L"兄弟中国区", L"打印机、扫描仪", L"https://support.brother.com/g/b/countrytop.aspx?c=cn&lang=zh", L"厂商支持页"},
    {L"奔图中国", L"打印机、一体机", L"https://www.pantum.com.cn/service/download/", L"厂商支持页"},
    {L"罗技中文站", L"键盘、鼠标、摄像头", L"https://support.logi.com/hc/zh-cn", L"厂商支持页"},
    {L"荣耀中国", L"MagicBook、整机", L"https://www.honor.com/cn/support/", L"厂商支持页"},
    {L"三星中国", L"打印机、显示器、存储", L"https://www.samsung.com/cn/support/downloadcenter/", L"厂商支持页"},
    {L"水星网络", L"无线、有线、USB 网卡", L"https://service.mercurycom.com.cn/download", L"厂商支持页"},
    {L"迅捷网络", L"无线、有线、USB 网卡", L"https://service.fastcom.com.cn/download", L"厂商支持页"},
    {L"机械革命", L"笔记本、整机", L"https://www.mechrevo.com/service/", L"厂商支持页"},
    {L"京瓷中国", L"打印机、复合机", L"https://www.kyoceradocumentsolutions.com.cn/support/mfp/download.html", L"厂商支持页"},
    {L"微软更新目录", L"按硬件 ID 搜索驱动", L"https://www.catalog.update.microsoft.com/", L"厂商支持页"}
};
#endif
