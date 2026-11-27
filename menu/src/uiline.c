/* menu/src/uiline.c -- see uiline.h. BSD-3-Clause. */
#include <string.h>
#include "libsort.h"
#include "title.h"
#include "uiline.h"

static void cat(char *out, uint32_t cap, const char *s)
{
    uint32_t n = (uint32_t)strlen(out);
    while (*s && n + 1 < cap)
        out[n++] = *s++;
    out[n] = 0;
}

static void cat_u(char *out, uint32_t cap, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) {
        char c[2];
        c[0] = t[--n]; c[1] = 0;
        cat(out, cap, c);
    }
}

static void part(char *out, uint32_t cap)
{
    if (out[0])
        cat(out, cap, OM_SEP_DOT);
}

static const char k_area_order[] = "JTUBKAEL";                 /* IP.BIN area-code order (ST-040-R4) */
static const char *const k_area_name[8] = { "Japan", "Asia", "USA", "Brazil", "Korea", "Asia PAL", "Europe",
                                            "Latin America" };

void om_area_codes(const om_lib_entry *e, const om_game *g, const om_sysid *ip, char out[9])
{   /* IP.BIN's area string when it is known (it has K, A and L); otherwise the GAMES.DB / LIBRARY.IDX region bits
     * J0 T1 U2 E3 B4, put in the same canonical order */
    static const char bit_letter[5] = { 'J', 'T', 'U', 'E', 'B' };
    uint8_t bits = (g && g->region) ? g->region : e->area ? e->area : ip ? ip->area_flags : 0;
    uint32_t n = 0, i, j;
    if (ip && ip->area[0]) {
        for (i = 0; i < 8; i++)
            for (j = 0; ip->area[j]; j++)
                if (ip->area[j] == k_area_order[i] || ip->area[j] == k_area_order[i] + 32) {
                    out[n++] = k_area_order[i];
                    break;
                }
    } else
        for (i = 0; i < 8; i++)
            for (j = 0; j < 5; j++)
                if (bit_letter[j] == k_area_order[i] && (bits >> j & 1u))
                    out[n++] = k_area_order[i];
    out[n] = 0;
}

char om_primary_area(const om_lib_entry *e, const om_game *g, const om_sysid *ip)
{   /* the region sort's group (libsort.c om_region_bits / om_region_primary); the IP.BIN's area string only when there
     * are no region bits at all, so a row's flag never changes when the game's IP.BIN is loaded on selection */
    uint8_t p = om_region_primary(om_region_bits(e, g));
    char codes[9];
    if (p < 5)
        return "JTUEB"[p];
    om_area_codes(e, g, ip, codes);
    return codes[0];
}

const char *om_area_name(char code)
{
    uint32_t i;
    for (i = 0; i < 8; i++)
        if (k_area_order[i] == code)
            return k_area_name[i];
    return "";
}

void om_meta_line(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char *out,
                  uint32_t cap)
{
    uint32_t year = 0, date = 0, i;
    char codes[9];
    if (!cap)
        return;
    out[0] = 0;
    if (genre && genre[0])
        cat(out, cap, genre);
    if (g && om_game_year(g))
        year = om_game_year(g);                                    /* what the year sort uses (libsort.c) */
    else if (ip && strlen(ip->date) == 8) {
        for (i = 0; i < 8 && ip->date[i] >= '0' && ip->date[i] <= '9'; i++)
            date = date * 10u + (uint32_t)(ip->date[i] - '0');
        if (i != 8)
            date = 0;
    }
    if (!year && date)
        year = date / 10000u;
    if (year) {
        part(out, cap);
        cat_u(out, cap, year);
    }
    if (g && g->players) {
        part(out, cap);
        cat_u(out, cap, g->players);
        cat(out, cap, g->players == 1 ? " player" : " players");
    }
    if (e->disc_total > 1) {
        part(out, cap);
        cat(out, cap, "Disc ");
        cat_u(out, cap, e->disc ? e->disc : 1u);
        cat(out, cap, "/");
        cat_u(out, cap, e->disc_total);
    }
    om_area_codes(e, g, ip, codes);
    for (i = 0; codes[i]; i++) {                                   /* every area in words (addendum 6) */
        part(out, cap);
        cat(out, cap, om_area_name(codes[i]));
    }
}

void om_crumb(const char *path, char *out, uint32_t cap)
{   /* the whole path, never abbreviated: the view marquees it when it is wider than its box (addendum 3) */
    const char *q = path;
    if (!cap)
        return;
    out[0] = 0;
    cat(out, cap, "Games");
    while (*q) {
        char comp[64];
        uint32_t n = 0;
        while (*q == '/') q++;
        if (!*q) break;
        while (q[n] && q[n] != '/') {
            if (n + 1 < sizeof comp) comp[n] = q[n];
            n++;
        }
        comp[n < sizeof comp ? n : sizeof comp - 1] = 0;
        cat(out, cap, OM_SEP_CRUMB);
        cat(out, cap, comp);
        q += n;
    }
}

void om_count_line(uint32_t games, uint32_t items, char *out, uint32_t cap)
{
    if (!cap)
        return;
    out[0] = 0;
    if (games) {
        cat_u(out, cap, games);
        cat(out, cap, games == 1 ? " game" : " games");
    } else if (items) {
        cat_u(out, cap, items);
        cat(out, cap, items == 1 ? " item" : " items");
    } else
        cat(out, cap, "empty");
}

const char *om_split_rc(const char *msg)
{
    const char *rc = 0, *p;
    for (p = msg; (p = strstr(p, " rc ")) != 0; p++)
        rc = p;                                        /* the last " rc " is om_msg_rc's suffix */
    return rc;
}

void om_load_meta(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char *l1,
                  uint32_t cap1, char *l2, uint32_t cap2)
{
    uint32_t year = 0, date = 0, i;
    char codes[9];
    const char *maker = g && g->maker && g->maker[0] ? g->maker : NULL;
    if (!cap1 || !cap2)
        return;
    l1[0] = l2[0] = 0;
    if (maker)
        cat(l1, cap1, maker);
    if (g && om_game_year(g))
        year = om_game_year(g);                                    /* what the year sort uses (libsort.c) */
    else if (ip && strlen(ip->date) == 8) {
        for (i = 0; i < 8 && ip->date[i] >= '0' && ip->date[i] <= '9'; i++)
            date = date * 10u + (uint32_t)(ip->date[i] - '0');
        if (i != 8)
            date = 0;
    }
    if (!year && date)
        year = date / 10000u;
    if (year) {
        part(l1, cap1);
        cat_u(l1, cap1, year);
    }
    if (genre && genre[0]) {
        part(l1, cap1);
        cat(l1, cap1, genre);
    }
    if (e->disc_total > 1) {
        cat(l2, cap2, "Disc ");
        cat_u(l2, cap2, e->disc ? e->disc : 1u);
        cat(l2, cap2, " of ");
        cat_u(l2, cap2, e->disc_total);
    }
    om_area_codes(e, g, ip, codes);
    for (i = 0; codes[i]; i++) {                                   /* every area in words (addendum 6) */
        part(l2, cap2);
        cat(l2, cap2, om_area_name(codes[i]));
    }
    if (g && g->players) {
        part(l2, cap2);
        cat_u(l2, cap2, g->players);
        cat(l2, cap2, g->players == 1 ? " player" : " players");
    }
}

int om_title_wrap(const om_face *f, const char *s, int box_w, char *l1, uint32_t cap1, char *l2, uint32_t cap2)
{
    uint32_t n = (uint32_t)strlen(s), j, cut = 0;
    if (!cap1 || !cap2)
        return 0;
    l1[0] = l2[0] = 0;
    if (om_uf_width(f, s) <= box_w || n + 1 > cap1) {
        cat(l1, cap1, s);
        return 1;
    }
    for (j = 1; j < n; j++)                                        /* the last space before which line 1 fits */
        if (s[j] == ' ') {
            memcpy(l1, s, j);
            l1[j] = 0;
            if (om_uf_width(f, l1) <= box_w) cut = j;
            else break;
        }
    if (!cut) {
        l1[0] = 0;
        cat(l1, cap1, s);
        return 1;
    }
    l1[cut] = 0;
    memcpy(l1, s, cut);
    while (s[cut] == ' ') cut++;
    cat(l2, cap2, s + cut);
    return 2;
}
