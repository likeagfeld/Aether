/* menu/launcher/lscreen.c -- see lscreen.h. Geometry: menu/src/layout.h (LD_LABEL .. LD_BRAND, OM_LD_*). BSD-3-Clause. */
#include <string.h>
#include "lgfx.h"
#include "lscreen.h"

#define LABEL_BASE 55
#define TITLE_BASE 82
#define TITLE_LH   20
#define META_MID   116                  /* small face: base = mid + 4 */
#define META_LH    13
#define LINE_MID   148
#define LINE_LH    11
#define HINT_MID   194

static void split(const char *t, char *a, char *b, uint32_t cap)
{   /* the longest first line that fits on a word boundary; the rest on the second */
    uint32_t n = (uint32_t)strlen(t), cut = n, i;
    a[0] = b[0] = 0;
    if (n >= cap) n = cap - 1;
    memcpy(a, t, n);
    a[n] = 0;
    if (om_lg_width(OM_LGF_HERO, a) <= OM_LS_W) return;
    for (i = n; i > 0; i--)
        if (t[i] == ' ') {
            memcpy(a, t, i);
            a[i] = 0;
            if (om_lg_width(OM_LGF_HERO, a) <= OM_LS_W) { cut = i; break; }
        }
    if (cut == n) { memcpy(a, t, n); a[n] = 0; return; }   /* one long word: cut with a fade */
    strncpy(b, t + cut + 1, cap - 1);
    b[cap - 1] = 0;
}

void om_ls_title(const char *title)
{
    char a[64], b[64];
    if (!om_lg_on) return;
    om_lg_clear(OM_LS_X0, 64, OM_LS_X1, 107);
    split(title ? title : "", a, b, sizeof a);
    om_lg_text(OM_LGF_HERO, OM_LS_X0, TITLE_BASE, OM_LS_W, a, om_lg_c.txt, 0, OM_LG_FADE);
    if (b[0]) om_lg_text(OM_LGF_HERO, OM_LS_X0, TITLE_BASE + TITLE_LH, OM_LS_W, b, om_lg_c.txt, 0, OM_LG_FADE);
}

void om_ls_page(const char *label, const char *title, int cover_dim)
{
    if (!om_lg_on) return;
    om_lg_clear(0, 0, 319, 223);
    om_lg_tracked(OM_LS_X0, LABEL_BASE, label, om_lg_c.acc2);
    om_lg_fill(OM_LS_X0, 58, 46, 59, om_lg_c.acc);
    om_ls_title(title);
    om_lg_cover(cover_dim, 0);
    om_lg_brand();
}

void om_ls_meta(int line, const char *s, uint16_t c)
{
    int mid = META_MID + line * META_LH;
    if (!om_lg_on) return;
    om_lg_clear(OM_LS_X0, mid - 6, OM_LS_X1, mid + 6);
    if (s && s[0]) om_lg_text(OM_LGF_SMALL, OM_LS_X0, mid + 4, OM_LS_W, s, c, 0, OM_LG_FADE);
}

void om_ls_line(int i, const char *s, uint16_t c)
{
    int mid = LINE_MID + i * LINE_LH;
    if (!om_lg_on || i < 0 || i >= OM_LS_LINES) return;
    om_lg_clear(OM_LS_X0, mid - 5, OM_LS_X1, mid + 5);
    if (s && s[0]) om_lg_text(OM_LGF_SMALL, OM_LS_X0, mid + 4, OM_LS_W, s, c, 0, OM_LG_FADE);
}

void om_ls_bar(uint32_t num, uint32_t den)
{   /* LD_BAR: a track and its fill in the accent */
    int x1 = 166, xf;
    if (!om_lg_on) return;
    om_lg_clear(OM_LS_X0, 182, x1, 186);
    if (!den) return;
    if (num > den) num = den;
    xf = OM_LS_X0 + (int)((uint32_t)(x1 - OM_LS_X0) * num / den);
    om_lg_fill(OM_LS_X0, 183, x1, 185, om_lg_mix(om_lg_under(OM_LS_X0, 182, x1, 186), 0xFFFFu, 40));
    if (xf > OM_LS_X0) om_lg_fill(OM_LS_X0, 183, xf, 185, om_lg_c.acc);
    om_lg_fill(xf - 4 < OM_LS_X0 ? OM_LS_X0 : xf - 4, 182, xf, 186, om_lg_c.acc2);
}

static int badge(int x, int base, char key)
{   /* an 11 x 11 disc in the accent, its letter in the text color on it (the letter's edges blend into the disc) */
    static const uint8_t half[11] = { 2, 4, 5, 5, 5, 5, 5, 5, 5, 4, 2 };
    char one[2] = { key, 0 };
    int r, y0 = base - 9, w;
    for (r = 0; r < 11; r++) om_lg_fill(x + 5 - half[r], y0 + r, x + 5 + half[r], y0 + r, om_lg_c.acc);
    w = om_lg_width(OM_LGF_SMALL, one);
    om_lg_text(OM_LGF_SMALL, x + 6 - (w + 1) / 2, base, 0, one, om_lg_c.txt, om_lg_c.acc, 0);
    return 11;
}

void om_ls_hints(int x, int base, int x1, const char *s, uint16_t c)
{
    char part[64];
    if (!om_lg_on || !s) return;
    while (*s && x < x1) {
        const char *e = strstr(s, "   ");
        uint32_t n = e ? (uint32_t)(e - s) : (uint32_t)strlen(s);
        const char *t = part;
        if (n >= sizeof part) n = sizeof part - 1;
        memcpy(part, s, n);
        part[n] = 0;
        if (n > 3 && part[1] == ':' && part[2] == ' ' && strchr("ABCXYZLR", part[0])) {
            x += badge(x, base, part[0]) + 3;
            t = part + 3;
        }
        if (x < x1) x += om_lg_text(OM_LGF_SMALL, x, base, x1 - x, t, c, 0, OM_LG_FADE);
        if (!e) break;
        for (s = e; *s == ' '; s++) ;
        x += 12;
    }
}

void om_ls_status(const char *s, uint16_t c)
{
    if (!om_lg_on) return;
    om_lg_clear(OM_LS_X0, HINT_MID - 6, OM_LS_X1, HINT_MID + 5);
    if (s && s[0]) om_lg_text(OM_LGF_SMALL, OM_LS_X0, HINT_MID + 4, OM_LS_W, s, c, 0, OM_LG_FADE);
}
