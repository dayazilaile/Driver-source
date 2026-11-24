#ifndef DRIVER_DEVICE_DB_H
#define DRIVER_DEVICE_DB_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t key;
    const char *name;
    size_t length;
} IdEntry;
typedef struct {
    IdEntry *items;
    size_t count, capacity;
} IdTable;
typedef struct {
    uint32_t key;
    const char *name;
    size_t length;
    char source[32];
} TabletEntry;
typedef struct {
    IdTable usb_vendors, usb_products, pci_vendors, pci_devices, pci_subsystems;
    TabletEntry *tablets;
    size_t tablet_count, tablet_capacity;
} DeviceDatabase;

typedef enum { DB_UNKNOWN, DB_VENDOR, DB_DEVICE, DB_SUBSYSTEM, DB_TABLET } MatchLevel;
typedef struct {
    char vendor[256], model[768], sources[8][32];
    int source_count, tablet_candidates;
    MatchLevel level;
} DeviceMatch;

/* Input buffers remain owned by the caller and must live until device_db_free. */
int device_db_load(DeviceDatabase *db, const char *usb, size_t usb_size,
                   const char *pci, size_t pci_size, const char *tablets, size_t tablet_size);
int device_db_hint_name(DeviceMatch *match, const char *name);
void device_db_free(DeviceDatabase *db);
int device_db_match(const DeviceDatabase *db, const char *hardware_id, DeviceMatch *match);
#endif
