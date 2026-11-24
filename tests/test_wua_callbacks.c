#define COBJMACROS
#define INITGUID
#include "../native/wua_callbacks.h"
#include <objbase.h>
#include <wuapi.h>
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

static void check_hr(HRESULT result, const char *stage) {
    if (FAILED(result)) printf("::error::%s returned 0x%08lX\n", stage, (unsigned long)result);
    assert(SUCCEEDED(result));
}

/* Exercise the real WUA boundary using its local cache, then cancel the read-only search. */
static void check_offline_search(void) {
    IUpdateSession *session = NULL;
    IUpdateSearcher *searcher = NULL;
    ISearchJob *job = NULL;
    ISearchResult *result = NULL;
    WuaCallbacks callbacks = {0};
    VARIANT state;
    VARIANT_BOOL complete = VARIANT_FALSE;
    VariantInit(&state);
    check_hr(CoInitializeEx(NULL, COINIT_MULTITHREADED), "CoInitializeEx");
    HRESULT security = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    assert(SUCCEEDED(security) || security == RPC_E_TOO_LATE);
    check_hr(CoCreateInstance(&CLSID_UpdateSession, NULL, CLSCTX_INPROC_SERVER,
        &IID_IUpdateSession, (void **)&session), "CreateUpdateSession");
    check_hr(IUpdateSession_CreateUpdateSearcher(session, &searcher), "CreateUpdateSearcher");
    check_hr(IUpdateSearcher_put_Online(searcher, VARIANT_FALSE), "Offline mode");
    check_hr(wua_callbacks_create(WUA_SEARCH, &callbacks), "Create callback");
    BSTR criteria = SysAllocString(L"Type='Driver' and IsInstalled=0 and IsHidden=0");
    assert(criteria);
    check_hr(IUpdateSearcher_BeginSearch(searcher, criteria, callbacks.completed, state, &job), "BeginSearch with callback");
    assert(job);
    ISearchJob_RequestAbort(job);
    ULONGLONG started = GetTickCount64();
    while (!complete) {
        check_hr(ISearchJob_get_IsCompleted(job, &complete), "Search completion");
        if (!complete) {
            if (GetTickCount64() - started >= 30000) puts("::error::Read-only search cancellation timed out");
            assert(GetTickCount64() - started < 30000);
            Sleep(100);
        }
    }
    IUpdateSearcher_EndSearch(searcher, job, &result);
    ISearchJob_CleanUp(job);
    ISearchJob_Release(job);
    if (result) { ISearchResult_Release(result); result = NULL; }
    check_hr(IUpdateSearcher_Search(searcher, criteria, &result), "Synchronous offline Search");
    assert(result);
    ISearchResult_Release(result);
    wua_callbacks_release(&callbacks);
    IUpdateSearcher_Release(searcher);
    IUpdateSession_Release(session);
    SysFreeString(criteria);
    CoUninitialize();
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
    assert(wua_should_fallback(E_ACCESSDENIED, FALSE, FALSE));
    assert(wua_should_fallback(RPC_E_ACCESS_DENIED, FALSE, FALSE));
    assert(wua_should_fallback(E_POINTER, FALSE, FALSE));
    assert(wua_should_fallback(E_NOTIMPL, FALSE, FALSE));
    assert(!wua_should_fallback(E_ACCESSDENIED, TRUE, FALSE));
    assert(!wua_should_fallback(E_ACCESSDENIED, FALSE, TRUE));
    assert(!wua_should_fallback(S_OK, FALSE, FALSE));
    assert(!wua_should_fallback(E_INVALIDARG, FALSE, FALSE));
    check_offline_search();
    puts("Windows Update callbacks, reference counts and offline BeginSearch passed");
    return 0;
}
