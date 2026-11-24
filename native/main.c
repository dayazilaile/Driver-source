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
#include <wuapi.h>
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
#include "transfer.h"
#include "hardware_database.h"
#include "wua_callbacks.h"
#include "update_diagnostics.h"

#define APP_VERSION L"0.1.6"
/* IID from the Windows SDK, also usable with MinGW's base wuapi.h. */
static const GUID driver_update_iid = {0xb383cd1a, 0x5ce9, 0x4504, {0x9f,0x63,0x76,0x4b,0x12,0x36,0xf1,0x91}};
#define MAX_UPDATES 2048
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

typedef enum { EV_STATUS, EV_HARDWARE, EV_UPDATE, EV_RESET, EV_PROGRESS,
               EV_ITEM_STATUS, EV_INSTALLED, EV_SOURCE_STATUS, EV_DATABASE, EV_DIAGNOSTICS, EV_TRANSFER, EV_DONE, EV_EXIT } EventKind;
typedef struct {
    WCHAR name[512], hardware[2048], version[128], state[128];
    HardwareIdentity identity;
} Hardware;
typedef struct {
    WCHAR title[512], hardware[1024], provider[256];
    WCHAR *eula;
    BOOL accepted, installed;
    int index;
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
static HWND auto_install_box, source_list, check_sources_button, open_source_button;
static HWND transfer_label, pre_backup_box, backup_description, backup_edit;
static HWND backup_button, backup_folder_button, open_backup_button, restore_button, restore_description;
static WCHAR app_directory[MAX_PATH];
static HWND database_summary, device_sources_button, all_sources_button, source_context;
static Hardware **hardware_rows;
static size_t hardware_count, hardware_capacity;
static WCHAR source_status[ARRAYSIZE(driver_sources)][128];
static BOOL source_filter[ARRAYSIZE(driver_sources)], filtering_sources;
static HWND diagnostics_edit, diagnostics_button, copy_diagnostics_button, open_logs_button;
static HRESULT com_security_result = E_UNEXPECTED;
static HFONT ui_font;
static HANDLE task_event, worker_handle;
static CRITICAL_SECTION task_lock;
static Task next_task;
static volatile LONG cancelled, quitting;
static BOOL busy;
static Candidate *candidates[MAX_UPDATES];
static int candidate_count;

/* Every COM object stays on the worker thread. Posted events own their payload. */
static void emit(EventKind kind, int index, const WCHAR *text, void *data) {
    Event *event = calloc(1, sizeof(*event));
    if (!event) {
        if (kind == EV_UPDATE && data) free(((Candidate *)data)->eula);
        free(data);
        return;
    }
    event->kind = kind;
    event->index = index;
    event->data = data;
    if (text) StringCchCopyW(event->text, ARRAYSIZE(event->text), text);
    if (!PostMessageW(main_window, EVENT_MESSAGE, 0, (LPARAM)event)) {
        if (kind == EV_UPDATE && data) free(((Candidate *)data)->eula);
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

static void publish_diagnostics(int source, HRESULT error, const WCHAR *step) {
    WCHAR system[6000];
    WCHAR *text = calloc(8192, sizeof(WCHAR));
    if (!text) return;
    collect_update_diagnostics(system, ARRAYSIZE(system));
    StringCchPrintfW(text, 8192,
        L"驱动源 " APP_VERSION L"\r\n最近查询来源 = %s\r\n最近步骤 = %s\r\n最近结果 = 0x%08lX\r\n"
        L"CoInitializeSecurity = 0x%08lX\r\n请求认证级别 = RPC Packet Privacy\r\n\r\n%s",
        source == 1 ? L"微软 Windows Update" : L"系统更新源", step, (unsigned long)error,
        (unsigned long)com_security_result, system);
    HRESULT log = append_update_log(app_directory, text);
    if (FAILED(log)) {
        WCHAR message[128];
        StringCchPrintfW(message, ARRAYSIZE(message), L"\r\n诊断日志写入失败 = 0x%08lX，可复制当前诊断文本。", (unsigned long)log);
        StringCchCatW(text, 8192, message);
    } else StringCchCatW(text, 8192, L"\r\n诊断日志位于软件目录 logs\\DriverSource.log");
    emit(EV_DIAGNOSTICS, 0, NULL, text);
}

static void report_error(const WCHAR *operation, HRESULT error) {
    WCHAR message[1024], detail[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)error, 0, detail, ARRAYSIZE(detail), NULL);
    log_update_result(operation, error);
    if (error == E_ACCESSDENIED || error == RPC_E_ACCESS_DENIED)
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"%s被拒绝（0x%08lX）。当前令牌%s。请查看“系统诊断”的服务与策略状态，详情保存在 logs/DriverSource.log。",
            operation, (unsigned long)error, update_process_elevated() ? L"已提权" : L"未提权");
    else if (error == E_POINTER || error == E_NOINTERFACE)
        StringCchPrintfW(message, ARRAYSIZE(message),
            L"%s失败（0x%08lX）。%s 更新接口返回参数或接口兼容性错误，请记录此步骤和错误码。",
            operation, (unsigned long)error, detail);
    else StringCchPrintfW(message, ARRAYSIZE(message),
        L"%s失败（0x%08lX）。%s 请检查网络、Windows Update 服务和单位更新策略；权限不足时以管理员身份运行。",
        operation, (unsigned long)error, detail);
    emit(EV_STATUS, 0, message, NULL);
}

static void read_registry_string(HKEY key, const WCHAR *name, WCHAR *buffer, DWORD chars) {
    DWORD bytes = chars * sizeof(WCHAR);
    if (RegGetValueW(key, NULL, name, RRF_RT_REG_SZ, NULL, buffer, &bytes) != ERROR_SUCCESS)
        buffer[0] = 0;
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
        HKEY key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            read_registry_string(key, L"DriverVersion", row->version, ARRAYSIZE(row->version));
            RegCloseKey(key);
        }
        if (CM_Get_DevNode_Status(&flags, &problem, device.DevInst, 0) == CR_SUCCESS && (flags & DN_HAS_PROBLEM))
            StringCchPrintfW(row->state, ARRAYSIZE(row->state), L"设备问题 %lu", (unsigned long)problem);
        else
            StringCchCopyW(row->state, ARRAYSIZE(row->state), L"已检测");
        hardware_identify(database, set, &device, &row->identity);
        if (!row->hardware[0])
            StringCchCopyW(row->hardware, ARRAYSIZE(row->hardware), row->identity.primary_id);
        if (row->name[0]) emit(EV_HARDWARE, 0, NULL, row);
        else free(row);
    }
    SetupDiDestroyDeviceInfoList(set);
}

/* Driver-specific automation properties extend IUpdate on current Windows. */
static void driver_property(IUpdate *update, WCHAR *name, WCHAR *out, size_t chars) {
    IDispatch *dispatch = NULL;
    DISPID id;
    DISPPARAMS args = {0};
    VARIANT result;
    VariantInit(&result);
    HRESULT hr = IUpdate_QueryInterface(update, &driver_update_iid, (void **)&dispatch);
    if (FAILED(hr)) hr = IUpdate_QueryInterface(update, &IID_IDispatch, (void **)&dispatch);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(IDispatch_GetIDsOfNames(dispatch, &IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id)) &&
            SUCCEEDED(IDispatch_Invoke(dispatch, id, &IID_NULL, LOCALE_USER_DEFAULT,
                       DISPATCH_PROPERTYGET, &args, &result, NULL, NULL)) && result.vt == VT_BSTR)
            StringCchCopyW(out, chars, result.bstrVal ? result.bstrVal : L"");
        IDispatch_Release(dispatch);
    }
    VariantClear(&result);
}

static HRESULT find_updates(IUpdateSession *session, int source, IUpdateCollection **updates, const WCHAR **step) {
    IUpdateSearcher *searcher = NULL;
    ISearchJob *job = NULL;
    ISearchResult *result = NULL;
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE;
    BOOL abort_sent = FALSE;
    OperationResultCode result_code = orcNotStarted;
    BSTR criteria = SysAllocString(L"Type='Driver' and IsInstalled=0 and IsHidden=0");
    WuaCallbacks callbacks = {0};
    HRESULT hr;
    VariantInit(&state);
    if (!criteria) return E_OUTOFMEMORY;
    *step = L"创建搜索器（CreateUpdateSearcher）";
    hr = IUpdateSession_CreateUpdateSearcher(session, &searcher);
    if (FAILED(hr)) goto done;
    *step = L"设置更新源（ServerSelection）";
    log_update_result(source == 1 ? L"查询来源 Microsoft Windows Update" : L"查询来源 系统默认", S_OK);
    hr = IUpdateSearcher_put_ServerSelection(searcher, source == 1 ? ssWindowsUpdate : ssDefault);
    if (FAILED(hr)) goto done;
    *step = L"设置在线查询（Online）";
    hr = IUpdateSearcher_put_Online(searcher, VARIANT_TRUE);
    if (FAILED(hr)) goto done;
    emit(EV_STATUS, 0, L"正在按本机硬件和系统版本匹配驱动，首次查询可能需要几分钟…", NULL);
    *step = L"创建搜索回调";
    hr = wua_callbacks_create(WUA_SEARCH, &callbacks);
    if (FAILED(hr)) goto done;
    *step = L"开始异步搜索（BeginSearch）";
    hr = IUpdateSearcher_BeginSearch(searcher, criteria, callbacks.completed, state, &job);
    log_update_result(L"BeginSearch", hr);
    if (wua_should_fallback(hr, job != NULL, should_cancel())) {
        emit(EV_STATUS, 0, L"异步查询受限，正在使用同步兼容查询。取消将在服务返回后生效…", NULL);
        *step = L"兼容同步搜索（Search）";
        hr = IUpdateSearcher_Search(searcher, criteria, &result);
        log_update_result(L"Search 同步兼容查询", hr);
        if (FAILED(hr)) goto done;
    } else {
        if (FAILED(hr)) goto done;
        while (!complete) {
            if (should_cancel() && !abort_sent) {
                ISearchJob_RequestAbort(job);
                abort_sent = TRUE;
            }
            *step = L"检查异步搜索状态（IsCompleted）";
            hr = ISearchJob_get_IsCompleted(job, &complete);
            if (FAILED(hr)) {
                ISearchJob_RequestAbort(job);
                goto done;
            }
            if (!complete) Sleep(200);
        }
        *step = L"完成异步搜索（EndSearch）";
        hr = IUpdateSearcher_EndSearch(searcher, job, &result);
        if (FAILED(hr)) goto done;
    }
    if (!result) { hr = E_UNEXPECTED; goto done; }
    *step = L"读取搜索结果（ResultCode）";
    hr = ISearchResult_get_ResultCode(result, &result_code);
    if (FAILED(hr)) goto done;
    if (should_cancel()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    if (result_code != orcSucceeded && result_code != orcSucceededWithErrors) {
        hr = E_FAIL;
        goto done;
    }
    *step = L"读取可用驱动集合（Updates）";
    hr = ISearchResult_get_Updates(result, updates);
    if (SUCCEEDED(hr) && !*updates) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr) && result_code == orcSucceededWithErrors) hr = S_FALSE;
done:
    if (job) { ISearchJob_CleanUp(job); ISearchJob_Release(job); }
    wua_callbacks_release(&callbacks);
    if (result) ISearchResult_Release(result);
    if (searcher) IUpdateSearcher_Release(searcher);
    SysFreeString(criteria);
    return hr;
}

static HRESULT publish_updates(IUpdateCollection *updates) {
    LONG count = 0;
    HRESULT hr = IUpdateCollection_get_Count(updates, &count);
    if (FAILED(hr)) return hr;
    for (LONG index = 0; index < count && index < MAX_UPDATES && !should_cancel(); ++index) {
        IUpdate *update = NULL;
        BSTR title = NULL, eula = NULL;
        VARIANT_BOOL accepted = VARIANT_FALSE;
        Candidate *row = calloc(1, sizeof(*row));
        if (!row) return E_OUTOFMEMORY;
        hr = IUpdateCollection_get_Item(updates, index, &update);
        if (FAILED(hr)) { free(row); return hr; }
        row->index = index;
        IUpdate_get_Title(update, &title);
        StringCchCopyW(row->title, ARRAYSIZE(row->title), title ? title : L"驱动更新");
        driver_property(update, L"DriverHardwareID", row->hardware, ARRAYSIZE(row->hardware));
        driver_property(update, L"DriverManufacturer", row->provider, ARRAYSIZE(row->provider));
        hr = IUpdate_get_EulaAccepted(update, &accepted);
        row->accepted = SUCCEEDED(hr) && accepted;
        if (!row->accepted && SUCCEEDED(IUpdate_get_EulaText(update, &eula)) && eula)
            row->eula = _wcsdup(eula);
        SysFreeString(title);
        SysFreeString(eula);
        IUpdate_Release(update);
        emit(EV_UPDATE, index, NULL, row);
    }
    WCHAR status[256];
    if (should_cancel()) StringCchCopyW(status, ARRAYSIZE(status), L"扫描已取消，列表可能尚未完整。");
    else if (count)
        StringCchPrintfW(status, ARRAYSIZE(status), L"找到 %ld 个适用驱动。请在“可下载驱动”勾选后下载。%s",
            count, count > MAX_UPDATES ? L"列表显示前 2048 项。" : L"");
    else StringCchCopyW(status, ARRAYSIZE(status),
        L"更新服务未返回适用的新驱动。可在“本机硬件”检查设备；未收录的型号仍需厂商提供驱动。");
    emit(EV_STATUS, 0, status, NULL);
    return S_OK;
}

static void remove_temporary_folder(const WCHAR *path) {
    WCHAR pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW entry;
    StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\*", path);
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            if (FAILED(StringCchPrintfW(child, ARRAYSIZE(child), L"%s\\%s", path, entry.cFileName))) continue;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                remove_temporary_folder(child);
            else if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(child);
            else DeleteFileW(child);
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}

static HRESULT export_update(IUpdate *update, const WCHAR *folder, WCHAR *saved, size_t chars) {
    WCHAR temporary[MAX_PATH], guid[40], destination[MAX_PATH];
    GUID unique;
    BSTR target;
    HRESULT hr = CoCreateGuid(&unique);
    if (FAILED(hr)) return hr;
    StringFromGUID2(&unique, guid, ARRAYSIZE(guid));
    hr = StringCchPrintfW(destination, ARRAYSIZE(destination), L"%s\\Driver-%s", folder, guid);
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(temporary, ARRAYSIZE(temporary), L"%s.part", destination);
    if (FAILED(hr)) return hr;
    if (!CreateDirectoryW(temporary, NULL)) return HRESULT_FROM_WIN32(GetLastError());
    target = SysAllocString(temporary);
    hr = target ? IUpdate_CopyFromCache(update, target, VARIANT_FALSE) : E_OUTOFMEMORY;
    SysFreeString(target);
    if (SUCCEEDED(hr) && should_cancel()) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (SUCCEEDED(hr) && !MoveFileW(temporary, destination)) hr = HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) remove_temporary_folder(temporary);
    else StringCchCopyW(saved, chars, destination);
    return hr;
}

static HRESULT single_update_collection(IUpdate *update, IUpdateCollection **selection) {
    CLSID collection_class;
    LONG added;
    HRESULT hr = CLSIDFromProgID(L"Microsoft.Update.UpdateColl", &collection_class);
    if (FAILED(hr)) return hr;
    hr = CoCreateInstance(&collection_class, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUpdateCollection, (void **)selection);
    if (FAILED(hr)) return hr;
    hr = IUpdateCollection_Add(*selection, update, &added);
    if (FAILED(hr)) { IUpdateCollection_Release(*selection); *selection = NULL; }
    return hr;
}

static void report_transfer(IDownloadProgress *progress, TransferSample *sample) {
    LONG percent = 0;
    DECIMAL downloaded_decimal, total_decimal;
    double downloaded = 0, total = 0;
    WCHAR text[512], amount[192], speed[128];
    IDownloadProgress_get_PercentComplete(progress, &percent);
    emit(EV_PROGRESS, percent, NULL, NULL);
    HRESULT bytes_hr = IDownloadProgress_get_TotalBytesDownloaded(progress, &downloaded_decimal);
    if (SUCCEEDED(bytes_hr)) bytes_hr = VarR8FromDec(&downloaded_decimal, &downloaded);
    if (FAILED(bytes_hr) || downloaded < 0) {
        sample->initialized = 0;
        StringCchPrintfW(text, ARRAYSIZE(text), L"下载 %ld%% · 等待更新服务的字节统计", percent);
        emit(EV_TRANSFER, 0, text, NULL);
        return;
    }
    HRESULT total_hr = IDownloadProgress_get_TotalBytesToDownload(progress, &total_decimal);
    if (SUCCEEDED(total_hr)) total_hr = VarR8FromDec(&total_decimal, &total);
    if (SUCCEEDED(total_hr) && total >= 1048576.0)
        StringCchPrintfW(amount, ARRAYSIZE(amount), L"%.1f / %.1f MiB", downloaded / 1048576.0, total / 1048576.0);
    else if (SUCCEEDED(total_hr) && total > 0)
        StringCchPrintfW(amount, ARRAYSIZE(amount), L"%.1f / %.1f KiB", downloaded / 1024.0, total / 1024.0);
    else StringCchPrintfW(amount, ARRAYSIZE(amount), L"已下载 %.1f KiB", downloaded / 1024.0);
    double rate = transfer_sample(sample, GetTickCount64(), downloaded);
    if (!sample->measured) StringCchCopyW(speed, ARRAYSIZE(speed), L"计算中");
    else if (rate >= 1048576.0) StringCchPrintfW(speed, ARRAYSIZE(speed), L"%.2f MiB/s", rate / 1048576.0);
    else StringCchPrintfW(speed, ARRAYSIZE(speed), L"%.1f KiB/s", rate / 1024.0);
    DownloadPhase phase = dphInitializing;
    IDownloadProgress_get_CurrentUpdateDownloadPhase(progress, &phase);
    if (phase == dphVerifying)
        StringCchPrintfW(text, ARRAYSIZE(text), L"下载 %ld%% · %s · 正在校验", percent, amount);
    else StringCchPrintfW(text, ARRAYSIZE(text), L"下载 %ld%% · %s · 速度（估算）%s", percent, amount, speed);
    emit(EV_TRANSFER, 0, text, NULL);
}

static HRESULT download_one(IUpdateSession *session, IUpdate *update) {
    IUpdateCollection *selection = NULL;
    IUpdateDownloader *downloader = NULL;
    IDownloadJob *job = NULL;
    IDownloadResult *result = NULL;
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE, accepted = VARIANT_FALSE, downloaded = VARIANT_FALSE;
    OperationResultCode result_code = orcNotStarted;
    BOOL abort_sent = FALSE;
    TransferSample sample = {0};
    WuaCallbacks callbacks = {0};
    HRESULT hr;
    VariantInit(&state);
    emit(EV_TRANSFER, 0, L"正在连接下载服务，等待传输统计…", NULL);
    hr = IUpdate_get_EulaAccepted(update, &accepted);
    if (FAILED(hr)) goto done;
    /* UI obtains explicit consent for each unaccepted license before starting. */
    if (!accepted) {
        hr = IUpdate_AcceptEula(update);
        if (FAILED(hr)) goto done;
    }
    hr = single_update_collection(update, &selection);
    if (FAILED(hr)) goto done;
    hr = IUpdateSession_CreateUpdateDownloader(session, &downloader);
    if (FAILED(hr)) goto done;
    hr = IUpdateDownloader_put_Updates(downloader, selection);
    if (FAILED(hr)) goto done;
    hr = wua_callbacks_create(WUA_DOWNLOAD, &callbacks);
    if (FAILED(hr)) goto done;
    hr = IUpdateDownloader_BeginDownload(downloader, callbacks.progress, callbacks.completed, state, &job);
    log_update_result(L"BeginDownload", hr);
    if (wua_should_fallback(hr, job != NULL, should_cancel())) {
        emit(EV_TRANSFER, 0, L"同步兼容下载中，取消将在当前系统调用返回后生效…", NULL);
        emit(EV_PROGRESS, -1, NULL, NULL);
        hr = IUpdateDownloader_Download(downloader, &result);
        log_update_result(L"Download 同步兼容下载", hr);
        if (FAILED(hr)) goto done;
    } else {
        if (FAILED(hr)) goto done;
        while (!complete) {
            if (should_cancel() && !abort_sent) {
                IDownloadJob_RequestAbort(job);
                abort_sent = TRUE;
            }
            hr = IDownloadJob_get_IsCompleted(job, &complete);
            if (FAILED(hr)) { IDownloadJob_RequestAbort(job); goto done; }
            IDownloadProgress *progress = NULL;
            if (SUCCEEDED(IDownloadJob_GetProgress(job, &progress))) {
                report_transfer(progress, &sample);
                IDownloadProgress_Release(progress);
            }
            if (!complete) Sleep(200);
        }
        hr = IUpdateDownloader_EndDownload(downloader, job, &result);
        if (FAILED(hr)) goto done;
    }
    if (!result) { hr = E_UNEXPECTED; goto done; }
    if (should_cancel()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    hr = IDownloadResult_get_ResultCode(result, &result_code);
    if (FAILED(hr)) goto done;
    if (result_code != orcSucceeded) {
        LONG code = 0;
        IUpdateDownloadResult *item_result = NULL;
        if (SUCCEEDED(IDownloadResult_GetUpdateResult(result, 0, &item_result))) {
            IUpdateDownloadResult_get_HResult(item_result, &code);
            IUpdateDownloadResult_Release(item_result);
        }
        hr = FAILED((HRESULT)code) ? (HRESULT)code : E_FAIL;
        goto done;
    }
    hr = IUpdate_get_IsDownloaded(update, &downloaded);
    if (SUCCEEDED(hr) && !downloaded) hr = E_FAIL;
    if (SUCCEEDED(hr)) {
        emit(EV_PROGRESS, 100, NULL, NULL);
        emit(EV_TRANSFER, 0, L"下载完成 100% · 驱动包已准备就绪", NULL);
    }
done:
    if (job) { IDownloadJob_CleanUp(job); IDownloadJob_Release(job); }
    wua_callbacks_release(&callbacks);
    if (result) IDownloadResult_Release(result);
    if (downloader) IUpdateDownloader_Release(downloader);
    if (selection) IUpdateCollection_Release(selection);
    return hr;
}

static HRESULT quiet_installer(IUpdateInstaller *installer) {
    static const GUID installer2_iid = {0x3442d4fe,0x224d,0x4cee,{0x98,0xcf,0x30,0xe0,0xc4,0xd2,0x29,0xe6}};
    IDispatch *dispatch = NULL;
    WCHAR *property = L"ForceQuiet";
    DISPID id, named = DISPID_PROPERTYPUT;
    VARIANTARG value;
    VariantInit(&value);
    value.vt = VT_BOOL;
    value.boolVal = VARIANT_TRUE;
    DISPPARAMS args = { .rgvarg = &value, .rgdispidNamedArgs = &named, .cArgs = 1, .cNamedArgs = 1 };
    HRESULT hr = IUpdateInstaller_QueryInterface(installer, &installer2_iid, (void **)&dispatch);
    if (FAILED(hr)) return hr;
    hr = IDispatch_GetIDsOfNames(dispatch, &IID_NULL, &property, 1, LOCALE_USER_DEFAULT, &id);
    if (SUCCEEDED(hr))
        hr = IDispatch_Invoke(dispatch, id, &IID_NULL, LOCALE_USER_DEFAULT,
                              DISPATCH_PROPERTYPUT, &args, NULL, NULL, NULL);
    IDispatch_Release(dispatch);
    return hr;
}

static HRESULT install_one(IUpdateSession *session, IUpdate *update, BOOL *reboot) {
    IUpdateCollection *selection = NULL;
    IUpdateInstaller *installer = NULL;
    IInstallationJob *job = NULL;
    IInstallationResult *result = NULL;
    IUpdateInstallationResult *item_result = NULL;
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE, restart = VARIANT_FALSE;
    OperationResultCode code = orcNotStarted;
    BOOL abort_sent = FALSE;
    WuaCallbacks callbacks = {0};
    HRESULT hr = single_update_collection(update, &selection);
    VariantInit(&state);
    *reboot = FALSE;
    if (FAILED(hr)) goto done;
    hr = IUpdateSession_CreateUpdateInstaller(session, &installer);
    if (FAILED(hr)) goto done;
    hr = IUpdateInstaller_put_Updates(installer, selection);
    if (FAILED(hr)) goto done;
    hr = IUpdateInstaller_get_RebootRequiredBeforeInstallation(installer, &restart);
    if (FAILED(hr)) goto done;
    if (restart) {
        *reboot = TRUE;
        hr = HRESULT_FROM_WIN32(ERROR_SUCCESS_REBOOT_REQUIRED);
        goto done;
    }
    hr = IUpdateInstaller_put_AllowSourcePrompts(installer, VARIANT_FALSE);
    if (FAILED(hr)) goto done;
    hr = quiet_installer(installer);
    if (FAILED(hr)) goto done;
    if (should_cancel()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    hr = wua_callbacks_create(WUA_INSTALL, &callbacks);
    if (FAILED(hr)) goto done;
    hr = IUpdateInstaller_BeginInstall(installer, callbacks.progress, callbacks.completed, state, &job);
    log_update_result(L"BeginInstall", hr);
    if (wua_should_fallback(hr, job != NULL, should_cancel())) {
        emit(EV_TRANSFER, 0, L"同步兼容安装中，取消将在当前系统调用返回后生效…", NULL);
        emit(EV_PROGRESS, -1, NULL, NULL);
        hr = IUpdateInstaller_Install(installer, &result);
        log_update_result(L"Install 同步兼容安装", hr);
        if (FAILED(hr)) goto done;
    } else {
        if (FAILED(hr)) goto done;
        while (!complete) {
            if (should_cancel() && !abort_sent) {
                IInstallationJob_RequestAbort(job);
                abort_sent = TRUE;
            }
            hr = IInstallationJob_get_IsCompleted(job, &complete);
            if (FAILED(hr)) { IInstallationJob_RequestAbort(job); goto done; }
            IInstallationProgress *progress = NULL;
            if (SUCCEEDED(IInstallationJob_GetProgress(job, &progress))) {
                LONG percent = 0;
                if (SUCCEEDED(IInstallationProgress_get_PercentComplete(progress, &percent))) {
                    WCHAR text[128];
                    StringCchPrintfW(text, ARRAYSIZE(text), L"安装进度 %ld%%", percent);
                    emit(EV_PROGRESS, percent, NULL, NULL);
                    emit(EV_TRANSFER, 0, text, NULL);
                }
                IInstallationProgress_Release(progress);
            }
            if (!complete) Sleep(200);
        }
        hr = IUpdateInstaller_EndInstall(installer, job, &result);
        if (FAILED(hr)) goto done;
    }
    if (!result) { hr = E_UNEXPECTED; goto done; }
    hr = IInstallationResult_get_RebootRequired(result, &restart);
    if (FAILED(hr)) goto done;
    *reboot = restart != VARIANT_FALSE;
    hr = IInstallationResult_GetUpdateResult(result, 0, &item_result);
    if (FAILED(hr)) goto done;
    hr = IUpdateInstallationResult_get_ResultCode(item_result, &code);
    if (FAILED(hr)) goto done;
    if (code != orcSucceeded) {
        LONG item_error = 0;
        IUpdateInstallationResult_get_HResult(item_result, &item_error);
        hr = FAILED((HRESULT)item_error) ? (HRESULT)item_error :
             code == orcAborted ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : E_FAIL;
    }
    /* Read the actual result even after cancellation, an install may have completed. */
done:
    if (item_result) IUpdateInstallationResult_Release(item_result);
    if (job) { IInstallationJob_CleanUp(job); IInstallationJob_Release(job); }
    wua_callbacks_release(&callbacks);
    if (result) IInstallationResult_Release(result);
    if (installer) IUpdateInstaller_Release(installer);
    if (selection) IUpdateCollection_Release(selection);
    return hr;
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

static void download_selected(IUpdateSession *session, IUpdateCollection *updates, const Task *task) {
    int successful = 0, failed = 0, save_failed = 0;
    BOOL reboot = FALSE;
    if (task->install && task->pre_backup) {
        HRESULT checkpoint = create_backup(task);
        if (FAILED(checkpoint) || should_cancel()) return;
    }
    for (int item = 0; item < task->count && !should_cancel() && !reboot; ++item) {
        IUpdate *update = NULL;
        WCHAR text[1024], saved[MAX_PATH];
        int index = task->indices[item];
        StringCchPrintfW(text, ARRAYSIZE(text), L"正在处理第 %d / %d 项。来源由所选 Windows 更新服务决定。", item + 1, task->count);
        emit(EV_STATUS, 0, text, NULL);
        emit(EV_PROGRESS, 0, NULL, NULL);
        emit(EV_ITEM_STATUS, index, L"下载中", NULL);
        HRESULT hr = IUpdateCollection_get_Item(updates, index, &update);
        HRESULT save_result = S_OK;
        if (SUCCEEDED(hr)) hr = download_one(session, update);
        if (SUCCEEDED(hr)) {
            emit(EV_ITEM_STATUS, index, L"正在保存", NULL);
            emit(EV_TRANSFER, 0, L"正在保存下载的驱动包…", NULL);
            save_result = export_update(update, task->folder, saved, ARRAYSIZE(saved));
            if (FAILED(save_result)) ++save_failed;
            if (!task->install) hr = save_result;
        }
        if (SUCCEEDED(hr) && task->install) {
            if (should_cancel()) hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            else {
                emit(EV_ITEM_STATUS, index, L"正在安装", NULL);
                emit(EV_PROGRESS, 0, NULL, NULL);
                hr = install_one(session, update, &reboot);
            }
        }
        if (update) IUpdate_Release(update);
        log_update_result(L"驱动项下载/安装结果", hr);
        if (SUCCEEDED(hr)) {
            ++successful;
            const WCHAR *outcome = task->install ?
                reboot ? L"已安装，需要重启" :
                FAILED(save_result) ? L"已安装，下载包保存失败" : L"已安装" : L"已保存";
            emit(task->install ? EV_INSTALLED : EV_ITEM_STATUS, index, outcome, NULL);
        } else {
            ++failed;
            StringCchPrintfW(text, ARRAYSIZE(text), L"%s 0x%08lX",
                            reboot ? L"需先重启" : should_cancel() ? L"已取消" : L"失败", (unsigned long)hr);
            emit(EV_ITEM_STATUS, index, text, NULL);
        }
    }
    WCHAR text[1024];
    StringCchPrintfW(text, ARRAYSIZE(text),
        L"%s %s %d 项，失败或取消 %d 项，未执行 %d 项，下载包保存失败 %d 项。%s",
        should_cancel() ? L"任务已取消。" : L"任务结束。",
        task->install ? L"已安装" : L"已保存", successful, failed,
        task->count - successful - failed, save_failed,
        reboot ? L"Windows 要求重启，请保存工作后手动重启，再重新扫描。" : L"可在列表查看每项结果。");
    emit(EV_STATUS, 0, text, NULL);
    emit(EV_TRANSFER, 0, L"传输任务已结束", NULL);
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
    IUpdateSession *session = NULL;
    IUpdateCollection *updates = NULL;
    HardwareDatabase database;
    WCHAR summary[1024];
    hardware_database_open(&database, app_directory, summary, ARRAYSIZE(summary));
    emit(EV_DATABASE, 0, summary, NULL);
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT session_hr = FAILED(init) ? init :
        CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IUpdateSession, (void **)&session);
    if (session) {
        BSTR name = SysAllocString(L"DriverSource Native " APP_VERSION);
        if (name) { IUpdateSession_put_ClientApplicationID(session, name); SysFreeString(name); }
    }
    HRESULT last_error = S_OK;
    const WCHAR *last_step = L"启动检查";
    int last_source = 0;
    publish_diagnostics(last_source, last_error, last_step);
    for (;;) {
        WaitForSingleObject(task_event, INFINITE);
        if (InterlockedCompareExchange(&quitting, 0, 0)) break;
        Task task;
        EnterCriticalSection(&task_lock);
        task = next_task;
        LeaveCriticalSection(&task_lock);
        if (task.command == ID_SCAN) {
            emit(EV_RESET, 0, NULL, NULL);
            scan_hardware(&database);
            if (updates) { IUpdateCollection_Release(updates); updates = NULL; }
            if (should_cancel()) emit(EV_STATUS, 0, L"扫描已取消。", NULL);
            else if (FAILED(session_hr)) {
                last_error = session_hr; last_step = L"初始化 Windows 驱动更新服务"; last_source = task.source;
                report_error(last_step, last_error);
                publish_diagnostics(last_source, last_error, last_step);
            } else {
                const WCHAR *step = L"在线匹配驱动";
                HRESULT hr = find_updates(session, task.source, &updates, &step);
                if (SUCCEEDED(hr)) {
                    BOOL partial = hr == S_FALSE;
                    step = L"显示驱动匹配结果";
                    hr = publish_updates(updates);
                    if (SUCCEEDED(hr) && partial)
                        emit(EV_STATUS, 0, L"更新服务仅返回部分匹配结果。可下载列表中的驱动，并稍后重新扫描。", NULL);
                }
                last_error = hr; last_step = step; last_source = task.source;
                if (FAILED(hr)) {
                    if (should_cancel()) emit(EV_STATUS, 0, L"扫描已取消。", NULL);
                    else {
                        report_error(step, hr);
                        publish_diagnostics(last_source, hr, step);
                    }
                }
            }
        } else if (task.command == ID_DIAGNOSTICS) {
            publish_diagnostics(last_source, last_error, last_step);
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
        } else if (task.command == ID_DOWNLOAD && updates && session) {
            download_selected(session, updates, &task);
        }
        emit(EV_DONE, 0, NULL, NULL);
    }
    if (updates) IUpdateCollection_Release(updates);
    if (session) IUpdateSession_Release(session);
    hardware_database_close(&database);
    if (SUCCEEDED(init)) CoUninitialize();
    emit(EV_EXIT, 0, NULL, NULL);
    return 0;
}

static void clear_candidates(void) {
    for (int index = 0; index < candidate_count; ++index) {
        free(candidates[index]->eula);
        free(candidates[index]);
    }
    candidate_count = 0;
}

static void set_busy(BOOL value) {
    busy = value;
    EnableWindow(scan_button, !value);
    EnableWindow(update_list, !value);
    EnableWindow(source_box, !value);
    EnableWindow(download_button, !value && candidate_count > 0);
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

static INT_PTR CALLBACK license_dialog(HWND dialog, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_INITDIALOG) {
        Candidate *row = (Candidate *)lp;
        SetWindowTextW(dialog, row->title);
        SetDlgItemTextW(dialog, 202, row->eula);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
        EndDialog(dialog, LOWORD(wp));
        return TRUE;
    }
    return FALSE;
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
        if (row->installed) continue;
        if (!row->accepted) {
            if (!row->eula || !row->eula[0]) {
                MessageBoxW(main_window, L"无法读取所选驱动的许可协议，请重新扫描后再试。", L"许可协议", MB_OK | MB_ICONWARNING);
                return;
            }
            if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(201),
                                main_window, license_dialog, (LPARAM)row) != IDOK) return;
            row->accepted = TRUE;
        }
        task.indices[task.count++] = row->index;
    }
    if (!task.count) {
        MessageBoxW(main_window, L"请勾选需要下载的驱动。", L"选择驱动", MB_OK);
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
    int catalog = source_index("microsoft-catalog");
    if (catalog >= 0) source_filter[catalog] = TRUE;
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
    SendMessageW(source_box, CB_ADDSTRING, 0, (LPARAM)L"系统更新源（支持内网 WSUS）");
    SendMessageW(source_box, CB_ADDSTRING, 0, (LPARAM)L"微软 Windows Update（官方 CDN）");
    SendMessageW(source_box, CB_SETCURSEL, 0, 0);
    cancel_button = control(window, L"BUTTON", L"取消", WS_TABSTOP, ID_CANCEL);
    tab_control = control(window, WC_TABCONTROLW, L"", WS_TABSTOP, ID_TAB);
    TCITEMW tab = { .mask = TCIF_TEXT, .pszText = L"可下载驱动" };
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
    ListView_SetExtendedListViewStyle(update_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    add_column(update_list, 0, L"适用驱动及版本", 400);
    add_column(update_list, 1, L"厂商", 130);
    add_column(update_list, 2, L"匹配硬件 ID", 280);
    add_column(update_list, 3, L"下载状态", 170);
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
    all_button = control(window, L"BUTTON", L"全选 / 取消全选", WS_TABSTOP, ID_ALL);
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
    MoveWindow(pre_backup_box, 440, height - 210, 240, 28, TRUE);
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
            ListView_SetItemText(hardware_list, index, 7, sources[0] ? sources : L"可使用微软目录或全部来源");
        }
    } else if (event->kind == EV_UPDATE) {
        Candidate *row = event->data;
        if (candidate_count < MAX_UPDATES) {
            int index = candidate_count++;
            candidates[index] = row;
            event->data = NULL;
            LVITEMW item = { .mask = LVIF_TEXT, .iItem = index, .pszText = row->title };
            ListView_InsertItem(update_list, &item);
            ListView_SetItemText(update_list, index, 1, row->provider);
            ListView_SetItemText(update_list, index, 2, row->hardware);
            ListView_SetItemText(update_list, index, 3, L"可下载");
        }
    } else if (event->kind == EV_STATUS) {
        SetWindowTextW(status_label, event->text);
    } else if (event->kind == EV_PROGRESS) {
        set_progress(event->index);
    } else if (event->kind == EV_ITEM_STATUS || event->kind == EV_INSTALLED) {
        for (int index = 0; index < candidate_count; ++index) {
            if (candidates[index]->index != event->index) continue;
            ListView_SetItemText(update_list, index, 3, event->text);
            if (event->kind == EV_INSTALLED) {
                candidates[index]->installed = TRUE;
                ListView_SetCheckState(update_list, index, FALSE);
            }
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
        if (event->kind == EV_UPDATE) free(((Candidate *)event->data)->eula);
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
        limits->ptMinTrackSize.x = 850;
        limits->ptMinTrackSize.y = 640;
        return 0;
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
                if (!candidates[index]->installed && !ListView_GetCheckState(update_list, index)) all = FALSE;
            for (int index = 0; index < candidate_count; ++index)
                ListView_SetCheckState(update_list, index, !all && !candidates[index]->installed);
            break;
        }
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (InterlockedExchange(&quitting, 1)) return 0;
        InterlockedExchange(&cancelled, 1);
        EnableWindow(window, FALSE);
        SetWindowTextW(status_label, L"正在等待系统工具或更新服务结束，请稍候…");
        SetEvent(task_event);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}

static int window_smoke_test(void) {
    WCHAR expected[MAX_PATH], actual[MAX_PATH];
    if (!folder_edit || !backup_edit || !transfer_label || !progress_bar || !backup_button ||
        !restore_button || !pre_backup_box || !diagnostics_edit || !diagnostics_button) return 5;
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
    return ListView_GetItemCount(source_list) == (int)ARRAYSIZE(driver_sources) ? 0 : 12;
}

static int self_test(void) {
    HDEVINFO set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return 1;
    SP_DEVINFO_DATA data = { .cbSize = sizeof(data) };
    BOOL found = SetupDiEnumDeviceInfo(set, 0, &data);
    SetupDiDestroyDeviceInfoList(set);
    if (!found) return 2;
    IUpdateSession *session = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUpdateSession, (void **)&session);
    if (session) {
        IUpdateInstaller *installer = NULL;
        hr = IUpdateSession_CreateUpdateInstaller(session, &installer);
        if (SUCCEEDED(hr)) hr = quiet_installer(installer);
        if (installer) IUpdateInstaller_Release(installer);
        IUpdateSession_Release(session);
    }
    return FAILED(hr) ? 3 : 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)previous;
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    /* WUA requires impersonation for the Windows Update service. */
    HRESULT security = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                             RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    com_security_result = security;
    if (FAILED(security) && security != RPC_E_TOO_LATE) {
        CoUninitialize();
        return 4;
    }
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
