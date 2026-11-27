/* menu/src/match.h -- product-code matcher, contract/SPEC.md §6 v3 (one spec, two implementations; the other is
 * tools/omsat/match.py). BSD-3-Clause. */
#ifndef MATCH_H
#define MATCH_H
#include <stdint.h>
#include "gamesdb.h"

typedef enum { OM_HOW_NONE = 0, OM_HOW_EXACT, OM_HOW_ALIAS, OM_HOW_SKELETON, OM_HOW_PREFIX, OM_HOW_SUFFIX } om_how;
#define OM_MATCH_MAXCAND 64

typedef struct {
    void *ctx;
    uint32_t count;
    void (*key_at)(void *ctx, uint32_t i, char out[17]);
    int (*has)(void *ctx, const char *key);
    int (*alias)(void *ctx, const char *alias, char canon[17]);
} om_keysrc;

typedef struct { char key[17]; om_how how; uint32_t ncand; int overflow; } om_match_result;

const char *om_how_name(om_how h);
void om_cleanup(const char *in, uint32_t len, char *out, uint32_t cap);
void om_normalise(const char *in, uint32_t len, char out[17]);
int om_match(const om_keysrc *ks, const char *product, uint32_t plen, const char *area, om_match_result *r);
void om_keysrc_gamesdb(om_keysrc *ks, const om_gamesdb *db);
#endif
