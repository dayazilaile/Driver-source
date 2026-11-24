#ifndef UNICODE
#define UNICODE
#endif
#include "../native/vendor_download.h"
#include <objbase.h>
#include <strsafe.h>
#include <shlwapi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static BOOL never_cancel(void) { return FALSE; }
static BOOL always_cancel(void) { return TRUE; }
static void progress(int percent, const WCHAR *text) { (void)percent; (void)text; }
static void check(HRESULT hr, const char *step) {
    if (FAILED(hr)) printf("::error::%s returned 0x%08lX\n", step, (unsigned long)hr);
    assert(SUCCEEDED(hr));
}
static void remove_test_tree(const WCHAR *root) {
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW entry;
    check(StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\*", root), "cleanup pattern");
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            check(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s", root, entry.cFileName), "cleanup path");
            assert(!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_test_tree(path);
            else assert(DeleteFileW(path));
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    assert(RemoveDirectoryW(root));
}
int main(int argc, char **argv) {
    WCHAR base[MAX_PATH], root[MAX_PATH], path[MAX_PATH], id[40];
    GUID guid;
    assert(GetTempPathW(ARRAYSIZE(base), base));
    check(CoCreateGuid(&guid), "GUID"); assert(StringFromGUID2(&guid, id, ARRAYSIZE(id)));
    check(StringCchPrintfW(root, ARRAYSIZE(root), L"%sVendorTest-%s", base, id), "temp root");
    assert(CreateDirectoryW(root, NULL));
    check(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\hash.bin", root), "hash path");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE); DWORD written;
    assert(WriteFile(file, "hello", 5, &written, NULL) && written == 5); CloseHandle(file);
    VendorPackage fixture = {0};
    fixture.bytes = 5;
    fixture.sha256 = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    check(vendor_verify_file(path, &fixture), "SHA-256 known vector");
    fixture.bytes = 0;
    check(vendor_verify_file(path, &fixture), "SHA-256 with catalog size unknown");
    fixture.bytes = 6;
    assert(vendor_verify_file(path, &fixture) == HRESULT_FROM_WIN32(ERROR_CRC));
    fixture.bytes = 5; fixture.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    assert(vendor_verify_file(path, &fixture) == HRESULT_FROM_WIN32(ERROR_CRC));
    if (argc > 1 && !strcmp(argv[1], "--online")) {
        static VendorCatalog catalog;
        char text[10000], error[256];
        FILE *input = fopen("database/vendor-packages.tsv", "rb"); assert(input);
        size_t len = fread(text, 1, sizeof(text) - 1, input); assert(!ferror(input) && feof(input));
        fclose(input); text[len] = 0;
        assert(vendor_catalog_parse(&catalog, text, error, sizeof(error)) && catalog.count);
        for (size_t index = 0; index < catalog.count; ++index) {
        const VendorPackage *package = &catalog.packages[index];
        WCHAR saved[MAX_PATH], inf[MAX_PATH], zip[MAX_PATH];
        assert(vendor_download_package(package, root, always_cancel, progress, saved, ARRAYSIZE(saved)) == HRESULT_FROM_WIN32(ERROR_CANCELLED));
        assert(!saved[0]);
        check(vendor_download_package(package, root, never_cancel, progress, saved, ARRAYSIZE(saved)), "Vendor HTTPS download");
        check(vendor_extract_package(package, saved, never_cancel, inf, ARRAYSIZE(inf)), "Verified package extraction");
        assert(GetFileAttributesW(inf) != INVALID_FILE_ATTRIBUTES);
        check(StringCchPrintfW(zip, ARRAYSIZE(zip), L"%s\\package.zip", saved), "zip path");
        file = CreateFileW(zip, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); assert(file != INVALID_HANDLE_VALUE);
        assert(WriteFile(file, "!", 1, &written, NULL) && written == 1); CloseHandle(file);
        assert(vendor_verify_file(zip, package) == HRESULT_FROM_WIN32(ERROR_CRC));
        assert(vendor_extract_package(package, saved, never_cancel, inf, ARRAYSIZE(inf)) == HRESULT_FROM_WIN32(ERROR_CRC));
        }
        puts("All catalog packages: official download, extraction, cancellation and tamper rejection passed; no driver installation executed");
    }
    remove_test_tree(root);
    puts("Vendor SHA-256 verification passed");
    return 0;
}
