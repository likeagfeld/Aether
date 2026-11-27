/* menu/src/ini.h -- contract/SPEC.md §5.5 text files: ASCII key=value lines, [section] headers, '#' comment lines,
 * CRLF or LF, at most 255 bytes per line (the line terminator is not counted). BSD-3-Clause. */
#ifndef OM_INI_H
#define OM_INI_H
#include <stdint.h>
#define OM_INI_LINE 255u
/* section is "" for the global section. A non-zero return from fn stops the walk and is returned. */
typedef int (*om_ini_fn)(void *ctx, const char *section, const char *key, const char *value);
int om_ini_parse(const char *text, uint32_t len, om_ini_fn fn, void *ctx);
/* Next line of text from *pos, without CR/LF, NUL-terminated in out. 1 = a line, 0 = end, OM_E_FIELD = longer than
 * OM_INI_LINE, or the line itself contains an embedded NUL byte (pos is not advanced either way). Shared with the
 * FAVS/RECENT reader (plist.c). */
int om_text_line(const char *text, uint32_t len, uint32_t *pos, char out[OM_INI_LINE + 2]);
#endif
