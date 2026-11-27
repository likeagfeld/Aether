/* menu/src/msgfmt.c -- see msgfmt.h. BSD-3-Clause. */
#include <string.h>
#include "msgfmt.h"

static char *itoa10(char *buf, int v)
{
    char t[12];
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) buf[i++] = '-';
    while (n) buf[i++] = t[--n];
    buf[i] = 0;
    return buf;
}

void om_msg_rc(char *out, uint32_t cap, const char *prefix, const char *reason, int rc)
{
    char num[12], suf[16];
    int pl, rl, sl, room, cap_i;
    if (cap == 0) return;
    cap_i = (int)cap;
    strcpy(suf, " rc ");
    strcat(suf, itoa10(num, rc));
    sl = (int)strlen(suf);
    if (sl > cap_i - 1) sl = cap_i - 1;          /* the rc suffix gets priority over everything else --
                                                   * it is the last thing ever cut (point 1) */
    pl = (int)strlen(prefix);
    if (pl > cap_i - 1 - sl) pl = cap_i - 1 - sl;  /* prefix tail yields to the suffix, never the reverse */
    room = cap_i - 1 - pl - sl;                  /* what's left for the reason text; >= 0 by construction */
    rl = (int)strlen(reason);
    if (rl > room) rl = room;
    memcpy(out, prefix, (size_t)pl);
    memcpy(out + pl, reason, (size_t)rl);
    memcpy(out + pl + rl, suf, (size_t)sl);
    out[pl + rl + sl] = 0;
}
