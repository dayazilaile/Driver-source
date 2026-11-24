#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <setupapi.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "hardware_database.h"

static char *read_database(const WCHAR *path, size_t *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER length;
    DWORD received = 0;
    char *buffer = NULL;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 16 * 1024 * 1024) {
        buffer = malloc((size_t)length.QuadPart + 1);
        if (buffer && ReadFile(file, buffer, (DWORD)length.QuadPart, &received, NULL) &&
            received == (DWORD)length.QuadPart) {
            buffer[received] = 0;
            *size = received;
        } else { free(buffer); buffer = NULL; }
    }
    CloseHandle(file);
    return buffer;
}

HRESULT hardware_database_open(HardwareDatabase *db, const WCHAR *root, WCHAR *summary, size_t chars) {
    const WCHAR *files[] = {L"usb.ids", L"pci.ids", L"tablets.tsv"};
    size_t sizes[3] = {0};
    int loaded = 0;
    memset(db, 0, sizeof(*db));
    for (int index = 0; index < 3; ++index) {
        WCHAR path[MAX_PATH];
        if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\database\\%s", root, files[index])))
            db->buffers[index] = read_database(path, &sizes[index]);
        if (db->buffers[index]) ++loaded;
    }
    int parsed = device_db_load(&db->index, db->buffers[0] ? db->buffers[0] : "", sizes[0],
        db->buffers[1] ? db->buffers[1] : "", sizes[1], db->buffers[2] ? db->buffers[2] : "", sizes[2]);
    if (!parsed) {
        StringCchCopyW(summary, chars, L"离线识别库解析失败，请重新解压 database 目录。系统设备枚举仍可使用。");
        return E_INVALIDARG;
    }
    StringCchPrintfW(summary, chars,
        L"已加载 %d/3 份离线数据 · USB 产品 %llu · PCI 设备 %llu · PCI 子系统 %llu · 数位板标识映射 %llu%s",
        loaded, (unsigned long long)db->index.usb_products.count, (unsigned long long)db->index.pci_devices.count,
        (unsigned long long)db->index.pci_subsystems.count, (unsigned long long)db->index.tablet_count,
        loaded < 3 ? L" · 部分库缺失，请检查 EXE 旁的 database 目录" : L"");
    return loaded == 3 ? S_OK : S_FALSE;
}

void hardware_database_close(HardwareDatabase *db) {
    device_db_free(&db->index);
    for (int index = 0; index < 3; ++index) free(db->buffers[index]);
    memset(db, 0, sizeof(*db));
}

static WCHAR *device_ids(HDEVINFO set, SP_DEVINFO_DATA *device, DWORD property) {
    DWORD bytes = 0, type = 0;
    SetupDiGetDeviceRegistryPropertyW(set, device, property, &type, NULL, 0, &bytes);
    if (!bytes || bytes > 65536) return NULL;
    WCHAR *list = calloc(1, bytes + 2 * sizeof(WCHAR));
    if (!list) return NULL;
    if (!SetupDiGetDeviceRegistryPropertyW(set, device, property, &type, (BYTE *)list, bytes, NULL) ||
        type != REG_MULTI_SZ) { free(list); return NULL; }
    return list;
}

static void classify_device(HDEVINFO set, SP_DEVINFO_DATA *device, WCHAR *out, size_t chars) {
    static const struct { const WCHAR *name, *label; } classes[] = {
        {L"Display", L"显卡"}, {L"Net", L"网络"}, {L"MEDIA", L"音频 / 视频"},
        {L"HIDClass", L"HID 输入"}, {L"USB", L"USB"}, {L"Mouse", L"鼠标"},
        {L"Keyboard", L"键盘"}, {L"Image", L"图像 / 扫描"}, {L"Printer", L"打印"},
        {L"Bluetooth", L"蓝牙"}, {L"Ports", L"串口 / 并口"},
        {L"SCSIAdapter", L"存储控制器"}, {L"HDC", L"磁盘控制器"}, {L"DiskDrive", L"磁盘"},
        {L"System", L"系统 / 芯片组"}, {L"Monitor", L"显示器"}, {L"Biometric", L"生物识别"},
        {L"WPD", L"便携设备"}, {L"SoftwareComponent", L"软件组件"},
        {L"Firmware", L"固件"}, {L"AudioEndpoint", L"音频端点"}, {L"Camera", L"摄像头"}
    };
    WCHAR name[128] = L"";
    SetupDiGetDeviceRegistryPropertyW(set, device, SPDRP_CLASS, NULL, (BYTE *)name, sizeof(name), NULL);
    StringCchCopyW(out, chars, name[0] ? name : L"其他设备");
    for (size_t index = 0; index < ARRAYSIZE(classes); ++index)
        if (!_wcsicmp(name, classes[index].name)) {
            StringCchCopyW(out, chars, classes[index].label);
            break;
        }
}

void hardware_identify(const HardwareDatabase *db, HDEVINFO set, SP_DEVINFO_DATA *device,
                       HardwareIdentity *identity) {
    DWORD properties[] = {SPDRP_HARDWAREID, SPDRP_COMPATIBLEIDS};
    BOOL compatible = FALSE;
    memset(identity, 0, sizeof(*identity));
    classify_device(set, device, identity->category, ARRAYSIZE(identity->category));
    for (int property = 0; property < 2; ++property) {
        WCHAR *ids = device_ids(set, device, properties[property]);
        if (!ids) continue;
        for (const WCHAR *id = ids; *id; id += wcslen(id) + 1) {
            if (!property && !identity->primary_id[0])
                StringCchCopyW(identity->primary_id, ARRAYSIZE(identity->primary_id), id);
            DeviceMatch candidate;
            char utf8[8192];
            if (!WideCharToMultiByte(CP_UTF8, 0, id, -1, utf8, sizeof(utf8), NULL, NULL)) continue;
            if (!device_db_match(&db->index, utf8, &candidate)) continue;
            if (candidate.level > identity->match.level ||
                (candidate.level == identity->match.level && candidate.source_count > identity->match.source_count)) {
                identity->match = candidate;
                StringCchCopyW(identity->matched_id, ARRAYSIZE(identity->matched_id), id);
                compatible = property != 0;
            }
        }
        free(ids);
    }
    WCHAR manufacturer[256] = L"", name[512] = L"";
    char utf8[4096];
    SetupDiGetDeviceRegistryPropertyW(set, device, SPDRP_MFG, NULL, (BYTE *)manufacturer, sizeof(manufacturer), NULL);
    SetupDiGetDeviceRegistryPropertyW(set, device, SPDRP_FRIENDLYNAME, NULL, (BYTE *)name, sizeof(name), NULL);
    BOOL name_hint = FALSE;
    if (WideCharToMultiByte(CP_UTF8, 0, manufacturer, -1, utf8, sizeof(utf8), NULL, NULL))
        name_hint = device_db_hint_name(&identity->match, utf8);
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), NULL, NULL))
        name_hint = device_db_hint_name(&identity->match, utf8) || name_hint;
    const WCHAR *basis = L"未命中识别库";
    switch (identity->match.level) {
    case DB_TABLET:
        basis = L"VID/PID 型号候选，需核对";
        StringCchCopyW(identity->category, ARRAYSIZE(identity->category), L"数位板 / 手绘屏");
        break;
    case DB_SUBSYSTEM: basis = L"PCI 子系统标识"; break;
    case DB_DEVICE: basis = L"USB/PCI 设备标识"; break;
    case DB_VENDOR: basis = L"厂商标识，型号待核对"; break;
    default:
        if (name_hint) basis = L"厂商名称提示，型号待核对";
        else if (identity->match.source_count > 1) basis = L"厂商来源规则，型号待核对";
        break;
    }
    StringCchPrintfW(identity->basis, ARRAYSIZE(identity->basis), L"%s%s", compatible ? L"兼容 ID · " : L"", basis);
}
