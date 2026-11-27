/* menu/src/savegen.h -- M6 Task 24b (user decision 2026-11-22 (a)): the reader rule of the add-only save folders
 * (contract/SPEC.md §5.17). The launcher never deletes, overwrites or moves a save file; each archived set is written
 * under a NEW name instead, and this rule says which copy is current.
 *
 * A slot folder D ("/aether/SAVES/<game>", ".../<game>/0n" or ".../_GENERAL") keeps each archived set in a
 * generation folder D/G<sss>/G<nnnnn> (n = 1..99999, five digits; sss = n / 100, three digits), so no listing the
 * reader makes holds more than D's shards plus one shard's 100 generations. A generation is COMPLETE once it holds
 * DONE.TXT, an empty file created only after every file in it was verified (each console-memory record byte for byte;
 * each other file, a carried one -- pgsrun.c carry_cur, review I6 -- byte-identical to the set it follows; an import may
 * add files to it later). D's CURRENT set is
 * its highest-numbered complete generation; with none, D's own "*.BUP" (Kai's layout, Aether before Task 24b, or an
 * import into a game Aether has not archived yet). Sub-folders 01..09 (slots) and CART (cart-only saves) are never
 * part of a set. Used by the launcher (pgsrun.c, pgsbak.c) and the menu's slot status (folders.c).
 * One listing open at a time, each drained in full (satfs.c, HWF-22); names go to libsatiator from s->al (4-aligned).
 * BSD-3-Clause. */
#ifndef OM_SAVEGEN_H
#define OM_SAVEGEN_H
#include <stdint.h>

#define OM_GEN_MAX   99999u
#define OM_GEN_DONE  "DONE.TXT"
#define OM_GEN_SUFFIX 12u                 /* "/Gsss/Gnnnnn" */

typedef struct { uint32_t al[64]; uint32_t st[(300 + 4) / 4]; } om_gen_scr;

/* out = d + "/G<g / 100>/G<g>" (g >= 1); out holds strlen(d) + OM_GEN_SUFFIX + 1 */
void om_gen_dir(char *out, const char *d, uint32_t g);
/* out = d + "/G<sss>" of generation g's shard */
void om_gen_shard(char *out, const char *d, uint32_t g);
/* "G" + digits (3: a shard, 5: a generation) -> its number, else -1 */
int om_gen_num(const char *name, uint32_t digits);
/* the current set of d: out = its folder (d itself when *g = 0, no complete generation); 0 or an SD error (< 0). An
 * absent d is current at d with *g = 0. out holds strlen(d) + OM_GEN_SUFFIX + 10 (it is borrowed for "/DONE.TXT"). */
int om_gen_cur(const char *d, char *out, uint32_t *g, om_gen_scr *s);
/* the highest generation number of d present, complete or not (0 = none) */
int om_gen_max(const char *d, uint32_t *max, om_gen_scr *s);
#endif
