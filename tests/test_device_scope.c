#include "../native/device_scope.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(device_in_download_scope(L"Display",L"PCI\\VEN_8086&DEV_9B41",0));
    assert(device_in_download_scope(L"Net",L"PCI\\VEN_8086&DEV_2723",0));
    assert(device_in_download_scope(L"Net",L"USB\\VID_0BDA&PID_8153",0));
    assert(device_in_download_scope(L"MEDIA",L"HDAUDIO\\FUNC_01&VEN_10EC",0));
    assert(device_in_download_scope(L"Bluetooth",L"USB\\VID_8087&PID_0026",0));
    assert(device_in_download_scope(L"HIDClass",L"HID\\VID_056A&PID_0376",1));
    assert(device_in_download_scope(L"USB",L"USB\\VID_256C&PID_006D",1));
    assert(device_in_download_scope(L"Ports",L"USB\\VID_1A86&PID_7523",0));
    assert(device_in_download_scope(L"",L"USB\\VID_10C4&PID_EA60",0));
    assert(device_in_download_scope(L"Ports",L"FTDIBUS\\VID_0403+PID_6001+123",0));
    assert(!device_in_download_scope(L"Net",L"ROOT\\MS_NDISWANIP",0));
    assert(!device_in_download_scope(L"Net",L"BTH\\MS_BTHPAN",0));
    assert(!device_in_download_scope(L"System",L"PCI\\VEN_8086&DEV_02A9",0));
    assert(!device_in_download_scope(L"System",L"ACPI\\PNP0C02",0));
    assert(!device_in_download_scope(L"SoftwareDevice",L"SWD\\PRINTENUM",0));
    assert(!device_in_download_scope(L"Bluetooth",L"BTHENUM\\AVRCP",0));
    assert(!device_in_download_scope(L"HIDClass",L"HID\\VID_046D&PID_C52B",0));
    assert(!device_in_download_scope(L"AudioEndpoint",L"SWD\\MMDEVAPI",0));
    assert(!device_in_download_scope(NULL,NULL,0));
    assert(missing_device_class_in_scope(L"PCI\\VEN_8086&DEV_9B41",L"PCI\\CC_030000"));
    assert(missing_device_class_in_scope(L"PCI\\VEN_10EC&DEV_8168",L"PCI\\VEN_10EC&CC_020000"));
    assert(!missing_device_class_in_scope(L"PCI\\VEN_8086&DEV_02A9",L"PCI\\CC_070002"));
    assert(!missing_device_class_in_scope(L"ROOT\\SOMETHING",L"PCI\\CC_030000"));
    puts("Common physical driver scope and virtual/system exclusions passed");
    return 0;
}
