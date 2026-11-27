/* menu/src/ini.c -- see ini.h. BSD-3-Clause. */
#include <string.h>
#include "ini.h"
#include "om_fmt.h"

int om_text_line(const char *text, uint32_t len, uint32_t *pos, char out[OM_INI_LINE + 2])
{
    uint32_t p = *pos, n = 0;
    if (p >= len)
        return 0;
    while (p < len && text[p] != '\n') {
        if (!text[p])                            /* a NUL mid-line would silently truncate every
                                                   * downstream C-string op (trim/strchr/fn's key+value); reject it
                                                   * the same way a missing '=' is rejected, not copied through */
            return OM_E_FIELD;
        if (n > OM_INI_LINE)                     /* 256 bytes may be held so a trailing CR still fits */
            return OM_E_FIELD;
        out[n++] = text[p++];
    }
    if (n && out[n - 1] == '\r')
        n--;
    if (n > OM_INI_LINE)
        return OM_E_FIELD;
    out[n] = 0;
    *pos = p < len ? p + 1 : p;
    return 1;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    return s;
}

int om_ini_parse(const char *text, uint32_t len, om_ini_fn fn, void *ctx)
{
    char line[OM_INI_LINE + 2], sec[OM_INI_LINE + 2];
    uint32_t pos = 0;
    int r;
    sec[0] = 0;
    while ((r = om_text_line(text, len, &pos, line)) > 0) {
        char *s = trim(line), *eq;
        if (!*s || *s == '#')
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close)
                return OM_E_FIELD;
            *close = 0;
            strcpy(sec, trim(s + 1));
            continue;
        }
        if (!(eq = strchr(s, '=')))
            return OM_E_FIELD;
        *eq = 0;
        if ((r = fn(ctx, sec, trim(s), trim(eq + 1))) != 0)
            return r;
    }
    return r < 0 ? r : OM_OK;
}
