#ifndef UNICODE
#define UNICODE
#endif
#include "../native/installed_driver.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(installed_is_oem_inf(L"oem123.inf"));
    assert(installed_is_oem_inf(L"OEM0.INF"));
    const WCHAR *bad[]={L"",L"oem.inf",L"oem1.inf.exe",L"oem1.inf:stream",L"..\\oem1.inf",L"C:\\Windows\\INF\\oem1.inf",L"winusb.inf",L"oem-1.inf"};
    WCHAR path[MAX_PATH];
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        assert(!installed_is_oem_inf(bad[i]));
        assert(installed_store_inf(bad[i],path,ARRAYSIZE(path))==E_INVALIDARG);
    }
    HDEVINFO set=SetupDiGetClassDevsW(NULL,NULL,NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    assert(set!=INVALID_HANDLE_VALUE);
    unsigned devices=0,registered=0,versions=0,local=0;
    SP_DEVINFO_DATA device={.cbSize=sizeof(device)};
    for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&device);++i) {
        InstalledDriver driver; installed_driver_read(set,&device,&driver); ++devices;
        if(driver.registered)++registered;
        if(driver.version[0])++versions;
        if(driver.local_available) {
            ++local;
            assert(installed_is_oem_inf(driver.published_inf));
            assert(GetFileAttributesW(driver.store_inf)!=INVALID_FILE_ATTRIBUTES);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    assert(devices>0 && registered>0 && versions>0);
    assert(installed_driver_check(L"",L"oem0.inf",L"",path,ARRAYSIZE(path))==E_INVALIDARG);
    assert(installed_driver_check(L"not-a-device",L"oem0.inf",L"",path,ARRAYSIZE(path))!=S_OK);
    printf("Read-only Windows inventory: %u devices, %u registered, %u versions, %u available local third-party packages; no installation executed\n",devices,registered,versions,local);
    return 0;
}
