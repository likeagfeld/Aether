/* menu/host-tests/test_ovpages.c -- the settings list scrolls (OM_OV_VIS rows), the new rows
 * edit their keys, the Controls page remaps (swap on conflict) and resets, and the fixed buttons always reach "Reset
 * all". Rows after Safe area: Loader and Soft reset come before Last game. BSD-3-Clause. */
#include "th.h"
#include "layout.h"
#include "overlay.h"
#include "remap.h"
#include "safearea.h"
#include "settings.h"

static int press(om_overlay *o, uint16_t k, om_settings *s) { return om_overlay_key(o, k, s); }

int main(void)
{
    om_overlay o;
    om_settings s;
    int i, f;
    om_settings_default(&s);
    /* ovdraw.c's Settings labels are 56 px wide textures; "In Game Reset" has a 72 px slot, "Start on Last Game" 88 px
     * and "Manage / Import Saves" 104 px (both reach past OV_LABEL into the value column: their values are short and
     * right-aligned), the game panel's labels (Manage saves, Reset to defaults) 80 px */
    printf("label widths: Manage / Import Saves %d px (104), Animations %d px (pool 56), Reset to defaults %d px (80)\n",
           om_uf_width(&om_face_small, "Manage / Import Saves"), om_uf_width(&om_face_small, "Animations"),
           om_uf_width(&om_face_small, "Reset to defaults"));
    CHECK(om_uf_width(&om_face_small, "Animations") <= 56);
    CHECK(om_uf_width(&om_face_small, "In Game Reset") <= 72);
    CHECK(om_uf_width(&om_face_small, "Reset to defaults") <= 80);
    CHECK(om_uf_width(&om_face_small, "Manage saves") <= 80);
    CHECK(om_uf_width(&om_face_small, "Screensaver") <= 56);
    CHECK(om_uf_width(&om_face_small, "Start on Last Game") <= 88);
    CHECK(om_uf_width(&om_face_small, "Manage / Import Saves") <= 104);
    {   /* the two wide labels end 8 px or more before their value starts (values are right-aligned at OV_VALUE's
         * right edge; the label starts at OV_LABEL's left) */
        const om_box *lb = OM_BX(OV_LABEL), *vb = OM_BX(OV_VALUE);
        CHECK(lb->x0 + om_uf_width(&om_face_small, "Start on Last Game") + 8 <=
              vb->x1 + 1 - om_uf_width(&om_face_small, "Select"));
        CHECK(lb->x0 + om_uf_width(&om_face_small, "Manage / Import Saves") + 8 <=
              vb->x1 + 1 - om_uf_width(&om_face_small, "\xE2\x80\xBA"));
    }
    CHECK(om_uf_width(&om_face_small, "Overscan") <= 56);
    {   /* ovdraw.c's slot widths (the UI pool shares VDP1 VRAM with the cover cache): the game panel's labels, the
         * titles, and the values that are not marquees (the Loader row's widest, the other rows' fixed values) */
        static const char *const gl[7] = { "Saves Per Game", "Slot", "Cheats", "Pad", "Reset to defaults", "Launch",
                                           "Manage saves" };
        static const int glcap[7] = { 72, 24, 32, 24, 80, 40, 64 };
        static const char *const lw[3] = { "Auto (CWX, database: may fail)", "Auto (JHL, database: may fail)",
                                           "Auto (JHL, CWX not available)" };
        for (i = 0; i < 7; i++) CHECK(om_uf_width(&om_face_small, gl[i]) <= glcap[i]);
        CHECK(om_uf_width(&om_face_title, "Launch Options") <= 104);
        CHECK(om_uf_width(&om_face_title, "Settings") <= 56 && om_uf_width(&om_face_title, "Controls") <= 56);
        CHECK(om_uf_width(&om_face_title, "Filter") <= 56);
        for (i = 0; i < 3; i++) CHECK(om_uf_width(&om_face_small, lw[i]) <= 136);
        CHECK(om_uf_width(&om_face_small, "Needs CWX loader âº") <= 120);
        CHECK(om_uf_width(&om_face_small, "Press A again") <= 120);
        CHECK(om_uf_width(&om_face_small, "64 of 64 on \xE2\x80\xBA") <= 120);   /* 2026-11-25: the Cheats row's count */
        CHECK(om_uf_width(&om_face_small, "None \xE2\x80\xBA") <= 120);
        CHECK(om_uf_width(&om_face_small, "JHL (CWX not available)") <= 120);
    }
    om_overlay_open(&o, OM_OV_SETTINGS, 0, 2, -1, 0);
    CHECK_INT(o.page, OM_OVP_MAIN); CHECK_INT(o.top, 0);
    for (i = 0; i < 5; i++) press(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_SAFE); CHECK_INT(o.top, 0);                 /* rows 0..8 fit */
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_SAFE);
    CHECK_INT(s.overscan, OM_OVS_LARGE);
    press(&o, OM_K_RIGHT, &s); press(&o, OM_K_RIGHT, &s);
    CHECK_INT(s.overscan, OM_OVS_OFF);                                  /* 8 -> 16 -> 24 -> off */
    CHECK_INT(press(&o, OM_K_LEFT, &s), OM_OV_SAFE);
    CHECK_INT(s.overscan, OM_OVS_MAX);
    press(&o, OM_K_DOWN, &s);                                            /* over Loader */
    press(&o, OM_K_DOWN, &s);                                            /* Soft reset (no Per-game) */
    CHECK_INT(o.row, OM_OVR_IGR);
    press(&o, OM_K_DOWN, &s);                                            /* and Saves */
    CHECK_INT(o.row, OM_OVR_SAVES);
    press(&o, OM_K_DOWN, &s);                                            /* Manage / Import Saves: every game's saves,
                                                                          * the import its popup's first row */
    CHECK_INT(o.row, OM_OVR_SAVEMGR); CHECK_INT(o.top, 1);              /* the window follows */
    CHECK_INT(press(&o, OM_K_LEFT, &s), 0);                              /* Left: nothing */
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_SAVEMGR);                 /* only a choice runs it */
    CHECK_INT(press(&o, OM_K_A, &s), OM_OV_SAVEMGR);
    CHECK_INT(o.kind, OM_OV_SETTINGS);
    press(&o, OM_K_DOWN, &s);                                            /* no separate Import row any more */
    CHECK_INT(o.row, OM_OVR_RESELECT);
    CHECK_INT(press(&o, OM_K_A, &s), 0);
    CHECK_INT(s.reselect, 0);                                            /* Last game: a switch */
    press(&o, OM_K_DOWN, &s);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_SAVER);
    CHECK_INT(s.saver_min, 3);                                           /* 1 (the default) -> 3 */
    press(&o, OM_K_RIGHT, &s);
    CHECK_INT(s.saver_min, 5);
    CHECK_INT(press(&o, OM_K_LEFT, &s), OM_OV_SAVER);
    CHECK_INT(s.saver_min, 3);                                           /* and back */
    press(&o, OM_K_RIGHT, &s);
    press(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_CONTROLS); CHECK_INT(o.top, OM_OVR_COUNT - OM_OV_VIS - 1);   /* CONTROLS is not last now */
    press(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_ABOUT); CHECK_INT(o.top, OM_OVR_COUNT - OM_OV_VIS);          /* the new last row */
    press(&o, OM_K_DOWN | OM_K_REP, &s);
    CHECK_INT(o.row, OM_OVR_ABOUT);                                      /* held: stops at the last row */
    CHECK_INT(press(&o, OM_K_LEFT, &s), 0);                              /* Left: nothing */
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_ABOUT);                   /* only a choice opens it */
    CHECK_INT(press(&o, OM_K_A, &s), OM_OV_ABOUT);
    CHECK_INT(o.kind, OM_OV_SETTINGS);                                   /* the caller shows the page, then returns */
    press(&o, OM_K_UP, &s);
    CHECK_INT(o.row, OM_OVR_CONTROLS);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), 0);                             /* D-pad right enters the page */
    CHECK_INT(o.page, OM_OVP_CONTROLS); CHECK_INT(o.row, 0); CHECK_INT(o.top, 0);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_PAD);                     /* Play: A -> B, Back takes A */
    CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_B); CHECK_INT(s.pad.btn[OM_ACT_BACK], OM_BTN_A);
    CHECK(om_remap_valid(&s.pad));
    CHECK_INT(press(&o, OM_K_A, &s), OM_OV_PAD);                         /* the mapped OK steps forward too */
    CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_C); CHECK_INT(s.pad.btn[OM_ACT_INFO], OM_BTN_B);
    CHECK_INT(press(&o, OM_K_LEFT, &s), OM_OV_PAD);                      /* left steps back, swapping again */
    CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_B); CHECK_INT(s.pad.btn[OM_ACT_INFO], OM_BTN_C);
    /* 2026-11-23: no Info row (C does nothing on the game list): row 2 is Favorite, the page has 7 actions + Reset */
    CHECK_INT(OM_OVC_COUNT, 8); CHECK_INT(om_ovc_act(1), OM_ACT_BACK); CHECK_INT(om_ovc_act(2), OM_ACT_FAV);
    CHECK_INT(om_ovc_act(6), OM_ACT_PGDN);
    press(&o, OM_K_DOWN, &s); press(&o, OM_K_DOWN, &s);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_PAD);                     /* Favorite: X -> Y, Sort takes X */
    CHECK_INT(s.pad.btn[OM_ACT_FAV], OM_BTN_Y); CHECK_INT(s.pad.btn[OM_ACT_SORT], OM_BTN_X);
    CHECK_INT(press(&o, OM_K_LEFT, &s), OM_OV_PAD);
    CHECK_INT(s.pad.btn[OM_ACT_FAV], OM_BTN_X); CHECK_INT(s.pad.btn[OM_ACT_INFO], OM_BTN_C);
    press(&o, OM_K_UP, &s); press(&o, OM_K_UP, &s);
    CHECK_INT(press(&o, OM_K_B, &s) & OM_OV_CLOSED, 0);                  /* Back on the page never closes Settings */
    CHECK_INT(o.page, OM_OVP_MAIN); CHECK_INT(o.row, OM_OVR_CONTROLS);
    press(&o, OM_K_RIGHT, &s);
    CHECK_INT(o.page, OM_OVP_CONTROLS);
    for (i = 0; i < OM_OVC_COUNT + 3; i++) press(&o, OM_K_DOWN | OM_K_REP, &s);
    CHECK_INT(o.row, OM_OVC_RESET); CHECK_INT(o.top, 0);                 /* the whole page fits */
    press(&o, OM_K_DOWN, &s);                                            /* a fresh press wraps the page too */
    CHECK_INT(o.row, 0);
    press(&o, OM_K_UP, &s);
    CHECK_INT(o.row, OM_OVC_RESET);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_PAD);                     /* fixed D-pad: Reset all */
    CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_A); CHECK_INT(s.pad.btn[OM_ACT_BACK], OM_BTN_B);
    CHECK_INT(press(&o, OM_K_RIGHT, &s), 0);                             /* already the defaults: no change */
    CHECK_INT(press(&o, OM_K_B, &s), 0);                                 /* Back: to the main page, on Controls */
    CHECK_INT(o.page, OM_OVP_MAIN); CHECK_INT(o.row, OM_OVR_CONTROLS); CHECK_INT(o.kind, OM_OV_SETTINGS);
    press(&o, OM_K_A, &s);                                               /* the mapped OK also enters */
    CHECK_INT(o.page, OM_OVP_CONTROLS);
    f = press(&o, OM_K_START, &s);                                       /* Start closes from any page */
    CHECK_INT(f, OM_OV_CLOSED | OM_OV_CHANGED); CHECK_INT(o.kind, OM_OV_NONE);
    om_overlay_open(&o, OM_OV_SETTINGS, 0, 2, -1, 0);
    CHECK_INT(o.page, OM_OVP_MAIN); CHECK_INT(o.top, 0); CHECK_INT(o.row, 0);   /* open resets the page */
    for (i = 0; i < 20; i++) press(&o, OM_K_UP | OM_K_REP, &s);
    CHECK_INT(o.row, 0); CHECK_INT(o.top, 0);
    {   /* a scrambled map: Reset all is still reachable with Start + D-pad alone (no face button pressed) */
        om_remap_assign(&s.pad, OM_ACT_OK, OM_BTN_Z);
        om_remap_assign(&s.pad, OM_ACT_BACK, OM_BTN_L);
        press(&o, OM_K_UP, &s);                            /* wraps to the last row (About) */
        CHECK_INT(o.row, OM_OVR_ABOUT);
        press(&o, OM_K_UP, &s);                            /* onto Controls */
        press(&o, OM_K_RIGHT, &s);
        press(&o, OM_K_UP, &s);                            /* wraps to Reset all */
        CHECK_INT(press(&o, OM_K_RIGHT, &s), OM_OV_PAD);
        CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_A); CHECK_INT(s.pad.btn[OM_ACT_BACK], OM_BTN_B);
        CHECK_INT(press(&o, OM_K_START, &s), OM_OV_CLOSED | OM_OV_CHANGED);
    }
    {   /* the overlay hints follow the map: each pair (0+1, 2+3) with every letter fits OV_HINT on one line */
        char h[OM_OVHINT_MAX];
        int w, b1, b2, worst = 0;
        om_remap m;
        om_remap_default(&m);
        om_ovhint_text(h, 0, &m); CHECK_STR(h, "L/R or A: change");
        om_ovhint_text(h, 1, &m); CHECK_STR(h, "\xC2\xB7 B/Start: close, save");
        om_ovhint_text(h, 2, &m); CHECK_STR(h, "L/R move \xC2\xB7 A toggle");
        om_ovhint_text(h, 3, &m); CHECK_STR(h, "\xC2\xB7 Z/B: close, apply");
        om_remap_assign(&m, OM_ACT_OK, OM_BTN_B);
        om_ovhint_text(h, 0, &m); CHECK_STR(h, "L/R or B: change");
        om_ovhint_text(h, 1, &m); CHECK_STR(h, "\xC2\xB7 A/Start: close, save");
        for (i = 0; i < 4; i += 2)
            for (b1 = 0; b1 < OM_BTN_COUNT; b1++)
                for (b2 = 0; b2 < OM_BTN_COUNT; b2++) {
                    int w0;
                    om_remap_default(&m);
                    om_remap_assign(&m, OM_ACT_OK, (uint8_t)b1);         /* each pair's OK letter */
                    om_remap_assign(&m, i ? OM_ACT_FILTER : OM_ACT_BACK, (uint8_t)b2);
                    om_ovhint_text(h, (uint8_t)i, &m);
                    w0 = om_uf_width(&om_face_small, h);
                    CHECK(w0 <= (i ? 96 : 80));                          /* ovdraw.c's hint slots */
                    om_ovhint_text(h, (uint8_t)(i + 1), &m);
                    w = om_uf_width(&om_face_small, h);
                    CHECK(w <= (i ? 88 : 96));
                    w += w0 + 4;                                         /* drawn 4 px after the first */
                    if (w > worst) worst = w;
                    CHECK(w <= om_box_w(OM_B_OV_HINT));
                }
        CHECK(worst > 0);
    }
    return th_done("test_ovpages");
}
