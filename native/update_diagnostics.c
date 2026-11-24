#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winsvc.h>
#include <shlobj.h>
#include <strsafe.h>
#include <stdarg.h>
#include <stdlib.h>
#include "update_diagnostics.h"

BOOL update_process_elevated(void) {
    HANDLE token;
    TOKEN_ELEVATION elevation;
    DWORD bytes;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return FALSE;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

static void add_line(WCHAR *text, size_t chars, const WCHAR *format, ...) {
    WCHAR line[512];
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(line, ARRAYSIZE(line), format, args);
    va_end(args);
    StringCchCatW(text, chars, line);
    StringCchCatW(text, chars, L"\r\n");
}

static void policy_value(WCHAR *text, size_t chars, const WCHAR *path, const WCHAR *name) {
    DWORD value = 0, bytes = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, path, name,
        RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, NULL, &value, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        add_line(text, chars, L"%s = 未配置", name);
    else if (status == ERROR_SUCCESS)
        add_line(text, chars, L"%s = %lu", name, (unsigned long)value);
    else add_line(text, chars, L"%s = 读取失败 %lu", name, (unsigned long)status);
}

static void service_state(WCHAR *text, size_t chars, SC_HANDLE manager, const WCHAR *name) {
    SC_HANDLE service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!service) {
        add_line(text, chars, L"%s = 服务信息读取失败 %lu", name, (unsigned long)GetLastError());
        return;
    }
    SERVICE_STATUS_PROCESS status;
    DWORD bytes;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &bytes)) {
        const WCHAR *state = L"转换中";
        if (status.dwCurrentState == SERVICE_RUNNING) state = L"运行";
        else if (status.dwCurrentState == SERVICE_STOPPED) state = L"停止";
        add_line(text, chars, L"%s 当前状态 = %s", name, state);
    } else add_line(text, chars, L"%s 状态读取失败 %lu", name, (unsigned long)GetLastError());
    DWORD required = 0;
    QueryServiceConfigW(service, NULL, 0, &required);
    if (required && required <= 65536) {
        QUERY_SERVICE_CONFIGW *config = malloc(required);
        if (config && QueryServiceConfigW(service, config, required, &required)) {
            const WCHAR *start = L"其他";
            if (config->dwStartType == SERVICE_DISABLED) start = L"禁用";
            else if (config->dwStartType == SERVICE_AUTO_START) start = L"自动";
            else if (config->dwStartType == SERVICE_DEMAND_START) start = L"按需启动";
            add_line(text, chars, L"%s 启动类型 = %s", name, start);
        }
        free(config);
    }
    CloseServiceHandle(service);
}

void collect_update_diagnostics(WCHAR *text, size_t chars) {
    if (!chars) return;
    text[0] = 0;
    add_line(text, chars, L"进程令牌提权状态 = %s", update_process_elevated() ? L"已提权" : L"未提权");
#ifdef _WIN64
    add_line(text, chars, L"进程架构 = x64");
#else
    add_line(text, chars, L"进程架构 = x86");
#endif
    add_line(text, chars, L"\r\nWindows 更新策略（DWORD 值）");
    const WCHAR *policy = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate";
    const WCHAR *au = L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
    policy_value(text, chars, policy, L"DisableWindowsUpdateAccess");
    policy_value(text, chars, policy, L"DoNotConnectToWindowsUpdateInternetLocations");
    policy_value(text, chars, policy, L"SetDisableUXWUAccess");
    policy_value(text, chars, policy, L"ExcludeWUDriversInQualityUpdate");
    policy_value(text, chars, policy, L"SetPolicyDrivenUpdateSourceForDriverUpdates");
    policy_value(text, chars, au, L"UseWUServer");
    policy_value(text, chars, au, L"NoAutoUpdate");
    DWORD bytes = 0;
    LSTATUS server = RegGetValueW(HKEY_LOCAL_MACHINE, policy, L"WUServer",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, NULL, &bytes);
    add_line(text, chars, L"WSUS 地址配置 = %s", server == ERROR_SUCCESS && bytes > sizeof(WCHAR) ? L"已配置" : L"未读取到配置");
    add_line(text, chars, L"\r\n更新相关服务（即时只读状态）");
    SC_HANDLE manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (manager) {
        const WCHAR *services[] = {L"wuauserv", L"BITS", L"UsoSvc", L"RpcSs"};
        for (size_t index = 0; index < ARRAYSIZE(services); ++index)
            service_state(text, chars, manager, services[index]);
        CloseServiceHandle(manager);
    } else add_line(text, chars, L"服务管理器读取失败 %lu", (unsigned long)GetLastError());
    add_line(text, chars, L"\r\n按需启动的服务可能暂时处于停止状态。策略与服务信息用于定位，需要结合失败步骤判断。");
}

HRESULT append_update_log(const WCHAR *root, const WCHAR *text) {
    WCHAR folder[MAX_PATH], path[MAX_PATH], entry[12000];
    SYSTEMTIME now;
    HRESULT hr = StringCchPrintfW(folder, ARRAYSIZE(folder), L"%s\\logs", root);
    if (FAILED(hr)) return hr;
    int error = SHCreateDirectoryExW(NULL, folder, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
    hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\DriverSource.log", folder);
    if (FAILED(hr)) return hr;
    GetLocalTime(&now);
    hr = StringCchPrintfW(entry, ARRAYSIZE(entry), L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, text);
    if (FAILED(hr)) return hr;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, entry, -1, NULL, 0, NULL, NULL);
    if (!bytes) return HRESULT_FROM_WIN32(GetLastError());
    char *utf8 = malloc((size_t)bytes);
    if (!utf8) return E_OUTOFMEMORY;
    if (!WideCharToMultiByte(CP_UTF8, 0, entry, -1, utf8, bytes, NULL, NULL)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        free(utf8);
        return hr;
    }
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
    else {
        DWORD written;
        if (!WriteFile(file, utf8, (DWORD)(bytes - 1), &written, NULL)) hr = HRESULT_FROM_WIN32(GetLastError());
        else if (written != (DWORD)(bytes - 1)) hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        CloseHandle(file);
    }
    free(utf8);
    return hr;
}
