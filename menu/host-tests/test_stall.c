/* menu/host-tests/test_stall.c -- the hang watchdog's rule and lines (hardware 2026-11-25: the menu froze in the Cheats
 * popup and in Settings with no trace; menu/app/hang.h shows a stalled frame beat). BSD-3-Clause. */
#include "th.h"
#include "stall.h"

int main(void)
{
    om_stall s;
    char l[41];
    const uint32_t hang[3] = { 0x060176A4u, 0x06017668u, 0x060FF9C8u }, exc[4] = { 9, 0x0601776Cu, 1, 0x060176D8u };
    uint32_t i, shows = 0, beat = 7;
    memset(&s, 0, sizeof s);
    for (i = 0; i < 1000u; i++)                                   /* a live menu: the beat moves every V-blank */
        CHECK_INT(om_stall_step(&s, ++beat, OM_STALL_VB), OM_STALL_NONE);
    for (i = 1; i < OM_STALL_VB; i++)                             /* a slow frame: 239 V-blanks, nothing shown */
        CHECK_INT(om_stall_step(&s, beat, OM_STALL_VB), OM_STALL_NONE);
    CHECK_INT(om_stall_step(&s, ++beat, OM_STALL_VB), OM_STALL_NONE);
    for (i = 1; i <= 5000u; i++)                                  /* a hang: shown once, at the 240th V-blank */
        if (om_stall_step(&s, beat, OM_STALL_VB) == OM_STALL_SHOW) { shows++; CHECK_INT(i, OM_STALL_VB); }
    CHECK_INT(shows, 1);
    CHECK_INT(om_stall_step(&s, ++beat, OM_STALL_VB), OM_STALL_CLEAR);   /* it moved again: the lines go */
    CHECK_INT(om_stall_step(&s, ++beat, OM_STALL_VB), OM_STALL_NONE);
    memset(&s, 0, sizeof s);
    s.beat = 0xFFFFFFFFu;
    CHECK_INT(om_stall_step(&s, 0, OM_STALL_VB), OM_STALL_NONE);         /* the beat wraps: a move, not a stall */
    om_stall_hex(l, "HANG", hang, 3);
    CHECK_STR(l, "HANG 060176A4 06017668 060FF9C8");
    om_stall_hex(l, "EXC", exc, 4);
    CHECK_STR(l, "EXC 00000009 0601776C 00000001 060176D8");
    CHECK(strlen(l) <= 40u);
    om_stall_hex(l, "SP", hang + 2, 1);
    CHECK_STR(l, "SP 060FF9C8");
    printf("test_stall: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
