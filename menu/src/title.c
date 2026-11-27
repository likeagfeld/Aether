/* menu/src/title.c -- see title.h. Output is ASCII only: the v1 font is ASCII (spec §5.3). BSD-3-Clause. */
#include <string.h>
#include "title.h"

static uint32_t put(char *out, uint32_t o, uint32_t cap, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && s[i] && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        out[o++] = c >= 0x80 ? '?' : c < 0x20 ? ' ' : (char)c;
    }
    out[o] = 0;
    return o;
}

static uint32_t put_u(char *out, uint32_t o, uint32_t cap, uint32_t v, int width)
{
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < width) t[n++] = '0';
    while (n && o + 1 < cap) out[o++] = t[--n];
    out[o] = 0;
    return o;
}

const char *om_entry_raw(const om_libidx *l, const om_lib_entry *e, uint32_t *len)
{
    const char *p = e->path, *b = strrchr(p, '/'), *s;
    if (b && l && e->folder < l->nfolders) {
        om_lib_folder f;
        uint32_t dl = (uint32_t)(b - p);
        om_libidx_folder(l, e->folder, &f);
        if (strlen(f.path) != dl || memcmp(f.path, p, dl)) {                            /* 3 its single-game folder */
            for (s = b; s > p && s[-1] != '/'; s--) ;
            *len = (uint32_t)(b - s);
            return s;
        }
    }
    s = b ? b + 1 : p;                                                                  /* 4 its exact file name */
    *len = (uint32_t)strlen(s);
    return s;
}

const char *om_entry_name(const om_libidx *l, const om_lib_entry *e, const om_game *g, uint32_t *len)
{
    const char *s;
    if ((e->flags & OM_LIB_TITLE) && e->title && e->title[0]) s = e->title;            /* 1 LIBRARY.IDX override */
    else if (g && (g->user & OM_GU_TITLE) && g->title_en && g->title_en[0]) s = g->title_en;   /* 2 USEREDIT.INI */
    else return om_entry_raw(l, e, len);
    *len = (uint32_t)strlen(s);
    return s;
}

void om_title_raw(const om_libidx *l, const om_lib_entry *e, char *out, uint32_t cap)
{
    uint32_t n;
    const char *s = om_entry_raw(l, e, &n);
    out[0] = 0;
    put(out, 0, cap, s, n);
}

void om_title(const om_libidx *l, const om_lib_entry *e, const om_game *g, char *out, uint32_t cap)
{
    uint32_t n, o;
    const char *s = om_entry_name(l, e, g, &n);
    out[0] = 0;
    o = put(out, 0, cap, s, n);
    if (e->disc_total > 1 && (s == e->title || (g && s == g->title_en))) {   /* a user title: the discs share it */
        o = put(out, o, cap, " (Disc ", 7);
        o = put_u(out, o, cap, e->disc, 0);
        put(out, o, cap, ")", 1);
    }
}

void om_region_letters(uint8_t f, char out[6])
{
    static const char L[5] = { 'J', 'T', 'U', 'E', 'B' };
    int i, o = 0;
    for (i = 0; i < 5; i++)
        if (f & (1u << i)) out[o++] = L[i];
    out[o] = 0;
}

void om_details(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char out[3][23])
{
    char rl[6];
    uint32_t o;
    uint8_t region = (g && g->region) ? g->region : e->area ? e->area : ip ? ip->area_flags : 0;
    uint32_t date = (g && g->date) ? g->date : 0;
    put(out[0], 0, 23, genre && genre[0] ? genre : "Unknown genre", 22);
    om_region_letters(region, rl);
    o = put(out[1], 0, 23, "Region ", 7);
    o = put(out[1], o, 23, rl[0] ? rl : "?", 5);
    if (g && g->players) {
        o = put(out[1], o, 23, "  ", 2);
        o = put_u(out[1], o, 23, g->players, 0);
        put(out[1], o, 23, "P", 1);
    }
    if (!date && ip && strlen(ip->date) == 8) {
        uint32_t i;
        for (i = 0; i < 8 && ip->date[i] >= '0' && ip->date[i] <= '9'; i++) date = date * 10 + (uint32_t)(ip->date[i] - '0');
        if (i != 8) date = 0;
    }
    o = put(out[2], 0, 23, "Disc ", 5);
    o = put_u(out[2], o, 23, e->disc ? e->disc : 1, 0);
    o = put(out[2], o, 23, "/", 1);
    o = put_u(out[2], o, 23, e->disc_total ? e->disc_total : 1, 0);
    if (date) {
        o = put(out[2], o, 23, "  ", 2);
        o = put_u(out[2], o, 23, date / 10000, 4);
        o = put(out[2], o, 23, "-", 1);
        o = put_u(out[2], o, 23, date / 100 % 100, 2);
        o = put(out[2], o, 23, "-", 1);
        put_u(out[2], o, 23, date % 100, 2);
    }
}
