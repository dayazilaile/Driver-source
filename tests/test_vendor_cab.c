#ifndef UNICODE
#define UNICODE
#endif
#include "../native/vendor_download.h"
#include <assert.h>
#include <stdio.h>
#include <strsafe.h>
#include <shlwapi.h>
static BOOL never_cancel(void) { return FALSE; }
int main(void) {
    WCHAR root[MAX_PATH], path[MAX_PATH], inf[MAX_PATH];
    DWORD length = GetFullPathNameW(L"build\\cab test", ARRAYSIZE(root), root, NULL);
    assert(length && length < ARRAYSIZE(root));
    assert(SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\package.cab", root)));
    WIN32_FILE_ATTRIBUTE_DATA info;
    assert(GetFileAttributesExW(path, GetFileExInfoStandard, &info));
    FILE *file = fopen("build/cab test/hash.txt", "rb"); assert(file);
    char hash[65] = {0}; assert(fread(hash, 1, 64, file) == 64); fclose(file);
    VendorPackage package = {0};
    package.format = VENDOR_CAB_PACK; package.inf = "*.inf";
    package.bytes = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    package.sha256 = hash;
    HRESULT hr = vendor_extract_package(&package, root, never_cancel, inf, ARRAYSIZE(inf));
    if (FAILED(hr)) printf("::error::CAB extraction returned 0x%08lX\n", (unsigned long)hr);
    assert(SUCCEEDED(hr));
    assert(!wcscmp(PathFindFileNameW(inf), L"*.inf"));
    assert(SUCCEEDED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\extracted\\fixture.inf", root)));
    assert(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
    package.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    assert(vendor_extract_package(&package, root, never_cancel, inf, ARRAYSIZE(inf)) == HRESULT_FROM_WIN32(ERROR_CRC));
    package.format = VENDOR_EXE_DOWNLOAD;
    assert(vendor_extract_package(&package, root, never_cancel, inf, ARRAYSIZE(inf)) == E_NOTIMPL);
    puts("Native CAB extraction, INF discovery and EXE execution refusal passed; no driver installed");
    return 0;
}
