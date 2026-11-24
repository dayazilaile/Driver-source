#include "vendor_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

static int equal_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
    return 1;
}

int vendor_safe_member(const char *path) {
    if (!path || !*path || !strcmp(path, ".") || !strcmp(path, "..")) return 0;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.') return 0;
    return 1;
}

static int parse_number(const char *text, uint64_t *value) {
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    errno = 0;
    char *end;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno || *end) return 0;
    *value = (uint64_t)number;
    return 1;
}

static int valid_ids(const char *ids) {
    const char *p = ids;
    while (*p) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len < 21 || len > 200 ||
            !(equal_n(p, "USB\\VID_", 8) || equal_n(p, "PCI\\VEN_", 8))) return 0;
        /* A complete device ID is required, never a vendor-only pattern or wildcard. */
        if (!(equal_n(p + 12, "&PID_", 5) || equal_n(p + 12, "&DEV_", 5))) return 0;
        for (size_t i = 8; i < 12; ++i) if (!isxdigit((unsigned char)p[i])) return 0;
        for (size_t i = 17; i < 21; ++i) if (!isxdigit((unsigned char)p[i])) return 0;
        for (size_t i = 21; i < len; ++i)
            if (!isalnum((unsigned char)p[i]) && p[i] != '&' && p[i] != '_') return 0;
        if (!end) return 1;
        p = end + 1;
        if (!*p) return 0;
    }
    return 0;
}

int vendor_catalog_parse(VendorCatalog *catalog, char *text, char *error, size_t chars) {
    catalog->count = 0;
    if (chars) error[0] = 0;
    size_t line_number = 0;
    for (char *line = text; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        ++line_number;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = 0;
        if (!len || line[0] == '#') { line = next; continue; }
        char *fields[11];
        size_t count = 1;
        fields[0] = line;
        for (char *p = line; *p; ++p) {
            if (*p == '\t') {
                *p = 0;
                if (count == 11) goto invalid;
                fields[count++] = p + 1;
            }
        }
        if (count != 11 || catalog->count >= VENDOR_MAX_PACKAGES) goto invalid;
        for (size_t i = 0; i < count; ++i) if (!fields[i][0]) goto invalid;
        uint64_t bytes, build;
        if (!vendor_safe_member(fields[0]) || strlen(fields[0]) > 48 ||
            !vendor_safe_member(fields[10]) || strlen(fields[10]) > 80 ||
            strlen(fields[10]) < 5 || strcmp(fields[10] + strlen(fields[10]) - 4, ".inf") ||
            !valid_ids(fields[4]) || strncmp(fields[5], "https://", 8) ||
            strlen(fields[5]) > 1900 || strchr(fields[5], ' ') || strchr(fields[5], '"') ||
            strchr(fields[5] + 8, '@') || strlen(fields[6]) != 64 ||
            !parse_number(fields[7], &bytes) || !bytes || bytes > UINT64_C(2147483648) ||
            !parse_number(fields[8], &build) || build < 17763 || build > 999999 ||
            strcmp(fields[9], "x64")) goto invalid;
        for (size_t i = 0; i < 64; ++i) if (!isxdigit((unsigned char)fields[6][i])) goto invalid;
        for (size_t i = 0; i < catalog->count; ++i)
            if (!strcmp(catalog->packages[i].key, fields[0])) goto invalid;
        VendorPackage *row = &catalog->packages[catalog->count++];
        *row = (VendorPackage){fields[0], fields[1], fields[2], fields[3], fields[4],
                              fields[5], fields[6], fields[10], bytes, (unsigned)build};
        line = next;
        continue;
invalid:
        if (chars) snprintf(error, chars, "Invalid vendor catalog at line %zu", line_number);
        catalog->count = 0;
        return 0;
    }
    return 1;
}

int vendor_package_matches(const VendorPackage *package, const char *hardware_id,
                           unsigned build, int native_x64) {
    if (!native_x64 || build < package->min_build) return 0;
    size_t actual = strlen(hardware_id);
    const char *p = package->hardware_ids;
    while (*p) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (actual == len && equal_n(p, hardware_id, len)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}
