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
#include <wchar.h>
#include <winhttp.h>
#include "sources.h"

#define APP_VERSION L"0.1.2"
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

typedef enum { EV_STATUS, EV_HARDWARE, EV_UPDATE, EV_RESET, EV_PROGRESS,
               EV_ITEM_STATUS, EV_INSTALLED, EV_SOURCE_STATUS, EV_DONE, EV_EXIT } EventKind;
typedef struct {
    WCHAR name[512], hardware[2048], version[128], state[128];
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
    BOOL install;
    WCHAR folder[MAX_PATH];
} Task;

static HWND main_window, hardware_list, update_list, status_label, progress_bar;
static HWND scan_button, download_button, cancel_button, source_box, folder_edit;
static HWND tab_control, folder_button, open_button, all_button, copy_button;
static HWND auto_install_box, source_list, check_sources_button, open_source_button;
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

static void report_error(const WCHAR *operation, HRESULT error) {
    WCHAR message[1024], detail[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)error, 0, detail, ARRAYSIZE(detail), NULL);
    StringCchPrintfW(message, ARRAYSIZE(message),
        L"%s失败（0x%08lX）。%s 请检查网络、Windows Update 服务和单位更新策略；权限不足时以管理员身份运行。",
        operation, (unsigned long)error, detail);
    emit(EV_STATUS, 0, message, NULL);
}

static void read_registry_string(HKEY key, const WCHAR *name, WCHAR *buffer, DWORD chars) {
    DWORD bytes = chars * sizeof(WCHAR);
    if (RegGetValueW(key, NULL, name, RRF_RT_REG_SZ, NULL, buffer, &bytes) != ERROR_SUCCESS)
        buffer[0] = 0;
}

static void scan_hardware(void) {
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

static HRESULT find_updates(IUpdateSession *session, int source, IUpdateCollection **updates) {
    IUpdateSearcher *searcher = NULL;
    ISearchJob *job = NULL;
    ISearchResult *result = NULL;
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE;
    BOOL abort_sent = FALSE;
    OperationResultCode result_code = orcNotStarted;
    BSTR criteria = SysAllocString(L"Type='Driver' and IsInstalled=0 and IsHidden=0");
    HRESULT hr;
    VariantInit(&state);
    if (!criteria) return E_OUTOFMEMORY;
    hr = IUpdateSession_CreateUpdateSearcher(session, &searcher);
    if (FAILED(hr)) goto done;
    hr = IUpdateSearcher_put_ServerSelection(searcher, source == 1 ? ssWindowsUpdate : ssDefault);
    if (FAILED(hr)) goto done;
    hr = IUpdateSearcher_put_Online(searcher, VARIANT_TRUE);
    if (FAILED(hr)) goto done;
    emit(EV_STATUS, 0, L"正在按本机硬件和系统版本匹配驱动，首次查询可能需要几分钟…", NULL);
    hr = IUpdateSearcher_BeginSearch(searcher, criteria, NULL, state, &job);
    if (FAILED(hr)) goto done;
    while (!complete) {
        if (should_cancel() && !abort_sent) {
            ISearchJob_RequestAbort(job);
            abort_sent = TRUE;
        }
        hr = ISearchJob_get_IsCompleted(job, &complete);
        if (FAILED(hr)) {
            ISearchJob_RequestAbort(job);
            goto done;
        }
        if (!complete) Sleep(200);
    }
    hr = IUpdateSearcher_EndSearch(searcher, job, &result);
    if (FAILED(hr)) goto done;
    hr = ISearchResult_get_ResultCode(result, &result_code);
    if (FAILED(hr)) goto done;
    if (should_cancel()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    if (result_code != orcSucceeded && result_code != orcSucceededWithErrors) {
        hr = E_FAIL;
        goto done;
    }
    hr = ISearchResult_get_Updates(result, updates);
    if (SUCCEEDED(hr) && result_code == orcSucceededWithErrors) hr = S_FALSE;
done:
    if (job) { ISearchJob_CleanUp(job); ISearchJob_Release(job); }
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

static HRESULT download_one(IUpdateSession *session, IUpdate *update) {
    IUpdateCollection *selection = NULL;
    IUpdateDownloader *downloader = NULL;
    IDownloadJob *job = NULL;
    IDownloadResult *result = NULL;
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE, accepted = VARIANT_FALSE, downloaded = VARIANT_FALSE;
    OperationResultCode result_code = orcNotStarted;
    BOOL abort_sent = FALSE;
    HRESULT hr;
    VariantInit(&state);
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
    hr = IUpdateDownloader_BeginDownload(downloader, NULL, NULL, state, &job);
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
            LONG percent = 0;
            if (SUCCEEDED(IDownloadProgress_get_PercentComplete(progress, &percent)))
                emit(EV_PROGRESS, percent, NULL, NULL);
            IDownloadProgress_Release(progress);
        }
        if (!complete) Sleep(200);
    }
    hr = IUpdateDownloader_EndDownload(downloader, job, &result);
    if (FAILED(hr)) goto done;
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
done:
    if (job) { IDownloadJob_CleanUp(job); IDownloadJob_Release(job); }
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
    hr = IUpdateInstaller_BeginInstall(installer, NULL, NULL, state, &job);
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
            if (SUCCEEDED(IInstallationProgress_get_PercentComplete(progress, &percent)))
                emit(EV_PROGRESS, percent, NULL, NULL);
            IInstallationProgress_Release(progress);
        }
        if (!complete) Sleep(200);
    }
    hr = IUpdateInstaller_EndInstall(installer, job, &result);
    if (FAILED(hr)) goto done;
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
    if (result) IInstallationResult_Release(result);
    if (installer) IUpdateInstaller_Release(installer);
    if (selection) IUpdateCollection_Release(selection);
    return hr;
}

static void download_selected(IUpdateSession *session, IUpdateCollection *updates, const Task *task) {
    int successful = 0, failed = 0, export_failed = 0;
    BOOL reboot = FALSE;
    for (int item = 0; item < task->count && !should_cancel() && !reboot; ++item) {
        IUpdate *update = NULL;
        WCHAR text[1024], saved[MAX_PATH];
        int index = task->indices[item];
        StringCchPrintfW(text, ARRAYSIZE(text), L"正在处理第 %d / %d 项。来源由所选 Windows 更新服务决定。", item + 1, task->count);
        emit(EV_STATUS, 0, text, NULL);
        emit(EV_PROGRESS, 0, NULL, NULL);
        emit(EV_ITEM_STATUS, index, L"下载中", NULL);
        HRESULT hr = IUpdateCollection_get_Item(updates, index, &update);
        HRESULT backup = S_OK;
        if (SUCCEEDED(hr)) hr = download_one(session, update);
        if (SUCCEEDED(hr)) {
            emit(EV_ITEM_STATUS, index, L"正在保存", NULL);
            backup = export_update(update, task->folder, saved, ARRAYSIZE(saved));
            if (FAILED(backup)) ++export_failed;
            if (!task->install) hr = backup;
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
        if (SUCCEEDED(hr)) {
            ++successful;
            const WCHAR *outcome = task->install ?
                reboot ? L"已安装，需要重启" :
                FAILED(backup) ? L"已安装，备份失败" : L"已安装" : L"已保存";
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
        L"%s %s %d 项，失败或取消 %d 项，未执行 %d 项，备份失败 %d 项。%s",
        should_cancel() ? L"任务已取消。" : L"任务结束。",
        task->install ? L"已安装" : L"已保存", successful, failed,
        task->count - successful - failed, export_failed,
        reboot ? L"Windows 要求重启，请保存工作后手动重启，再重新扫描。" : L"可在列表查看每项结果。");
    emit(EV_STATUS, 0, text, NULL);
}

static void check_sources(void) {
    HINTERNET session = WinHttpOpen(L"DriverSource/" APP_VERSION,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { report_error(L"初始化来源检查", HRESULT_FROM_WIN32(GetLastError())); return; }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    for (int index = 0; index < (int)ARRAYSIZE(driver_sources) && !should_cancel(); ++index) {
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
        emit(EV_PROGRESS, (index + 1) * 100 / (int)ARRAYSIZE(driver_sources), NULL, NULL);
    }
    WinHttpCloseHandle(session);
    emit(EV_STATUS, 0, should_cancel() ? L"来源检查已取消。" :
         L"来源检查结束。HTTP 状态仅反映入口连通性，具体型号和安装包仍需核验。", NULL);
}

static DWORD WINAPI worker_main(void *unused) {
    (void)unused;
    IUpdateSession *session = NULL;
    IUpdateCollection *updates = NULL;
    HRESULT init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT session_hr = FAILED(init) ? init :
        CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IUpdateSession, (void **)&session);
    if (session) {
        BSTR name = SysAllocString(L"DriverSource Native " APP_VERSION);
        if (name) { IUpdateSession_put_ClientApplicationID(session, name); SysFreeString(name); }
    }
    for (;;) {
        WaitForSingleObject(task_event, INFINITE);
        if (InterlockedCompareExchange(&quitting, 0, 0)) break;
        Task task;
        EnterCriticalSection(&task_lock);
        task = next_task;
        LeaveCriticalSection(&task_lock);
        if (task.command == ID_SCAN) {
            emit(EV_RESET, 0, NULL, NULL);
            scan_hardware();
            if (updates) { IUpdateCollection_Release(updates); updates = NULL; }
            if (should_cancel()) emit(EV_STATUS, 0, L"扫描已取消。", NULL);
            else if (FAILED(session_hr)) report_error(L"初始化 Windows 驱动更新服务", session_hr);
            else {
                HRESULT hr = find_updates(session, task.source, &updates);
                if (SUCCEEDED(hr)) {
                    BOOL partial = hr == S_FALSE;
                    hr = publish_updates(updates);
                    if (SUCCEEDED(hr) && partial)
                        emit(EV_STATUS, 0, L"更新服务仅返回部分匹配结果。可下载列表中的驱动，并稍后重新扫描。", NULL);
                }
                if (FAILED(hr)) {
                    if (should_cancel()) emit(EV_STATUS, 0, L"扫描已取消。", NULL);
                    else report_error(L"在线匹配驱动", hr);
                }
            }
        } else if (task.command == ID_CHECK_SOURCES) {
            check_sources();
        } else if (task.command == ID_DOWNLOAD && updates && session) {
            download_selected(session, updates, &task);
        }
        emit(EV_DONE, 0, NULL, NULL);
    }
    if (updates) IUpdateCollection_Release(updates);
    if (session) IUpdateSession_Release(session);
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
    EnableWindow(cancel_button, value);
    if (!value) SendMessageW(progress_bar, PBM_SETMARQUEE, FALSE, 0);
}

static void start_task(Task *task) {
    InterlockedExchange(&cancelled, 0);
    EnterCriticalSection(&task_lock);
    next_task = *task;
    LeaveCriticalSection(&task_lock);
    set_busy(TRUE);
    if (task->command == ID_SCAN)
        SetWindowTextW(status_label, L"正在读取本机硬件…");
    SendMessageW(progress_bar, PBM_SETPOS, 0, 0);
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
    task.install = SendMessageW(auto_install_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
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

static void choose_folder(void) {
    BROWSEINFOW info = {0};
    info.hwndOwner = main_window;
    info.lpszTitle = L"选择驱动包保存文件夹";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item) {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDListW(item, path)) SetWindowTextW(folder_edit, path);
        CoTaskMemFree(item);
    }
}

static void copy_hardware_id(void) {
    int selected = ListView_GetNextItem(hardware_list, -1, LVNI_SELECTED);
    WCHAR text[2048];
    if (selected < 0) return;
    ListView_GetItemText(hardware_list, selected, 1, text, ARRAYSIZE(text));
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

static void show_tab(void) {
    int tab = TabCtrl_GetCurSel(tab_control);
    ShowWindow(update_list, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(hardware_list, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(source_list, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(all_button, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(copy_button, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(check_sources_button, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(open_source_button, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(auto_install_box, tab == 0 ? SW_SHOW : SW_HIDE);
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
    tab.pszText = L"厂商下载来源";
    TabCtrl_InsertItem(tab_control, 2, &tab);
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
    add_column(hardware_list, 3, L"设备状态", 150);
    source_list = control(window, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0);
    ListView_SetExtendedListViewStyle(source_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    add_column(source_list, 0, L"来源", 150);
    add_column(source_list, 1, L"覆盖设备", 260);
    add_column(source_list, 2, L"接入方式", 200);
    add_column(source_list, 3, L"入口连通性", 170);
    for (int index = 0; index < (int)ARRAYSIZE(driver_sources); ++index) {
        LVITEMW item = { .mask = LVIF_TEXT, .iItem = index, .pszText = (WCHAR *)driver_sources[index].name };
        ListView_InsertItem(source_list, &item);
        ListView_SetItemText(source_list, index, 1, (WCHAR *)driver_sources[index].devices);
        ListView_SetItemText(source_list, index, 2, (WCHAR *)driver_sources[index].mode);
        ListView_SetItemText(source_list, index, 3, L"待检查");
    }
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
    progress_bar = control(window, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0);
    SendMessageW(progress_bar, PBM_SETRANGE32, 0, 100);
    status_label = control(window, L"STATIC", L"正在检测本机硬件…", 0, 0);
    PWSTR downloads = NULL;
    WCHAR path[MAX_PATH] = L"";
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &downloads))) {
        StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\DriverSource", downloads);
        CoTaskMemFree(downloads);
        SetWindowTextW(folder_edit, path);
    }
    show_tab();
    set_busy(FALSE);
}

static void resize_controls(int width, int height) {
    MoveWindow(scan_button, 16, 16, 155, 32, TRUE);
    MoveWindow(source_box, 185, 18, width - 305, 180, TRUE);
    MoveWindow(cancel_button, width - 104, 16, 88, 32, TRUE);
    MoveWindow(tab_control, 16, 62, width - 32, height - 244, TRUE);
    MoveWindow(update_list, 24, 96, width - 48, height - 287, TRUE);
    MoveWindow(hardware_list, 24, 96, width - 48, height - 287, TRUE);
    MoveWindow(source_list, 24, 96, width - 48, height - 287, TRUE);
    MoveWindow(check_sources_button, 16, height - 174, 165, 28, TRUE);
    MoveWindow(open_source_button, 192, height - 174, 190, 28, TRUE);
    MoveWindow(auto_install_box, 205, height - 174, 210, 28, TRUE);
    MoveWindow(all_button, 16, height - 174, 160, 28, TRUE);
    MoveWindow(copy_button, 16, height - 174, 180, 28, TRUE);
    MoveWindow(folder_edit, 16, height - 134, width - 398, 28, TRUE);
    MoveWindow(folder_button, width - 370, height - 134, 100, 28, TRUE);
    MoveWindow(open_button, width - 258, height - 134, 100, 28, TRUE);
    MoveWindow(download_button, width - 146, height - 134, 130, 28, TRUE);
    MoveWindow(progress_bar, 16, height - 94, width - 32, 14, TRUE);
    MoveWindow(status_label, 16, height - 70, width - 32, 60, TRUE);
}

static void process_event(Event *event) {
    if (event->kind == EV_RESET) {
        ListView_DeleteAllItems(hardware_list);
        ListView_DeleteAllItems(update_list);
        clear_candidates();
    } else if (event->kind == EV_HARDWARE) {
        Hardware *row = event->data;
        LVITEMW item = { .mask = LVIF_TEXT, .iItem = ListView_GetItemCount(hardware_list), .pszText = row->name };
        int index = ListView_InsertItem(hardware_list, &item);
        ListView_SetItemText(hardware_list, index, 1, row->hardware);
        ListView_SetItemText(hardware_list, index, 2, row->version);
        ListView_SetItemText(hardware_list, index, 3, row->state);
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
        SendMessageW(progress_bar, PBM_SETPOS, event->index, 0);
    } else if (event->kind == EV_ITEM_STATUS || event->kind == EV_INSTALLED) {
        for (int index = 0; index < candidate_count; ++index) {
            if (candidates[index]->index != event->index) continue;
            ListView_SetItemText(update_list, index, 3, event->text);
            if (event->kind == EV_INSTALLED) {
                candidates[index]->installed = TRUE;
                ListView_SetCheckState(update_list, index, FALSE);
            }
        }
    } else if (event->kind == EV_SOURCE_STATUS) {
        ListView_SetItemText(source_list, event->index, 3, event->text);
    } else if (event->kind == EV_DONE) {
        if (!InterlockedCompareExchange(&quitting, 0, 0)) set_busy(FALSE);
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
        limits->ptMinTrackSize.y = 580;
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
                SetWindowTextW(status_label, L"正在检查官方来源入口连通性…");
                start_task(&task);
            }
            break;
        case ID_OPEN_SOURCE: {
            int selected = ListView_GetNextItem(source_list, -1, LVNI_SELECTED);
            if (selected >= 0)
                ShellExecuteW(window, L"open", driver_sources[selected].url, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_CANCEL:
            InterlockedExchange(&cancelled, 1);
            EnableWindow(cancel_button, FALSE);
            SetWindowTextW(status_label, L"正在请求 Windows 更新服务取消，请等待当前操作结束…");
            break;
        case ID_FOLDER: if (!busy) choose_folder(); break;
        case ID_OPEN: {
            WCHAR path[MAX_PATH];
            GetWindowTextW(folder_edit, path, ARRAYSIZE(path));
            if (path[0]) ShellExecuteW(window, L"open", path, NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_COPY: copy_hardware_id(); break;
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
        SetWindowTextW(status_label, L"正在结束更新服务任务，请稍候…");
        SetEvent(task_event);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
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
    HRESULT security = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                                             RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
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
    HWND window = CreateWindowExW(0, type.lpszClassName, L"驱动源 " APP_VERSION L" · 自动检测、下载与安装",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760,
                                  NULL, NULL, instance, NULL);
    if (!window) { CloseHandle(task_event); DeleteCriticalSection(&task_lock); CoUninitialize(); return 1; }
    ShowWindow(window, show);
    UpdateWindow(window);
    if (!wcscmp(command, L"--smoke-test")) {
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
    if (ui_font) DeleteObject(ui_font);
    CloseHandle(task_event);
    DeleteCriticalSection(&task_lock);
    CoUninitialize();
    return 0;
}
