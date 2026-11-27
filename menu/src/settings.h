/* menu/src/settings.h -- SETTINGS.INI (contract/SPEC.md §5.5). Only the global section is modelled here; the
 * merge-writer keeps every other line ([<key>] sections for M6, unknown keys, comments) byte-for-byte. BSD-3-Clause. */
#ifndef OM_SETTINGS_H
#define OM_SETTINGS_H
#include <stdint.h>
#include "remap.h"
#define OM_SETTINGS_MAX 8192u
enum { OM_SORT_AZ, OM_SORT_GENRE, OM_SORT_REGION, OM_SORT_YEAR, OM_SORT_COUNT };
typedef struct {
    char theme[32];
    char filter_genre[32];     /* genre NAME, "" = every genre (names survive a GAMES.DB rebuild, ids need not) */
    uint8_t sort, filter_region, music, sfx, clock24;   /* filter_region: GAMES.DB region bits J0 T1 U2 E3 B4, 0 = all */
    uint8_t anim;              /* OM_ANIM_FULL / REDUCED / OFF (menu/src/anim.h) */
    uint8_t music_vol;         /* 1..10: 10 = the packed level (-14 dB mean), each step -3 dB */
    char anim_bad[16];         /* an unrecognised anim= value as read ("" = none), for a named message */
    uint8_t overscan;      /* OM_OVS_*, default OM_OVS_STD */
    uint8_t loader;        /* OM_LDR_*, default OM_LDR_AUTO (requirement 1: Auto = DB hint, else CWX) */
    uint8_t igr;           /* 1 = A+B+C+Start in a game returns to Aether (default 1; used only after Task 20) */
    uint8_t saves;         /* 1 = per-game saves on (default 1, the user's decision; no prompt) */
    uint8_t saves_first;   /* OM_SVF_*: Move or Share (default SHARE); a parsed NONE ("never decided") reads as on +
                            * SHARE (om_settings_parse) */
    uint8_t cheats;        /* 1 = cheats may run (default 1; CHEATS.PAK says which are on) */
    char bad_key[16];      /* the first new key whose value was not recognized ("" = none) */
    /* M6 Task 10 (M3.5 Task 4): */
    uint8_t reselect;      /* 1 = put the cursor on RECENT.TXT's first game at start (default 1) */
    uint8_t saver_min;     /* 0 (off), 1, 3, 5 (default 1) */
    uint8_t fw_check;      /* 1 = check the Satiator firmware against the tested minimum (default 1) */
    uint8_t pad_bad;       /* 1 = the pad_* keys named one button twice: defaults used */
    om_remap pad;          /* the button map (remap.h); always a permutation after om_settings_parse */
} om_settings;
enum { OM_LDR_AUTO, OM_LDR_CWX, OM_LDR_JHL, OM_LDR_COUNT };
enum { OM_SVF_NONE, OM_SVF_MOVE, OM_SVF_SHARE };
void om_settings_default(om_settings *s);
int om_settings_parse(om_settings *s, const char *text, uint32_t len);
int om_settings_merge(const om_settings *s, const char *old, uint32_t old_len, char *out, uint32_t cap, uint32_t *out_len);
const char *om_sort_name(uint8_t mode);
const char *om_anim_name(uint8_t mode);       /* "full", "reduced", "off": the SETTINGS.INI value and the overlay label */
uint8_t om_region_mask(const char *letters);
const char *om_loader_key(uint8_t l);   /* "auto" "cwx" "jhl" */
const char *om_loader_label(uint8_t l); /* "Auto" "CWX" "JHL" */
const char *om_saver_key(uint8_t minutes);          /* "off" "1" "3" "5" */
uint8_t om_saver_next(uint8_t minutes, int dir);    /* cycles 0 -> 1 -> 3 -> 5 -> 0 */
#endif
