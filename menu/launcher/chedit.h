/* menu/launcher/chedit.h -- the cheat editor (M6 Task 28, ruling C-EDIT: it runs in LAUNCHER.BIN, whose LWRAM has
 * room for the whole CHEATS.PAK; the menu, 1.3 KB above its HWRAM floor, only hands off). Launch Options > Cheats
 * hands off with OM_HO_CHEATS; lcheat.c draws this model on the launcher's text screen and saves.
 *
 * The model (pure, host-tested by test_chedit.c), on one game's record (cheatpak.h om_ch_file):
 *  - LIST: one row per cheat, "[x] NAME". Only an explicit A changes `on` (Kai's cursor bug designed out). A cheat
 *    that would take the enabled codes past the cap (chkai.h om_chk_cap: 30, or Kai's own cache size) is refused
 *    with the slots left. X adds a cheat (name, then its first code), Y opens a user cheat's codes (Kai's own are
 *    read only: a CHEATS.PAK update replaces them, SPEC §5.15 merge), Z deletes a user cheat (asks twice), B ends.
 *  - NAME: the cheat's name on a character wheel (Up/Down change the letter, Left/Right move), A done, B cancel.
 *  - CODE: ADDRESS (8 hex digits), VALUE (4), TYPE (00 once at boot, 01 16-bit, 03 8-bit, 0D only-if), each picked
 *    with the D-pad; A checks it with om_chk_code_ok and refuses a bad one with the reason, B cancels.
 *  - CODES: a user cheat's codes; A edits one, X adds one, Y renames, Z deletes one (asks twice), B back.
 * A corrupt file opens read only (the list empty, every edit refused) so a user's file is never replaced by a blank.
 *
 * The file (om_cpk_*): the whole CHEATS.PAK is rewritten with one key's record replaced, added or (no cheats left)
 * removed, in the PC writer's own layout (tools/omsat/cheatpak.py write_pak: rows sorted by key, records in row order,
 * identical records stored once, the header's build date kept), so a save that changes nothing rewrites the file
 * byte for byte, and no other key's record changes. BSD-3-Clause. */
#ifndef OM_CHEDIT_H
#define OM_CHEDIT_H
#include <stdint.h>
#include "cheatpak.h"

/* the launcher pad's bits (smpcpad.c: SGL's PER_DGT_* layout, SL_DEF.H:1250-1262) */
#define OM_CK_R     (1u << 15)
#define OM_CK_L     (1u << 14)
#define OM_CK_D     (1u << 13)
#define OM_CK_U     (1u << 12)
#define OM_CK_START (1u << 11)
#define OM_CK_A     (1u << 10)
#define OM_CK_C     (1u << 9)
#define OM_CK_B     (1u << 8)
#define OM_CK_X     (1u << 6)
#define OM_CK_Y     (1u << 5)
#define OM_CK_Z     (1u << 4)
#define OM_CK_REP   (1u << 0)      /* with U/D: a held key's repeat -- a list stops at its ends, a fresh press wraps */

enum { OM_CE_LIST, OM_CE_NAME, OM_CE_CODE, OM_CE_CODES };
/* what the last key did (the view's message line) */
enum { OM_CEM_NONE, OM_CEM_FULL, OM_CEM_BAD, OM_CEM_READONLY, OM_CEM_KAI, OM_CEM_FILEFULL, OM_CEM_ASK, OM_CEM_NAME,
       OM_CEM_LABEL, OM_CEM_LAST };
#define OM_CE_VIS   15u            /* rows on screen */
#define OM_CE_FIELDS 13u           /* 8 address digits, 4 value digits, the type */

typedef struct {
    om_ch_file *f;
    uint8_t page, ro, dirty, armed, msg, why, field, is_new, npos;
    uint16_t row, top, crow, ctop, cheat, code, cap;
    uint32_t addr; uint16_t val; uint8_t cmd;
    char name[OM_CH_MAX_NAME + 1];
} om_ced;

/* file_state: 0 read, 1 none (a new record), 2 corrupt (read only) */
void om_ced_open(om_ced *e, om_ch_file *f, int file_state);
/* one press (OM_CK_*): 1 = B on the list (done: save when e->dirty), else 0 */
int om_ced_key(om_ced *e, uint16_t keys);
uint32_t om_ced_active(const om_ced *e);
/* "30 - 29 = 1 SLOT LEFT" */
void om_ced_slots(const om_ced *e, char *out, uint32_t cap);
/* the CODE page's line, "ADDR:VALUE:TYPE" with the cursor's digit index (0..12) */
void om_ced_code_text(const om_ced *e, char out[18]);
extern const uint8_t om_ced_types[4];   /* 0x00, 0x01, 0x03, 0x0D */

/* CHEATS.PAK in memory: 1 = key found (*f filled), 0 = no record for the key, < 0 = the file does not read (OM_E_*) */
int om_cpk_find(const uint8_t *pak, uint32_t len, const char *key, om_ch_file *f);
/* The file with key's record = f (f->ncheats 0: the row goes). pak/len: the old file (NULL/0: none, a new file with
 * build date `date`). rec: OM_CP_MAX_REC bytes of scratch. The new file is at most len + OM_CP_MAX_REC + OM_CP_ROW
 * bytes (OM_CPK_GROW). OM_OK with *n bytes in out; OM_E_FULL past cap or OM_CP_MAX_FILE; OM_E_FIELD a bad record. */
#define OM_CPK_GROW (OM_CP_MAX_REC + OM_CP_ROW)
int om_cpk_put(const uint8_t *pak, uint32_t len, const char *key, const om_ch_file *f, uint32_t date, uint8_t *out,
               uint32_t cap, uint8_t *rec, uint32_t *n);
#endif
