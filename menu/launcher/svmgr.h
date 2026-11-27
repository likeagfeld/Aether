/* menu/launcher/svmgr.h -- the save manager's operations ("Manage saves", OM_HO_SAVEMGR): the saves of the console's
 * memory, of a cartridge and of the card, with their details, copied between them, backed up to the card, deleted.
 * The feature set is Slinga's Save Game Copier's (GPL-3.0; github.com/slinga-homebrew/Save-Game-Copier: list per
 * device, details, copy, delete, back up); the code is Aether's, over its own BIOS backup-library layer (bram.h)
 * and its add-only save folders (SPEC §5.17, user decisions 2026-11-24):
 *  - nothing on the card is ever deleted, moved or written over: a save "deleted" from the card is a new generation
 *    of its slot folder without it (the old copy stays in the earlier generation); a backup is a new file in a new
 *    folder SAVES/_MANAGER/<seq>/; every file goes through sdwrite's write-then-verify;
 *  - before a save leaves the console's memory or is written over there, the whole memory is backed up (pgsbak.h,
 *    a .BKP generation) and that save is copied to SAVES/_MANAGER; a cartridge save is copied there first too;
 *  - a save goes into the console's memory only while it holds the general set (no marker) or the set of the game the
 *    save belongs to (the next launch files it under that set, as a game's own save);
 *  - with a save swap unfinished (JOURNAL.TXT) or Pseudo Saturn Kai's set in the console (PSEUDO_BRAM), nothing is
 *    written; reserved records (pgsplan.h om_pgs_reserved) are never listed, copied or deleted.
 * Paths are relative to /aether (the caller s_chdir's there: every command then skips the card root's names).
 * Host-tested (test_svmgr.c over fake_bram.c and fake_satiator.c). BSD-3-Clause. */
#ifndef OM_SVMGR_H
#define OM_SVMGR_H
#include <stdint.h>
#include "bupfile.h"
#include "gameid.h"
#include "pgsplan.h"
#include "pgsrun.h"

enum { OM_SV_CON, OM_SV_CART, OM_SV_SD, OM_SV_NDEV };
#define OM_SV_PATH   112u
#define OM_SV_SETS   11u                /* one game's slot sets 0..9, or the general set */
#define OM_SV_MGR    "SAVES/_MANAGER"

/* what an operation did: 0 done, OM_SV_ASK (a save of that name is there: call again with confirm), a refusal
 * (OM_SVR_*), or an SD / backup-library error (< 0) */
#define OM_SV_ASK 1
enum { OM_SVR_JOURNAL = 2, OM_SVR_KAI, OM_SVR_OTHERSET, OM_SVR_NOROOM, OM_SVR_RESERVED, OM_SVR_TOOBIG, OM_SVR_NODEV,
       OM_SVR_GENS, OM_SVR_BAD, OM_SVR_FOREIGN };
/* a backup-library code (bupfile.h OM_BUP_*, all > 0) as an error, so it never reads as OM_SV_ASK or a refusal */
#define OM_SV_LIB(rc)  (-1000 - (rc))

typedef struct {
    om_bup_dir d;                       /* name, comment, language, date, data size, blocks */
    uint8_t dev, set, alt, mine;        /* SD: its set (om_sv.set), an alternate (ALT/); mine: this game's */
    char file[OM_PGS_FNAME + 4];        /* SD: its file in the set's folder ("ALT/<file>" for an alternate) */
} om_sv_item;

typedef struct {
    char slot_dir[OM_PGS_DIR];          /* "SAVES/<folder>[/0n]" */
    char cur[OM_SV_PATH];               /* its current set (the folder itself, or its newest complete generation) */
    uint8_t slot, general;
} om_sv_set;

typedef struct {
    const om_pgs_env *e;                /* the save swap's arena: library buffers, list, record buffers, scratch */
    uint8_t *big;                       /* one record file (64-byte header + data), bigcap bytes, 4-aligned */
    uint32_t bigcap;
    om_sv_item *it;                     /* the listed saves, at most cap */
    uint32_t n, cap;
    om_sv_set set[OM_SV_SETS];
    uint32_t nset;
    char gid[OM_GID_MAX];               /* the game the manager was opened for ("" = every game) */
    char folder[OM_PGS_DIR];            /* the SD view's game folder name ("" = none chosen; "_GENERAL") */
    int con_rc, cart_rc;                /* the console memory / cartridge as the library found them (0 = usable) */
    uint8_t marker, kai, journal;       /* VIBEMENU_PG in BRAM; Kai's PSEUDO_BRAM; a save swap unfinished */
    uint8_t foreign;                    /* the marker without its own ACTIVE.TXT (R3): the console's set is unknown */
    char mgid[OM_GID_MAX];              /* the marker's game and slot */
    uint8_t mslot;
    const char *date;                   /* "YYYYMMDD-HHMMSS" or "" for the backups */
} om_sv;

/* the library's Init (pgsbak's BRAM backup needs it), the marker, the journal, the cartridge. gid: the game (or ""). */
int om_sv_open(om_sv *v, const om_pgs_env *e, const char *gid, uint8_t *big, uint32_t bigcap, om_sv_item *items,
               uint32_t cap, const char *date);
/* dev's saves into v->it / v->n. OM_SV_SD lists v->folder's slot sets (the game's own folder when opened for one). */
int om_sv_list(om_sv *v, int dev);
/* the card's save folders (global view): names under SAVES/ ("_GENERAL" first when it exists; other "_" folders not) */
int om_sv_folders(om_sv *v, char (*out)[OM_PGS_DIR], uint32_t cap, uint32_t *n);
int om_sv_set_folder(om_sv *v, const char *folder);   /* the SD view's folder; "" back to the game's own */

int om_sv_backup(om_sv *v, const om_sv_item *x, uint32_t *seq);         /* to SAVES/_MANAGER/<seq>/ */
int om_sv_copy(om_sv *v, const om_sv_item *x, int to, int confirm);    /* to OM_SV_CON or OM_SV_CART */
int om_sv_delete(om_sv *v, const om_sv_item *x);                       /* the caller has asked twice */
/* an SD save whose set the console holds, with a save of that name there: "Also in console memory" */
int om_sv_in_console(om_sv *v, const om_sv_item *x, om_sv_item *con);
/* the in-menu popup (2026-11-25): 1 = the console holds set si (the marker's game folder and slot, or with no marker
 * the general set) */
int om_sv_set_loaded(om_sv *v, uint32_t si);
/* "Back up now": the whole console memory as a new .BKP generation (pgsbak.h), the guard's own backup; *same = 1 when
 * it equals the newest generation (nothing written). 0 or a refusal / error. */
int om_sv_backup_all(om_sv *v, uint32_t *seq, int *same);
const char *om_sv_why(int r);
#endif
