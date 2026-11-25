#ifndef DRIVER_STATUS_H
#define DRIVER_STATUS_H
typedef enum {
    DRIVER_NO_SOURCE, DRIVER_LOCAL_MATCH, DRIVER_CAN_INSTALL,
    DRIVER_CAN_UPDATE, DRIVER_CAN_REINSTALL, DRIVER_KEEP_NEWER, DRIVER_VERSION_UNCERTAIN
} DriverAdvice;
int driver_version_compare(const char *left, const char *right, int *valid);
DriverAdvice driver_advice(int installed, int has_package, int same_provider, int has_local,
                          const char *current_version, const char *available_version);
#endif
