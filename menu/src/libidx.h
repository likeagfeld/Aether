/* menu/src/libidx.h -- LIBRARY.IDX reader + writer (contract/SPEC.md §5.4, v1 clarifications 6-7). BSD-3-Clause. */
#ifndef LIBIDX_H
#define LIBIDX_H
#include <stdint.h>
#include "om_fmt.h"

#define OM_LIB_BOX   1u
#define OM_LIB_TITLE 2u

typedef struct {
    char key[17];
    uint8_t disc, disc_total, area, image_type;
    uint16_t folder, flags;
    const char *path, *title;     /* title is "" when there is no override */
} om_lib_entry;

typedef struct { const char *path; uint16_t parent; } om_lib_folder;

typedef struct {
    const uint8_t *table;
    uint32_t count;
    const uint8_t *fold;
    uint32_t nfolders;
    const char *pool;
    uint32_t pool_size;
    uint32_t fingerprint;
} om_libidx;

int om_libidx_open(om_libidx *l, const uint8_t *buf, uint32_t len);
void om_libidx_entry(const om_libidx *l, uint32_t i, om_lib_entry *e);
/* the number of GAMES (not entries): a multi-disc game's discs 2.. are not counted again. */
uint32_t om_libidx_games(const om_libidx *l);
void om_libidx_folder(const om_libidx *l, uint32_t i, om_lib_folder *f);

typedef struct {
    om_lib_entry *ent; uint32_t ent_cap, nent;
    uint16_t *order;
    om_lib_folder *fold; uint32_t fold_cap, nfold;
    char *pool; uint32_t pool_cap, pool_len;
} om_libw;

void om_libw_init(om_libw *w, om_lib_entry *ent, uint32_t ent_cap, uint16_t *order, om_lib_folder *fold,
                  uint32_t fold_cap, char *pool, uint32_t pool_cap);
int om_libw_folder(om_libw *w, const char *path, uint16_t parent);
int om_libw_entry(om_libw *w, const om_lib_entry *e);
int om_libw_finish(om_libw *w, int sort, uint32_t fingerprint, uint32_t build_date, uint8_t *out, uint32_t cap, uint32_t *out_len);

int om_is_hidden_name(const char *name, int at_root);
uint32_t om_fingerprint(const char *const *names, uint32_t n, uint16_t *scratch);

/* The card root's fingerprint over a listing of any size (2026-11-23 hardware report: a 698-name root overflowed the scan
 * listing's 512 names / 16 KB and the index read stale on every boot). Names go into the caller's arena (the card-scan
 * work area at boot) from the front, an 8 B row a name from the back; hidden root names are skipped, not stored. The
 * result equals om_fingerprint's over the same names. om_fpacc_add returns OM_E_FULL once the arena is full. */
typedef struct {
    char *buf; uint32_t cap, used, n, max;
    uint32_t *off; uint16_t *idx, *tmp;
    int overflow;
} om_fpacc;
void om_fpacc_init(om_fpacc *a, void *arena, uint32_t len);
int om_fpacc_add(om_fpacc *a, const char *name);
uint32_t om_fpacc_finish(om_fpacc *a);
#endif
