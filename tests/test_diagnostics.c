#ifndef UNICODE
#define UNICODE
#endif
#include "../native/update_diagnostics.h"
#include <objbase.h>
#include <strsafe.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    WCHAR text[6000], base[MAX_PATH], root[MAX_PATH], folder[MAX_PATH], log[MAX_PATH], id[40];
    GUID guid;
    collect_update_diagnostics(text, ARRAYSIZE(text));
    assert(wcsstr(text, L"wuauserv"));
    assert(wcsstr(text, L"UseWUServer"));
    assert(wcsstr(text, L"进程令牌"));
    assert(GetTempPathW(ARRAYSIZE(base), base));
    assert(SUCCEEDED(CoCreateGuid(&guid)));
    assert(StringFromGUID2(&guid, id, ARRAYSIZE(id)));
    assert(SUCCEEDED(StringCchPrintfW(root, ARRAYSIZE(root), L"%sDriverSourceTest-%s", base, id)));
    assert(CreateDirectoryW(root, NULL));
    assert(SUCCEEDED(append_update_log(root, L"read-only diagnostics log test")));
    assert(SUCCEEDED(StringCchPrintfW(folder, ARRAYSIZE(folder), L"%s\\logs", root)));
    assert(SUCCEEDED(StringCchPrintfW(log, ARRAYSIZE(log), L"%s\\DriverSource.log", folder)));
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    assert(GetFileAttributesExW(log, GetFileExInfoStandard, &attributes));
    assert(attributes.nFileSizeLow > 0);
    assert(DeleteFileW(log));
    assert(RemoveDirectoryW(folder));
    assert(RemoveDirectoryW(root));
    puts("Read-only diagnostics and local UTF-8 logging passed");
    return 0;
}
