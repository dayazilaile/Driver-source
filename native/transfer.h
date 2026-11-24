#ifndef DRIVER_TRANSFER_H
#define DRIVER_TRANSFER_H
#include <stdint.h>

typedef struct {
    uint64_t time_ms;
    double bytes, bytes_per_second;
    int initialized, measured;
} TransferSample;

/* The first counter may already include cached bytes. Only later deltas form a rate. */
static double transfer_sample(TransferSample *sample, uint64_t now_ms, double bytes) {
    if (!sample->initialized || now_ms < sample->time_ms || bytes < sample->bytes) {
        sample->initialized = 1;
        sample->measured = 0;
        sample->time_ms = now_ms;
        sample->bytes = bytes;
        sample->bytes_per_second = 0;
    } else if (now_ms - sample->time_ms >= 500) {
        sample->bytes_per_second = (bytes - sample->bytes) * 1000.0 / (double)(now_ms - sample->time_ms);
        sample->bytes = bytes;
        sample->time_ms = now_ms;
        sample->measured = 1;
    }
    return sample->bytes_per_second;
}
#endif
