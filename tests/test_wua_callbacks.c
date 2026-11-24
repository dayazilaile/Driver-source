#define COBJMACROS
#include "../native/wua_callbacks.h"
#include <objbase.h>
#include <assert.h>
#include <stdio.h>

typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IUnknown *);
    ULONG (STDMETHODCALLTYPE *Release)(IUnknown *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(IUnknown *, IUnknown *, IUnknown *);
} CallbackVtable;

static void check_callback(IUnknown *callback, const WCHAR *interface_id) {
    IID iid, unsupported;
    IUnknown *other = NULL;
    assert(callback);
    assert(SUCCEEDED(CLSIDFromString(interface_id, &iid)));
    assert(SUCCEEDED(CLSIDFromString(L"{11111111-2222-3333-4444-555555555555}", &unsupported)));
    assert(IUnknown_QueryInterface(callback, &IID_IUnknown, (void **)&other) == S_OK);
    assert(other == callback && IUnknown_Release(other) == 1);
    assert(IUnknown_QueryInterface(callback, &iid, (void **)&other) == S_OK);
    assert(other == callback);
    const CallbackVtable *vtable = *(const CallbackVtable **)other;
    assert(vtable->Invoke(other, NULL, NULL) == S_OK);
    assert(IUnknown_Release(other) == 1);
    other = callback;
    assert(IUnknown_QueryInterface(callback, &unsupported, (void **)&other) == E_NOINTERFACE);
    assert(other == NULL);
    assert(IUnknown_QueryInterface(callback, &iid, NULL) == E_POINTER);
}

int main(void) {
    const WCHAR *completed[] = {
        L"{88AEE058-D4B0-4725-A2F1-814A67AE964C}",
        L"{77254866-9F5B-4C8E-B9E2-C77A8530D64B}",
        L"{45F4F6F3-D602-4F98-9A8A-3EFA152AD2D3}"
    };
    const WCHAR *progress[] = {NULL,
        L"{8C3F1CDD-6173-4591-AEBD-A56A53CA77C1}",
        L"{E01402D5-F8DA-43BA-A012-38894BD048F1}"
    };
    for (int operation = 0; operation < 3; ++operation) {
        WuaCallbacks callbacks = {0};
        assert(wua_callbacks_create((WuaOperation)operation, &callbacks) == S_OK);
        check_callback(callbacks.completed, completed[operation]);
        if (operation) check_callback(callbacks.progress, progress[operation]);
        else assert(!callbacks.progress);
        wua_callbacks_release(&callbacks);
        assert(!callbacks.progress && !callbacks.completed);
        wua_callbacks_release(&callbacks);
    }
    WuaCallbacks callbacks = {0};
    assert(wua_callbacks_create((WuaOperation)99, &callbacks) == E_INVALIDARG);
    assert(wua_callbacks_create(WUA_SEARCH, NULL) == E_POINTER);
    puts("Windows Update callback interfaces and reference counts passed");
    return 0;
}
