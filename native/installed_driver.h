#ifndef DRIVER_INSTALLED_H
#define DRIVER_INSTALLED_H
#include <windows.h>
#include <setupapi.h>
typedef struct {
    WCHAR provider[256], version[128], date[128], published_inf[128], store_inf[MAX_PATH];
    BOOL registered, local_available;
} InstalledDriver;
BOOL installed_is_oem_inf(const WCHAR *name);
HRESULT installed_store_inf(const WCHAR *published_inf, WCHAR *path, DWORD chars);
void installed_driver_read(HDEVINFO set, SP_DEVINFO_DATA *device, InstalledDriver *driver);
HRESULT installed_driver_check(const WCHAR *instance_id, const WCHAR *expected_inf,
                               const WCHAR *expected_version, WCHAR *path, DWORD chars);
#endif
