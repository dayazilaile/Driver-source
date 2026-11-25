#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define COBJMACROS
#define INITGUID
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <oleauto.h>
#include <cguid.h>
#include <knownfolders.h>
#include <strsafe.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winhttp.h>
#include "sources.h"
#include "driver_backup.h"
#include "hardware_database.h"
#include "vendor_catalog.h"
#include "oem_catalog.h"
#include "vendor_download.h"
#include "update_diagnostics.h"
#include "driver_status.h"
#include "installed_driver.h"

#define APP_VERSION L"0.3.2"
#define MAX_UPDATES 4096
#define EVENT_MESSAGE (WM_APP + 1)
#define ID_SCAN 101
#define ID_DOWNLOAD 102
#define ID_CANCEL 103
#define ID_FOLDER 104
#define ID_OPEN 105
#define ID_ALL 106
#define ID_COPY 107
#define ID_TAB 108
#define ID_AUTO_INSTALL 109
#define ID_CHECK_SOURCES 110
#define ID_OPEN_SOURCE 111
#define ID_BACKUP 112
#define ID_BACKUP_FOLDER 113
#define ID_OPEN_BACKUP 114
#define ID_RESTORE 115
#define ID_PRE_BACKUP 116
#define ID_DEVICE_SOURCES 117
#define ID_ALL_SOURCES 118
#define ID_DIAGNOSTICS 119
#define ID_COPY_DIAGNOSTICS 120
#define ID_OPEN_LOGS 121
#define ID_REINSTALL_LOCAL 122

typedef enum { EV_STATUS, EV_HARDWARE, EV_UPDATE, EV_RESET, EV_PROGRESS,
               EV_ITEM_STATUS, EV_SUBMITTED, EV_LOCAL_STATUS, EV_SOURCE_STATUS, EV_DATABASE, EV_DIAGNOSTICS, EV_TRANSFER, EV_DONE, EV_EXIT } EventKind;
typedef struct {
    WCHAR name[512], hardware[2048], version[128], state[128], package_status[128];
    HardwareIdentity identity;
    InstalledDriver driver;
} Hardware;
typedef struct {
    WCHAR title[512], hardware[1024], provider[256];
    WCHAR current_version[128], available_version[128], driver_state[128], advice[192], local_inf[128];
    BOOL processed, local_available;
    int index, device_index;
} Candidate;
typedef struct {
    EventKind kind;
    int index;
    WCHAR text[1024];
    void *data;
} Event;
typedef struct {
    int command, source, count, indices[MAX_UPDATES];
    BOOL install, pre_backup;
    BOOL source_mask[ARRAYSIZE(driver_sources)];
    WCHAR folder[MAX_PATH], backup_root[MAX_PATH];
} Task;

static HWND main_window, hardware_list, update_list, status_label, progress_bar;
static HWND scan_button, download_button, cancel_button, source_box, folder_edit;
static HWND tab_control, folder_button, open_button, all_button, copy_button;
static HWND auto_install_box, source_list, check_sources_button, open_source_button, reinstall_button;
static HWND transfer_label, pre_backup_box, backup_description, backup_edit;
static HWND backup_button, backup_folder_button, open_backup_button, restore_button, restore_description;
static WCHAR app_directory[MAX_PATH];
static HWND database_summary, device_sources_button, all_sources_button, source_context;
static Hardware **hardware_rows;
static size_t hardware_count, hardware_capacity;
static WCHAR source_status[ARRAYSIZE(driver_sources)][128];
static BOOL source_filter[ARRAYSIZE(driver_sources)], filtering_sources;
static HWND diagnostics_edit, diagnostics_button, copy_diagnostics_button, open_logs_button;
static HFONT ui_font;
static HANDLE task_event, worker_handle;
static CRITICAL_SECTION task_lock;
static Task next_task;
static volatile LONG cancelled, quitting;
static BOOL busy;
static Candidate *candidates[MAX_UPDATES];
static int candidate_count;

/* The worker owns the catalog. Posted events own their payload. */
static void emit(EventKind kind, int index, const WCHAR *text, void *data) {
    Event *event = calloc(1, sizeof(*event));
    if (!event) {
        free(data);
        return;
    }
    event->kind = kind;
    event->index = index;
    event->data = data;
    if (text) StringCchCopyW(event->text, ARRAYSIZE(event->text), text);
    if (!PostMessageW(main_window, EVENT_MESSAGE, 0, (LPARAM)event)) {
        free(data);
        free(event);
    }
}

static BOOL should_cancel(void) {
    return InterlockedCompareExchange(&cancelled, 0, 0) ||
           InterlockedCompareExchange(&quitting, 0, 0);
}

static void log_update_result(const WCHAR *step, HRESULT result) {
    WCHAR text[1200];
    StringCchPrintfW(text, ARRAYSIZE(text), L"%s = 0x%08lX", step, (unsigned long)result);
    append_update_log(app_directory, text);
}

static void utf8_text(const char *text, WCHAR *out, int chars);
static VendorCatalog vendor_catalog;
static char *vendor_catalog_text;
static BOOL vendor_seen[VENDOR_MAX_PACKAGES];
static unsigned windows_build;
static WCHAR computer_vendor[128], computer_model[128];
static BOOL native_x64;
static size_t matched_packages;

static OemCatalog oem_catalog;
static char *oem_catalog_text;
static VendorPackage selected_oem;
static int oem_match = -1;
static WCHAR computer_sku[128], computer_baseboard[128];
static size_t detected_devices;

typedef struct {
    WCHAR instance_id[MAX_DEVICE_ID_LEN], published_inf[128], version[128];
    BOOL available;
} LocalReference;
static LocalReference local_drivers[MAX_UPDATES];
static size_t local_driver_count, local_package_devices;

static const WCHAR *advice_text(DriverAdvice advice) {
    switch (advice) {
    case DRIVER_LOCAL_MATCH: return L"暂无下载源；可本机匹配安装";
    case DRIVER_CAN_INSTALL: return L"有适用安装包";
    case DRIVER_CAN_UPDATE: return L"可更新";
    case DRIVER_CAN_REINSTALL: return L"同版本，可重新匹配安装";
    case DRIVER_KEEP_NEWER: return L"本机版本较新，建议保留";
    case DRIVER_VERSION_UNCERTAIN: return L"有安装包，版本待核对";
    default: return L"暂无已核验下载源";
    }
}

static Candidate *device_candidate(const Hardware *hardware, int package_index, BOOL ambiguous,
                                    BOOL registered, ULONG flags, ULONG problem, int local_index) {
    Candidate *row = calloc(1, sizeof(*row));
    if (!row) return NULL;
    row->index = package_index;
    row->device_index = local_index;
    row->local_available = hardware->driver.local_available;
    StringCchCopyW(row->local_inf, ARRAYSIZE(row->local_inf), hardware->driver.published_inf);
    StringCchCopyW(row->title, ARRAYSIZE(row->title), hardware->name);
    StringCchCopyW(row->hardware, ARRAYSIZE(row->hardware), hardware->hardware);
    StringCchCopyW(row->provider, ARRAYSIZE(row->provider), hardware->driver.provider[0] ? hardware->driver.provider : L"厂商信息待核对");
    StringCchCopyW(row->current_version, ARRAYSIZE(row->current_version), hardware->version[0] ? hardware->version : L"未读取到版本");
    BOOL installed = registered || (flags & DN_DRIVER_LOADED);
    const WCHAR *state = installed ? L"已安装" : L"信息待核对";
    if (problem == CM_PROB_FAILED_INSTALL) { installed = FALSE; state = L"缺少驱动（代码 28）"; }
    else if (problem) state = installed ? L"已安装，设备有问题" : L"设备有问题";
    StringCchCopyW(row->driver_state, ARRAYSIZE(row->driver_state), state);
    StringCchCopyW(row->available_version, ARRAYSIZE(row->available_version), L"暂无已核验版本");
    const VendorPackage *package = package_index >= 0 ? &vendor_catalog.packages[package_index] : NULL;
    char current[256] = "", available[256] = "";
    WideCharToMultiByte(CP_UTF8, 0, hardware->version, -1, current, sizeof(current), NULL, NULL);
    BOOL same_provider = FALSE;
    if (package) {
        WCHAR provider[256]; utf8_text(package->provider, provider, ARRAYSIZE(provider));
        same_provider = hardware->driver.provider[0] && !_wcsicmp(hardware->driver.provider, provider);
        utf8_text(package->version, row->available_version, ARRAYSIZE(row->available_version));
        StringCchCopyW(row->provider, ARRAYSIZE(row->provider), provider);
        StringCchCopyA(available, ARRAYSIZE(available), package->version);
    }
    DriverAdvice advice = driver_advice(installed, package != NULL, same_provider,
        row->local_available, current, available);
    StringCchCopyW(row->advice, ARRAYSIZE(row->advice), ambiguous ? L"多个候选包，需核对来源" : advice_text(advice));
    return row;
}

static HRESULT load_oem_catalog(void) {
    WCHAR path[MAX_PATH]; char error[256];
    HRESULT hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\database\\oem-packages.tsv", app_directory);
    if (FAILED(hr)) return hr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 4194304) {
        CloseHandle(file); return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    oem_catalog_text = malloc((size_t)size.QuadPart + 1);
    if (!oem_catalog_text) { CloseHandle(file); return E_OUTOFMEMORY; }
    DWORD received;
    BOOL ok = ReadFile(file, oem_catalog_text, (DWORD)size.QuadPart, &received, NULL);
    CloseHandle(file);
    if (!ok || received != (DWORD)size.QuadPart) return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    oem_catalog_text[received] = 0;
    if (memchr(oem_catalog_text, 0, received)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (!oem_catalog_parse(&oem_catalog, oem_catalog_text, error, sizeof(error))) {
        WCHAR message[256]; utf8_text(error, message, ARRAYSIZE(message)); append_update_log(app_directory, message);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

static void match_oem_package(void) {
    char vendor[512], product[512], sku[512], board[512];
    oem_match = -1;
    if (!WideCharToMultiByte(CP_UTF8, 0, computer_vendor, -1, vendor, sizeof(vendor), NULL, NULL) ||
        !WideCharToMultiByte(CP_UTF8, 0, computer_model, -1, product, sizeof(product), NULL, NULL) ||
        !WideCharToMultiByte(CP_UTF8, 0, computer_sku, -1, sku, sizeof(sku), NULL, NULL) ||
        !WideCharToMultiByte(CP_UTF8, 0, computer_baseboard, -1, board, sizeof(board), NULL, NULL)) return;
    OemMachine machine = {vendor, product, sku, board, windows_build, native_x64};
    oem_match = oem_catalog_select_machine(&oem_catalog, &machine);
    if (oem_match < 0) return;
    const OemPackage *pack = &oem_catalog.packages[oem_match];
    oem_package_as_vendor(pack, &selected_oem);
    Candidate *row = calloc(1, sizeof(*row));
    if (!row) { oem_match = -1; return; }
    WCHAR model[256], release[24], family[24], amount[64];
    utf8_text(pack->model, model, ARRAYSIZE(model));
    utf8_text(pack->release, release, ARRAYSIZE(release));
    utf8_text(pack->os, family, ARRAYSIZE(family));
    if (pack->bytes) StringCchPrintfW(amount, ARRAYSIZE(amount), L"%.2f GiB", (double)pack->bytes / 1073741824.0);
    else StringCchCopyW(amount, ARRAYSIZE(amount), L"大小由下载服务器提供");
    StringCchPrintfW(row->title, ARRAYSIZE(row->title), L"%s 整机驱动包 · %s %s · %s · %s", model,
        family, release, amount, pack->format == VENDOR_EXE_DOWNLOAD ? L"仅下载，需手动安装" : L"支持 INF 匹配安装");
    StringCchCopyW(row->provider, ARRAYSIZE(row->provider), !strcmp(pack->manufacturer, "lenovo") ? L"联想官方" : !strcmp(pack->manufacturer, "dell") ? L"戴尔官方" : L"惠普官方");
    StringCchPrintfW(row->hardware, ARRAYSIZE(row->hardware), L"型号：%s / SKU %s / 主板 %s", computer_model, computer_sku, computer_baseboard);
    StringCchCopyW(row->driver_state, ARRAYSIZE(row->driver_state), L"整机安装包");
    StringCchCopyW(row->current_version, ARRAYSIZE(row->current_version), L"各设备分别显示");
    StringCchCopyW(row->available_version, ARRAYSIZE(row->available_version), L"包内版本待核对");
    StringCchCopyW(row->advice, ARRAYSIZE(row->advice), pack->format == VENDOR_EXE_DOWNLOAD ? L"整机包仅下载，需手动安装" : L"可下载并提交 INF 匹配安装");
    row->device_index = -1;
    row->index = VENDOR_MAX_PACKAGES;
    emit(EV_UPDATE, row->index, NULL, row);
}

static const VendorPackage *selected_package(int index) {
    if (index == VENDOR_MAX_PACKAGES && oem_match >= 0) return &selected_oem;
    if (index >= 0 && (size_t)index < vendor_catalog.count) return &vendor_catalog.packages[index];
    return NULL;
}

static HRESULT load_vendor_catalog(void) {
    WCHAR path[MAX_PATH];
    char error[256];
    HRESULT hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\database\\vendor-packages.tsv", app_directory);
    if (FAILED(hr)) return hr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) { hr = HRESULT_FROM_WIN32(GetLastError()); CloseHandle(file); return hr; }
    if (size.QuadPart <= 0 || size.QuadPart > 4194304) { CloseHandle(file); return HRESULT_FROM_WIN32(ERROR_INVALID_DATA); }
    vendor_catalog_text = malloc((size_t)size.QuadPart + 1);
    if (!vendor_catalog_text) { CloseHandle(file); return E_OUTOFMEMORY; }
    DWORD received;
    BOOL ok = ReadFile(file, vendor_catalog_text, (DWORD)size.QuadPart, &received, NULL);
    CloseHandle(file);
    if (!ok || received != (DWORD)size.QuadPart) return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    vendor_catalog_text[received] = 0;
    if (memchr(vendor_catalog_text, 0, received)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (!vendor_catalog_parse(&vendor_catalog, vendor_catalog_text, error, sizeof(error))) {
        WCHAR message[256]; utf8_text(error, message, ARRAYSIZE(message));
        append_update_log(app_directory, message);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

static void detect_platform(void) {
    DWORD chars = sizeof(computer_vendor);
    const WCHAR *bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    RegGetValueW(HKEY_LOCAL_MACHINE, bios, L"SystemManufacturer", RRF_RT_REG_SZ, NULL, computer_vendor, &chars);
    chars = sizeof(computer_model);
    RegGetValueW(HKEY_LOCAL_MACHINE, bios, L"SystemProductName", RRF_RT_REG_SZ, NULL, computer_model, &chars);
    chars = sizeof(computer_sku);
    RegGetValueW(HKEY_LOCAL_MACHINE, bios, L"SystemSKU", RRF_RT_REG_SZ, NULL, computer_sku, &chars);
    chars = sizeof(computer_baseboard);
    RegGetValueW(HKEY_LOCAL_MACHINE, bios, L"BaseBoardProduct", RRF_RT_REG_SZ, NULL, computer_baseboard, &chars);
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    native_x64 = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
    typedef LONG (WINAPI *VersionFunction)(OSVERSIONINFOW *);
    union { FARPROC generic; VersionFunction typed; } function;
    function.generic = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    if (function.typed && function.typed(&version) == 0) windows_build = version.dwBuildNumber;
}

static int match_vendor_device(HDEVINFO set, SP_DEVINFO_DATA *device, Hardware *hardware, BOOL *ambiguous) {
    WCHAR ids[8192] = L""; DWORD type, bytes; int match = -1;
    *ambiguous = FALSE;
    if (SetupDiGetDeviceRegistryPropertyW(set, device, SPDRP_HARDWAREID, &type,
            (BYTE *)ids, sizeof(ids) - 2 * sizeof(WCHAR), &bytes) && type == REG_MULTI_SZ) {
        for (const WCHAR *id = ids; *id; id += wcslen(id) + 1) {
            char utf8[4096];
            if (!WideCharToMultiByte(CP_UTF8, 0, id, -1, utf8, sizeof(utf8), NULL, NULL)) continue;
            for (size_t index = 0; index < vendor_catalog.count; ++index) {
                if (!vendor_package_matches(&vendor_catalog.packages[index], utf8, windows_build, native_x64)) continue;
                if (match >= 0 && match != (int)index) *ambiguous = TRUE;
                match = (int)index;
                if (!vendor_seen[index]) { vendor_seen[index] = TRUE; ++matched_packages; }
            }
        }
    }
    StringCchCopyW(hardware->package_status, ARRAYSIZE(hardware->package_status),
        *ambiguous ? L"多个候选包，待核对" : match >= 0 ? L"已匹配单设备安装包" :
        oem_match >= 0 ? L"已提供整机包，设备内容需解压核对" : L"暂无适用下载源；已安装驱动仍显示");
    return *ambiguous ? -1 : match;
}

static void publish_diagnostics(HRESULT error, const WCHAR *step) {
    WCHAR *text = calloc(8192, sizeof(WCHAR));
    if (!text) return;
    StringCchPrintfW(text, 8192,
        L"驱动源 " APP_VERSION L" · 厂商独立下载模式\r\n"
        L"整机厂商 = %s\r\n整机型号 = %s\r\n进程令牌 = %s\r\nWindows 内部版本 = %u\r\n本机架构符合 x64 安装包 = %s\r\n"
        L"单设备安装包目录 = %zu 项\r\n整机型号/系统目录 = %zu 条\r\n整机 SKU = %s\r\n主板标识 = %s\r\n整机包匹配 = %s\r\n最近步骤 = %s\r\n最近结果 = 0x%08lX\r\n\r\n"
        L"检测使用本机设备硬件 ID；下载使用 HTTPS 厂商发布地址。\r\n"
        L"下载完成核对文件大小和 SHA-256，安装由 Windows 检查签名及设备匹配。\r\n"
        L"主列表显示已检测设备及已安装版本；只有已匹配包的项目可下载。可从“本机硬件”查看来源建议。\r\n",
        computer_vendor, computer_model, update_process_elevated() ? L"已提权" : L"未提权", windows_build,
        native_x64 ? L"是" : L"否", vendor_catalog.count, oem_catalog.count, computer_sku, computer_baseboard, oem_match >= 0 ? L"已匹配" : L"未匹配", step, (unsigned long)error);
    HRESULT log = append_update_log(app_directory, text);
    StringCchCatW(text, 8192, SUCCEEDED(log) ? L"\r\n日志位于软件目录 logs\\DriverSource.log" : L"\r\n日志写入失败，可复制当前诊断文本。");
    emit(EV_DIAGNOSTICS, 0, NULL, text);
}

static void report_error(const WCHAR *operation, HRESULT error) {
    WCHAR message[1024], detail[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)error, 0, detail, ARRAYSIZE(detail), NULL);
    log_update_result(operation, error);
    if (error == E_ACCESSDENIED || error == RPC_E_ACCESS_DENIED)
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"%s被拒绝（0x%08lX）。当前令牌%s。请查看“系统诊断”和下载目录内的处理日志，详情保存在 logs/DriverSource.log。",
            operation, (unsigned long)error, update_process_elevated() ? L"已提权" : L"未提权");
    else if (error == E_POINTER || error == E_NOINTERFACE)
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"%s失败（0x%08lX）。%s 系统接口返回参数或兼容性错误，请记录此步骤和错误码。",
            operation, (unsigned long)error, detail);
    else StringCchPrintfW(message, ARRAYSIZE(message),
        L"%s失败（0x%08lX）。%s 请检查厂商地址连通性、驱动目录和本机日志。",
        operation, (unsigned long)error, detail);
    emit(EV_STATUS, 0, message, NULL);
}

static void scan_hardware(const HardwareDatabase *database) {
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    SP_DEVINFO_DATA device = { .cbSize = sizeof(device) };
    if (set == INVALID_HANDLE_VALUE) {
        report_error(L"读取本机硬件", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    for (DWORD index = 0; !should_cancel(); ++index) {
        if (!SetupDiEnumDeviceInfo(set, index, &device)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                report_error(L"枚举设备", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        Hardware *row = calloc(1, sizeof(*row));
        DWORD type, bytes;
        ULONG flags = 0, problem = 0;
        if (!row) break;
        if (!SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_FRIENDLYNAME, &type,
                (BYTE *)row->name, sizeof(row->name), &bytes))
            SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_DEVICEDESC, &type,
                (BYTE *)row->name, sizeof(row->name), &bytes);
        /* SPDRP_HARDWAREID is MULTI_SZ. Display the most specific first ID. */
        SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, &type,
                (BYTE *)row->hardware, sizeof(row->hardware) - sizeof(WCHAR), &bytes);
        installed_driver_read(set, &device, &row->driver);
        StringCchCopyW(row->version, ARRAYSIZE(row->version), row->driver.version);
        if (CM_Get_DevNode_Status(&flags, &problem, device.DevInst, 0) == CR_SUCCESS && (flags & DN_HAS_PROBLEM))
            StringCchPrintfW(row->state, ARRAYSIZE(row->state), L"设备问题 %lu", (unsigned long)problem);
        else
            StringCchCopyW(row->state, ARRAYSIZE(row->state), L"已检测");
        hardware_identify(database, set, &device, &row->identity);
        if (!row->hardware[0])
            StringCchCopyW(row->hardware, ARRAYSIZE(row->hardware), row->identity.primary_id);
        if (!row->name[0]) StringCchCopyW(row->name, ARRAYSIZE(row->name), row->hardware[0] ? row->hardware : L"未命名设备");
        BOOL ambiguous;
        int package_index = match_vendor_device(set, &device, row, &ambiguous);
        Candidate *candidate = NULL;
        if (local_driver_count < MAX_UPDATES - 1) {
            int local_index = (int)local_driver_count++;
            LocalReference *reference = &local_drivers[local_index];
            ZeroMemory(reference, sizeof(*reference));
            BOOL instance_ok = CM_Get_Device_IDW(device.DevInst, reference->instance_id, ARRAYSIZE(reference->instance_id), 0) == CR_SUCCESS;
            reference->available = instance_ok && row->driver.local_available;
            if (!reference->available) row->driver.local_available = FALSE;
            if (reference->available) ++local_package_devices;
            StringCchCopyW(reference->published_inf, ARRAYSIZE(reference->published_inf), row->driver.published_inf);
            StringCchCopyW(reference->version, ARRAYSIZE(reference->version), row->version);
            candidate = device_candidate(row, package_index, ambiguous, row->driver.registered, flags, problem, local_index);
        }
        WCHAR device_log[3400];
        StringCchPrintfW(device_log, ARRAYSIZE(device_log), L"硬件 %s | %s | 驱动版本 %s | %s", row->name, row->hardware, row->version, row->package_status);
        append_update_log(app_directory, device_log);
        ++detected_devices;
        emit(EV_HARDWARE, 0, NULL, row);
        if (candidate) emit(EV_UPDATE, candidate->index, NULL, candidate);
    }
    SetupDiDestroyDeviceInfoList(set);
}

static HRESULT create_backup(const Task *task) {
    WCHAR saved[MAX_PATH], text[1024];
    emit(EV_STATUS, 0, L"正在备份现有第三方驱动。取消将在系统导出结束后生效…", NULL);
    emit(EV_TRANSFER, 0, L"DISM 正在导出驱动，日志保存在备份目录", NULL);
    emit(EV_PROGRESS, -1, NULL, NULL);
    HRESULT hr = backup_drivers(task->backup_root, saved, ARRAYSIZE(saved));
    if (FAILED(hr)) {
        StringCchPrintfW(text, ARRAYSIZE(text),
            L"驱动备份失败（0x%08lX）。%s请检查备份路径和 .partial 目录内的日志。",
            (unsigned long)hr, task->command == ID_BACKUP ? L"" : L"后续安装已暂停。");
    } else {
        StringCchPrintfW(text, ARRAYSIZE(text), L"第三方驱动备份完成，保存在 %s。%s",
            saved, should_cancel() ? L"后续任务已取消。" : L"");
    }
    emit(EV_STATUS, 0, text, NULL);
    emit(EV_PROGRESS, SUCCEEDED(hr) ? 100 : 0, NULL, NULL);
    return hr;
}

static void vendor_transfer(int percent, const WCHAR *text) {
    emit(EV_PROGRESS, percent, NULL, NULL);
    emit(EV_TRANSFER, 0, text, NULL);
}

static HRESULT download_selected(const Task *task) {
    HRESULT outcome = S_OK;
    int successful = 0, downloaded_count = 0, submitted_count = 0, failed = 0;
    BOOL needs_install = FALSE;
    for (int i = 0; i < task->count; ++i) {
        const VendorPackage *package = selected_package(task->indices[i]);
        if (package && package->format != VENDOR_EXE_DOWNLOAD) needs_install = TRUE;
    }
    BOOL reboot = FALSE;
    if (task->install && needs_install && task->pre_backup) {
        HRESULT checkpoint = create_backup(task);
        if (FAILED(checkpoint) || should_cancel()) return FAILED(checkpoint) ? checkpoint : HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    for (int item = 0; item < task->count && !should_cancel() && !reboot; ++item) {
        int index = task->indices[item];
        const VendorPackage *package = selected_package(index);
        if (!package) { ++failed; outcome = E_INVALIDARG; continue; }
        BOOL install = task->install && package->format != VENDOR_EXE_DOWNLOAD;
        WCHAR text[1024], saved[MAX_PATH] = L"", inf[MAX_PATH];
        StringCchPrintfW(text, ARRAYSIZE(text), L"正在处理厂商驱动 %d / %d。", item + 1, task->count);
        emit(EV_STATUS, 0, text, NULL);
        emit(EV_ITEM_STATUS, index, L"厂商直连下载中", NULL);
        HRESULT hr = vendor_download_package(package, task->folder, should_cancel,
            vendor_transfer, saved, ARRAYSIZE(saved));
        log_update_result(L"厂商直连下载与 SHA-256 校验", hr);
        if (SUCCEEDED(hr)) ++downloaded_count;
        BOOL submitted = FALSE;
        if (SUCCEEDED(hr) && install && !should_cancel()) {
            emit(EV_ITEM_STATUS, index, L"正在解压已校验驱动包", NULL);
            hr = vendor_extract_package(package, saved, should_cancel, inf, ARRAYSIZE(inf));
            log_update_result(L"已校验厂商包解压", hr);
            if (SUCCEEDED(hr) && !should_cancel()) {
                emit(EV_ITEM_STATUS, index, L"系统正在匹配安装", NULL);
                vendor_transfer(-1, L"Windows 正在检查签名并匹配安装，取消将在当前安装返回后生效…");
                hr = vendor_install_inf(inf, saved, &reboot);
                log_update_result(L"PnPUtil 系统匹配安装", hr);
                submitted = SUCCEEDED(hr);
            }
        }
        if (SUCCEEDED(hr) && should_cancel() && !submitted) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (SUCCEEDED(hr) && install && !submitted) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        log_update_result(L"厂商驱动处理结果", hr);
        if (saved[0]) append_update_log(app_directory, saved);
        if (SUCCEEDED(hr)) {
            ++successful;
            if (submitted) ++submitted_count;
            emit(submitted ? EV_SUBMITTED : EV_ITEM_STATUS, index,
                submitted ? reboot ? L"已提交匹配安装，需要重启" : L"已提交系统匹配安装" :
                package->format == VENDOR_EXE_DOWNLOAD ? L"已下载并校验，需手动安装" : L"已保存并校验", NULL);
        } else {
            ++failed;
            outcome = hr;
            StringCchPrintfW(text, ARRAYSIZE(text), L"%s 0x%08lX，详见日志",
                hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? L"已取消" : L"失败", (unsigned long)hr);
            emit(EV_ITEM_STATUS, index, text, NULL);
        }
    }
    WCHAR text[1024];
    StringCchPrintfW(text, ARRAYSIZE(text), L"%s下载校验完成 %d 项，已提交匹配安装 %d 项，失败或取消 %d 项，未执行 %d 项。%s",
        should_cancel() ? L"任务已取消。" : L"任务结束。", downloaded_count, submitted_count, failed,
        task->count - successful - failed, reboot ? L"系统要求重启，请保存工作后手动重启。" :
        L"整机 EXE 包仅保存，不自动运行；安装详情见下载目录日志。");
    emit(EV_STATUS, 0, text, NULL);
    if (should_cancel() && SUCCEEDED(outcome)) outcome = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return outcome;
}

static HRESULT reinstall_local(const Task *task) {
    if (task->pre_backup) {
        HRESULT hr = create_backup(task);
        if (FAILED(hr) || should_cancel()) return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    int completed = 0, failed = 0;
    BOOL reboot = FALSE;
    HRESULT outcome = S_OK;
    for (int item = 0; item < task->count && !should_cancel() && !reboot; ++item) {
        int index = task->indices[item];
        if (index < 0 || (size_t)index >= local_driver_count || !local_drivers[index].available) {
            ++failed; outcome = E_INVALIDARG; continue;
        }
        const LocalReference *reference = &local_drivers[index];
        WCHAR inf[MAX_PATH], folder[MAX_PATH] = L"", guid[40], text[1024]; GUID id;
        emit(EV_LOCAL_STATUS, index, L"正在核对本机驱动包…", NULL);
        HRESULT hr = installed_driver_check(reference->instance_id, reference->published_inf,
            reference->version, inf, ARRAYSIZE(inf));
        if (SUCCEEDED(hr) && should_cancel()) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        if (SUCCEEDED(hr)) hr = CoCreateGuid(&id);
        if (SUCCEEDED(hr)) {
            StringFromGUID2(&id, guid, ARRAYSIZE(guid));
            hr = StringCchPrintfW(folder, ARRAYSIZE(folder), L"%s\\LocalMatch-%s", task->folder, guid);
        }
        if (SUCCEEDED(hr) && !CreateDirectoryW(folder, NULL)) hr = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(hr) && !should_cancel()) {
            emit(EV_STATUS, 0, L"正在使用本机已安装的驱动包重新匹配安装。Windows 可能保留当前同版本，详情见 install.log。", NULL);
            emit(EV_PROGRESS, -1, NULL, NULL);
            emit(EV_TRANSFER, 0, L"使用本机驱动包，等待 Windows 完成匹配安装…", NULL);
            hr = vendor_install_inf(inf, folder, &reboot);
        } else if (SUCCEEDED(hr)) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        log_update_result(L"本机驱动库匹配安装", hr);
        if (folder[0]) append_update_log(app_directory, folder);
        if (SUCCEEDED(hr)) {
            ++completed;
            emit(EV_LOCAL_STATUS, index, reboot ? L"已提交本机匹配安装，需要重启" : L"已提交本机匹配安装，同版可能保留", NULL);
        } else {
            ++failed; outcome = hr;
            StringCchPrintfW(text, ARRAYSIZE(text), hr == HRESULT_FROM_WIN32(ERROR_RETRY) ?
                L"驱动状态已变化，请重新检测（0x%08lX）" : L"本机匹配安装失败 0x%08lX", (unsigned long)hr);
            emit(EV_LOCAL_STATUS, index, text, NULL);
        }
        emit(EV_PROGRESS, (item + 1) * 100 / task->count, NULL, NULL);
    }
    WCHAR summary[1024];
    StringCchPrintfW(summary, ARRAYSIZE(summary), L"本机匹配安装：提交 %d 个驱动包，失败 %d 个，未执行 %d 个。%s",
        completed, failed, task->count - completed - failed, reboot ? L"请保存工作后手动重启。" :
        L"Windows 可能保留已安装的最佳驱动；可重新检测刷新当前版本，详情见所选目录的 LocalMatch 日志。");
    emit(EV_STATUS, 0, summary, NULL);
    if (should_cancel() && SUCCEEDED(outcome)) outcome = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return outcome;
}

static void check_sources(const Task *task) {
    HINTERNET session = WinHttpOpen(L"DriverSource/" APP_VERSION,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { report_error(L"初始化来源检查", HRESULT_FROM_WIN32(GetLastError())); return; }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    int total = 0, completed = 0;
    for (size_t index = 0; index < ARRAYSIZE(driver_sources); ++index)
        if (task->source_mask[index]) ++total;
    for (int index = 0; index < (int)ARRAYSIZE(driver_sources) && !should_cancel(); ++index) {
        if (!task->source_mask[index]) continue;
        WCHAR host[256] = L"", path[2048] = L"", extra[1024] = L"", target[3072], status[128];
        URL_COMPONENTS url = { .dwStructSize = sizeof(url), .lpszHostName = host,
            .dwHostNameLength = ARRAYSIZE(host), .lpszUrlPath = path, .dwUrlPathLength = ARRAYSIZE(path),
            .lpszExtraInfo = extra, .dwExtraInfoLength = ARRAYSIZE(extra) };
        HINTERNET connection = NULL, request = NULL;
        DWORD error = ERROR_SUCCESS, http_status = 0, size = sizeof(http_status);
        emit(EV_SOURCE_STATUS, index, L"检查中", NULL);
        if (!WinHttpCrackUrl(driver_sources[index].url, 0, 0, &url)) error = GetLastError();
        if (!error) {
            connection = WinHttpConnect(session, host, url.nPort, 0);
            if (!connection) error = GetLastError();
        }
        if (!error && FAILED(StringCchPrintfW(target, ARRAYSIZE(target), L"%s%s", path, extra)))
            error = ERROR_INSUFFICIENT_BUFFER;
        if (!error) {
            request = WinHttpOpenRequest(connection, L"GET", target, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) error = GetLastError();
        }
        if (!error && (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                       !WinHttpReceiveResponse(request, NULL))) error = GetLastError();
        if (!error && !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &http_status, &size,
                                           WINHTTP_NO_HEADER_INDEX)) error = GetLastError();
        if (error) StringCchPrintfW(status, ARRAYSIZE(status), L"连接错误 %lu", (unsigned long)error);
        else StringCchPrintfW(status, ARRAYSIZE(status), L"HTTP %lu", (unsigned long)http_status);
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        emit(EV_SOURCE_STATUS, index, status, NULL);
        ++completed;
        emit(EV_PROGRESS, total ? completed * 100 / total : 0, NULL, NULL);
    }
    WinHttpCloseHandle(session);
    emit(EV_STATUS, 0, should_cancel() ? L"来源检查已取消。" :
         L"来源检查结束。HTTP 状态仅反映入口连通性，具体型号和安装包仍需核验。", NULL);
}

static DWORD WINAPI worker_main(void *unused) {
    (void)unused;
    HardwareDatabase database;
    WCHAR summary[1024];
    hardware_database_open(&database, app_directory, summary, ARRAYSIZE(summary));
    emit(EV_DATABASE, 0, summary, NULL);
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    detect_platform();
    HRESULT catalog_hr = load_vendor_catalog();
    HRESULT oem_hr = load_oem_catalog();
    HRESULT last_error = FAILED(catalog_hr) ? catalog_hr : oem_hr;
    const WCHAR *last_step = L"加载厂商安装包目录";
    publish_diagnostics(last_error, last_step);
    for (;;) {
        WaitForSingleObject(task_event, INFINITE);
        if (InterlockedCompareExchange(&quitting, 0, 0)) break;
        Task task;
        EnterCriticalSection(&task_lock);
        task = next_task;
        LeaveCriticalSection(&task_lock);
        if (task.command == ID_SCAN) {
            emit(EV_RESET, 0, NULL, NULL);
            ZeroMemory(vendor_seen, sizeof(vendor_seen));
            matched_packages = 0; detected_devices = 0; local_driver_count = 0; local_package_devices = 0;
            match_oem_package();
            scan_hardware(&database);
            last_error = FAILED(catalog_hr) ? catalog_hr : oem_hr; last_step = L"本机硬件与厂商驱动包匹配";
            if (FAILED(catalog_hr)) report_error(L"加载单设备安装包目录", catalog_hr);
            if (FAILED(oem_hr)) report_error(L"加载整机驱动包目录", oem_hr);
            if (SUCCEEDED(catalog_hr) || SUCCEEDED(oem_hr)) {
                WCHAR message[1024];
                if (!native_x64 || windows_build < 17763)
                    StringCchCopyW(message, ARRAYSIZE(message), L"当前安装包目录支持 Windows 10 1809 及以上 x64 系统。其他系统仍可查看硬件与来源建议。");
                else StringCchPrintfW(message, ARRAYSIZE(message),
                    L"%s已检测 %zu 个设备及当前驱动，匹配 %zu 个单设备包、%d 个整机包；%zu 个设备有本机第三方驱动包。暂无来源的项目也会显示。",
                    should_cancel() ? L"扫描已取消。" : L"检测完成。", detected_devices, matched_packages, oem_match >= 0 ? 1 : 0, local_package_devices);
                emit(EV_STATUS, 0, message, NULL);
            }
            publish_diagnostics(last_error, last_step);
        } else if (task.command == ID_REINSTALL_LOCAL) {
            last_error = reinstall_local(&task);
            last_step = L"本机驱动库匹配安装";
            publish_diagnostics(last_error, last_step);
        } else if (task.command == ID_DIAGNOSTICS) {
            publish_diagnostics(last_error, last_step);
            emit(EV_STATUS, 0, L"系统诊断已刷新，可复制诊断或打开日志目录。", NULL);
        } else if (task.command == ID_BACKUP) {
            create_backup(&task);
        } else if (task.command == ID_RESTORE) {
            HRESULT hr = S_OK;
            DWORD code = 0;
            if (task.pre_backup) hr = create_backup(&task);
            if (SUCCEEDED(hr) && !should_cancel()) {
                emit(EV_STATUS, 0, L"正在导入备份驱动，请等待 Windows 完成处理…", NULL);
                emit(EV_TRANSFER, 0, L"PnPUtil 正在处理备份，取消将在系统操作结束后生效", NULL);
                emit(EV_PROGRESS, -1, NULL, NULL);
                hr = restore_drivers(task.folder, task.backup_root, &code);
                WCHAR text[1024];
                if (FAILED(hr))
                    StringCchPrintfW(text, ARRAYSIZE(text), L"备份导入失败（0x%08lX）。请选择完整备份，详见备份目录的 Restore 日志。",
                                    (unsigned long)hr);
                else StringCchPrintfW(text, ARRAYSIZE(text),
                    L"备份导入已完成，Windows 已尝试安装匹配项。%s如需版本降级，请在设备管理器回退驱动。",
                    code == ERROR_SUCCESS_REBOOT_REQUIRED || code == ERROR_SUCCESS_REBOOT_INITIATED ?
                    L"系统要求重启，请保存工作后手动重启。" : L"");
                emit(EV_STATUS, 0, text, NULL);
                emit(EV_TRANSFER, 0, L"备份导入操作已结束，具体结果见系统工具日志", NULL);
                emit(EV_PROGRESS, SUCCEEDED(hr) ? 100 : 0, NULL, NULL);
            }
        } else if (task.command == ID_CHECK_SOURCES) {
            check_sources(&task);
        } else if (task.command == ID_DOWNLOAD && (SUCCEEDED(catalog_hr) || SUCCEEDED(oem_hr))) {
            last_error = download_selected(&task);
            last_step = L"厂商下载与匹配安装";
            publish_diagnostics(last_error, last_step);
        }
        emit(EV_DONE, 0, NULL, NULL);
    }
    free(vendor_catalog_text);
    vendor_catalog_text = NULL;
    free(oem_catalog_text); oem_catalog_text = NULL;
    hardware_database_close(&database);
    if (SUCCEEDED(init)) CoUninitialize();
    emit(EV_EXIT, 0, NULL, NULL);
    return 0;
}

static void clear_candidates(void) {
    for (int index = 0; index < candidate_count; ++index) {
        free(candidates[index]);
    }
    candidate_count = 0;
}

static void set_busy(BOOL value) {
    busy = value;
    EnableWindow(scan_button, !value);
    EnableWindow(update_list, !value);
    EnableWindow(source_box, !value);
    BOOL can_download = FALSE, can_local = FALSE;
    for (int i = 0; i < candidate_count; ++i) {
        if (candidates[i]->index >= 0 && !candidates[i]->processed) can_download = TRUE;
        if (candidates[i]->local_available) can_local = TRUE;
    }
    EnableWindow(download_button, !value && can_download);
    EnableWindow(reinstall_button, !value && can_local);
    EnableWindow(folder_button, !value);
    EnableWindow(folder_edit, !value);
    EnableWindow(all_button, !value);
    EnableWindow(auto_install_box, !value);
    EnableWindow(check_sources_button, !value);
    EnableWindow(device_sources_button, !value);
    EnableWindow(all_sources_button, !value);
    EnableWindow(diagnostics_button, !value);
    EnableWindow(pre_backup_box, !value);
    EnableWindow(backup_button, !value);
    EnableWindow(restore_button, !value);
    EnableWindow(backup_edit, !value);
    EnableWindow(backup_folder_button, !value);
    EnableWindow(cancel_button, value);
    if (!value) SendMessageW(progress_bar, PBM_SETMARQUEE, FALSE, 0);
}

static void set_progress(int percent) {
    LONG_PTR style = GetWindowLongPtrW(progress_bar, GWL_STYLE);
    if (percent < 0) {
        SetWindowLongPtrW(progress_bar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessageW(progress_bar, PBM_SETMARQUEE, TRUE, 40);
    } else {
        SendMessageW(progress_bar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtrW(progress_bar, GWL_STYLE, style & ~((LONG_PTR)PBS_MARQUEE));
        SendMessageW(progress_bar, PBM_SETPOS, percent, 0);
    }
}

static void start_task(Task *task) {
    InterlockedExchange(&cancelled, 0);
    EnterCriticalSection(&task_lock);
    next_task = *task;
    LeaveCriticalSection(&task_lock);
    set_busy(TRUE);
    if (task->command == ID_SCAN)
        SetWindowTextW(status_label, L"正在读取本机硬件…");
    set_progress(0);
    SetWindowTextW(transfer_label, L"等待任务进度…");
    SetEvent(task_event);
}

static BOOL candidate_selectable(const Candidate *row) {
    return row->local_available || (row->index >= 0 && !row->processed);
}

static void start_local_reinstall(void) {
    if (busy) return;
    Task task = {.command = ID_REINSTALL_LOCAL, .install = TRUE};
    task.pre_backup = SendMessageW(pre_backup_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(backup_edit, task.backup_root, ARRAYSIZE(task.backup_root));
    GetWindowTextW(folder_edit, task.folder, ARRAYSIZE(task.folder));
    if (!update_process_elevated()) {
        MessageBoxW(main_window, L"本机驱动匹配安装需要管理员权限。", L"安装权限", MB_OK); return;
    }
    if (!task.folder[0] || PathIsRelativeW(task.folder) || wcslen(task.folder) > 150 ||
        (task.pre_backup && (!task.backup_root[0] || PathIsRelativeW(task.backup_root) || wcslen(task.backup_root) > 150))) {
        MessageBoxW(main_window, L"请选择完整且较短的日志保存目录及备份目录。", L"保存位置", MB_OK); return;
    }
    int selected_devices = 0;
    for (int i = 0; i < candidate_count; ++i) {
        Candidate *row = candidates[i];
        if (!ListView_GetCheckState(update_list, i) || !row->local_available || row->device_index < 0) continue;
        ++selected_devices;
        BOOL duplicate = FALSE;
        for (int earlier = 0; earlier < i; ++earlier) {
            Candidate *previous = candidates[earlier];
            if (ListView_GetCheckState(update_list, earlier) && previous->local_available &&
                !_wcsicmp(row->local_inf, previous->local_inf)) duplicate = TRUE;
        }
        if (!duplicate) task.indices[task.count++] = row->device_index;
    }
    if (!task.count) {
        MessageBoxW(main_window, L"请勾选可从本机驱动库匹配安装的设备。Windows 系统自带包由系统维护。", L"选择设备", MB_OK); return;
    }
    WCHAR message[1024];
    StringCchPrintfW(message, ARRAYSIZE(message),
        L"已选择 %d 个设备，对应 %d 个本机第三方驱动包。\n\n将使用设备当前登记的驱动包重新提交系统匹配安装。Windows 可能保留当前同版本，不强制降级或替换。%s\n\n继续？",
        selected_devices, task.count, task.pre_backup ? L"安装前会备份现有第三方驱动。" : L"");
    if (MessageBoxW(main_window, message, L"本机驱动匹配安装", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    int error = SHCreateDirectoryExW(main_window, task.folder, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) {
        MessageBoxW(main_window, L"无法创建日志目录。", L"保存位置", MB_OK); return;
    }
    start_task(&task);
}

static void start_download(void) {
    Task task = { .command = ID_DOWNLOAD };
    if (busy) return;
    task.source = (int)SendMessageW(source_box, CB_GETCURSEL, 0, 0);
    task.install = SendMessageW(auto_install_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    task.pre_backup = SendMessageW(pre_backup_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(backup_edit, task.backup_root, ARRAYSIZE(task.backup_root));
    if (task.install && task.pre_backup &&
        (!task.backup_root[0] || PathIsRelativeW(task.backup_root) || wcslen(task.backup_root) > 150)) {
        MessageBoxW(main_window, L"请在“驱动备份”中选择完整且较短的备份目录。", L"备份位置", MB_OK);
        return;
    }
    if (task.install && !IsUserAnAdmin()) {
        MessageBoxW(main_window, L"自动安装驱动需要管理员权限，请以管理员身份运行程序。", L"安装权限", MB_OK | MB_ICONINFORMATION);
        return;
    }
    GetWindowTextW(folder_edit, task.folder, ARRAYSIZE(task.folder));
    if (!task.folder[0] || PathIsRelativeW(task.folder)) {
        MessageBoxW(main_window, L"请选择完整的驱动保存路径。", L"保存位置", MB_OK | MB_ICONINFORMATION);
        return;
    }
    /* Reserve enough space for unique package subdirectories and file names. */
    if (wcslen(task.folder) > 150) {
        MessageBoxW(main_window, L"请选择较短的保存路径（150 字以内）。", L"保存位置", MB_OK);
        return;
    }
    for (int row_index = 0; row_index < candidate_count; ++row_index) {
        if (!ListView_GetCheckState(update_list, row_index)) continue;
        Candidate *row = candidates[row_index];
        if (row->processed || row->index < 0) continue;
        BOOL duplicate = FALSE;
        for (int i = 0; i < task.count; ++i) if (task.indices[i] == row->index) duplicate = TRUE;
        if (!duplicate) task.indices[task.count++] = row->index;
    }
    if (!task.count) {
        MessageBoxW(main_window, L"请勾选已有厂商安装包的项目。已安装但暂无下载源的设备，可查看本机匹配安装选项。", L"选择驱动", MB_OK);
        return;
    }
    int error = SHCreateDirectoryExW(main_window, task.folder, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) {
        MessageBoxW(main_window, L"无法创建保存目录，请选择有写入权限的位置。", L"保存位置", MB_OK | MB_ICONERROR);
        return;
    }
    DWORD attributes = GetFileAttributesW(task.folder);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(main_window, L"保存位置需要是文件夹。", L"保存位置", MB_OK | MB_ICONERROR);
        return;
    }
    start_task(&task);
}

static BOOL select_folder(const WCHAR *title, WCHAR *path) {
    BROWSEINFOW info = {0};
    info.hwndOwner = main_window;
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    BOOL selected = FALSE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item) {
        selected = SHGetPathFromIDListW(item, path);
        CoTaskMemFree(item);
    }
    return selected;
}

static void choose_folder(HWND edit) {
    WCHAR path[MAX_PATH];
    if (select_folder(L"选择保存文件夹", path)) SetWindowTextW(edit, path);
}

static void open_directory(HWND edit) {
    WCHAR path[MAX_PATH];
    GetWindowTextW(edit, path, ARRAYSIZE(path));
    if (!path[0] || PathIsRelativeW(path)) return;
    int error = SHCreateDirectoryExW(main_window, path, NULL);
    if (error == ERROR_SUCCESS || error == ERROR_ALREADY_EXISTS)
        ShellExecuteW(main_window, L"open", path, NULL, NULL, SW_SHOWNORMAL);
    else MessageBoxW(main_window, L"无法打开目录，请检查路径和写入权限。", L"文件夹", MB_OK | MB_ICONERROR);
}

static void start_backup_task(int command) {
    if (busy) return;
    Task task = { .command = command };
    GetWindowTextW(backup_edit, task.backup_root, ARRAYSIZE(task.backup_root));
    if (!task.backup_root[0] || PathIsRelativeW(task.backup_root) || wcslen(task.backup_root) > 150) {
        MessageBoxW(main_window, L"请选择完整且较短的备份保存目录（150 字以内）。", L"备份位置", MB_OK);
        return;
    }
    if (command == ID_RESTORE) {
        if (!select_folder(L"选择要导入的完整备份文件夹（Backup-日期-GUID）", task.folder)) return;
        WCHAR marker[MAX_PATH];
        if (FAILED(StringCchPrintfW(marker, ARRAYSIZE(marker), L"%s\\backup-complete.txt", task.folder)) ||
            GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(main_window, L"所选目录没有完整备份标记，请选择本软件成功导出的 Backup 文件夹。", L"选择备份", MB_OK);
            return;
        }
        task.pre_backup = SendMessageW(pre_backup_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (MessageBoxW(main_window, L"将从所选备份导入驱动，并由 Windows 为兼容设备安装匹配项。继续？",
                        L"导入备份驱动", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    start_task(&task);
}

static void copy_text(const WCHAR *text) {
    SIZE_T size = (wcslen(text) + 1) * sizeof(WCHAR);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    void *data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return; }
    memcpy(data, text, size);
    GlobalUnlock(memory);
    if (OpenClipboard(main_window)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        CloseClipboard();
    } else GlobalFree(memory);
}

static void copy_hardware_id(void) {
    int selected = ListView_GetNextItem(hardware_list, -1, LVNI_SELECTED);
    WCHAR text[2048];
    if (selected < 0) return;
    ListView_GetItemText(hardware_list, selected, 1, text, ARRAYSIZE(text));
    copy_text(text);
}

static int source_index(const char *key) {
    for (size_t index = 0; index < ARRAYSIZE(driver_sources); ++index)
        if (!strcmp(driver_sources[index].key, key)) return (int)index;
    return -1;
}

static void clear_hardware(void) {
    for (size_t index = 0; index < hardware_count; ++index) free(hardware_rows[index]);
    hardware_count = 0;
}

static BOOL remember_hardware(Hardware *row) {
    if (hardware_count == hardware_capacity) {
        size_t capacity = hardware_capacity ? hardware_capacity * 2 : 256;
        Hardware **rows = realloc(hardware_rows, capacity * sizeof(*rows));
        if (!rows) return FALSE;
        hardware_rows = rows; hardware_capacity = capacity;
    }
    hardware_rows[hardware_count++] = row;
    return TRUE;
}

static void render_sources(void) {
    ListView_DeleteAllItems(source_list);
    for (int index = 0; index < (int)ARRAYSIZE(driver_sources); ++index) {
        if (filtering_sources && !source_filter[index]) continue;
        LVITEMW item = { .mask = LVIF_TEXT | LVIF_PARAM, .iItem = ListView_GetItemCount(source_list),
            .pszText = (WCHAR *)driver_sources[index].name, .lParam = index };
        int row = ListView_InsertItem(source_list, &item);
        ListView_SetItemText(source_list, row, 1, (WCHAR *)driver_sources[index].devices);
        ListView_SetItemText(source_list, row, 2, (WCHAR *)driver_sources[index].mode);
        ListView_SetItemText(source_list, row, 3, source_status[index][0] ? source_status[index] : L"待检查");
    }
}

static void select_device_sources(void) {
    if (busy) return;
    int selected = ListView_GetNextItem(hardware_list, -1, LVNI_SELECTED);
    LVITEMW item = { .mask = LVIF_PARAM, .iItem = selected };
    if (selected < 0 || !ListView_GetItem(hardware_list, &item)) {
        MessageBoxW(main_window, L"请先选择一个本机设备。", L"设备来源", MB_OK);
        return;
    }
    Hardware *row = (Hardware *)item.lParam;
    memset(source_filter, 0, sizeof(source_filter));
    for (int index = 0; index < row->identity.match.source_count; ++index) {
        int provider = source_index(row->identity.match.sources[index]);
        if (provider >= 0) source_filter[provider] = TRUE;
    }
    filtering_sources = TRUE;
    WCHAR text[1024];
    StringCchPrintfW(text, ARRAYSIZE(text), L"设备来源建议 · %s · %s", row->name, row->identity.basis);
    SetWindowTextW(source_context, text);
    render_sources();
    TabCtrl_SetCurSel(tab_control, 2);
}

static void utf8_text(const char *text, WCHAR *out, int chars) {
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, out, chars)) out[0] = 0;
}

static void show_tab(void) {
    int tab = TabCtrl_GetCurSel(tab_control);
    ShowWindow(update_list, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(hardware_list, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(database_summary, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(device_sources_button, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(source_context, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(all_sources_button, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(source_list, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(all_button, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(copy_button, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(check_sources_button, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(open_source_button, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(auto_install_box, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(reinstall_button, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(diagnostics_edit, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(diagnostics_button, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(copy_diagnostics_button, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(open_logs_button, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(pre_backup_box, tab == 0 || tab == 3 ? SW_SHOW : SW_HIDE);
    HWND backup_controls[] = { backup_description, backup_edit, backup_folder_button,
        backup_button, open_backup_button, restore_button, restore_description };
    for (size_t index = 0; index < ARRAYSIZE(backup_controls); ++index)
        ShowWindow(backup_controls[index], tab == 3 ? SW_SHOW : SW_HIDE);
}

static void add_column(HWND list, int index, const WCHAR *title, int width) {
    LVCOLUMNW column = { .mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM };
    column.pszText = (WCHAR *)title;
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

static HWND control(HWND parent, const WCHAR *class_name, const WCHAR *text,
                    DWORD style, int id) {
    HWND window = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(window, WM_SETFONT, (WPARAM)ui_font, TRUE);
    return window;
}

static void create_controls(HWND window) {
    NONCLIENTMETRICSW metrics = { .cbSize = sizeof(metrics) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    ui_font = CreateFontIndirectW(&metrics.lfMessageFont);
    scan_button = control(window, L"BUTTON", L"重新检测并匹配", WS_TABSTOP, ID_SCAN);
    source_box = control(window, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 0);
    SendMessageW(source_box, CB_ADDSTRING, 0, (LPARAM)L"厂商官方驱动包 · 自动硬件匹配 · 独立下载");
    SendMessageW(source_box, CB_SETCURSEL, 0, 0);
    cancel_button = control(window, L"BUTTON", L"取消", WS_TABSTOP, ID_CANCEL);
    tab_control = control(window, WC_TABCONTROLW, L"", WS_TABSTOP, ID_TAB);
    TCITEMW tab = { .mask = TCIF_TEXT, .pszText = L"驱动管理" };
    TabCtrl_InsertItem(tab_control, 0, &tab);
    tab.pszText = L"本机硬件";
    TabCtrl_InsertItem(tab_control, 1, &tab);
    tab.pszText = L"驱动来源";
    TabCtrl_InsertItem(tab_control, 2, &tab);
    tab.pszText = L"驱动备份";
    TabCtrl_InsertItem(tab_control, 3, &tab);
    tab.pszText = L"系统诊断";
    TabCtrl_InsertItem(tab_control, 4, &tab);
    update_list = control(window, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0);
    ListView_SetExtendedListViewStyle(update_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    add_column(update_list, 0, L"本机设备 / 安装包", 290);
    add_column(update_list, 1, L"驱动状态", 120);
    add_column(update_list, 2, L"当前版本", 145);
    add_column(update_list, 3, L"可用版本", 145);
    add_column(update_list, 4, L"建议 / 可用操作", 250);
    add_column(update_list, 5, L"厂商 / 包来源", 180);
    add_column(update_list, 6, L"处理结果", 280);
    add_column(update_list, 7, L"硬件 ID", 300);
    hardware_list = control(window, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0);
    ListView_SetExtendedListViewStyle(hardware_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    add_column(hardware_list, 0, L"本机设备", 280);
    add_column(hardware_list, 1, L"硬件 ID", 390);
    add_column(hardware_list, 2, L"当前驱动版本", 140);
    add_column(hardware_list, 3, L"设备状态", 120);
    add_column(hardware_list, 4, L"离线识别结果 / 型号候选", 360);
    add_column(hardware_list, 5, L"设备类别", 140);
    add_column(hardware_list, 6, L"识别依据", 230);
    add_column(hardware_list, 7, L"来源建议", 300);
    add_column(hardware_list, 8, L"厂商安装包收录状态", 290);
    add_column(hardware_list, 9, L"驱动提供商", 180);
    add_column(hardware_list, 10, L"已安装 INF", 140);
    add_column(hardware_list, 11, L"驱动日期", 150);
    database_summary = control(window, L"STATIC", L"正在加载离线识别库…", 0, 0);
    device_sources_button = control(window, L"BUTTON", L"查看设备来源建议", WS_TABSTOP, ID_DEVICE_SOURCES);
    source_list = control(window, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0);
    ListView_SetExtendedListViewStyle(source_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    add_column(source_list, 0, L"来源", 150);
    add_column(source_list, 1, L"覆盖设备", 260);
    add_column(source_list, 2, L"接入方式", 200);
    add_column(source_list, 3, L"入口连通性", 170);
    source_context = control(window, L"STATIC", L"全部来源 · 选择具体设备可查看来源建议，型号和系统版本需核对", 0, 0);
    all_sources_button = control(window, L"BUTTON", L"显示全部来源", WS_TABSTOP, ID_ALL_SOURCES);
    render_sources();
    check_sources_button = control(window, L"BUTTON", L"检查来源连通性", WS_TABSTOP, ID_CHECK_SOURCES);
    open_source_button = control(window, L"BUTTON", L"打开所选官方来源", WS_TABSTOP, ID_OPEN_SOURCE);
    auto_install_box = control(window, L"BUTTON", L"下载后自动安装", WS_TABSTOP | BS_AUTOCHECKBOX, ID_AUTO_INSTALL);
    SendMessageW(auto_install_box, BM_SETCHECK, BST_CHECKED, 0);
    all_button = control(window, L"BUTTON", L"全选可操作项", WS_TABSTOP, ID_ALL);
    reinstall_button = control(window, L"BUTTON", L"本机驱动匹配安装", WS_TABSTOP, ID_REINSTALL_LOCAL);
    copy_button = control(window, L"BUTTON", L"复制所选硬件 ID", WS_TABSTOP, ID_COPY);
    folder_edit = control(window, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 0);
    folder_button = control(window, L"BUTTON", L"保存位置…", WS_TABSTOP, ID_FOLDER);
    open_button = control(window, L"BUTTON", L"打开文件夹", WS_TABSTOP, ID_OPEN);
    download_button = control(window, L"BUTTON", L"下载并安装", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_DOWNLOAD);
    pre_backup_box = control(window, L"BUTTON", L"安装前备份现有驱动", WS_TABSTOP | BS_AUTOCHECKBOX, ID_PRE_BACKUP);
    SendMessageW(pre_backup_box, BM_SETCHECK, BST_CHECKED, 0);
    backup_description = control(window, L"STATIC",
        L"备份当前系统的第三方驱动包，按时间保存独立副本。安装前默认先备份，备份失败会暂停后续安装。", 0, 0);
    backup_edit = control(window, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 0);
    backup_folder_button = control(window, L"BUTTON", L"备份位置…", WS_TABSTOP, ID_BACKUP_FOLDER);
    backup_button = control(window, L"BUTTON", L"备份当前驱动", WS_TABSTOP, ID_BACKUP);
    open_backup_button = control(window, L"BUTTON", L"打开备份目录", WS_TABSTOP, ID_OPEN_BACKUP);
    restore_button = control(window, L"BUTTON", L"导入备份驱动", WS_TABSTOP, ID_RESTORE);
    restore_description = control(window, L"STATIC",
        L"备份内容为第三方驱动包。导入使用 Windows 驱动匹配规则，如需降级，可在设备管理器回退驱动。系统自带驱动由 Windows 维护。", 0, 0);
    diagnostics_edit = control(window, L"EDIT", L"正在读取系统诊断…",
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 0);
    diagnostics_button = control(window, L"BUTTON", L"刷新诊断", WS_TABSTOP, ID_DIAGNOSTICS);
    copy_diagnostics_button = control(window, L"BUTTON", L"复制诊断", WS_TABSTOP, ID_COPY_DIAGNOSTICS);
    open_logs_button = control(window, L"BUTTON", L"打开日志目录", WS_TABSTOP, ID_OPEN_LOGS);
    transfer_label = control(window, L"STATIC", L"等待传输统计", 0, 0);
    progress_bar = control(window, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0);
    SendMessageW(progress_bar, PBM_SETRANGE32, 0, 100);
    status_label = control(window, L"STATIC", L"正在检测本机硬件…", 0, 0);
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, app_directory, ARRAYSIZE(app_directory));
    if (length && length < ARRAYSIZE(app_directory) && PathRemoveFileSpecW(app_directory)) {
        if (PathCombineW(path, app_directory, L"下载")) SetWindowTextW(folder_edit, path);
        if (PathCombineW(path, app_directory, L"驱动备份")) SetWindowTextW(backup_edit, path);
    }
    show_tab();
    set_busy(FALSE);
}

static void resize_controls(int width, int height) {
    MoveWindow(scan_button, 16, 16, 155, 32, TRUE);
    MoveWindow(source_box, 185, 18, width - 305, 180, TRUE);
    MoveWindow(cancel_button, width - 104, 16, 88, 32, TRUE);
    MoveWindow(tab_control, 16, 62, width - 32, height - 280, TRUE);
    MoveWindow(update_list, 24, 96, width - 48, height - 323, TRUE);
    MoveWindow(hardware_list, 24, 142, width - 48, height - 369, TRUE);
    MoveWindow(database_summary, 32, 102, width - 64, 38, TRUE);
    MoveWindow(device_sources_button, 205, height - 210, 200, 28, TRUE);
    MoveWindow(source_list, 24, 136, width - 48, height - 363, TRUE);
    MoveWindow(source_context, 32, 102, width - 64, 32, TRUE);
    MoveWindow(all_sources_button, 400, height - 210, 160, 28, TRUE);
    MoveWindow(check_sources_button, 16, height - 210, 165, 28, TRUE);
    MoveWindow(open_source_button, 192, height - 210, 190, 28, TRUE);
    MoveWindow(auto_install_box, 205, height - 210, 180, 28, TRUE);
    MoveWindow(pre_backup_box, 420, height - 210, 230, 28, TRUE);
    MoveWindow(reinstall_button, width - 200, height - 210, 184, 28, TRUE);
    MoveWindow(all_button, 16, height - 210, 160, 28, TRUE);
    MoveWindow(copy_button, 16, height - 210, 180, 28, TRUE);
    MoveWindow(backup_description, 36, 112, width - 72, 54, TRUE);
    MoveWindow(backup_edit, 36, 176, width - 215, 28, TRUE);
    MoveWindow(backup_folder_button, width - 165, 176, 125, 28, TRUE);
    MoveWindow(backup_button, 36, 220, 165, 32, TRUE);
    MoveWindow(open_backup_button, 218, 220, 165, 32, TRUE);
    MoveWindow(restore_button, 400, 220, 165, 32, TRUE);
    MoveWindow(restore_description, 36, 270, width - 72, 75, TRUE);
    MoveWindow(diagnostics_edit, 28, 104, width - 56, height - 332, TRUE);
    MoveWindow(diagnostics_button, 16, height - 210, 135, 28, TRUE);
    MoveWindow(copy_diagnostics_button, 165, height - 210, 135, 28, TRUE);
    MoveWindow(open_logs_button, 314, height - 210, 160, 28, TRUE);
    MoveWindow(folder_edit, 16, height - 170, width - 398, 28, TRUE);
    MoveWindow(folder_button, width - 370, height - 170, 100, 28, TRUE);
    MoveWindow(open_button, width - 258, height - 170, 100, 28, TRUE);
    MoveWindow(download_button, width - 146, height - 170, 130, 28, TRUE);
    MoveWindow(transfer_label, 16, height - 130, width - 32, 24, TRUE);
    MoveWindow(progress_bar, 16, height - 104, width - 32, 14, TRUE);
    MoveWindow(status_label, 16, height - 80, width - 32, 70, TRUE);
}

static void process_event(Event *event) {
    if (event->kind == EV_RESET) {
        ListView_DeleteAllItems(hardware_list);
        clear_hardware();
        ListView_DeleteAllItems(update_list);
        clear_candidates();
        filtering_sources = FALSE;
        SetWindowTextW(source_context, L"全部来源 · 选择具体设备可查看来源建议，型号和系统版本需核对");
        render_sources();
    } else if (event->kind == EV_HARDWARE) {
        Hardware *row = event->data;
        if (remember_hardware(row)) {
            event->data = NULL;
            LVITEMW item = { .mask = LVIF_TEXT | LVIF_PARAM, .iItem = ListView_GetItemCount(hardware_list),
                .pszText = row->name, .lParam = (LPARAM)row };
            int index = ListView_InsertItem(hardware_list, &item);
            ListView_SetItemText(hardware_list, index, 1, row->hardware);
            ListView_SetItemText(hardware_list, index, 2, row->version);
            ListView_SetItemText(hardware_list, index, 3, row->state);
            WCHAR model[1024], detail[1200], sources[1024] = L"";
            utf8_text(row->identity.match.model[0] ? row->identity.match.model : row->identity.match.vendor,
                      model, ARRAYSIZE(model));
            if (row->identity.match.tablet_candidates)
                StringCchPrintfW(detail, ARRAYSIZE(detail), L"%s%s（共 %d 个型号候选）", model,
                    row->identity.match.tablet_candidates > 3 ? L" …" : L"", row->identity.match.tablet_candidates);
            else StringCchCopyW(detail, ARRAYSIZE(detail), model[0] ? model : L"型号待补充");
            ListView_SetItemText(hardware_list, index, 4, detail);
            ListView_SetItemText(hardware_list, index, 5, row->identity.category);
            ListView_SetItemText(hardware_list, index, 6, row->identity.basis);
            for (int key = 0; key < row->identity.match.source_count; ++key) {
                int provider = source_index(row->identity.match.sources[key]);
                if (provider < 0) continue;
                if (sources[0]) StringCchCatW(sources, ARRAYSIZE(sources), L" / ");
                StringCchCatW(sources, ARRAYSIZE(sources), driver_sources[provider].name);
            }
            ListView_SetItemText(hardware_list, index, 7, sources[0] ? sources : L"请查看全部厂商来源");
            ListView_SetItemText(hardware_list, index, 8, row->package_status);
            ListView_SetItemText(hardware_list, index, 9, row->driver.provider);
            ListView_SetItemText(hardware_list, index, 10, row->driver.published_inf);
            ListView_SetItemText(hardware_list, index, 11, row->driver.date);
        }
    } else if (event->kind == EV_UPDATE) {
        Candidate *row = event->data;
        if (candidate_count < MAX_UPDATES) {
            int index = candidate_count++;
            candidates[index] = row;
            event->data = NULL;
            LVITEMW item = { .mask = LVIF_TEXT, .iItem = index, .pszText = row->title };
            ListView_InsertItem(update_list, &item);
            ListView_SetItemText(update_list, index, 1, row->driver_state);
            ListView_SetItemText(update_list, index, 2, row->current_version);
            ListView_SetItemText(update_list, index, 3, row->available_version);
            ListView_SetItemText(update_list, index, 4, row->advice);
            ListView_SetItemText(update_list, index, 5, row->provider);
            ListView_SetItemText(update_list, index, 6, L"尚未执行");
            ListView_SetItemText(update_list, index, 7, row->hardware);
        }
    } else if (event->kind == EV_STATUS) {
        SetWindowTextW(status_label, event->text);
    } else if (event->kind == EV_PROGRESS) {
        set_progress(event->index);
    } else if (event->kind == EV_ITEM_STATUS || event->kind == EV_SUBMITTED) {
        for (int index = 0; index < candidate_count; ++index) {
            if (candidates[index]->index != event->index) continue;
            ListView_SetItemText(update_list, index, 6, event->text);
            if (event->kind == EV_SUBMITTED) {
                candidates[index]->processed = TRUE;
                ListView_SetItemText(update_list, index, 4, L"已提交安装，请重新检测刷新版本");
                ListView_SetCheckState(update_list, index, FALSE);
            }
        }
    } else if (event->kind == EV_LOCAL_STATUS) {
        const WCHAR *inf = NULL;
        for (int i = 0; i < candidate_count; ++i)
            if (candidates[i]->device_index == event->index) { inf = candidates[i]->local_inf; break; }
        if (inf && *inf) for (int i = 0; i < candidate_count; ++i) {
            if (!candidates[i]->local_available || _wcsicmp(candidates[i]->local_inf, inf)) continue;
            ListView_SetItemText(update_list, i, 6, event->text);
            ListView_SetCheckState(update_list, i, FALSE);
        }
    } else if (event->kind == EV_TRANSFER) {
        SetWindowTextW(transfer_label, event->text);
    } else if (event->kind == EV_DIAGNOSTICS) {
        SetWindowTextW(diagnostics_edit, (WCHAR *)event->data);
    } else if (event->kind == EV_DATABASE) {
        SetWindowTextW(database_summary, event->text);
    } else if (event->kind == EV_SOURCE_STATUS) {
        if (event->index >= 0 && event->index < (int)ARRAYSIZE(driver_sources)) {
            StringCchCopyW(source_status[event->index], ARRAYSIZE(source_status[0]), event->text);
            for (int row = 0; row < ListView_GetItemCount(source_list); ++row) {
                LVITEMW item = { .mask = LVIF_PARAM, .iItem = row };
                if (ListView_GetItem(source_list, &item) && item.lParam == event->index)
                    ListView_SetItemText(source_list, row, 3, event->text);
            }
        }
    } else if (event->kind == EV_DONE) {
        if (!InterlockedCompareExchange(&quitting, 0, 0)) {
            set_busy(FALSE);
            SetWindowTextW(transfer_label, L"任务已结束，详情见下方结果");
        }
    } else if (event->kind == EV_EXIT) {
        DestroyWindow(main_window);
    }
    if (event->data) {
        free(event->data);
    }
    free(event);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CREATE) {
        main_window = window;
        create_controls(window);
        return 0;
    }
    if (message == WM_SIZE) {
        resize_controls(LOWORD(lp), HIWORD(lp));
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        MINMAXINFO *limits = (MINMAXINFO *)lp;
        limits->ptMinTrackSize.x = 1000;
        limits->ptMinTrackSize.y = 640;
        return 0;
    }
    if (message == WM_NOTIFY && ((NMHDR *)lp)->hwndFrom == update_list &&
        ((NMHDR *)lp)->code == LVN_ITEMCHANGING) {
        NMLISTVIEW *change = (NMLISTVIEW *)lp;
        if (change->iItem >= 0 && change->iItem < candidate_count && (change->uChanged & LVIF_STATE) &&
            (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2) &&
            !candidate_selectable(candidates[change->iItem])) return TRUE;
    }
    if (message == WM_NOTIFY && ((NMHDR *)lp)->hwndFrom == tab_control &&
        ((NMHDR *)lp)->code == TCN_SELCHANGE) {
        show_tab();
        return 0;
    }
    if (message == EVENT_MESSAGE) {
        process_event((Event *)lp);
        return 0;
    }
    if (message == WM_COMMAND) {
        if ((HWND)lp == source_box && HIWORD(wp) == CBN_SELCHANGE && !busy) {
            Task task = { .command = ID_SCAN, .source = (int)SendMessageW(source_box, CB_GETCURSEL, 0, 0) };
            start_task(&task);
            return 0;
        }
        switch (LOWORD(wp)) {
        case ID_SCAN:
            if (!busy) {
                Task task = { .command = ID_SCAN, .source = (int)SendMessageW(source_box, CB_GETCURSEL, 0, 0) };
                start_task(&task);
            }
            break;
        case ID_DOWNLOAD: start_download(); break;
        case ID_REINSTALL_LOCAL: start_local_reinstall(); break;
        case ID_AUTO_INSTALL:
            SetWindowTextW(download_button, SendMessageW(auto_install_box, BM_GETCHECK, 0, 0) == BST_CHECKED ?
                           L"下载并安装" : L"仅下载");
            break;
        case ID_CHECK_SOURCES:
            if (!busy) {
                Task task = { .command = ID_CHECK_SOURCES };
                for (size_t index = 0; index < ARRAYSIZE(driver_sources); ++index)
                    task.source_mask[index] = !filtering_sources || source_filter[index];
                SetWindowTextW(status_label, L"正在检查官方来源入口连通性…");
                start_task(&task);
            }
            break;
        case ID_OPEN_SOURCE: {
            int selected = ListView_GetNextItem(source_list, -1, LVNI_SELECTED);
            LVITEMW item = { .mask = LVIF_PARAM, .iItem = selected };
            if (selected >= 0 && ListView_GetItem(source_list, &item) &&
                item.lParam >= 0 && item.lParam < (LPARAM)ARRAYSIZE(driver_sources))
                ShellExecuteW(window, L"open", driver_sources[item.lParam].url, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_CANCEL:
            InterlockedExchange(&cancelled, 1);
            EnableWindow(cancel_button, FALSE);
            SetWindowTextW(status_label, L"已请求取消，正在等待当前系统操作结束…");
            break;
        case ID_FOLDER: if (!busy) choose_folder(folder_edit); break;
        case ID_OPEN: open_directory(folder_edit); break;
        case ID_BACKUP_FOLDER: if (!busy) choose_folder(backup_edit); break;
        case ID_OPEN_BACKUP: open_directory(backup_edit); break;
        case ID_BACKUP: start_backup_task(ID_BACKUP); break;
        case ID_RESTORE: start_backup_task(ID_RESTORE); break;
        case ID_COPY: copy_hardware_id(); break;
        case ID_DIAGNOSTICS:
            if (!busy) {
                Task task = { .command = ID_DIAGNOSTICS };
                start_task(&task);
            }
            break;
        case ID_COPY_DIAGNOSTICS: {
            WCHAR text[8192];
            GetWindowTextW(diagnostics_edit, text, ARRAYSIZE(text));
            copy_text(text);
            break;
        }
        case ID_OPEN_LOGS: {
            WCHAR path[MAX_PATH];
            if (SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\logs", app_directory)))
                ShellExecuteW(window, L"open", path, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_DEVICE_SOURCES:
            select_device_sources();
            show_tab();
            break;
        case ID_ALL_SOURCES:
            if (!busy) {
                filtering_sources = FALSE;
                SetWindowTextW(source_context, L"全部来源 · 选择具体设备可查看来源建议，型号和系统版本需核对");
                render_sources();
            }
            break;
        case ID_ALL: {
            BOOL all = TRUE;
            for (int index = 0; index < candidate_count; ++index)
                if (candidate_selectable(candidates[index]) && !ListView_GetCheckState(update_list, index)) all = FALSE;
            for (int index = 0; index < candidate_count; ++index)
                ListView_SetCheckState(update_list, index, !all && candidate_selectable(candidates[index]));
            break;
        }
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (InterlockedExchange(&quitting, 1)) return 0;
        InterlockedExchange(&cancelled, 1);
        EnableWindow(window, FALSE);
        SetWindowTextW(status_label, L"正在等待当前下载请求或系统工具结束，请稍候…");
        SetEvent(task_event);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}

static int inventory_ui_test(void) {
    Hardware hardware = {0};
    StringCchCopyW(hardware.name, ARRAYSIZE(hardware.name), L"Installed device without catalog source");
    StringCchCopyW(hardware.hardware, ARRAYSIZE(hardware.hardware), L"PCI\\VEN_1234&DEV_5678");
    StringCchCopyW(hardware.version, ARRAYSIZE(hardware.version), L"31.0.15.5161");
    StringCchCopyW(hardware.driver.provider, ARRAYSIZE(hardware.driver.provider), L"Example provider");
    Candidate *first = device_candidate(&hardware, -1, FALSE, TRUE, DN_DRIVER_LOADED, 0, 0);
    Event *event = calloc(1, sizeof(*event));
    if (!first || !event) { free(first); free(event); return 20; }
    event->kind = EV_UPDATE; event->data = first;
    process_event(event);
    set_busy(FALSE);
    WCHAR text[128];
    if (ListView_GetItemCount(update_list) != 1) return 21;
    ListView_GetItemText(update_list, 0, 1, text, ARRAYSIZE(text));
    if (wcscmp(text, L"已安装")) return 22;
    ListView_GetItemText(update_list, 0, 2, text, ARRAYSIZE(text));
    if (wcscmp(text, hardware.version)) return 23;
    ListView_GetItemText(update_list, 0, 3, text, ARRAYSIZE(text));
    if (wcscmp(text, L"暂无已核验版本")) return 24;
    if (IsWindowEnabled(download_button) || IsWindowEnabled(reinstall_button)) return 25;
    ListView_SetCheckState(update_list, 0, TRUE);
    if (ListView_GetCheckState(update_list, 0)) return 26;
    hardware.driver.local_available = TRUE;
    StringCchCopyW(hardware.driver.published_inf, ARRAYSIZE(hardware.driver.published_inf), L"oem123.inf");
    Candidate *second = device_candidate(&hardware, -1, FALSE, TRUE, DN_DRIVER_LOADED, 0, 1);
    event = calloc(1, sizeof(*event));
    if (!second || !event) { free(second); free(event); return 27; }
    event->kind = EV_UPDATE; event->data = second; process_event(event); set_busy(FALSE);
    if (ListView_GetItemCount(update_list) != 2 || IsWindowEnabled(download_button) || !IsWindowEnabled(reinstall_button)) return 28;
    SendMessageW(main_window, WM_COMMAND, ID_ALL, 0);
    if (ListView_GetCheckState(update_list, 0) || !ListView_GetCheckState(update_list, 1)) return 29;
    SendMessageW(main_window, WM_COMMAND, ID_ALL, 0);
    if (ListView_GetCheckState(update_list, 1)) return 30;
    ListView_DeleteAllItems(update_list); clear_candidates(); set_busy(FALSE);
    return 0;
}

static int window_smoke_test(void) {
    WCHAR expected[MAX_PATH], actual[MAX_PATH];
    if (!folder_edit || !backup_edit || !transfer_label || !progress_bar || !backup_button ||
        !restore_button || !pre_backup_box || !diagnostics_edit || !diagnostics_button || !reinstall_button) return 5;
    if (!PathCombineW(expected, app_directory, L"下载")) return 6;
    GetWindowTextW(folder_edit, actual, ARRAYSIZE(actual));
    if (wcscmp(expected, actual)) return 7;
    if (!PathCombineW(expected, app_directory, L"驱动备份")) return 8;
    GetWindowTextW(backup_edit, actual, ARRAYSIZE(actual));
    if (wcscmp(expected, actual)) return 9;
    if (SendMessageW(pre_backup_box, BM_GETCHECK, 0, 0) != BST_CHECKED) return 10;
    HardwareDatabase database;
    WCHAR summary[1024];
    HRESULT hr = hardware_database_open(&database, app_directory, summary, ARRAYSIZE(summary));
    BOOL loaded = hr == S_OK && database.index.usb_products.count > 10000 &&
                  database.index.pci_devices.count > 10000 && database.index.tablet_count > 400;
    hardware_database_close(&database);
    if (!loaded) return 11;
    HRESULT catalog_hr = load_vendor_catalog();
    BOOL packages_loaded = SUCCEEDED(catalog_hr) && vendor_catalog.count >= 2;
    free(vendor_catalog_text); vendor_catalog_text = NULL;
    ZeroMemory(&vendor_catalog, sizeof(vendor_catalog));
    if (!packages_loaded) return 13;
    HRESULT oem_hr = load_oem_catalog();
    BOOL oem_loaded = SUCCEEDED(oem_hr) && oem_catalog.count > 3000;
    free(oem_catalog_text); oem_catalog_text = NULL; ZeroMemory(&oem_catalog, sizeof(oem_catalog));
    if (!oem_loaded) return 15;
    if (SendMessageW(source_box, CB_GETCOUNT, 0, 0) != 1) return 14;
    if (ListView_GetItemCount(source_list) != (int)ARRAYSIZE(driver_sources)) return 12;
    return inventory_ui_test();
}

static int self_test(void) {
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return 1;
    SP_DEVINFO_DATA data = { .cbSize = sizeof(data) };
    BOOL found = SetupDiEnumDeviceInfo(set, 0, &data);
    SetupDiDestroyDeviceInfoList(set);
    if (!found) return 2;
    detect_platform();
    return windows_build ? 0 : 3;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    if (!wcscmp(command, L"--self-test")) {
        int result = self_test();
        CoUninitialize();
        return result;
    }
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&controls);
    InitializeCriticalSection(&task_lock);
    task_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!task_event) { CoUninitialize(); return 1; }
    WNDCLASSW type = {0};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.lpszClassName = L"DriverSourceNative";
    type.hCursor = LoadCursorW(NULL, IDC_ARROW);
    type.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    type.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&type);
    WCHAR title[256];
    StringCchPrintfW(title, ARRAYSIZE(title), L"驱动源 " APP_VERSION L" · %s · 自动检测、下载与安装",
                    update_process_elevated() ? L"已提权" : L"未提权");
    HWND window = CreateWindowExW(0, type.lpszClassName, title,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
                                  NULL, NULL, instance, NULL);
    if (!window) { CloseHandle(task_event); DeleteCriticalSection(&task_lock); CoUninitialize(); return 1; }
    ShowWindow(window, show);
    UpdateWindow(window);
    int exit_code = 0;
    if (!wcscmp(command, L"--smoke-test")) {
        exit_code = window_smoke_test();
        DestroyWindow(window);
    } else {
        worker_handle = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
        if (!worker_handle) {
            MessageBoxW(window, L"无法启动后台工作线程。", L"启动失败", MB_OK | MB_ICONERROR);
            DestroyWindow(window);
        } else {
            Task task = { .command = ID_SCAN, .source = 0 };
            start_task(&task);
        }
    }
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (worker_handle) { WaitForSingleObject(worker_handle, INFINITE); CloseHandle(worker_handle); }
    clear_candidates();
    clear_hardware();
    free(hardware_rows);
    if (ui_font) DeleteObject(ui_font);
    CloseHandle(task_event);
    DeleteCriticalSection(&task_lock);
    CoUninitialize();
    return exit_code;
}
