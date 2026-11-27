/* menu/pop/pcheat.c -- see pcheat.h. BSD-3-Clause. */
#include <string.h>
#include "cheats.h"
#include "om_fmt.h"
#include "pcheat.h"

#define FILE_CAP (OM_CP_MAX_FILE + 4u)

int om_pc_open(om_pc *c, const char *key, om_ch_file *f, uint8_t *arena, uint32_t cap)
{
    int rc;
    memset(c, 0, sizeof *c);
    c->f = f;
    c->arena = arena;
    c->cap = cap;
    strncpy(c->key, key ? key : "", sizeof c->key - 1);
    memset(f, 0, sizeof *f);
    if (!c->key[0] || cap < OM_CP_STAGE) c->state = 2;             /* nothing to save under */
    else if ((rc = om_cheats_load(c->key, arena, cap, f)) == 1) c->state = 0;
    else if (rc == 0 || rc == OM_SDW_NOFILE) { memset(f, 0, sizeof *f); c->state = 1; }   /* a new record */
    else { memset(f, 0, sizeof *f); c->state = 2; }                /* does not read: read only */
    om_ced_open(&c->ed, f, c->state);
    return c->state;
}

int om_pc_save(om_pc *c, om_sdw_gap_fn gap, char *msg, uint32_t cap)
{
    uint8_t *in = c->arena, *out = in + FILE_CAP, *rec = out + FILE_CAP, *scr = rec + OM_CP_MAX_REC + 4u;
    uint32_t len = 0, n = 0, ocap;
    om_ch_file *chk;
    om_sdw_stats st;
    int rc;
    const char *why = 0;
    if (c->state == 2 || c->ed.ro) why = "Read only: not saved";
    else if (c->cap < OM_PC_ARENA) why = "Not saved: no memory";
    else if ((rc = om_sdw_read_file(OM_PC_PATH, in, FILE_CAP, &len)) == OM_SDW_NOFILE) len = 0;
    else if (rc == OM_E_FULL) why = "Not saved: CHEATS.PAK too big";
    else if (rc) why = "Not saved: the card did not read";
    if (!why && len) {                                            /* the file must still read before it is rewritten */
        chk = (om_ch_file *)(void *)out;                          /* out is free until om_cpk_put */
        if (om_cpk_find(in, len, c->key, chk) < 0) why = "Not saved: CHEATS.PAK does not read";
    }
    if (why) {
        strncpy(msg, why, cap - 1);
        msg[cap - 1] = 0;
        return OM_E_FIELD;
    }
    ocap = len + OM_CPK_GROW < OM_CP_MAX_FILE ? len + OM_CPK_GROW : OM_CP_MAX_FILE;
    if ((rc = om_cpk_put(len ? in : 0, len, c->key, c->f, 20261124u, out, ocap, rec, &n)) < 0) {
        strncpy(msg, rc == OM_E_FULL ? "Not saved: CHEATS.PAK would be too big" : "Not saved: a bad record", cap - 1);
        msg[cap - 1] = 0;
        return rc;
    }
    memset(&st, 0, sizeof st);
    if ((rc = om_sdw_replace(OM_PC_PATH, out, n, scr, gap, &st)) < 0) {
        strncpy(msg, "Not saved: the card write failed", cap - 1);
        msg[cap - 1] = 0;
        return rc;
    }
    strncpy(msg, "Saved", cap - 1);
    msg[cap - 1] = 0;
    c->ed.dirty = 0;
    return 0;
}
