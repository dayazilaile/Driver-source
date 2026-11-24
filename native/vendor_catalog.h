#ifndef DRIVER_VENDOR_CATALOG_H
#define DRIVER_VENDOR_CATALOG_H
#include <stddef.h>
#include <stdint.h>
#define VENDOR_MAX_PACKAGES 1024
/* Strings refer to the mutable UTF-8 buffer supplied to vendor_catalog_parse. */
typedef struct {
    const char *key, *title, *provider, *version, *hardware_ids;
    const char *url, *sha256, *inf;
    uint64_t bytes;
    unsigned min_build;
} VendorPackage;
typedef struct {
    VendorPackage packages[VENDOR_MAX_PACKAGES];
    size_t count;
} VendorCatalog;
int vendor_catalog_parse(VendorCatalog *catalog, char *text, char *error, size_t chars);
int vendor_package_matches(const VendorPackage *package, const char *hardware_id,
                           unsigned build, int native_x64);
int vendor_safe_member(const char *path);
#endif
