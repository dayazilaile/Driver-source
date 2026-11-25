#ifndef DRIVER_OEM_CATALOG_H
#define DRIVER_OEM_CATALOG_H
#include "vendor_catalog.h"
#define OEM_MAX_PACKAGES 8192
typedef struct {
    const char *key, *manufacturer, *model, *system_ids, *os, *release, *date;
    const char *url, *sha256;
    uint64_t bytes;
    VendorFormat format;
} OemPackage;
typedef struct { OemPackage packages[OEM_MAX_PACKAGES]; size_t count; } OemCatalog;
typedef struct {
    const char *manufacturer, *product, *system_sku, *baseboard;
    unsigned build;
    int native_x64;
} OemMachine;
int oem_catalog_parse(OemCatalog *catalog, char *text, char *error, size_t chars);
/* Returns the best catalog record or -1. No fuzzy or cross-brand matching. */
int oem_catalog_select(const OemCatalog *catalog, const char *manufacturer,
    const char *product, const char *system_sku, unsigned build, int native_x64);
int oem_catalog_select_machine(const OemCatalog *catalog, const OemMachine *machine);
void oem_package_as_vendor(const OemPackage *pack, VendorPackage *vendor);
const char *oem_windows_release(unsigned build);
#endif
