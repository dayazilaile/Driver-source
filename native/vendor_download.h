#ifndef DRIVER_VENDOR_DOWNLOAD_H
#define DRIVER_VENDOR_DOWNLOAD_H
#include <windows.h>
#include "vendor_catalog.h"
typedef BOOL (*VendorCancelled)(void);
typedef void (*VendorProgress)(int percent, const WCHAR *text);
HRESULT vendor_download_package(const VendorPackage *package, const WCHAR *root,
    VendorCancelled cancelled, VendorProgress progress, WCHAR *saved, size_t chars);
HRESULT vendor_extract_package(const VendorPackage *package, const WCHAR *saved,
    VendorCancelled cancelled, WCHAR *inf, size_t chars);
HRESULT vendor_install_inf(const WCHAR *inf, const WCHAR *saved, BOOL *reboot);
HRESULT vendor_verify_file(const WCHAR *path, const VendorPackage *package);
#endif
