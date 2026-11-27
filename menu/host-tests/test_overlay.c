/* menu/host-tests/test_overlay.c -- the settings and filter overlays' key handling (spec §7.5 Y/Z/Start). */
#include "th.h"
#include "anim.h"
#include "overlay.h"
#include "settings.h"

int main(void)
{
    om_overlay o;
    om_settings s;
    om_settings_default(&s);
    om_overlay_open(&o, OM_OV_SETTINGS, 0, 3, -1, 0);
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), OM_OV_THEME);         /* theme 0 -> 1, apply live */
    CHECK_INT(o.theme, 1);
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s) & OM_OV_THEME, OM_OV_THEME);
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), OM_OV_THEME);
    CHECK_INT(o.theme, 2);                                               /* wraps */
    CHECK_INT(om_overlay_key(&o, OM_K_DOWN, &s), 0);
    CHECK_INT(om_overlay_key(&o, OM_K_A, &s), 0);
    CHECK_INT(s.clock24, 1);                                             /* A or L/R toggles a switch row (12h -> 24h) */
    om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), 0);                    /* Music: on at 10, the top already */
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), OM_OV_SOUND);
    CHECK_INT(s.music_vol, 9); CHECK_INT(s.music, 1);                    /* left: -3 dB */
    CHECK_INT(om_overlay_key(&o, OM_K_A, &s), OM_OV_SOUND);
    CHECK_INT(s.music, 0); CHECK_INT(s.music_vol, 9);                    /* A: off, the volume kept */
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), 0);                     /* left while off: nothing */
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), OM_OV_SOUND);
    CHECK_INT(s.music, 1); CHECK_INT(s.music_vol, 9);                    /* right: on again at 9 */
    { int k; for (k = 0; k < 8; k++) om_overlay_key(&o, OM_K_LEFT, &s); }
    CHECK_INT(s.music_vol, 1); CHECK_INT(s.music, 1);
    CHECK_INT(om_overlay_key(&o, OM_K_LEFT, &s), OM_OV_SOUND);
    CHECK_INT(s.music, 0); CHECK_INT(s.music_vol, 1);                    /* below 1: off */
    om_overlay_key(&o, OM_K_A, &s);
    CHECK_INT(s.music, 1);
    om_overlay_key(&o, OM_K_DOWN, &s);
    om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, OM_OVR_ANIM);                                       /* the anim= row */
    CHECK_INT(om_overlay_key(&o, OM_K_RIGHT, &s), OM_OV_ANIM);
    CHECK_INT(s.anim, OM_ANIM_REDUCED);
    CHECK_INT(om_overlay_key(&o, OM_K_A, &s), OM_OV_ANIM);
    CHECK_INT(s.anim, OM_ANIM_OFF);
    om_overlay_key(&o, OM_K_RIGHT, &s);
    CHECK_INT(s.anim, OM_ANIM_FULL);                                     /* wraps */
    om_overlay_key(&o, OM_K_LEFT, &s);
    CHECK_INT(s.anim, OM_ANIM_OFF);                                      /* and back */
    om_overlay_key(&o, OM_K_DOWN, &s);                                   /* the list goes on */
    CHECK_INT(o.row, OM_OVR_SAFE);
    CHECK_INT(om_overlay_key(&o, OM_K_B, &s), OM_OV_CLOSED | OM_OV_CHANGED);
    CHECK_INT(o.kind, OM_OV_NONE);
    om_overlay_open(&o, OM_OV_SETTINGS, 0, 3, -1, 0);
    CHECK_INT(om_overlay_key(&o, OM_K_START, &s), OM_OV_CLOSED);         /* nothing changed: no save */
    om_overlay_open(&o, OM_OV_FILTER, 0, 0, -1, 4);
    om_overlay_key(&o, OM_K_RIGHT, &s);
    CHECK_INT(o.genre, 0);
    om_overlay_key(&o, OM_K_LEFT, &s); om_overlay_key(&o, OM_K_LEFT, &s);
    CHECK_INT(o.genre, 3);                                               /* -1 (all) wraps to the last rank */
    om_overlay_key(&o, OM_K_DOWN, &s);
    om_overlay_key(&o, OM_K_RIGHT, &s); om_overlay_key(&o, OM_K_RIGHT, &s);
    CHECK_INT(o.rcur, 2);
    om_overlay_key(&o, OM_K_A, &s);
    CHECK_INT(s.filter_region, 1u << 2);                                 /* U */
    om_overlay_key(&o, OM_K_LEFT, &s); om_overlay_key(&o, OM_K_LEFT, &s); om_overlay_key(&o, OM_K_LEFT, &s);
    CHECK_INT(o.rcur, 0);                                                /* clamps */
    om_overlay_key(&o, OM_K_A, &s);
    CHECK_INT(s.filter_region, (1u << 2) | 1u);                          /* combinable: J + U */
    CHECK_INT(om_overlay_key(&o, OM_K_Z, &s), OM_OV_CLOSED | OM_OV_CHANGED);
    om_overlay_open(&o, OM_OV_FILTER, 0, 0, 3, 4);
    om_overlay_key(&o, OM_K_UP | OM_K_REP, &s);
    CHECK_INT(o.row, 0);                                                 /* held Up on Genre: stays */
    om_overlay_key(&o, OM_K_UP, &s);
    CHECK_INT(o.row, 1);                                                 /* a fresh Up wraps to the regions */
    om_overlay_key(&o, OM_K_DOWN | OM_K_REP, &s);
    CHECK_INT(o.row, 1);                                                 /* held Down there: stays */
    om_overlay_key(&o, OM_K_DOWN, &s);
    CHECK_INT(o.row, 0);                                                 /* a fresh Down wraps to Genre */
    CHECK_INT(om_overlay_key(&o, OM_K_Z, &s), OM_OV_CLOSED);
    om_overlay_open(&o, OM_OV_FILTER, 0, 0, -1, 0);                      /* no GAMES.DB: genre stays "all" */
    om_overlay_key(&o, OM_K_RIGHT, &s);
    CHECK_INT(o.genre, -1);
    return th_done("test_overlay");
}
