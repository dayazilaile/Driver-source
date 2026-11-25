#include "oem_catalog.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int normalized_equal(const char *a, const char *b) {
    while (*a || *b) {
        while (*a && !isalnum((unsigned char)*a)) ++a;
        while (*b && !isalnum((unsigned char)*b)) ++b;
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        if (!*a) return 1;
        ++a; ++b;
    }
    return 1;
}
static int id_equal(const char *ids, const char *id) {
    if (strlen(id) != 4) return 0;
    for (const char *p = ids; *p;) {
        if (toupper((unsigned char)p[0]) == toupper((unsigned char)id[0]) &&
            toupper((unsigned char)p[1]) == toupper((unsigned char)id[1]) &&
            toupper((unsigned char)p[2]) == toupper((unsigned char)id[2]) &&
            toupper((unsigned char)p[3]) == toupper((unsigned char)id[3])) return 1;
        p += 4;
        if (*p == ';') ++p;
    }
    return 0;
}
const char *oem_windows_release(unsigned build) {
    switch (build) {
    case 17763: return "1809"; case 18362: return "1903"; case 18363: return "1909";
    case 19041: return "2004"; case 19042: return "20H2"; case 19043: return "21H1";
    case 19044: return "21H2"; case 19045: return "22H2"; case 22000: return "21H2";
    case 22621: return "22H2"; case 22631: return "23H2"; case 26100: return "24H2";
    case 26200: return "25H2"; default: return "unknown";
    }
}
int oem_catalog_parse(OemCatalog *catalog, char *text, char *error, size_t chars) {
    catalog->count = 0;
    if (chars) error[0] = 0;
    size_t line_no = 0;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n'); if (next) *next++ = 0;
        ++line_no;
        size_t len = strlen(line); if (len && line[len - 1] == '\r') line[--len] = 0;
        if (!len || *line == '#') { line = next; continue; }
        char *f[11]; size_t n = 1; f[0] = line;
        for (char *p = line; *p; ++p) if (*p == '\t') {
            *p = 0; if (n == 11) goto invalid; f[n++] = p + 1;
        }
        if (n != 11 || catalog->count == OEM_MAX_PACKAGES) goto invalid;
        for (size_t i = 0; i < n; ++i) if (!*f[i]) goto invalid;
        if (!vendor_safe_member(f[0]) || strlen(f[0]) > 48 || strlen(f[2]) > 240 ||
            (strcmp(f[1], "lenovo") && strcmp(f[1], "dell") && strcmp(f[1], "hp")) ||
            (strcmp(f[4], "win10") && strcmp(f[4], "win11")) || strlen(f[5]) > 12 ||
            strlen(f[6]) != 10 || strlen(f[8]) != 64) goto invalid;
        const char *prefix = !strcmp(f[1], "lenovo") ? "https://download.lenovo.com/" :
            !strcmp(f[1], "dell") ? "https://downloads.dell.com/" : "https://ftp.hp.com/pub/softpaq/";
        if (strncmp(f[7], prefix, strlen(prefix)) || strlen(f[7]) > 1900 ||
            strpbrk(f[7], "\"@\r\n\t ")) goto invalid;
        for (size_t i = 0; i < 64; ++i) if (!isxdigit((unsigned char)f[8][i])) goto invalid;
        for (const char *p = f[3];;) {
            for (int i = 0; i < 4; ++i) { if (!*p || !isalnum((unsigned char)*p)) goto invalid; ++p; }
            if (!*p) break;
            if (*p++ != ';' || !*p) goto invalid;
        }
        for (const char *p = f[9]; *p; ++p) if (*p < '0' || *p > '9') goto invalid;
        errno = 0; char *end;
        unsigned long long bytes = strtoull(f[9], &end, 10);
        if (errno || *end || bytes > UINT64_C(17179869184)) goto invalid;
        VendorFormat format;
        if (!strcmp(f[10], "cab") && !strcmp(f[1], "dell") && bytes) format = VENDOR_CAB_PACK;
        else if (!strcmp(f[10], "exe-download")) format = VENDOR_EXE_DOWNLOAD;
        else goto invalid;
        size_t url_len = strlen(f[7]);
        const char *extension = format == VENDOR_CAB_PACK ? ".cab" : ".exe";
        if (url_len < 4 || !normalized_equal(f[7] + url_len - 4, extension)) goto invalid;
        OemPackage *row = &catalog->packages[catalog->count++];
        *row = (OemPackage){f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], (uint64_t)bytes, format};
        line = next; continue;
invalid:
        if (chars) snprintf(error, chars, "Invalid OEM catalog at line %zu", line_no);
        catalog->count = 0; return 0;
    }
    return 1;
}
static int hex_system_id(const char *id) {
    if (!id || strlen(id) != 4) return 0;
    for (size_t i = 0; i < 4; ++i) if (!isxdigit((unsigned char)id[i])) return 0;
    return 1;
}

int oem_catalog_select_machine(const OemCatalog *catalog, const OemMachine *machine) {
    if (!machine || !machine->manufacturer || !machine->product || !machine->native_x64 || machine->build < 17763) return -1;
    const char *manufacturer = machine->manufacturer, *product = machine->product;
    const char *sku = machine->system_sku ? machine->system_sku : "";
    const char *board = machine->baseboard ? machine->baseboard : "";
    unsigned build = machine->build;
    const char *vendor = NULL;
    if (normalized_equal(manufacturer, "Lenovo")) vendor = "lenovo";
    else if (normalized_equal(manufacturer, "Dell Inc") || normalized_equal(manufacturer, "Dell")) vendor = "dell";
    else if (normalized_equal(manufacturer, "HP") || normalized_equal(manufacturer, "HP Inc") ||
        normalized_equal(manufacturer, "Hewlett-Packard") || normalized_equal(manufacturer, "Hewlett-Packard Company")) vendor = "hp";
    if (!vendor || !*product) return -1;
    char type[5] = {0};
    if (!strcmp(vendor, "lenovo") && strlen(product) >= 4) {
        int valid = 1; for (int i = 0; i < 4; ++i) if (!isalnum((unsigned char)product[i])) valid = 0;
        if (valid) memcpy(type, product, 4);
    }
    int best = -1, best_score = -1;
    for (size_t i = 0; i < catalog->count; ++i) {
        const OemPackage *p = &catalog->packages[i];
        if (strcmp(vendor, p->manufacturer) || strcmp(p->os, build >= 22000 ? "win11" : "win10")) continue;
        int exact_release = !strcmp(p->release, oem_windows_release(build));
        if (!exact_release && strcmp(p->release, "*")) continue;
        int same_name = normalized_equal(product, p->model);
        int match;
        if (!strcmp(vendor, "lenovo")) match = id_equal(p->system_ids, type) || same_name;
        else if (!strcmp(vendor, "dell")) match = hex_system_id(sku) ? id_equal(p->system_ids, sku) : same_name;
        else match = hex_system_id(board) ? id_equal(p->system_ids, board) : same_name;
        if (!match) continue;
        int score = (exact_release ? 20 : 10) + same_name;
        if (score > best_score || (score == best_score && best >= 0 && strcmp(p->date, catalog->packages[best].date) > 0)) {
            best = (int)i; best_score = score;
        }
    }
    return best;
}

int oem_catalog_select(const OemCatalog *catalog, const char *manufacturer,
    const char *product, const char *system_sku, unsigned build, int native_x64) {
    OemMachine machine = {manufacturer, product, system_sku, "", build, native_x64};
    return oem_catalog_select_machine(catalog, &machine);
}
void oem_package_as_vendor(const OemPackage *pack, VendorPackage *vendor) {
    *vendor = (VendorPackage){pack->key, pack->model, pack->manufacturer, pack->date,
        pack->system_ids, pack->url, pack->sha256, "*.inf", pack->bytes, 17763, pack->format};
}
