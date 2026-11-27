/* menu/src/fwver.c -- see fwver.h. BSD-3-Clause. */
#include "fwver.h"

static int dig(char c) { return c >= '0' && c <= '9'; }

int om_fw_parse(const char *s)
{
    int i, n;
    for (i = 0; s[i]; i++) {                               /* rule 1: a run of exactly three digits */
        if (!dig(s[i]) || (i && dig(s[i - 1]))) continue;
        for (n = 0; dig(s[i + n]); n++) ;
        if (n == 3) return (s[i] - '0') * 100 + (s[i + 1] - '0') * 10 + (s[i + 2] - '0');
    }
    for (i = 0; s[i]; i++)                                 /* rule 2: d.dd */
        if (dig(s[i]) && (!i || !dig(s[i - 1])) && s[i + 1] == '.' && dig(s[i + 2]) && dig(s[i + 3]) && !dig(s[i + 4]))
            return (s[i] - '0') * 100 + (s[i + 2] - '0') * 10 + (s[i + 3] - '0');
    return -1;
}

int om_fw_judge(int read_rc, const char *s, int min, int *ver)
{
    *ver = -1;
    if (read_rc < 0) return OM_FW_READ_FAIL;
    if ((*ver = om_fw_parse(s)) < 0) return OM_FW_UNKNOWN;
    return *ver < min ? OM_FW_OLD : OM_FW_OK;
}

const char *om_fw_word(int status)
{
    return status == OM_FW_OK ? "OK" : status == OM_FW_OLD ? "OLD" : status == OM_FW_UNKNOWN ? "UNREADABLE"
         : "READ FAILED";
}
