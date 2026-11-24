#ifndef DRIVER_UPDATE_DIAGNOSTICS_H
#define DRIVER_UPDATE_DIAGNOSTICS_H
#include <windows.h>
#include <stddef.h>

BOOL update_process_elevated(void);
void collect_update_diagnostics(WCHAR *text, size_t chars);
HRESULT append_update_log(const WCHAR *root, const WCHAR *text);
#endif
