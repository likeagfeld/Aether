#include "th.h"
#include "artsched.h"

int main(void)
{
    om_artsched s;
    int i, starts = 0;
    CHECK_INT(om_debounce_frames(5994, 150), 9);           /* NTSC 59.94 Hz (HWF-11) */
    CHECK_INT(om_debounce_frames(5000, 150), 8);           /* PAL 50 Hz */
    om_artsched_init(&s, 9);
    for (i = 0; i < 60; i++) {                             /* scrolling: a new item every frame */
        om_artsched_select(&s, (uint32_t)i);
        starts += om_artsched_should_start(&s);
    }
    CHECK_INT(starts, 0);                                  /* fast scrolling never queues a read (spec §7.4) */
    for (i = 0; i < 8; i++) {
        om_artsched_select(&s, 7);
        CHECK_INT(om_artsched_should_start(&s), 0);
    }
    om_artsched_select(&s, 7);
    CHECK_INT(om_artsched_should_start(&s), 1);            /* rested 9 frames */
    CHECK_INT(om_artsched_back(&s), 1);
    om_artsched_select(&s, 7);
    CHECK_INT(om_artsched_should_start(&s), 0);            /* already loading */
    CHECK_INT(om_artsched_stale(&s), 0);
    om_artsched_done(&s, 1);
    CHECK_INT(s.front, 1); CHECK_INT(s.shown, 7);          /* double buffer flipped only when complete */
    for (i = 0; i < 20; i++) { om_artsched_select(&s, 7); CHECK_INT(om_artsched_should_start(&s), 0); }
    for (i = 0; i < 9; i++) { om_artsched_select(&s, 8); om_artsched_should_start(&s); }
    CHECK_INT(s.loading, 1);
    om_artsched_select(&s, 9);                             /* cursor moved mid-load */
    CHECK_INT(om_artsched_stale(&s), 1);
    om_artsched_abort(&s);
    CHECK_INT(s.front, 1); CHECK_INT(s.shown, 7);          /* old image stays visible */
    for (i = 0; i < 9; i++) { om_artsched_select(&s, 9); om_artsched_should_start(&s); }
    om_artsched_done(&s, 0);                               /* failure: no flip, but not retried in a loop */
    CHECK_INT(s.front, 1); CHECK_INT(s.shown, 9);
    om_artsched_select(&s, 9);
    CHECK_INT(om_artsched_should_start(&s), 0);
    return th_done("test_artsched");
}
