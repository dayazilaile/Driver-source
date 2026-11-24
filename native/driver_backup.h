#ifndef DRIVER_BACKUP_H
#define DRIVER_BACKUP_H
#include <windows.h>
#include <stddef.h>

/* Failed exports remain in .partial directories with diagnostic logs. */
HRESULT backup_drivers(const WCHAR *root, WCHAR *saved, size_t chars);
HRESULT restore_drivers(const WCHAR *folder, const WCHAR *log_root, DWORD *result_code);
#endif
