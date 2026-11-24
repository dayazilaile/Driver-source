#include "../native/vendor_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VendorCatalog catalog;
static const char valid[] = "test\tA device\tVendor\t1.0\tUSB\\VID_303A&PID_0002&MI_02;PCI\\VEN_1234&DEV_5678&SUBSYS_11223344\thttps://example.com/driver.zip\t5878e019324d3ecb57f60b46caa102e2ab01f560f8fef09e33ca31d87ef950a4\t9366\t17763\tx64\tdriver.inf\n";

static void rejected(const char *text) {
    char buffer[4096], error[256];
    assert(strlen(text) < sizeof(buffer));
    strcpy(buffer, text);
    assert(!vendor_catalog_parse(&catalog, buffer, error, sizeof(error)));
    assert(catalog.count == 0 && error[0]);
}

int main(void) {
    char buffer[4096], error[256];
    strcpy(buffer, valid);
    assert(vendor_catalog_parse(&catalog, buffer, error, sizeof(error)));
    assert(catalog.count == 1);
    const VendorPackage *p = &catalog.packages[0];
    assert(vendor_package_matches(p, "usb\\vid_303a&pid_0002&mi_02", 22631, 1));
    assert(vendor_package_matches(p, "PCI\\VEN_1234&DEV_5678&SUBSYS_11223344", 17763, 1));
    assert(!vendor_package_matches(p, "USB\\VID_303A&PID_0002&MI_00", 22631, 1));
    assert(!vendor_package_matches(p, "USB\\VID_303A&PID_9999&MI_02", 22631, 1));
    assert(!vendor_package_matches(p, "USB\\VID_303A&PID_0002", 22631, 1));
    assert(!vendor_package_matches(p, "USB\\VID_303A&PID_0002&MI_02", 17762, 1));
    assert(!vendor_package_matches(p, "USB\\VID_303A&PID_0002&MI_02", 22631, 0));
    assert(!vendor_package_matches(p, "PCI\\VEN_1234&DEV_5678&SUBSYS_AABBCCDD", 22631, 1));
    assert(!vendor_package_matches(p, "PCI\\VEN_1234&DEV_5678", 22631, 1));
    assert(vendor_safe_member("driver.inf"));
    assert(!vendor_safe_member("../driver.inf"));
    assert(!vendor_safe_member("..\\driver.inf"));
    assert(!vendor_safe_member("C:driver.inf"));
    assert(!vendor_safe_member("driver.inf:stream"));
    assert(!vendor_safe_member("driver\".inf"));
    rejected("broken\trow\n");
    strcpy(buffer, valid); strcat(buffer, valid); rejected(buffer);
    strcpy(buffer, valid); char *https = strstr(buffer, "https:"); https[4] = 'x'; rejected(buffer);
    strcpy(buffer, valid); char *sha = strstr(buffer, "5878e019"); sha[0] = 'z'; rejected(buffer);
    strcpy(buffer, valid); char *ids = strstr(buffer, "VID_303A"); ids[4] = '*'; rejected(buffer);
    FILE *file = fopen("database/vendor-packages.tsv", "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); long size = ftell(file);
    assert(size > 0 && size < 4194304); rewind(file);
    char *data = malloc((size_t)size + 1); assert(data);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size); fclose(file); data[size] = 0;
    assert(vendor_catalog_parse(&catalog, data, error, sizeof(error)));
    assert(catalog.count >= 2);
    for (size_t i = 0; i < catalog.count; ++i) {
        assert(!vendor_package_matches(&catalog.packages[i], "USB\\VID_0403&PID_6010&MI_00", 22631, 1));
        assert(!vendor_package_matches(&catalog.packages[i], "USB\\VID_8087&PID_0002", 22631, 1));
    }
    printf("Vendor catalog: %zu pinned packages, exact hardware/OS matching passed\n", catalog.count);
    free(data);
    return 0;
}
