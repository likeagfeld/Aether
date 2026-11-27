/* menu/pop/pcheat.h -- the Cheats popup's file side (mockup 2026-11-25): chedit.h's model (om_ced, unchanged) on one
 * game's record, read the way the Launch Options panel reads it (app/cheats.h om_cheats_load: the index in pieces, the
 * one record -- never the whole file), and saved the way the launcher's editor saves it (lcheat.c): the whole file read
 * into the lent arena, the record put with om_cpk_put, written by om_sdw_replace (CHEATS.PAK.NEW written, read back,
 * renamed over). A file that does not read opens read only, so a user's file is never replaced by a blank. Host-tested
 * by test_pcheat.c over fake_satiator.c. BSD-3-Clause. */
#ifndef OM_PCHEAT_H
#define OM_PCHEAT_H
#include <stdint.h>
#include "chedit.h"
#include "sdwrite.h"

#define OM_PC_PATH "/aether/CHEATS.PAK"
/* the arena a save needs: the old file and the new one (each up to OM_CP_MAX_FILE, + 4: libsatiator reads words),
 * one record's encode scratch, sdwrite's 2 KB */
#define OM_PC_ARENA (2u * (OM_CP_MAX_FILE + 4u) + OM_CP_MAX_REC + 4u + 2052u)

typedef struct {
    om_ced ed;
    om_ch_file *f;
    uint8_t *arena;                         /* 4-aligned, >= OM_PC_ARENA (open needs OM_CP_STAGE of it) */
    uint32_t cap;
    char key[17];
    int state;                              /* chedit.h file_state: 0 read, 1 none, 2 read only */
} om_pc;

/* key "" (no product key) opens read only. Returns the state. */
int om_pc_open(om_pc *c, const char *key, om_ch_file *f, uint8_t *arena, uint32_t cap);
/* after B on the list with c->ed.dirty: 0 saved (msg "Saved"), < 0 not saved (msg says why; the file is unchanged) */
int om_pc_save(om_pc *c, om_sdw_gap_fn gap, char *msg, uint32_t cap);
#endif
