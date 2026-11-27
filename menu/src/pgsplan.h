/* menu/src/pgsplan.h -- the per-game-save planner (contract/SPEC.md §5.17). Pure, portable C99,
 * host-tested (test_pgsplan.c); the launcher's executor (menu/launcher/pgsrun.c) runs what it plans.
 *
 * BRAM state: a backup-RAM record named OM_PGS_MARKER whose data is "<gid> <slot>\n" says that BRAM holds that game's
 * slot; no marker = BRAM holds the general set. The marker name is exactly 11 characters: ST-162-R1 §2.4 BUP_Read,
 * BUP_Delete and BUP_Verify say "11 characters are required in ASCII" (printed pp.46-47) and ST-203 §2.0 4) "File names
 * must be 11 characters in length" (printed p.4).
 *
 * Plans (every list ends with its marker op; every archive is followed by VERIFY_DIR before anything clears BRAM):
 *  1 marker M, want G (pgs), same game and slot      -> nothing
 *  2 marker M, want G (pgs), another game or slot     -> ARCHIVE M, VERIFY M, CHECK_FITS G, CLEAR,
 *                                                        [LOAD _GENERAL if share && !slot_exists], LOAD G, WRITE_MARKER G
 *  3 no marker, want G (pgs)                          -> ROTATE_GENERAL, ARCHIVE _GENERAL, VERIFY _GENERAL, CHECK_FITS G,
 *                                                        CLEAR, [LOAD _GENERAL if share && !slot_exists], LOAD G,
 *                                                        WRITE_MARKER G
 *  4 marker M, want no pgs (or a save tool)           -> ARCHIVE M, VERIFY M, CHECK_FITS _GENERAL, CLEAR,
 *                                                        LOAD _GENERAL, DELETE_MARKER
 *  5 no marker, want no pgs                           -> nothing
 * (Open decision 9, Share: a game's FIRST use starts with a copy of the general set, so the _GENERAL load applies in
 * case 2 as well as case 3; with share = 0 both lists are exactly the brief's.)
 * The undo list (om_pgs_undo) puts back the state before the swap; the executor runs it when an op at or after CLEAR
 * fails with an rc: 2 -> CLEAR, LOAD M, WRITE_MARKER M; 3 -> CLEAR, LOAD _GENERAL, DELETE_MARKER;
 * 4 -> CLEAR, LOAD M, WRITE_MARKER M; 1 and 5 -> nothing.
 * a FOREIGN BRAM -- a marker whose session nonce does not match
 * ACTIVE.TXT, a lost marker while ACTIVE.TXT names a session, or a journal whose BRAM changed outside Aether -- gets
 * the foreign plan (state.foreign): QUARANTINE Q, CHECK_FITS _GENERAL, CLEAR, LOAD _GENERAL, DELETE_MARKER, undo
 * CLEAR, LOAD Q, DELETE_MARKER, where Q = SAVES/_QUARANTINE/<qnum> (never overwritten). Reserved records
 * (om_pgs_reserved) are never archived, cleared, loaded or counted.
 * Op dirs are relative to /aether/: "SAVES/<folder>" (slot 0), "SAVES/<folder>/0<n>" (slots 1-9), "SAVES/_GENERAL";
 * WRITE_MARKER's dir is the slot dir the marker names; ROTATE_GENERAL, CLEAR and DELETE_MARKER have "".
 * BSD-3-Clause. */
#ifndef OM_PGSPLAN_H
#define OM_PGSPLAN_H
#include <stdint.h>
#include "gameid.h"

enum { OM_OP_ARCHIVE_BRAM_TO, OM_OP_VERIFY_DIR, OM_OP_ROTATE_GENERAL, OM_OP_CLEAR_BRAM_EXCEPT_MARKER,
       OM_OP_LOAD_DIR_TO_BRAM, OM_OP_WRITE_MARKER, OM_OP_DELETE_MARKER, OM_OP_CHECK_FITS,
       OM_OP_QUARANTINE,                          /* copy BRAM's records into Q, never overwriting */
       OM_OP_LOAD_NEWER,                          /* after a slot's first LOAD, a newer copy
                                                   * of one of its records from the dir (_GENERAL) replaces it */
       OM_OP_COUNT };

#define OM_PGS_DIR      48u
/* The Aether rename (2026-11-25) keeps both names below: the marker lives in consoles' backup RAM and the stamp in the
 * slot folders of cards set up before it, so renaming either would make the next swap see changed saves. */
#define OM_PGS_MARKER   "VIBEMENU_PG"            /* 11 characters (above) */
#define OM_PGS_GENERAL  "SAVES/_GENERAL"
#define OM_PGS_QUAR     "SAVES/_QUARANTINE"      /* SAVES/_QUARANTINE/<n>/, n = 1..65535 */
#define OM_PGS_MAX_OPS  12u                       /* the longest plan is 9 ops */
#define OM_PGS_STAMP    "VIBEMENU.TXT"           /* in a slot folder: Aether has loaded this slot */
#define OM_PGS_FNAME    20u                       /* "<11>~9.BUP" + NUL */

typedef struct { uint8_t op; char dir[OM_PGS_DIR]; } om_pgs_op;
/* gid buffers are OM_GID_MAX (gameid.h: "<10>@<8>" + NUL), not the brief's 16. share = the user chose Share
 * (SETTINGS.INI saves_first=share; the brief's first_share is share && !slot_exists, derived here). */
typedef struct {
    char marker_gid[OM_GID_MAX]; uint8_t marker_slot; uint8_t has_marker;   /* from BRAM */
    char want_gid[OM_GID_MAX]; uint8_t want_slot; uint8_t want_pgs;          /* this launch */
    uint8_t share;                                                           /* saves_first=share */
    uint8_t slot_exists;                                                     /* want's slot dir exists on SD */
    uint8_t foreign;                                                         /* the foreign plan */
    uint16_t qnum;                                                           /* its quarantine folder number */
    /* Kai's product-only folder rule (SPEC §5.14/§5.17): the executor sets these, never the journal --
     * 1 when "SAVES/<product>" exists and "SAVES/<product>[<areas>]" does not (om_pgs_product_dir), so a folder
     * imported from Kai or Rings without areas is the game's folder. Stable across a swap: Aether never creates
     * or removes either folder name while the other one decides. */
    uint8_t want_po, marker_po;
    /* M6 Task 24 fix round 2 (ruling, review minor 1): 1 = the wanted slot's folder exists and Aether never loaded it
     * (no SAVES/<folder>[/0n]/VIBEMENU.TXT, written when a swap that loaded it completes). Derived like want_po, never
     * journaled: the stamp is written only after the journal is gone, so it cannot change under a resume. The plan then
     * adds LOAD_NEWER _GENERAL after LOAD G: per record (§5.16 om_bup_prefer), a newer intact general-set copy is loaded
     * instead of the slot's; the other copy stays where it is (the slot's file, or _GENERAL's), never dropped. */
    uint8_t first_load;
} om_pgs_state;

/* OM_E_FIELD: a slot over 9, or a gid that fails om_gameid_check where it is used; OM_E_FULL: over cap. */
int om_pgs_plan(const om_pgs_state *s, om_pgs_op *ops, uint32_t cap, uint32_t *n);
int om_pgs_undo(const om_pgs_state *s, om_pgs_op *ops, uint32_t cap, uint32_t *n);
/* "SAVES/MK-81045[U]" (slot 0), "SAVES/MK-81045[U]/03"; _po with po != 0: "SAVES/MK-81045/03" */
int om_pgs_slot_dir(char out[OM_PGS_DIR], const char *gid, uint32_t slot);
int om_pgs_slot_dir_po(char out[OM_PGS_DIR], const char *gid, uint32_t slot, int po);
/* "SAVES/<product>" for a gid with areas (the folder Kai's rule prefers when it exists); OM_E_FIELD for a
 * gid without areas (its folder already is the product alone) or a bad gid */
int om_pgs_product_dir(char out[OM_PGS_DIR], const char *gid);
int om_pgs_q_dir(char out[OM_PGS_DIR], uint32_t n);  /* "SAVES/_QUARANTINE/<n>"; OM_E_FIELD for 0 or > 65535 */
const char *om_pgs_op_name(uint32_t op);             /* "ARCHIVE", "VERIFY", ... ; "?" past OM_OP_COUNT */

/* the marker's line is "<gid> <slot> <nonce>\n" and ACTIVE.TXT's "<gid> <slot> <nonce> <fp>\n"
 * (nonce: 8 hex digits, a new one per WRITE_MARKER; fp: om_pgs_fp of the set just loaded, 8 hex digits). The fmt calls
 * return the length, or OM_E_FIELD / OM_E_FULL. The parser takes either form: fp NULL = the marker form (exactly three
 * fields), else the ACTIVE.TXT form (four). A marker is valid only when ACTIVE.TXT holds the same gid, slot and nonce. */
int om_pgs_marker_fmt(char *out, uint32_t cap, const char *gid, uint32_t slot, uint32_t nonce);
int om_pgs_active_fmt(char *out, uint32_t cap, const char *gid, uint32_t slot, uint32_t nonce, uint32_t fp);
/* in-game deletes stick: a record VERSION's id -- CRC-32 over its normalized 12-byte name,
 * date, data size and the data's CRC-32 (big-endian). ACTIVE.TXT's second line "R <id> <id> ...\n" (8 hex digits each)
 * lists the versions BRAM held when the set was loaded (WRITE_MARKER), plus every version reconcile added since. */
uint32_t om_pgs_rec_id(const char name[12], uint32_t date, uint32_t size, uint32_t data_crc);
int om_pgs_marker_parse(const uint8_t *d, uint32_t n, char gid[OM_GID_MAX], uint8_t *slot, uint32_t *nonce,
                        uint32_t *fp);                                                       /* OM_OK, OM_E_FIELD */

/* R4: records Aether never moves, archives, clears, loads or counts -- the marker, PSEUDO_* (Kai's PSEUDO__SAT,
 * PSEUDO_XTRA, PSEUDO_BRAM), _CARTDMP* (_CARTDMPHDR, _CARTDMPDAT) and SAVRING* (Rings): the names of Kai's
 * pbram_is_reserved_save (cafe-alpha pseudo_bram.c:127-161, unverified, as task-23-review.md I1 reads it). */
int om_pgs_reserved(const char name[12]);
#define OM_PGS_KAI_MARKER "PSEUDO_BRAM"          /* Kai holds a game's set in BRAM: Aether pauses its swap */
/* R2: the fingerprint of a BRAM listing (om_bup_dir entries, stride bytes apart) -- CRC-32 over the non-reserved
 * records in name order, each as name (11 bytes, NUL-padded), date and datasize (big-endian); *count = those records.
 * Independent of the listing's order. */
uint32_t om_pgs_fp(const void *list, uint32_t n, uint32_t stride, uint32_t *count);
/* R1: a copy of a record (new) supersedes an older one of the same name when its date is later, or the dates are
 * equal and the bytes are the same (same != 0) -- only then may the older SD copy be deleted; else it is quarantined. */
int om_pgs_supersedes(uint32_t new_date, uint32_t old_date, int same);

/* JOURNAL.TXT: one line holding every planner input, so a resume re-plans the very same list, plus where it stopped:
 * "op=<i> ph=<p> undo=<0|1> gid=<want> slot=<n> pgs=<0|1> share=<0|1> had=<0|1> marker=<0|1> mgid=<gid> mslot=<n>\n"
 * then " foreign=<0|1> q=<n> fp=<8 hex> fn=<n>", all on one line; an empty gid is written "-". op indexes
 * the forward plan, or the undo list when undo=1; ph is the op's phase. fp/fn (R2): BRAM's fingerprint and record count
 * when the plan was made (the state before the swap): a resume before CLEAR runs only while BRAM still has them.
 * then " g=<n>" when the plan's ARCHIVE writes generation n of its folder (savegen.h; chosen when the plan
 * is made, so a resume writes the very same folder); absent (0) when that folder's current set already equals BRAM. */
typedef struct { om_pgs_state s; uint8_t op, ph, undo; uint32_t fp; uint16_t fn; uint32_t g; } om_pgs_journal;
int om_pgs_journal_fmt(char *out, uint32_t cap, const om_pgs_journal *j);        /* length, or OM_E_FULL */
int om_pgs_journal_parse(const char *t, uint32_t n, om_pgs_journal *j);          /* OM_OK, OM_E_FIELD */

/* A record's archive file name: every byte of the name outside A-Z 0-9 _ - becomes '_', then ".BUP"; when that is
 * already in taken (ASCII case-insensitive), "~1".."~9" go before the extension (OM_E_FULL past ~9). OM_E_FIELD: a
 * name om_bup_name_norm refuses. */
int om_pgs_file_name(char out[OM_PGS_FNAME], const char name[12], const char (*taken)[OM_PGS_FNAME], uint32_t ntaken);

/* Blocks a record needs on the built-in memory: the file's own count (0x30, BUP_Dir's on the device it came from)
 * when nonzero, else an estimate -- 1 + (datasize + 0x1D) / 58, Kai's and Yabause's formula (cafe-alpha
 * vmem/dev_internal.c:634, pseudo_bram.c:1011, unverified, from task-23-review.md M3; fix round 1). UNVERIFIED on
 * hardware: ST-162-R1 documents no block layout and TechBulletins #9 says the Memory Manager's Size "does not match"
 * device accounting; the undo list covers a wrong estimate (a BUP_Write that runs out of room undoes the swap). */
uint32_t om_pgs_blocks(uint32_t datasize, uint32_t file_blocks);
/* "SAVES: <n> KB DO NOT FIT IN 32 KB BRAM", n = ceil(blocks * block_size / 1024) */
void om_pgs_fit_msg(char *out, uint32_t cap, uint32_t blocks, uint32_t block_size);
#endif
