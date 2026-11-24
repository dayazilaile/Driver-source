#ifndef DRIVER_WUA_CALLBACKS_H
#define DRIVER_WUA_CALLBACKS_H
#include <windows.h>
#include <unknwn.h>

typedef enum { WUA_SEARCH, WUA_DOWNLOAD, WUA_INSTALL } WuaOperation;
typedef struct { IUnknown *progress, *completed; } WuaCallbacks;

/* Keep references through the operation result and job cleanup. Callbacks are thread-safe no-ops. */
HRESULT wua_callbacks_create(WuaOperation operation, WuaCallbacks *callbacks);
void wua_callbacks_release(WuaCallbacks *callbacks);
#endif
