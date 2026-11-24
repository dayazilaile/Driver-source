#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include "vendor_download.h"
#include "transfer.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <objbase.h>
#include <stdlib.h>

static HRESULT status_error(NTSTATUS status) {
    return status < 0 ? E_FAIL : S_OK;
}

HRESULT vendor_verify_file(const WCHAR *path, const VendorPackage *package) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE buffer[65536], digest[32];
    char hex[65];
    LARGE_INTEGER size;
    HRESULT hr = status_error(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0));
    if (FAILED(hr)) goto done;
    hr = status_error(BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0));
    if (FAILED(hr)) goto done;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) { hr = HRESULT_FROM_WIN32(GetLastError()); goto done; }
    if (!GetFileSizeEx(file, &size)) { hr = HRESULT_FROM_WIN32(GetLastError()); goto done; }
    if ((uint64_t)size.QuadPart != package->bytes) { hr = HRESULT_FROM_WIN32(ERROR_CRC); goto done; }
    for (;;) {
        DWORD count;
        if (!ReadFile(file, buffer, sizeof(buffer), &count, NULL)) {
            hr = HRESULT_FROM_WIN32(GetLastError()); goto done;
        }
        if (!count) break;
        hr = status_error(BCryptHashData(hash, buffer, count, 0));
        if (FAILED(hr)) goto done;
    }
    hr = status_error(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (FAILED(hr)) goto done;
    for (size_t i = 0; i < sizeof(digest); ++i) {
        static const char digits[] = "0123456789abcdef";
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
    if (_stricmp(hex, package->sha256)) hr = HRESULT_FROM_WIN32(ERROR_CRC);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return hr;
}

HRESULT vendor_download_package(const VendorPackage *package, const WCHAR *root,
    VendorCancelled cancelled, VendorProgress progress, WCHAR *saved, size_t chars) {
    HINTERNET session = NULL, connection = NULL, request = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    WCHAR url_text[2048], host[256] = L"", path[2048] = L"", extra[1024] = L"", target[3072];
    WCHAR guid[40], folder[MAX_PATH] = L"", partial[MAX_PATH] = L"", final[MAX_PATH];
    GUID id;
    BYTE buffer[65536];
    uint64_t received = 0;
    TransferSample sample = {0};
    saved[0] = 0;
    HRESULT hr = E_INVALIDARG;
    if (PathIsRelativeW(root) || wcslen(root) > 150) return hr;
    int error = SHCreateDirectoryExW(NULL, root, NULL);
    if (error != ERROR_SUCCESS && error != ERROR_ALREADY_EXISTS) return HRESULT_FROM_WIN32(error);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, package->url, -1, url_text, ARRAYSIZE(url_text)))
        return E_INVALIDARG;
    URL_COMPONENTS url = {.dwStructSize = sizeof(url), .lpszHostName = host,
        .dwHostNameLength = ARRAYSIZE(host), .lpszUrlPath = path, .dwUrlPathLength = ARRAYSIZE(path),
        .lpszExtraInfo = extra, .dwExtraInfoLength = ARRAYSIZE(extra)};
    if (!WinHttpCrackUrl(url_text, 0, 0, &url) || url.nScheme != INTERNET_SCHEME_HTTPS) return E_INVALIDARG;
    hr = StringCchPrintfW(target, ARRAYSIZE(target), L"%s%s", path, extra);
    if (FAILED(hr)) return hr;
    if (cancelled()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    session = WinHttpOpen(L"DriverSource Vendor/0.2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto winerror;
    WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);
    connection = WinHttpConnect(session, host, url.nPort, 0);
    if (!connection) goto winerror;
    request = WinHttpOpenRequest(connection, L"GET", target, NULL, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) goto winerror;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect))) goto winerror;
    progress(-1, L"正在连接厂商下载服务…");
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto winerror;
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX)) goto winerror;
    if (status != 200) {
        WCHAR text[128];
        StringCchPrintfW(text, ARRAYSIZE(text), L"厂商下载服务返回 HTTP %lu", (unsigned long)status);
        progress(0, text);
        hr = HRESULT_FROM_WIN32(ERROR_BAD_NET_RESP); goto done;
    }
    hr = CoCreateGuid(&id);
    if (FAILED(hr)) goto done;
    StringFromGUID2(&id, guid, ARRAYSIZE(guid));
    hr = StringCchPrintfW(folder, ARRAYSIZE(folder), L"%s\\Vendor-%s", root, guid);
    if (FAILED(hr)) goto done;
    if (!CreateDirectoryW(folder, NULL)) goto winerror;
    hr = StringCchPrintfW(partial, ARRAYSIZE(partial), L"%s\\package.zip.part", folder);
    if (SUCCEEDED(hr)) hr = StringCchPrintfW(final, ARRAYSIZE(final), L"%s\\package.zip", folder);
    if (FAILED(hr)) goto done;
    file = CreateFileW(partial, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto winerror;
    transfer_sample(&sample, GetTickCount64(), 0);
    while (!cancelled()) {
        DWORD count, written;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &count)) goto winerror;
        if (!count) break;
        if (received + count > package->bytes) { hr = HRESULT_FROM_WIN32(ERROR_CRC); goto done; }
        if (!WriteFile(file, buffer, count, &written, NULL)) goto winerror;
        if (written != count) { hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT); goto done; }
        received += count;
        double speed = transfer_sample(&sample, GetTickCount64(), (double)received);
        WCHAR text[256];
        if (sample.measured)
            StringCchPrintfW(text, ARRAYSIZE(text), L"下载 %.1f / %.1f KiB · 速度 %.1f KiB/s",
                (double)received / 1024.0, (double)package->bytes / 1024.0, speed / 1024.0);
        else StringCchPrintfW(text, ARRAYSIZE(text), L"下载 %.1f / %.1f KiB · 正在统计速度",
                (double)received / 1024.0, (double)package->bytes / 1024.0);
        progress((int)(received * 100 / package->bytes), text);
    }
    if (cancelled()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    if (!FlushFileBuffers(file)) goto winerror;
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    progress(-1, L"正在校验厂商驱动包 SHA-256…");
    hr = vendor_verify_file(partial, package);
    if (FAILED(hr)) goto done;
    if (cancelled()) { hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); goto done; }
    if (!MoveFileW(partial, final)) goto winerror;
    hr = StringCchCopyW(saved, chars, folder);
    if (SUCCEEDED(hr)) progress(100, L"厂商驱动包下载完成，SHA-256 校验通过");
    goto done;
winerror:
    hr = HRESULT_FROM_WIN32(GetLastError());
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (FAILED(hr) && partial[0]) DeleteFileW(partial);
    if (FAILED(hr) && folder[0]) RemoveDirectoryW(folder);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return hr;
}

/* No command interpreter, only an absolute Windows system executable. */
static HRESULT run_tool(const WCHAR *name, const WCHAR *args, const WCHAR *log_path,
                        VendorCancelled cancelled, BOOL allow_cancel, DWORD *result) {
    WCHAR system[MAX_PATH], tool[MAX_PATH], command[2048];
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, TRUE};
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    UINT length = GetSystemDirectoryW(system, ARRAYSIZE(system));
    if (!length || length >= ARRAYSIZE(system)) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    HRESULT hr = StringCchPrintfW(tool, ARRAYSIZE(tool), L"%s\\%s", system, name);
    if (SUCCEEDED(hr)) hr = StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\" %s", tool, args);
    if (FAILED(hr)) return hr;
    HANDLE output = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        hr = HRESULT_FROM_WIN32(GetLastError()); CloseHandle(output); return hr;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input; startup.hStdOutput = output; startup.hStdError = output;
    BOOL started = CreateProcessW(tool, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
    hr = started ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(input); CloseHandle(output);
    if (FAILED(hr)) return hr;
    CloseHandle(process.hThread);
    for (;;) {
        DWORD wait = WaitForSingleObject(process.hProcess, 200);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) { hr = HRESULT_FROM_WIN32(GetLastError()); break; }
        if (allow_cancel && cancelled && cancelled()) {
            TerminateProcess(process.hProcess, ERROR_CANCELLED);
            WaitForSingleObject(process.hProcess, INFINITE);
            hr = HRESULT_FROM_WIN32(ERROR_CANCELLED); break;
        }
    }
    if (SUCCEEDED(hr) && !GetExitCodeProcess(process.hProcess, result)) hr = HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(process.hProcess);
    return hr;
}

HRESULT vendor_extract_package(const VendorPackage *package, const WCHAR *saved,
    VendorCancelled cancelled, WCHAR *inf, size_t chars) {
    WCHAR archive[MAX_PATH], directory[MAX_PATH], log[MAX_PATH], arguments[1024], member[96];
    if (!vendor_safe_member(package->inf)) return E_INVALIDARG;
    HRESULT hr = StringCchPrintfW(archive, ARRAYSIZE(archive), L"%s\\package.zip", saved);
    if (FAILED(hr)) return hr;
    /* Verify again immediately before extraction, including when used outside the GUI. */
    hr = vendor_verify_file(archive, package);
    if (FAILED(hr)) return hr;
    if (cancelled()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    hr = StringCchPrintfW(directory, ARRAYSIZE(directory), L"%s\\extracted", saved);
    if (SUCCEEDED(hr)) hr = StringCchPrintfW(log, ARRAYSIZE(log), L"%s\\extract.log", saved);
    if (FAILED(hr)) return hr;
    if (!CreateDirectoryW(directory, NULL)) return HRESULT_FROM_WIN32(GetLastError());
    hr = StringCchPrintfW(arguments, ARRAYSIZE(arguments), L"-xf \"%s\" -C \"%s\"", archive, directory);
    if (FAILED(hr)) return hr;
    DWORD result = 0;
    hr = run_tool(L"tar.exe", arguments, log, cancelled, TRUE, &result);
    if (FAILED(hr)) return hr;
    if (result) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, package->inf, -1, member, ARRAYSIZE(member)))
        return E_INVALIDARG;
    hr = StringCchPrintfW(inf, chars, L"%s\\%s", directory, member);
    if (FAILED(hr)) return hr;
    DWORD attributes = GetFileAttributesW(inf);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return cancelled() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
}

HRESULT vendor_install_inf(const WCHAR *inf, const WCHAR *saved, BOOL *reboot) {
    WCHAR arguments[1024], log[MAX_PATH];
    *reboot = FALSE;
    HRESULT hr = StringCchPrintfW(arguments, ARRAYSIZE(arguments), L"/add-driver \"%s\" /install", inf);
    if (SUCCEEDED(hr)) hr = StringCchPrintfW(log, ARRAYSIZE(log), L"%s\\install.log", saved);
    if (FAILED(hr)) return hr;
    DWORD result = 0;
    hr = run_tool(L"pnputil.exe", arguments, log, NULL, FALSE, &result);
    if (FAILED(hr)) return hr;
    *reboot = result == ERROR_SUCCESS_REBOOT_REQUIRED || result == ERROR_SUCCESS_REBOOT_INITIATED;
    if (result == ERROR_SUCCESS || *reboot) return S_OK;
    return FAILED((HRESULT)result) ? (HRESULT)result : HRESULT_FROM_WIN32(result);
}
