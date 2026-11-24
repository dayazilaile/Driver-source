#include <assert.h>
#include "../native/transfer.h"

int main(void) {
    TransferSample sample = {0};
    /* An existing Windows Update cache must not appear as an initial network burst. */
    assert(transfer_sample(&sample, 1000, 8000000) == 0);
    assert(!sample.measured);
    assert(transfer_sample(&sample, 2000, 9000000) == 1000000);
    assert(transfer_sample(&sample, 3000, 9000000) == 0);
    /* Retry and counter reset re-establish a baseline. */
    assert(transfer_sample(&sample, 3500, 0) == 0);
    assert(!sample.measured);
    assert(transfer_sample(&sample, 4000, 500000) == 1000000);
    /* Several progress notifications may share one timestamp. */
    assert(transfer_sample(&sample, 4000, 500000) == 1000000);
    assert(transfer_sample(&sample, 500, 500000) == 0);
    return 0;
}
