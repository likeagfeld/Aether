/* menu/host-tests/test_sndmix.c -- the ring maths, fades, ducking, pitch and resume of menu/src/sndmix.c.
 * BSD-3-Clause. */
#include "th.h"
#include "sndmix.h"

int main(void)
{
    om_ring r;
    om_fade f;
    int8_t o;
    uint16_t fn;
    uint32_t i, played = 0;
    char buf[64];
    uint8_t d;

    /* the ring: prime 3, play through 3 laps with CA readings every ~1/11 block, refill whenever a slot is free */
    om_ring_reset(&r);
    CHECK(om_ring_starved(&r));
    r.filled = 3;
    CHECK_INT(om_ring_ahead(&r), 2);
    CHECK(!om_ring_starved(&r));
    for (i = 0; i < 3u * 7u * 11u; i++) {
        uint32_t ca = (i / 11u) % 7u;                 /* 11 V-blanks per 4096-sample block (186 ms) */
        om_ring_ca(&r, ca);
        while (om_ring_can_write(&r)) {
            CHECK(om_ring_slot(r.filled) != om_ring_slot(r.played) || r.filled == r.played);   /* never the live slot */
            r.filled++;
        }
        CHECK(!om_ring_starved(&r));
        CHECK_INT(om_ring_ahead(&r), 6);              /* refilled at once: 6 blocks (1.11 s) ahead */
    }
    CHECK_INT(r.played, 3u * 7u - 1u);
    om_ring_ca(&r, 7);                                /* CA = LEA's block on the loop sample: block 0 */
    CHECK_INT(r.last, 0);
    CHECK_INT(r.played, 21);
    /* a starving stream: nothing refilled while 3 blocks play */
    om_ring_reset(&r);
    r.filled = 3;
    om_ring_ca(&r, 1); CHECK_INT(om_ring_ahead(&r), 1);
    om_ring_ca(&r, 2); CHECK_INT(om_ring_ahead(&r), 0); CHECK(!om_ring_starved(&r));   /* the last valid block */
    om_ring_ca(&r, 3); CHECK(om_ring_starved(&r));
    (void)played;

    /* levels (HWF-52): volume steps of 3 dB, ducking 6 dB, clamped sums */
    CHECK_INT(om_snd_vol_tl(10), 0); CHECK_INT(om_snd_vol_tl(9), 8); CHECK_INT(om_snd_vol_tl(1), 72);
    CHECK_INT(om_snd_vol_tl(0), 255); CHECK_INT(om_snd_vol_tl(99), 0);
    CHECK_INT(om_snd_amp_tl(65536), 0); CHECK_INT(om_snd_amp_tl(32768), 16);   /* half amplitude = -6 dB */
    CHECK_INT(om_snd_amp_tl(0), 255);
    CHECK_INT(om_snd_tl_sum(200, 16, 100), 255); CHECK_INT(om_snd_tl_sum(8, 16, 0), 24);

    /* a 1 s fade-in (60 V-blanks), a 500 ms fade-out (30), monotonic, exact at the end */
    om_fade_set(&f, 0);
    om_fade_to(&f, 65536, 60);
    {
        uint32_t last = 0, a = 0;
        for (i = 0; i < 59; i++) { a = om_fade_tick(&f, 1); CHECK(a >= last); last = a; }
        CHECK(!om_fade_done(&f));
        CHECK(a > 60000);
        CHECK_INT(om_fade_tick(&f, 1), 65536);
        CHECK(om_fade_done(&f));
    }
    om_fade_to(&f, 0, 30);
    CHECK_INT(om_fade_tick(&f, 15), 65536 - 15 * ((65536 + 29) / 30));
    CHECK_INT(om_fade_tick(&f, 20), 0);               /* late V-blanks (a masked interrupt) still end it */
    om_fade_to(&f, 0, 30);
    CHECK(om_fade_done(&f));

    /* ducking: 4 units a V-blank down to -6 dB, and back */
    d = 0;
    d = om_duck_tick(d, 1, 1); CHECK_INT(d, 4);
    d = om_duck_tick(d, 1, 3); CHECK_INT(d, 16);
    d = om_duck_tick(d, 1, 1); CHECK_INT(d, 16);
    d = om_duck_tick(d, 0, 2); CHECK_INT(d, 8);
    d = om_duck_tick(d, 0, 9); CHECK_INT(d, 0);

    /* pitch (HWF-36): 22,050 Hz is OCT -1 FNS 0; -5 st = 16,520 Hz; +4 st = 27,782 Hz; -12 st = OCT -2 */
    om_snd_shift(-1, 0, 0, &o, &fn); CHECK_INT(o, -1); CHECK_INT(fn, 0);
    om_snd_shift(-1, 0, -5, &o, &fn); CHECK_INT(o, -2); CHECK_INT(fn, 510);
    om_snd_shift(-1, 0, 4, &o, &fn); CHECK_INT(o, -1); CHECK_INT(fn, 266);
    om_snd_shift(-1, 0, -12, &o, &fn); CHECK_INT(o, -2); CHECK_INT(fn, 0);
    om_snd_shift(-1, 0, 12, &o, &fn); CHECK_INT(o, 0); CHECK_INT(fn, 0);
    om_snd_shift(-1, 0, -7, &o, &fn); CHECK_INT(o, -2); CHECK_INT(fn, 342);
    om_snd_shift(-8, 0, -12, &o, &fn); CHECK_INT(o, -8); CHECK_INT(fn, 0);

    /* lengths: the scream (10,340 samples at 22,050 Hz = 0.469 s) is 29 NTSC V-blanks, 24 PAL */
    CHECK_INT(om_snd_vblanks(10340, -1, 0, 60), 29);
    CHECK_INT(om_snd_vblanks(10340, -1, 0, 50), 24);
    CHECK_INT(om_snd_vblanks(22050, -1, 0, 60), 60);
    CHECK_INT(om_snd_vblanks(4025, -2, 510, 60), 15);   /* select at -5 st: 0.244 s */

    /* the resume state file */
    CHECK_INT(om_mus_state_fmt(buf, sizeof buf, 1017694208u, 10416), 28);
    CHECK_STR(buf, "size=1017694208\nblock=10416\n");
    CHECK_INT(om_mus_state_parse(buf, 28, 1017694208u, 62115), 10416);
    CHECK_INT(om_mus_state_parse(buf, 28, 1017694207u, 62115), -1);   /* another file */
    CHECK_INT(om_mus_state_parse(buf, 28, 1017694208u, 10416), -1);   /* past the end */
    CHECK_INT(om_mus_state_parse("size=5\r\nblock=2\r\n", 17, 5, 3), 2);
    CHECK_INT(om_mus_state_parse("size=5\nblk=2\n", 13, 5, 3), -1);
    CHECK_INT(om_mus_state_parse("", 0, 5, 3), -1);
    CHECK_INT(om_mus_state_fmt(buf, 8, 123456, 1), 7);
    CHECK_STR(buf, "size=12");
    return th_done("test_sndmix");
}
