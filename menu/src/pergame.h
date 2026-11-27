/* menu/src/pergame.h -- PERGAME.INI, the remembered per-game choices (contract/SPEC.md §5.14): one [<game id>]
 * section per game (gameid.h), keys title / loader / saves / slot / cheats / igr, all optional (missing = the global
 * setting). Pure text transforms: put / del build the whole new file in the caller's buffer and never touch the old
 * text, so a failed call leaves nothing half-written (the SD replace itself is the caller's, Task 21 / Task 18).
 * The same rules are implemented for the PC by tools/omsat/pergame.py. BSD-3-Clause. */
#ifndef OM_PERGAME_H
#define OM_PERGAME_H
#include <stdint.h>
#include "gameid.h"

enum { OM_PG_DEFAULT = 0, OM_PG_ON = 1, OM_PG_OFF = 2 };
#define OM_PG_SLOT_ASK 0xFF
#define OM_PG_TITLE_MAX 40u
typedef struct {
    char id[OM_GID_MAX]; char title[OM_PG_TITLE_MAX + 1];
    uint8_t loader;               /* OM_LDR_* (settings.h); OM_LDR_AUTO = no choice */
    uint8_t saves, cheats, igr;   /* OM_PG_* */
    uint8_t slot;                 /* 0..9, OM_PG_SLOT_ASK */
} om_pergame;
#define OM_PERGAME_MAX_BYTES 16384u   /* Task 15 ruling carried to Task 18: two copies fit OM_LW_STAGE */
#define OM_PERGAME_MAX_SECTIONS 512u
/* Every call refuses text longer than OM_PERGAME_MAX_BYTES (OM_E_FULL) and a corrupt file (a line over 255 B or an
 * embedded NUL: OM_E_FIELD). Duplicate sections: get / put use the first, del drops every copy. */
int om_pergame_get(const char *text, uint32_t len, const char *id, om_pergame *out);   /* 1 found, 0 not, <0 error */
int om_pergame_count(const char *text, uint32_t len);                                    /* sections, <0 error */
/* Section n by position: 1 found; OM_PG_NTH_BAD when its header is not a game id (om_gameid_check: hand-edited, or
 * too long to be one; out is the empty default, never a truncated name); 0 past the end; < 0 an error. */
#define OM_PG_NTH_BAD 2
int om_pergame_nth(const char *text, uint32_t len, uint32_t n, om_pergame *out);
/* Changed section in place (a default value drops its line, a missing key goes after the section's last non-blank
 * line with the header's CRLF/LF), a new one appended; OM_E_FULL past cap, 16,384 B or 512 sections. */
int om_pergame_put(const char *old, uint32_t old_len, const om_pergame *g, char *out, uint32_t cap, uint32_t *n);
int om_pergame_del(const char *old, uint32_t old_len, const char *id, char *out, uint32_t cap, uint32_t *n);
void om_pergame_default(om_pergame *g, const char *id);
#endif
