/* menu/host-tests/test_ovscroll.c -- the settings list scrolls (OM_OV_VIS rows visible); Safe area and
 * Loader cycle their keys; the list wraps on a fresh press and stops on a held one. BSD-3-Clause. */
#include "th.h"
#include "overlay.h"
#include "safearea.h"
#include "settings.h"

int main(void)
{
    om_overlay o;
    om_settings s;
    int i;
    om_settings_default(&s);
    om_overlay_open(&o, OM_OV_SETTINGS, 0, 2, -1, 0);
    CHECK_INT(o.top, 0);
    for (i = 0; i < 5; i++) om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_SAFE); CHECK_INT(o.top, 0);                 /* rows 0..8 fit */
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), OM_OV_SAFE);
    CHECK_INT(s.overscan, OM_OVS_LARGE);
    om_overlay_key(&o, OM_K_RIGHT, &s); om_overlay_key(&o, OM_K_RIGHT, &s);
    CHECK_INT(s.overscan, OM_OVS_OFF);                                  /* 8 -> 16 -> 24 -> off */
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), OM_OV_SAFE);
    CHECK_INT(s.overscan, OM_OVS_MAX);
    om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_LOADER);
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), 0);
    CHECK_INT(s.loader, OM_LDR_CWX);                                    /* auto -> cwx -> jhl -> auto */
    om_overlay_key(&o, OM_K_A, &s);
    CHECK_INT(s.loader, OM_LDR_JHL);
    om_overlay_key(&o, OM_K_DOWN, &s);                                  /* Soft reset, right after
                                                                          * Loader (2026-11-23: no Per-game row) */
    CHECK_INT(o.row, OM_OVR_IGR); CHECK_INT(s.igr, 1); CHECK_INT(OM_OVR_IGR, OM_OVR_LOADER + 1);
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), 0);
    CHECK_INT(s.igr, 0);                                                /* on -> off */
    CHECK_INT(om_overlay_key(&o, OM_K_A, &s), 0);
    CHECK_INT(s.igr, 1);
    om_overlay_key(&o, OM_K_LEFT, &s);
    CHECK_INT(s.igr, 0);
    om_overlay_key(&o, OM_K_UP, &s);
    CHECK_INT(o.row, OM_OVR_LOADER); CHECK_INT(s.igr, 0); CHECK_INT(s.loader, OM_LDR_JHL);   /* moving does not toggle */
    /* "Saves" (per-game saves), right after Soft reset: on from the start, a plain switch, no prompt;
     * turning it off writes an explicit choice (saves_first stays set, so the next start keeps it off) */
    om_overlay_key(&o, OM_K_DOWN, &s);
    om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_SAVES); CHECK_INT(s.saves, 1);
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), 0);
    CHECK_INT(s.saves, 0); CHECK_INT(s.saves_first, OM_SVF_SHARE);
    CHECK_INT(om_overlay_key(&o, OM_K_A, &s), 0);
    CHECK_INT(s.saves, 1);
    s.saves_first = OM_SVF_NONE;                                         /* a hand-edited file: still never a prompt */
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), 0);
    CHECK_INT(s.saves, 0); CHECK_INT(s.saves_first, OM_SVF_SHARE);
    om_overlay_key(&o, OM_K_UP, &s);
    CHECK_INT(o.row, OM_OVR_IGR); CHECK_INT(s.saves, 0);                /* moving does not toggle */
    om_overlay_key(&o, OM_K_UP, &s);
    for (i = 0; i < 20; i++) om_overlay_key(&o, OM_K_DOWN | OM_K_REP, &s);
    CHECK_INT(o.row, OM_OVR_COUNT - 1); CHECK_INT(o.top, OM_OVR_COUNT - OM_OV_VIS);   /* held: stops at the end */
    om_overlay_key(&o, OM_K_DOWN, &s);                                  /* a fresh press wraps to the top */
    CHECK_INT(o.row, 0); CHECK_INT(o.top, 0);
    om_overlay_key(&o, OM_K_UP, &s);                                    /* and back round to the bottom */
    CHECK_INT(o.row, OM_OVR_COUNT - 1); CHECK_INT(o.top, OM_OVR_COUNT - OM_OV_VIS);
    for (i = 0; i < 20; i++) om_overlay_key(&o, OM_K_UP | OM_K_REP, &s);
    CHECK_INT(o.row, 0); CHECK_INT(o.top, 0);                           /* held: stops at the top */
    om_overlay_key(&o, OM_K_UP | OM_K_REP, &s);
    CHECK_INT(o.row, 0);
    CHECK_INT(om_overlay_key(&o, OM_K_START, &s), OM_OV_CLOSED | OM_OV_CHANGED);
    return th_done("test_ovscroll");
}
