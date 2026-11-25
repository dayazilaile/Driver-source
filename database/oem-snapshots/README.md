# 整机目录来源和验证边界

此目录保存 OSDeploy/OSD 仓库 `core/driverpacks/lenovo.xml`、`dell.xml` 和 `hp.xml` 的原始字节快照，用于复核和重建整机型号索引。

- 上游 https://github.com/OSDeploy/OSD
- 固定提交 `1bf395f2b673b2874992716a776c0be4a8c98c6e`
- 厂商原始目录为 https://download.lenovo.com/cdrt/td/catalogv2.xml 和 https://downloads.dell.com/catalog/DriverPackCatalog.cab 和 https://ftp.hp.com/pub/caps-softpaq/cmit/HPClientDriverPackCatalog.cab
- 上游采用 GPL-3.0，本目录原始数据和派生索引遵循该许可，完整许可见 `LICENSE-OSD-GPL-3.0.txt`。原始数据与重建脚本一起提供。
- 来源文件哈希、数量和验证状态见 `provenance.json`。`.gitattributes` 固定 XML 原始字节，避免 Windows 检出换行改变哈希。

生成 `database/oem-packages.tsv` 的命令为 `python scripts/build_oem_catalog.py`，只读取本地数据。生成程序从零编写，不执行上游 PowerShell。筛选规则排除 BIOS、固件、旧版 Windows 和 ARM64 记录，仅保留具有 SHA-256 的 Windows 10/11 x64 整机驱动包记录。相同型号的不同系统版本和别名会保留，运行时只选取条件符合的一条较新记录。

目录提供的 URL 和 SHA-256 可用于在用户电脑核对厂商原包。开发云环境仍拦截厂商域名，未能在线下载这些新增的整机原包。此处的目录收录不代表下载与安装已经验证。EXE 包只下载，未经核验的自动解包不会运行。戴尔 CAB 由系统 expand.exe 解压，随后由 PnPUtil 检查 INF 签名与本机匹配，真实整机包还需实机验证。

当前主要覆盖厂商商用机型，消费机、组装机和其他品牌可能没有记录。没有记录不会被解释为缺失驱动。

0.3.1 增加惠普 SoftPaq 的 SHA-256、大小、发布日期与 ProductOSDriverPack 关联。只保留 `Manageability - Driver Pack` 类别以及明确标注 Windows 10/11 64-bit 的适用关系，排除 IoT/LTSC 专用记录及早于 1809 的 Windows 10 记录。惠普 SystemId 对应主板 Product ID；商品 System SKU 不用于代替主板编号。

本版目录共 7,238 条，包含同型号多系统和标识别名，约 500 个联想、428 个戴尔和 570 个惠普型号名称。
