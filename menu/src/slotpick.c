/* menu/src/slotpick.c -- see slotpick.h. BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"
#include "overlay.h"
#include "slotpick.h"

int om_slot_key(uint8_t *slot, uint16_t k)
{
    uint8_t s = *slot <= 9 ? *slot : 0;
    if (k & (OM_K_A | OM_K_START)) return OM_SP_LAUNCH;
    if (k & OM_K_B) return OM_SP_CANCEL;
    if (k & OM_K_RIGHT) s = (uint8_t)(s == 9 ? 0 : s + 1);
    else if (k & OM_K_LEFT) s = (uint8_t)(s == 0 ? 9 : s - 1);
    *slot = s;
    return 0;
}

int om_active_parse(const char *t, uint32_t n, char gid[OM_GID_MAX], uint8_t *slot)
{
    uint32_t i = 0, g = 0;
    gid[0] = 0;
    while (i < n && t[i] != ' ' && t[i] != '\n' && g + 1 < OM_GID_MAX) gid[g++] = t[i++];
    gid[g] = 0;
    if (i + 2 > n || t[i] != ' ' || t[i + 1] < '0' || t[i + 1] > '9' || (i + 2 < n && t[i + 2] != ' ' &&
        t[i + 2] != '\n') || om_gameid_check(gid)) {
        gid[0] = 0;
        return OM_E_FIELD;
    }
    *slot = (uint8_t)(t[i + 1] - '0');
    return OM_OK;
}

uint8_t om_slot_first(const char *gid, const char *agid, uint8_t aslot)
{
    return gid[0] && !strcmp(gid, agid) && aslot <= 9 ? aslot : 0;
}

static char *cat(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

void om_slot_line(char *out, uint32_t cap, uint8_t slot, int saves, int in_console)
{
    char t[48], *o = cat(t, "SLOT ");
    *o++ = (char)('0' + (slot <= 9 ? slot : 0));
    o = cat(o, ": ");
    if (saves < 0) o = cat(o, "CARD UNREADABLE");
    else if (!saves) o = cat(o, "EMPTY");
    else {
        char d[4];
        int k = 0, v = saves > 255 ? 255 : saves;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (k) *o++ = d[--k];
        o = cat(o, saves == 1 ? " SAVE" : " SAVES");
    }
    if (in_console) cat(o, ", IN THE CONSOLE");
    if (!cap) return;
    strncpy(out, t, cap - 1);
    out[cap - 1] = 0;
}

void om_saves_held_line(char *out, uint32_t cap, uint8_t slot)
{
    char t[32];
    strcpy(t, "Saves in console: slot 0");
    t[23] = (char)('0' + (slot <= 9 ? slot : 0));
    if (!cap) return;
    strncpy(out, t, cap - 1);
    out[cap - 1] = 0;
}
