/* menu/app/boottime.h -- per-step timing of the start-up after the boot animation (Task 13d, Rulings M3-R40 (b), M3-R41,
 * M3-R42): the six steps the interstitial's progress bar follows, each in V-blanks (om_v2_vblanks) and FRT microseconds
 * (sub-frame steps). Task 16a's diagnostics screen displays om_boot_timing_get(). Steps 1-2 run in main.c, 3-5 in the
 * Folders view's load_lists (in the order settings/favs/recent, BG.BIN, sort), step 6 is the menu loop's first frame.
 * A hook (the interstitial) runs between steps, outside the timed part. BSD-3-Clause. */
#ifndef OM_BOOTTIME_H
#define OM_BOOTTIME_H
#include <stdint.h>

enum {
    OM_BT_SMODE,          /* 1 s_mode(s_api) */
    OM_BT_LIBRARY,        /* 2 GAMES.DB, ART.PAK keys, the card root, LIBRARY.IDX or the card scan, the folder tree */
    OM_BT_BG,             /* 3 the theme scan and BG.BIN (show_theme) */
    OM_BT_PERSIST,        /* 4 SETTINGS.INI, FAVS.TXT, RECENT.TXT (om_persist_load) */
    OM_BT_SORT,           /* 5 the sort contexts and the browse lists (om_sortctx_build, om_browse_init) */
    OM_BT_RASTER,         /* 6 the menu's first frame: rows rasterized, submitted, shown (slSynch returned) */
    OM_BT_N
};
#define OM_BT_ALL ((1u << OM_BT_N) - 1u)

typedef struct {
    uint32_t vbl[OM_BT_N];        /* V-blanks between the step's begin and end */
    uint32_t us[OM_BT_N];         /* the same in microseconds (FRT, wrap-corrected by the V-blank count) */
    uint32_t start_vbl;           /* om_v2_vblanks() when the interstitial began (the boot animation's end) */
    uint32_t total_vbl;           /* from start_vbl to the end of step 6 */
    uint32_t done;                /* bit per step that has ended */
    uint32_t ui_shown;            /* 1 = the loading label / bar became visible (the start-up took over ~0.5 s) */
} om_boot_timing;

const om_boot_timing *om_boot_timing_get(void);

typedef void (*om_boot_hook)(int step, int ended);
void om_boot_begin(om_boot_hook hook);       /* resets the table; the hook runs before each begin and after each end */
void om_boot_step_begin(int step);
void om_boot_step_end(int step);
uint32_t om_boot_done_count(void);           /* steps ended, 0..6 */
void om_boot_set_shown(void);

/* Fake build only (OMSAT_FAKECARD): the developer lines and counters are off-screen unless the fake card carries the
 * marker aether/FAKESCRIPT.DIAG (`python -m omsat.fakecard --diag`). Always 0 in --app. */
void om_boot_diag_set(int on);
#ifdef OMSAT_FAKECARD
int om_boot_diag(void);
#else
#define om_boot_diag() 0                     /* --app: the diag code compiles away */
#endif
#endif
