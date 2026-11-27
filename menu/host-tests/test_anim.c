/* menu/host-tests/test_anim.c -- motion maths: easing, retargetable tweens, anim= scaling,
 * the status toast's state machine and the RGB555 mixes behind the pre-blended VDP1 lookup tables. BSD-3-Clause. */
#include "th.h"
#include "anim.h"

int main(void)
{
    om_tween tw;
    om_toast t;
    int32_t prev, v, i;

    /* ease-out: endpoints, monotonic, front-loaded (more than half way at the midpoint) */
    CHECK_INT(om_ease_out(0), 0); CHECK_INT(om_ease_out(OM_Q), OM_Q);
    CHECK_INT(om_ease_out(-5), 0); CHECK_INT(om_ease_out(OM_Q + 9), OM_Q);
    CHECK(om_ease_out(OM_Q / 2) > OM_Q * 3 / 4);                  /* 1 - 0.5^3 = 0.875 */
    for (prev = -1, i = 0; i <= OM_Q; i += 64) {
        v = om_ease_out(i);
        CHECK(v >= prev);
        prev = v;
    }

    /* tween: reaches the target in exactly dur frames, never overshoots, eases out */
    om_tween_jump(&tw, 0);
    CHECK_INT(om_tween_value(&tw), 0); CHECK(!om_tween_active(&tw));
    om_tween_to(&tw, 140, 5);                                      /* the cursor glide: 10 rows of 14 px, 80 ms */
    CHECK(om_tween_active(&tw));
    CHECK_INT(om_tween_value(&tw), 0);
    om_tween_step(&tw);
    v = om_tween_value(&tw);
    CHECK(v > 140 / 5);                                            /* ease-out: the first frame moves the most */
    for (i = 0; i < 3; i++) { prev = v; om_tween_step(&tw); v = om_tween_value(&tw); CHECK(v >= prev && v <= 140); }
    om_tween_step(&tw);
    CHECK_INT(om_tween_value(&tw), 140); CHECK(!om_tween_active(&tw));
    om_tween_step(&tw);
    CHECK_INT(om_tween_value(&tw), 140);

    /* retarget mid-glide: continues from where it is (no jump), and a repeated target keeps the glide */
    om_tween_jump(&tw, 0);
    om_tween_to(&tw, 100, 10);
    om_tween_step(&tw); om_tween_step(&tw);
    prev = om_tween_value(&tw);
    om_tween_to(&tw, -50, 10);
    CHECK_INT(om_tween_value(&tw), prev);
    om_tween_step(&tw);
    CHECK(om_tween_value(&tw) < prev);
    om_tween_to(&tw, -50, 10);                                     /* same target: keeps going, t not reset */
    CHECK_INT(tw.t, 1);
    om_tween_to(&tw, 7, 0);                                        /* dur 0 = jump */
    CHECK_INT(om_tween_value(&tw), 7); CHECK(!om_tween_active(&tw));

    om_tween_run(&tw, 14, 0, 4);                                   /* a forced glide: the list scroll's offset */
    CHECK_INT(om_tween_value(&tw), 14);
    for (i = 0; i < 4; i++) om_tween_step(&tw);
    CHECK_INT(om_tween_value(&tw), 0);
    om_tween_run(&tw, 14, 0, 0);                                   /* anim=off: no glide */
    CHECK_INT(om_tween_value(&tw), 0);

    /* anim= scaling: 80 ms at 59.94 Hz = 5 frames, 50 Hz = 4; held halves (>= 2); off = 0 */
    CHECK_INT(om_anim_frames(80, OM_ANIM_FULL, 0, 5994), 5);
    CHECK_INT(om_anim_frames(80, OM_ANIM_FULL, 0, 5000), 4);
    CHECK_INT(om_anim_frames(150, OM_ANIM_FULL, 0, 5994), 9);
    CHECK_INT(om_anim_frames(80, OM_ANIM_FULL, 1, 5994), 2);
    CHECK_INT(om_anim_frames(250, OM_ANIM_REDUCED, 1, 5994), 7);
    CHECK_INT(om_anim_frames(10, OM_ANIM_FULL, 0, 5994), 2);
    CHECK_INT(om_anim_frames(150, OM_ANIM_OFF, 0, 5994), 0);
    CHECK_INT(om_anim_dist(14, OM_ANIM_FULL), 14);
    CHECK_INT(om_anim_dist(14, OM_ANIM_REDUCED), 7);
    CHECK_INT(om_anim_dist(-40, OM_ANIM_REDUCED), -20);
    CHECK_INT(om_anim_dist(14, OM_ANIM_OFF), 0);

    /* colours: RGB555 mixes, the VDP2 colour-calculation formula (HWF-31) and VDP1 RGB codes (HWF-01) */
    CHECK_INT(om_c555_rgb(0xFFFFFF), 0xFFFF);
    CHECK_INT(om_c555_rgb(0x000000), 0x8000);
    CHECK_INT(om_c555_rgb(0x409CFF), 0x8000 | (31 << 10) | (19 << 5) | 8);
    CHECK_INT(om_c555_mix(0x8000, 0xFFFF, 0), 0x8000);
    CHECK_INT(om_c555_mix(0x8000, 0xFFFF, 256), 0xFFFF);
    CHECK_INT(om_c555_mix(0x8000, 0x7FFF, 128) & 0x1F, 16);       /* halfway, rounded; bit 15 from a */
    CHECK_INT(om_c555_mix(0x8000, 0x7FFF, 128) >> 15, 1);
    CHECK_INT(om_c555_cc(0x001F, 0x0000, 0), 30);                  /* r = 0: 31:1 -> 31*31/32 = 30 */
    CHECK_INT(om_c555_cc(0x001F, 0x0000, 31), 0);                  /* r = 31: 0:32, all second */
    CHECK_INT(om_c555_cc(0x0000, 0x001F, 31), 31);
    CHECK_INT(om_c555_cc(0x7FFF, 0x7FFF, 14), 0x7FFF);

    /* toast: slides in, auto-hides after OM_TOAST_HOLD frames; an rc error stays until the next action */
    CHECK(om_msg_is_error("FAVS.TXT NOT SAVED: EIO rc -5"));
    CHECK(!om_msg_is_error("Added to favorites"));
    om_toast_init(&t, OM_ANIM_FULL, 6);
    CHECK_INT(om_toast_progress(&t), 0);
    om_toast_show(&t, "SORT: GENRE");
    CHECK(t.visible && !t.sticky);
    for (i = 0; i < 6; i++) om_toast_tick(&t);
    CHECK_INT(om_toast_progress(&t), OM_Q);
    om_toast_input(&t);                                            /* a plain message does not care about input */
    CHECK(t.visible);
    for (i = 6; i < (int32_t)OM_TOAST_HOLD - 1; i++) om_toast_tick(&t);
    CHECK(t.visible);
    om_toast_tick(&t);
    CHECK(!t.visible);                                             /* ~3 s: hides, the hints slide back */
    for (i = 0; i < 6; i++) om_toast_tick(&t);
    CHECK_INT(om_toast_progress(&t), 0);
    om_toast_show(&t, "LAUNCH FAILED: LAUNCHER.BIN EIO rc -5");
    CHECK(t.sticky);
    for (i = 0; i < 1000; i++) om_toast_tick(&t);
    CHECK(t.visible); CHECK_INT(om_toast_progress(&t), OM_Q);     /* never times out */
    om_toast_input(&t);
    CHECK(!t.visible);                                             /* the next action dismisses it */
    om_toast_show(&t, "x");
    om_toast_show(&t, "");
    CHECK(!t.visible);                                             /* an empty message hides */
    {   /* a message longer than the toast's buffer is cut, NUL-terminated */
        char big[200];
        memset(big, 'A', sizeof big - 1); big[sizeof big - 1] = 0;
        om_toast_show(&t, big);
        CHECK_INT(strlen(t.msg), OM_TOAST_MSG - 1);
    }
    {   /* the same rc error twice, with an input between, is shown both times (serials, not strings) */
        static const char err[] = "LAUNCH FAILED: LAUNCHER.BIN SD no file rc -104";
        om_toast_init(&t, OM_ANIM_FULL, 6);
        CHECK_INT(om_toast_offer(&t, err, 1, 0), 1);
        CHECK(t.visible && t.sticky);
        om_toast_input(&t);
        CHECK(!t.visible);
        CHECK_INT(om_toast_offer(&t, "Loading LAUNCHER.BIN...", 2, 0), 1);   /* the second launch */
        CHECK_INT(om_toast_offer(&t, err, 3, 0), 1);                         /* the same failure again */
        CHECK(t.visible && t.sticky);
        CHECK_STR(t.msg, err);
        CHECK_INT(om_toast_offer(&t, err, 3, 0), 0);                         /* the same post is not re-shown */
        /* M2: an automatic post never replaces a visible rc error; it shows after input dismisses the error */
        CHECK_INT(om_toast_offer(&t, "ART: BOX.TGA x", 4, 1), 0);
        CHECK_STR(t.msg, err);
        om_toast_input(&t);
        CHECK_INT(om_toast_offer(&t, "ART: BOX.TGA x", 4, 1), 1);
        CHECK_STR(t.msg, "ART: BOX.TGA x");
        CHECK_INT(om_toast_offer(&t, "Added to favorites", 5, 0), 1);       /* input-driven: replaces at once */
    }
    om_toast_init(&t, OM_ANIM_OFF, 6);                             /* anim=off: instant */
    om_toast_show(&t, "x");
    CHECK_INT(om_toast_progress(&t), OM_Q);
    return th_done("test_anim");
}
