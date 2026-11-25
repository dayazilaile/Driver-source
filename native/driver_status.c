#include "driver_status.h"
#include <stdint.h>
static int parse_version(const char *text, uint32_t parts[4]) {
    if (!text || !*text) return 0;
    unsigned index = 0;
    for (unsigned i = 0; i < 4; ++i) parts[i] = 0;
    for (;;) {
        if (*text < '0' || *text > '9' || index >= 4) return 0;
        uint32_t value = 0;
        do {
            value = value * 10 + (uint32_t)(*text++ - '0');
            if (value > 65535) return 0;
        } while (*text >= '0' && *text <= '9');
        parts[index++] = value;
        if (!*text) return 1;
        if (*text++ != '.') return 0;
    }
}
int driver_version_compare(const char *left, const char *right, int *valid) {
    uint32_t a[4], b[4];
    *valid = parse_version(left, a) && parse_version(right, b);
    if (!*valid) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}
DriverAdvice driver_advice(int installed, int has_package, int same_provider, int has_local,
                          const char *current_version, const char *available_version) {
    if (!has_package) return has_local ? DRIVER_LOCAL_MATCH : DRIVER_NO_SOURCE;
    if (!installed) return DRIVER_CAN_INSTALL;
    if (!same_provider) return DRIVER_VERSION_UNCERTAIN;
    int valid;
    int compared = driver_version_compare(current_version, available_version, &valid);
    if (!valid) return DRIVER_VERSION_UNCERTAIN;
    if (compared < 0) return DRIVER_CAN_UPDATE;
    if (compared > 0) return DRIVER_KEEP_NEWER;
    return DRIVER_CAN_REINSTALL;
}
