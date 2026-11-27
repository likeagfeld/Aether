/* menu/host-tests/test_ajbudget.c -- M3 Task 17 carried forward: the art job's reads per frame
 * follow the measured SD rate. The user's card, firmware 163: 245-296 KB/s; one s_read is at most S_MAXBUF = 2,048 B
 * (HWF-22). Rule (M3 plan Task 17): floor(8 ms / t_read), at least 1, taken at the slow end. BSD-3-Clause. */
#include "th.h"
#include "artjob.h"

int main(void)
{
    const uint32_t kbps = 245u;                                          /* the slow end of the user's readings */
    uint32_t t_read_us = (2048u * 1000000u) / (kbps * 1024u);           /* 8,163 us */
    uint32_t want = 8000u / t_read_us;
    if (want < 1u) want = 1u;
    CHECK_INT(t_read_us, 8163);
    CHECK_INT(OM_AJ_READS_PER_STEP, want);
    return th_done("test_ajbudget");
}
