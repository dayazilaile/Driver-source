#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <ole2.h>
#include "driver_backup.h"

static HRESULT process_error(DWORD code) {
    return FAILED((HRESULT)code) ? (HRESULT)code : HRESULT_FROM_WIN32(code);
}

static HRESULT ensure_directory(const WCHAR *path) {
    DWORD attributes;
    if (!path[0] || PathIsRelativeW(path)) return E_INVALIDARG;
    if (wcslen(path) > 150) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    int error = SHCreateDirectoryExW(NULL, path, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(GetLastError());
    return attributes & FILE_ATTRIBUTE_DIRECTORY ? S_OK : HRESULT_FROM_WIN32(ERROR_DIRECTORY);
}

static HRESULT unique_path(const WCHAR *root, const WCHAR *prefix, WCHAR *out, size_t chars) {
    GUID id;
    WCHAR guid[40];
    SYSTEMTIME time;
    HRESULT hr = CoCreateGuid(&id);
    if (FAILED(hr)) return hr;
    StringFromGUID2(&id, guid, ARRAYSIZE(guid));
    GetLocalTime(&time);
    return StringCchPrintfW(out, chars, L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%s",
        root, prefix, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, guid);
}

/* Only the absolute Windows system executable is started. Arguments never pass through a shell. */
static HRESULT run_system_tool(const WCHAR *name, const WCHAR *arguments, const WCHAR *log_path, DWORD *exit_code) {
    WCHAR system[MAX_PATH], tool[MAX_PATH], command[2048];
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process = {0};
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    HRESULT hr;
    UINT length = GetSystemDirectoryW(system, ARRAYSIZE(system));
    if (!length) return HRESULT_FROM_WIN32(GetLastError());
    if (length >= ARRAYSIZE(system)) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    hr = StringCchPrintfW(tool, ARRAYSIZE(tool), L"%s\\%s", system, name);
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\" %s", tool, arguments);
    if (FAILED(hr)) return hr;
    HANDLE log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        CloseHandle(log);
        return hr;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;
    BOOL started = CreateProcessW(tool, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                  NULL, NULL, &startup, &process);
    hr = started ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(input);
    CloseHandle(log);
    if (FAILED(hr)) return hr;
    CloseHandle(process.hThread);
    DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
    if (wait != WAIT_OBJECT_0) hr = HRESULT_FROM_WIN32(GetLastError());
    else if (!GetExitCodeProcess(process.hProcess, exit_code)) hr = HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(process.hProcess);
    return hr;
}

HRESULT backup_drivers(const WCHAR *root, WCHAR *saved, size_t chars) {
    WCHAR target[MAX_PATH], temporary[MAX_PATH], output[MAX_PATH], arguments[1024], marker[MAX_PATH];
    DWORD exit_code = 0, written = 0;
    HRESULT hr = ensure_directory(root);
    saved[0] = 0;
    if (FAILED(hr)) return hr;
    hr = unique_path(root, L"Backup", target, ARRAYSIZE(target));
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(temporary, ARRAYSIZE(temporary), L"%s.partial", target);
    if (FAILED(hr)) return hr;
    if (!CreateDirectoryW(temporary, NULL)) return HRESULT_FROM_WIN32(GetLastError());
    hr = StringCchPrintfW(output, ARRAYSIZE(output), L"%s\\output.log", temporary);
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(arguments, ARRAYSIZE(arguments),
        L"/online /export-driver /destination:\"%s\" /English /LogPath:\"%s\\dism.log\"", temporary, temporary);
    if (FAILED(hr)) return hr;
    hr = run_system_tool(L"dism.exe", arguments, output, &exit_code);
    if (FAILED(hr)) return hr;
    if (exit_code != ERROR_SUCCESS) return process_error(exit_code);
    hr = StringCchPrintfW(marker, ARRAYSIZE(marker), L"%s\\backup-complete.txt", temporary);
    if (FAILED(hr)) return hr;
    HANDLE file = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    const char info[] = "Driver Source backup v1\r\nDISM online export completed. Contains third-party driver packages.\r\n";
    BOOL ok = WriteFile(file, info, (DWORD)(sizeof(info) - 1), &written, NULL);
    DWORD error = ERROR_SUCCESS;
    if (!ok) error = GetLastError();
    else if (written != sizeof(info) - 1) error = ERROR_WRITE_FAULT;
    if (error == ERROR_SUCCESS && !FlushFileBuffers(file)) error = GetLastError();
    CloseHandle(file);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    if (!MoveFileW(temporary, target)) return HRESULT_FROM_WIN32(GetLastError());
    return StringCchCopyW(saved, chars, target);
}

HRESULT restore_drivers(const WCHAR *folder, const WCHAR *log_root, DWORD *result_code) {
    WCHAR marker[MAX_PATH], output[MAX_PATH], log_base[MAX_PATH], arguments[1024];
    HRESULT hr = StringCchPrintfW(marker, ARRAYSIZE(marker), L"%s\\backup-complete.txt", folder);
    if (FAILED(hr)) return hr;
    DWORD attributes = GetFileAttributesW(marker);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (wcslen(folder) >= 8 && !_wcsicmp(folder + wcslen(folder) - 8, L".partial"))) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    hr = ensure_directory(log_root);
    if (FAILED(hr)) return hr;
    hr = unique_path(log_root, L"Restore", log_base, ARRAYSIZE(log_base));
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(output, ARRAYSIZE(output), L"%s.log", log_base);
    if (FAILED(hr)) return hr;
    hr = StringCchPrintfW(arguments, ARRAYSIZE(arguments), L"/add-driver \"%s\\*.inf\" /subdirs /install", folder);
    if (FAILED(hr)) return hr;
    hr = run_system_tool(L"pnputil.exe", arguments, output, result_code);
    if (FAILED(hr)) return hr;
    if (*result_code == ERROR_SUCCESS || *result_code == ERROR_SUCCESS_REBOOT_REQUIRED ||
        *result_code == ERROR_SUCCESS_REBOOT_INITIATED) return S_OK;
    return process_error(*result_code);
}
