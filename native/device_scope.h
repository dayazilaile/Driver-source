#ifndef DRIVER_DEVICE_SCOPE_H
#define DRIVER_DEVICE_SCOPE_H
#include <stddef.h>
#include <wchar.h>
#include <wctype.h>
/* Keep the download page focused on physical, commonly serviced hardware.
   The full SetupAPI inventory is still retained on the hardware tab. */
static int scope_prefix(const wchar_t *text, const wchar_t *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) if (towupper(*text++) != towupper(*prefix++)) return 0;
    return 1;
}
static int scope_class(const wchar_t *actual, const wchar_t *expected) {
    return actual && scope_prefix(actual, expected) && wcslen(actual) == wcslen(expected);
}
static int device_in_download_scope(const wchar_t *class_name, const wchar_t *id, int tablet) {
    int pci = scope_prefix(id, L"PCI\\VEN_");
    int usb = scope_prefix(id, L"USB\\VID_");
    if (tablet) return usb || scope_prefix(id, L"HID\\VID_");
    if (scope_class(class_name, L"Display")) return pci || usb || scope_prefix(id, L"ACPI\\");
    if (scope_class(class_name, L"Net") || scope_class(class_name, L"Bluetooth"))
        return pci || usb || scope_prefix(id, L"SD\\VID_");
    if (scope_class(class_name, L"MEDIA")) return pci || usb ||
        scope_prefix(id, L"HDAUDIO\\") || scope_prefix(id, L"INTELAUDIO\\");
    if (scope_class(class_name, L"Ports")) return pci || usb || scope_prefix(id, L"FTDIBUS\\");
    /* Missing USB serial drivers have not acquired the Ports class yet. */
    return scope_prefix(id, L"USB\\VID_1A86&PID_7523") ||
        scope_prefix(id, L"USB\\VID_1A86&PID_5523") ||
        scope_prefix(id, L"USB\\VID_10C4&PID_EA60") ||
        scope_prefix(id, L"USB\\VID_0403&PID_6001");
}
/* Compatible PCI class IDs remain available before a driver assigns a class. */
static int missing_device_class_in_scope(const wchar_t *hardware, const wchar_t *compatible) {
    if (!scope_prefix(hardware, L"PCI\\VEN_") || !scope_prefix(compatible,L"PCI\\")) return 0;
    const wchar_t *code = wcsstr(compatible,L"CC_");
    if (!code) return 0;
    return scope_prefix(code,L"CC_02") || scope_prefix(code,L"CC_03") || scope_prefix(code,L"CC_04");
}
#endif
