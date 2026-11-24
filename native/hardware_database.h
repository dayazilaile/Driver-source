#ifndef DRIVER_HARDWARE_DATABASE_H
#define DRIVER_HARDWARE_DATABASE_H
#include <windows.h>
#include <setupapi.h>
#include "device_db.h"

typedef struct {
    DeviceDatabase index;
    char *buffers[3];
} HardwareDatabase;
typedef struct {
    DeviceMatch match;
    WCHAR category[128], basis[128], matched_id[2048], primary_id[2048];
} HardwareIdentity;

HRESULT hardware_database_open(HardwareDatabase *db, const WCHAR *root, WCHAR *summary, size_t chars);
void hardware_database_close(HardwareDatabase *db);
void hardware_identify(const HardwareDatabase *db, HDEVINFO set, SP_DEVINFO_DATA *device,
                       HardwareIdentity *identity);
#endif
