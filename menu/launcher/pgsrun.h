/* menu/launcher/pgsrun.h -- the per-game-save executor (contract/SPEC.md §5.17; M6 Task 23). LAUNCHER.BIN runs it
 * once per launch, in API mode, after image2desc (lseq.c step 3) and before step 4. Portable: the host tests run it
 * over fake_bram.c and fake_satiator.c with a power cut or a failed call at every step, and with another menu's
 * session between the cut and the next launch (test_pgsrun.c).
 *
 * IGR rule (progress.md, 2026-11-20: "the IGR path does no SD writes"): this is the only place console BRAM is copied
 * to the SD, and it runs at the NEXT launch from Aether, never inside the in-game reset.
 *
 * Gate: with per-game saves off for this launch, and neither SAVES/ACTIVE.TXT nor SAVES/JOURNAL.TXT on the card, it
 * makes no BRAM write or delete. M6 Task 24 (user decision 2026-11-21, the always-on BRAM backup): with env.backup the
 * library is initialized on every launch for the backup's reads (ST-162-R1 §1.2.5 lists Init among the calls that can
 * destroy data when interrupted: accepted by that decision); without it, no backup-library call at all, as before.
 *
 * Order of a run: (1) a journal on the card is resumed first ("RESUMING SAVE SWAP"), but only while BRAM is what the
 * journal expects (below); (2) Kai's own per-game marker (PSEUDO_BRAM) in BRAM pauses the swap for this launch;
 * (3) the marker and ACTIVE.TXT are read: a foreign BRAM gets the foreign plan first; (4) om_pgs_plan gives this
 * launch's op list (and, Task 24b, the generation its ARCHIVE writes: the journal's g); before the first op, before
 * CLEAR and before VERIFY_DIR's DONE.TXT (swap speed, 2026-11-24: only the lines a resume needs; run_list), JOURNAL.TXT is
 * replaced
 * (om_sdw_replace: a power cut keeps the previous line, and every op and phase is a no-op when run again after it
 * completed); (5) the journal is deleted at the end.
 * A failed rc before CLEAR deletes the journal (BRAM untouched; the marker still names what BRAM holds; a generation
 * it was writing stays incomplete, never read); at VERIFY_DIR's DONE.TXT it keeps it. A failed rc at or after CLEAR runs om_pgs_undo's list; a failed undo keeps its journal
 * (undo=1). Every rc is compared with its success value 0 (rc != 0), never with < 0 (Rings' dead check).
 *
 * Fix round 1 (task-23-review.md C1, I1-I3; progress.md ruling R1-R5):
 * R1 -- no SD copy of a record is ever lost. M6 Task 24b (user decision 2026-11-22 (a)): ADD-ONLY -- no op deletes,
 *       overwrites, moves or renames over a ".BUP" (or a ".BKP"): an archive is a new generation folder (savegen.h),
 *       and an older copy simply stops being current. SAVES/_QUARANTINE/<n>/ (a fresh folder each time, never written
 *       over) only receives a BRAM changed outside Aether (R2/R3). Nothing removes a
 *       copy (Tidy saves is gone in 0.1).
 * R2 -- the journal holds BRAM's fingerprint (om_pgs_fp) from when the plan was made. A resume before CLEAR runs only
 *       while BRAM still has it; else BRAM is copied to a quarantine folder, the journal is dropped and this launch
 *       plans afresh. A resume at or after CLEAR (and every CLEAR) runs only while every non-reserved BRAM record is
 *       byte-identical to a file in the plan's archive or load folders (a torn record counts as covered); else the
 *       foreign plan runs: BRAM quarantined, the general set given back, the marker dropped. Either way the step screen
 *       gets a notice line.
 * R3 -- the marker carries a nonce that must match ACTIVE.TXT's; a marker without its ACTIVE.TXT, a different one, or
 *       ACTIVE.TXT without a marker is a foreign BRAM.
 * R4 -- reserved records (om_pgs_reserved) stay in BRAM: never archived, cleared, loaded or counted.
 * R5 -- _GENERAL gets a new generation only when no valid marker is present and BRAM differs from its current set (no
 *       _GENERAL.OLD, no rotation since Task 24b).
 *
 * Archive (M6 Task 24b): the plan picks the generation g (the journal's "g": 0 when the folder's current set already
 * holds exactly BRAM's records -- nothing is written -- else one above every generation of the folder). ARCHIVE writes
 * every non-reserved BRAM record into <dir>/G<g/100>/G<g> as "<name>.TMP" (verify on) renamed to "<name>.BUP", and
 * carries from the current set every record BRAM lacks (an import, a torn or deleted one) or holds in an older
 * version (24b fix, review I6) -- but never one the game deleted (a version ACTIVE.TXT's line 2 lists and BRAM no
 * longer has: in-game deletes stick); case 1 writes those records into BRAM instead (reconcile); VERIFY_DIR reads every file back and compares the count, the
 * bytes and the directory fields with BRAM (ph0), then creates the generation's DONE.TXT (ph1): from then on it is the
 * folder's current set. Nothing is renamed or deleted. Sub-folders 01-09 and CART are never touched. BSD-3-Clause. */
#ifndef OM_PGSRUN_H
#define OM_PGSRUN_H
#include <stdint.h>
#include "bram.h"
#include "bupfile.h"
#include "pgsplan.h"
#include "sdwrite.h"

#define OM_PGS_SAVES   "/aether/SAVES"
#define OM_PGS_ACTIVE  "/aether/SAVES/ACTIVE.TXT"
#define OM_PGS_JOURNAL "/aether/SAVES/JOURNAL.TXT"
#define OM_PGS_GENLIST "/aether/SAVES/GENLIST.TXT"   /* the general set's loaded versions */
#define OM_PGS_REC     (OM_BUP_HDR + 32768u + 4u)   /* one .BUP file of a record up to the whole 32 KB, + read slack */
#define OM_PGS_LIST    512u                         /* records listed: 512 blocks, so a full BRAM never overflows */
#define OM_PGS_ENTS    256u                         /* entries of one SD folder (more fails before CLEAR) */
#define OM_PGS_ENAME   96u                          /* longest file name read (a longer one fails before CLEAR) */
#define OM_PGS_MSG     41u
#define OM_PGS_NOTICE  81u                          /* two text rows */
#define OM_PGS_E_FOREIGN (-150)                     /* internal: BRAM is not what the journal or the plan expects */

typedef struct { char name[OM_PGS_ENAME]; uint8_t hdr[OM_BUP_HDR]; uint8_t isdir, win; } om_pgs_ent;
struct om_pgs_prog;
/* every buffer 4-aligned; om_pgs_arena_split carves them out of one block of OM_PGS_ARENA bytes */
typedef struct {
    uint8_t *lib, *work;              /* OM_BRAM_LIB_BYTES + OM_BRAM_GUARD, OM_BRAM_WORK_BYTES + OM_BRAM_GUARD */
    om_bup_dir *list;                 /* OM_PGS_LIST */
    char (*taken)[OM_PGS_FNAME];      /* OM_PGS_LIST */
    om_pgs_ent *ents;                 /* OM_PGS_ENTS */
    uint8_t *rec, *chk;               /* OM_PGS_REC each */
    uint8_t *scr;                     /* 2,048 B + 4 */
    om_sdw_gap_fn gap;                /* between SD chunks (launcher: lgap.c), may be NULL */
    void (*say)(const char *line);    /* the step screen's status line, may be NULL */
    uint32_t (*nonce)(void);          /* a new session nonce (launcher: lgap.c om_lnonce); NULL = a counter */
    /* pgsbak.h: 1 = the BRAM backup runs right after the library's Init, before any resume or swap;
     * date = "YYYYMMDD-HHMMSS" or "" (the console clock was not set) */
    uint8_t backup;
    const char *date;
    /* the import's live progress (pgsbak.h om_pgs_prog), may be NULL: called at each game, save and piece */
    void (*prog)(const struct om_pgs_prog *p);
    /* the import's cancel (B held), may be NULL: asked only between files and between games */
    int (*cancel)(void);
    /* 1: om_pgs_run may s_chdir("/aether") and name its files from there (each command then skips the card root's
     * ~700 names); the CALLER puts its working folder back after om_pgs_run returns, whatever it returned (lseq.c
     * save_swap: the game folder, for s_emulate("out.desc")). 0: absolute names, the working folder untouched. The
     * import never sets it. */
    uint8_t rel;
    /* with rel: the fewest records or files an op names for it to s_chdir into that save folder for them (pgsrun.c
     * option A); 0 = 4, 255 = never (the host tests force it on and off) */
    uint8_t deep;
    /* the hardware timing (2026-11-24: one root walk was a 27..180 ms guess), may be NULL: a free-running microsecond
     * clock (wraps). With it the run times itself, one stat of LAUNCHER.BIN named from the card root and one named
     * from /aether (the difference is one walk of the root), into the result's t_* fields; without it, no extra
     * command */
    uint32_t (*now_us)(void);
} om_pgs_env;
#define OM_PGS_ARENA (OM_BRAM_LIB_BYTES + OM_BRAM_WORK_BYTES + 2u * OM_BRAM_GUARD + OM_PGS_LIST * sizeof(om_bup_dir) \
                      + OM_PGS_LIST * OM_PGS_FNAME + OM_PGS_ENTS * sizeof(om_pgs_ent) + 2u * OM_PGS_REC + 2052u + 64u)
void om_pgs_arena_split(om_pgs_env *e, uint8_t *arena);   /* arena: OM_PGS_ARENA bytes, 4-aligned */

typedef struct {
    int rc;                           /* 0, or the failing call's rc */
    uint8_t op;                       /* the failing op (OM_OP_*) */
    uint8_t ran;                      /* 1 = the backup library was used */
    uint8_t resumed;                  /* 1 = a journal was found and resumed */
    uint8_t undone;                   /* 1 = the failed swap was undone: BRAM is as it was */
    uint8_t foreign;                  /* 1 = BRAM was foreign: quarantined and the general set given back */
    uint8_t paused;                   /* 1 = Kai's PSEUDO_BRAM is in BRAM: no swap this launch */
    uint8_t rel;                      /* 1 = env.rel took: this run named its files from /aether */
    char line[OM_PGS_MSG];            /* the error line ("SAVES: 38 KB DO NOT FIT IN 32 KB BRAM", ...) */
    char notice[OM_PGS_NOTICE];       /* a non-fatal notice for the step screen (up to two rows): only when BRAM
                                       * changed outside Aether (r->foreign), Kai holds it (r->paused), or a save in
                                       * BRAM was torn (r->torn); "" = none */
    uint8_t torn;                     /* torn BRAM records met */
    uint8_t torn_kept;                /* their card copies kept current (carried into the new generation) */
    /* the BRAM backup's outcome (env.backup), and its notice ("" = none) */
    uint8_t bak_written, bak_same;
    uint16_t bak_records;
    uint32_t bak_seq;
    int bak_rc;
    char bak_notice[OM_PGS_MSG];
    /* env.now_us: the whole run (ms), a stat named from the card root and one named from /aether (us); 0 = not timed */
    uint32_t t_ms, t_root_us, t_rel_us;
} om_pgs_result;

/* saves: the hand-off's byte (0 off, 1 on / Move, 2 on / Share); gid "" = no game id (per-game saves cannot apply).
 * 0 (the launch goes on; r->notice may hold a line) or the failing rc (r says which op and why). */
int om_pgs_run(const om_pgs_env *e, uint8_t saves, uint8_t slot, const char *gid, om_pgs_result *r);
#endif
