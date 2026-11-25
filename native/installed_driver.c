#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include "installed_driver.h"
#include <strsafe.h>
#include <wchar.h>
#include <string.h>

BOOL installed_is_oem_inf(const WCHAR *name) {
    if (!name || _wcsnicmp(name, L"oem", 3)) return FALSE;
    const WCHAR *p = name + 3;
    if (*p < L'0' || *p > L'9') return FALSE;
    while (*p >= L'0' && *p <= L'9') ++p;
    return !_wcsicmp(p, L".inf");
}

HRESULT installed_store_inf(const WCHAR *published_inf, WCHAR *path, DWORD chars) {
    if (!path || !chars) return E_INVALIDARG;
    path[0] = 0;
    if (!installed_is_oem_inf(published_inf)) return E_INVALIDARG;
    WCHAR windows[MAX_PATH], source[MAX_PATH], system[MAX_PATH], prefix[MAX_PATH];
    UINT length = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (!length || length >= ARRAYSIZE(windows)) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    HRESULT hr = StringCchPrintfW(source, ARRAYSIZE(source), L"%s\\INF\\%s", windows, published_inf);
    if (FAILED(hr)) return hr;
    typedef BOOL (WINAPI *StoreLocation)(PCWSTR, PSP_ALTPLATFORM_INFO, PCWSTR, PWSTR, DWORD, PDWORD);
    union { FARPROC generic; StoreLocation typed; } function;
    function.generic = GetProcAddress(GetModuleHandleW(L"setupapi.dll"), "SetupGetInfDriverStoreLocationW");
    if (!function.typed) return E_NOTIMPL;
    DWORD required;
    if (!function.typed(source, NULL, NULL, path, chars, &required)) return HRESULT_FROM_WIN32(GetLastError());
    length = GetSystemDirectoryW(system, ARRAYSIZE(system));
    if (!length || length >= ARRAYSIZE(system)) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    hr = StringCchPrintfW(prefix, ARRAYSIZE(prefix), L"%s\\DriverStore\\FileRepository\\", system);
    if (FAILED(hr)) return hr;
    DWORD attributes = GetFileAttributesW(path);
    if (_wcsnicmp(path, prefix, wcslen(prefix)) || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        path[0] = 0;
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

static void read_value(HKEY key, const WCHAR *name, WCHAR *value, DWORD chars) {
    DWORD bytes = chars * (DWORD)sizeof(WCHAR);
    if (RegGetValueW(key, NULL, name, RRF_RT_REG_SZ, NULL, value, &bytes) != ERROR_SUCCESS) value[0] = 0;
}
void installed_driver_read(HDEVINFO set, SP_DEVINFO_DATA *device, InstalledDriver *driver) {
    ZeroMemory(driver, sizeof(*driver));
    HKEY key = SetupDiOpenDevRegKey(set, device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return;
    read_value(key, L"ProviderName", driver->provider, ARRAYSIZE(driver->provider));
    read_value(key, L"DriverVersion", driver->version, ARRAYSIZE(driver->version));
    read_value(key, L"DriverDate", driver->date, ARRAYSIZE(driver->date));
    read_value(key, L"InfPath", driver->published_inf, ARRAYSIZE(driver->published_inf));
    RegCloseKey(key);
    driver->registered = driver->published_inf[0] || driver->version[0];
    driver->local_available = SUCCEEDED(installed_store_inf(driver->published_inf, driver->store_inf, ARRAYSIZE(driver->store_inf)));
}
HRESULT installed_driver_check(const WCHAR *instance_id, const WCHAR *expected_inf,
                               const WCHAR *expected_version, WCHAR *path, DWORD chars) {
    if (!instance_id || !*instance_id || !installed_is_oem_inf(expected_inf)) return E_INVALIDARG;
    HDEVINFO set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (set == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    SP_DEVINFO_DATA device = {.cbSize = sizeof(device)};
    HRESULT hr;
    if (!SetupDiOpenDeviceInfoW(set, instance_id, NULL, 0, &device)) hr = HRESULT_FROM_WIN32(GetLastError());
    else {
        InstalledDriver current;
        installed_driver_read(set, &device, &current);
        if (_wcsicmp(current.published_inf, expected_inf) ||
            (expected_version && *expected_version && _wcsicmp(current.version, expected_version)))
            hr = HRESULT_FROM_WIN32(ERROR_RETRY);
        else if (!current.local_available) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        else hr = StringCchCopyW(path, chars, current.store_inf);
    }
    SetupDiDestroyDeviceInfoList(set);
    return hr;
}
