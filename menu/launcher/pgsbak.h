/* menu/launcher/pgsbak.h -- M6 Task 24 (contract/SPEC.md §5.17, "Import" and "BRAM backup"): the copy-only import of
 * Pseudo Saturn Kai's and Satiator Rings' per-game save folders, run when the user asks, and the always-on, copy-only,
 * dated BRAM backup taken before every launch. Both run in LAUNCHER.BIN, in API mode, inside the save-swap arena
 * (pgsrun.h's om_pgs_env), never in the in-game reset (the IGR path does no SD write; progress.md 2026-11-20).
 * Portable: test_pgsbak.c runs both over fake_bram.c and fake_satiator.c with a power cut after every mutating call.
 *
 * IMPORT (fix round 1, user decision 2026-11-21: optional, never automatic -- Settings > Import saves hands off with
 * OM_HO_IMPORT and no game; lseq.c om_run_import shows its progress and result, then resets to Aether). Every "*.BUP"
 * under Kai's /SAVES/BRAM/<folder>/ (slot 0) and <folder>/0n/ (slots 1-9; cafe-alpha pseudo_bram.c:674-684,
 * PSEUDO_BRAM_FOLDER pseudo_bram_common.h:24) and under Rings' /satiator-saves/<game id>/ (slot 0) and
 * /satiator-saves/[SLOTnn]/<game id>/ (slots 1-9; retrohead satiator-rings save_functions.c getPathForGameId) is COPIED
 * into the folder Aether reads for that game. The originals are never written, renamed or deleted.
 * - One product, one folder (review I3): a product's region folders ("<product>[<areas>]", Kai's, or Aether's own
 *   already on the card) each receive the product-only sources (Rings' id folder, Kai's product-only folder); with no
 *   region folder anywhere, the product folder itself (the launcher reads it while no region folder exists: pgsplan.h
 *   want_po). Kai's own rule, from its source (pseudo_bram.c:1230-1262): on a Satiator the region is used only when the
 *   product-only folder lists no entry -- so a Kai tree may hold live saves in either; both are merged.
 * - Newest intact wins (review I4): per record, all the sources' copies are decoded and SPEC §5.16's om_bup_prefer picks
 *   one (later date, then save_id, then the own-name file, then the name order). A losing copy that is not the same
 *   version (date, size, data) goes to SAVES/_QUARANTINE/IMPORT/<folder>[/0n]/ -- copied, never dropped.
 * - Never over an Aether save: a record the destination holds in the same version is "present"; in another version a
 *   "conflict", and the destination is left as it is.
 * - Skipped, and counted: '_' folders (both menus' _BACKUP: the general set), Rings slots 10-19, names that are not a
 *   product id, files over 32 KB (a cart or floppy save), files that do not decode, reserved records (om_pgs_reserved),
 *   folders over OM_PGS_ENTS (sources) or 256 (a destination or quarantine folder: not touched).
 * - M6 Task 24b: a record goes into the destination's CURRENT set (savegen.h: its newest complete generation, else
 *   the folder itself), under a new name; nothing on the card is deleted, moved or written over.
 * - Power-cut safe and idempotent: each file is written as "<name>.BUP.IMP" (verify on) and renamed; a run again copies
 *   nothing that is present and quarantines nothing already quarantined. IMPORT.TXT (the counts) is written last.
 *
 * BACKUP (user decision 2026-11-21, "always-on, copy-only, dated BRAM backup to SD before every launch"): after the
 * backup library's Init, before any journal resume or swap, so it holds BRAM as the last session left it. One file per
 * generation, /aether/SAVES/_BACKUP/<sss>/<seq>-<YYYYMMDD-HHMMSS>.BKP (M6 Task 24b: in shard folders, sss = seq / 100
 * in three digits, so no listing holds more than ~100 generations; <seq> 5 digits; "-<date>" only when the console
 * clock was set: SMPC OREG0 STE, SMPC_Manual.txt:1536-1550), written as ".TMP" (om_sdw_write_src, verify on) and renamed
 * to ".BKP" -- a ".TMP" left by a power cut is never counted (Task 24b: the next generation takes the next seq). Layout (big-endian): a 64-byte header
 * ("OMSATBKP", u16 version 1, u16 count, u32 body length, u32 body CRC-32, u32 seq, char date[16], char marker[24]),
 * count x 32-byte table entries (name[12], comment[11], language, u32 date, u32 datasize), then the body: each record
 * as a §5.16 .BUP image (64-byte header + data), padded to 4 bytes -- `python -m omsat.bramback split` turns a
 * generation back into .BUP files. Records: every readable BRAM record except Aether's own marker (whose line goes in
 * the header); torn ones are skipped and counted. A BRAM identical to the newest generation writes nothing.
 * - M6 Task 24b (user decision 2026-11-22 (a)): add-only -- no generation is ever deleted or written over by the
 *   launcher (the fix round 1 prune is gone); nothing removes one (Tidy saves is gone in 0.1).
 * - A failure is a notice, never a stopped
 *   launch. BSD-3-Clause. */
#ifndef OM_PGSBAK_H
#define OM_PGSBAK_H
#include <stdint.h>
#include "pgsrun.h"
#include "impscan.h"

#define OM_PGS_IMPORT_TXT  "/aether/SAVES/IMPORT.TXT"
/* a slot folder's sub-folder for saves over 32 KB (cartridge or floppy saves,
 * 19 of the 326 release files): kept with the game, standard .BUP files, never loaded into the internal memory (the
 * swap reads only a folder's own *.BUP; sub-folders are never touched, SPEC §5.17) */
#define OM_PGS_CART        "CART"
#define OM_PGS_BAK_DIR     "/aether/SAVES/_BACKUP"
#define OM_PGS_KAI_ROOT    "/SAVES/BRAM"
#define OM_PGS_RINGS_ROOT  "/satiator-saves"
#define OM_PGS_KAI_LATEST  "/SAVES/LATEST"           /* Kai's saves in no BRAM/<game> folder */
#define OM_PGS_BAK_HDR     64u
#define OM_PGS_BAK_ENT     32u
#define OM_PGS_BAK_MAGIC   "OMSATBKP"
#define OM_PGS_E_CANCEL    (-151)                  /* the import was canceled between two files (om_pgs_env.cancel) */

typedef struct {
    uint8_t ran;                      /* 1 = the sources were read */
    uint16_t kai, rings;              /* files copied from each */
    uint16_t present;                 /* records already in the destination, the same version (left alone) */
    uint16_t skipped;                 /* save files left out (by file, each named in IMPORT.TXT) */
    uint16_t conflicts;               /* records the destination already holds in another version: never written over */
    uint16_t older;                   /* losing copies (an older Kai or Rings version) put in _QUARANTINE/IMPORT */
    uint16_t cart;                    /* saves over 32 KB copied cart-only into <folder>[/0n]/CART */
    uint16_t alternates;              /* Rings [SLOT10-19] saves kept as alternates (every slot full) */
    uint8_t canceled;                /* the user canceled (B held): rc is OM_PGS_E_CANCEL, the copies made are kept */
    int rc;                           /* 0, or the call that stopped it (run it again) */
} om_pgs_imp;
/* a save over 32 KB (whatever its size) is copied cart-only into <folder>[/0n]/CART/, in pieces, and
 * counted in cart -- never skipped.
 * user decision 2026-11-21: run only when the user asks (Settings > Import, hand-off OM_HO_IMPORT).
 * 0 always (a failure is r->rc); notice: "" or "IMPORT STOPPED rc=<n>". Uses e's lib/work buffers for its name lists
 * (it makes no backup-library call), ents, rec, chk and scr. */
int om_pgs_import(const om_pgs_env *e, om_pgs_imp *r, char notice[OM_PGS_NOTICE]);
/* the import of the games ticked in the picker -- IMPORT.SEL (impscan.h) checked against gid ("SEL<crc>",
 * the hand-off's); absent, damaged or stale: r->rc and notice, nothing imported. The file is removed after a run. */
int om_pgs_import_sel(const om_pgs_env *e, const char *gid, om_pgs_imp *r, char notice[OM_PGS_NOTICE]);
/* The import's live progress (hardware 2026-11-24: "IMPORT: MK-81042" sat for minutes with nothing moving). Totals
 * are 0 when unknown. games: the picked games the picker's scan (impscan.h) finds; saves: the *.BUP files that scan
 * counts in them, done as each source folder is merged (clamped to the total); file_*: the save being copied or
 * compared now, in bytes; beat: +1 for every piece read, compared or written, so the screen moves while anything
 * runs. */
typedef struct om_pgs_prog {
    uint32_t games_done, games_total, saves_done, saves_total, file_done, file_total, beat;
    char game[24];                    /* the folder being filled, "MK-81042[U]" */
    char file[20];                    /* the save file now */
    /* the save swap (pgsrun.c, hardware 2026-11-24: "SAVE SWAP - KEEP THE POWER ON" for minutes): its journaled steps,
     * step n of steps, named by om_pgs_op_name ("ARCHIVE", "QUARANTINE", "VERIFY", "LOAD", ...) */
    uint32_t step, steps;
    char stage[16];
} om_pgs_prog;
/* a text bar of width cells ('#' done, '-' to go) then " nnn%": width + 5 chars + NUL in out */
void om_pgs_bar(char *out, uint32_t width, uint32_t done, uint32_t total);
/* the first save file the last import skipped ("" for none), for the result screen (24c) */
const char *om_pgs_import_first_skip(void);

typedef struct {
    uint8_t written;                  /* 1 = a new generation was written */
    uint8_t same;                     /* 1 = BRAM equals the newest generation: nothing written */
    uint16_t records, torn;           /* records in it; torn records left out */
    uint32_t seq;
    int rc;
} om_pgs_bak;
/* the backup library must be initialized (om_bram_init). date: "YYYYMMDD-HHMMSS" or "" (clock not set). 0 always. */
int om_pgs_backup(const om_pgs_env *e, const char *date, om_pgs_bak *r, char notice[OM_PGS_MSG]);
/* 1: the working folder is /aether (a save swap's run took env.rel, pgsrun.c): the backup names its folders from
 * there; 0 (the default): absolute */
void om_pgs_backup_rel(int on);

/* pure helpers (host-tested) */
int om_pgs_bak_name(char *out, uint32_t cap, uint32_t seq, const char *date, const char *ext);  /* "00012-<date><ext>" */
uint32_t om_pgs_bak_seq(const char *file, int *complete);   /* the seq of "<seq>[-...].BKP|.TMP"; 0 = not one */
/* om_pgs_imp_folder, om_pgs_rings_slot: impscan.h */
#endif
