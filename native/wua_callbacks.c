#define COBJMACROS
#include "wua_callbacks.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG (STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(IUnknown *, IUnknown *, IUnknown *);
} CallbackVtable;

typedef struct {
    const CallbackVtable *lpVtbl;
    LONG references;
    IID iid;
} Callback;

static ULONG STDMETHODCALLTYPE callback_addref(IUnknown *object) {
    Callback *callback = (Callback *)object;
    return (ULONG)InterlockedIncrement(&callback->references);
}

static HRESULT STDMETHODCALLTYPE callback_query(IUnknown *object, REFIID iid, void **result) {
    Callback *callback = (Callback *)object;
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &callback->iid)) return E_NOINTERFACE;
    *result = object;
    callback_addref(object);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE callback_release(IUnknown *object) {
    Callback *callback = (Callback *)object;
    LONG references = InterlockedDecrement(&callback->references);
    if (!references) free(callback);
    return (ULONG)references;
}

static HRESULT STDMETHODCALLTYPE callback_invoke(IUnknown *object, IUnknown *job, IUnknown *args) {
    (void)object; (void)job; (void)args;
    return S_OK;
}

static const CallbackVtable vtable = {callback_query, callback_addref, callback_release, callback_invoke};

/* Standard Windows Update callback IIDs from the Windows SDK. All have IUnknown + Invoke. */
static const IID search_completed = {0x88aee058,0xd4b0,0x4725,{0xa2,0xf1,0x81,0x4a,0x67,0xae,0x96,0x4c}};
static const IID download_completed = {0x77254866,0x9f5b,0x4c8e,{0xb9,0xe2,0xc7,0x7a,0x85,0x30,0xd6,0x4b}};
static const IID download_progress = {0x8c3f1cdd,0x6173,0x4591,{0xae,0xbd,0xa5,0x6a,0x53,0xca,0x77,0xc1}};
static const IID install_completed = {0x45f4f6f3,0xd602,0x4f98,{0x9a,0x8a,0x3e,0xfa,0x15,0x2a,0xd2,0xd3}};
static const IID install_progress = {0xe01402d5,0xf8da,0x43ba,{0xa0,0x12,0x38,0x89,0x4b,0xd0,0x48,0xf1}};

static IUnknown *make_callback(REFIID iid) {
    Callback *callback = calloc(1, sizeof(*callback));
    if (!callback) return NULL;
    callback->lpVtbl = &vtable;
    callback->references = 1;
    callback->iid = *iid;
    return (IUnknown *)callback;
}

void wua_callbacks_release(WuaCallbacks *callbacks) {
    if (callbacks->progress) IUnknown_Release(callbacks->progress);
    if (callbacks->completed) IUnknown_Release(callbacks->completed);
    memset(callbacks, 0, sizeof(*callbacks));
}

HRESULT wua_callbacks_create(WuaOperation operation, WuaCallbacks *callbacks) {
    if (!callbacks) return E_POINTER;
    memset(callbacks, 0, sizeof(*callbacks));
    switch (operation) {
    case WUA_SEARCH:
        callbacks->completed = make_callback(&search_completed);
        break;
    case WUA_DOWNLOAD:
        callbacks->completed = make_callback(&download_completed);
        callbacks->progress = make_callback(&download_progress);
        break;
    case WUA_INSTALL:
        callbacks->completed = make_callback(&install_completed);
        callbacks->progress = make_callback(&install_progress);
        break;
    default: return E_INVALIDARG;
    }
    if (!callbacks->completed || (operation != WUA_SEARCH && !callbacks->progress)) {
        wua_callbacks_release(callbacks);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

BOOL wua_should_fallback(HRESULT result, BOOL has_job, BOOL cancelled) {
    if (has_job || cancelled) return FALSE;
    return result == E_POINTER || result == E_ACCESSDENIED || result == RPC_E_ACCESS_DENIED ||
           result == E_NOTIMPL;
}
