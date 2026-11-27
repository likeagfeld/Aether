/* menu/host-tests/test_pabout.c -- the About popup (menu/pop/pabout.c): its layout inside the popups' panel (the logo's
 * tile in the header band, the lines beside it and the credits list clear of each other and of the footer, every fixed
 * line fitting its box at its face), the credits wrapped to the list (every line fits, nothing lost, none past the
 * table), the scroll's bounds, and the credits against CREDITS.md row by row. BSD-3-Clause. */
#include <ctype.h>
#include "th.h"
#include "pabout.h"
#include "pgeom.h"
#include "uifont.h"
#include "version.h"

static int wfn(int kind, const char *s, int len)
{
    char b[256];
    if (len > 255) len = 255;
    memcpy(b, s, (size_t)len);
    b[len] = 0;
    return om_uf_width(kind == OM_PA_NAME ? &om_face_label : &om_face_small, b);
}

static void squeeze(const char *s, int n, char *o)
{   /* without its spaces: a wrapped text and its source compare equal */
    int i, k = (int)strlen(o);
    for (i = 0; i < n; i++)
        if (s[i] != ' ') o[k++] = s[i];
    o[k] = 0;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* CREDITS.md: the creators' line, the one heading, and every table row matched by exactly one credit, in order */
static void credits_md(void)
{
    uint32_t len;
    char *md = (char *)th_load("../../CREDITS.md", &len), *line, *next, want[512];
    int rows = 0, header = 0;
    md[len] = 0;
    CHECK(strstr(md, OM_PA_BY) != NULL);
    CHECK(strstr(md, OM_PA_LINK) != NULL);
    {
        char h[64], *p;
        strcpy(h, "## ");
        strcat(h, OM_PA_HEADING);
        for (p = h + 4; *p; p++) *p = (char)tolower((unsigned char)*p);   /* "## Credited for ..." */
        CHECK(strstr(md, h) != NULL);
    }
    for (line = md; line && *line; line = next) {
        char *cell[3], *p;
        int k;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (line[0] != '|') continue;
        if (strstr(line, "---")) continue;                          /* |---|---|---| */
        if (!header) { header = 1; continue; }                     /* | Who | Project | License | */
        for (k = 0, p = line + 1; k < 3 && p; k++) {
            char *bar = strchr(p, '|');
            if (bar) *bar = 0;
            cell[k] = trim(p);
            p = bar ? bar + 1 : NULL;
        }
        CHECK_INT(k, 3);
        if (k != 3) continue;
        snprintf(want, sizeof want, "%s | %s", cell[1], cell[2]);
        CHECK(rows < om_pa_ncredits);
        if (rows >= om_pa_ncredits) break;
        if (strcmp(om_pa_credits[rows].who, cell[0]) || strcmp(om_pa_credits[rows].what, want))
            printf("credit %d differs from CREDITS.md:\n  %s / %s\n  %s / %s\n", rows, om_pa_credits[rows].who,
                   om_pa_credits[rows].what, cell[0], want);
        CHECK_STR(om_pa_credits[rows].who, cell[0]);
        CHECK_STR(om_pa_credits[rows].what, want);
        rows++;
    }
    CHECK_INT(rows, om_pa_ncredits);
    free(md);
}

static void layout(void)
{
    const om_pbox *p = &om_pd_box[PB_PANEL], *h = &om_pd_box[PB_HDR], *f = &om_pd_box[PB_FOOT];
    int tw = OM_PA_TEXT_X1 - OM_PA_TEXT_X + 1, lw = OM_PA_LIST_X1 - OM_PA_LIST_X0 + 1;
    int ls = om_face_label.asc, ld = om_face_label.desc, ss = om_face_small.asc, sd = om_face_small.desc;
    /* the tile in the header band, the text column right of it */
    CHECK(OM_PA_LOGO_W % 8 == 0);
    CHECK(OM_PA_LOGO_X > h->x0 && OM_PA_LOGO_Y > h->y0);
    CHECK(OM_PA_LOGO_Y + OM_PA_LOGO_H - 1 < OM_PA_HDR_Y1);
    CHECK(OM_PA_LOGO_X + OM_PA_LOGO_W + 4 <= OM_PA_TEXT_X);
    CHECK(OM_PA_TEXT_X1 < h->x1);
    /* the four lines one under another, inside the band */
    CHECK(OM_PA_BASE_LABEL - ss >= h->y0 + 1);
    CHECK(OM_PA_BASE_LABEL + sd < OM_PA_BASE_BY - ls);
    CHECK(OM_PA_BASE_BY + ld < OM_PA_BASE_LINK - ss);
    CHECK(OM_PA_BASE_LINK + 2 < OM_PA_BASE_VER - om_face_title.asc);   /* the link's underline */
    CHECK(OM_PA_BASE_VER + om_face_title.desc <= OM_PA_HDR_Y1);
    /* each fits its box */
    CHECK(om_uf_width_tracked(&om_face_small, "ABOUT", OM_PD_TRACK8) <= tw);
    CHECK(om_uf_width(&om_face_label, OM_PA_BY) <= tw);
    CHECK(om_uf_width(&om_face_small, OM_PA_LINK) <= tw);
    CHECK(om_uf_width(&om_face_title, OM_VERSION_TAG) <= tw);
    CHECK(om_uf_width(&om_face_title, OM_VERSION_TAG) <= 128);          /* its slot (pdraw.c about_open) */
    CHECK(om_uf_width(&om_face_label, OM_PA_BY) <= 192 && om_uf_width(&om_face_small, OM_PA_LINK) <= 192);
    CHECK(om_uf_width_tracked(&om_face_small, OM_PA_HEADING, OM_PD_TRACK8) <= lw);
    CHECK(lw <= 288);                                                  /* the rows' slots */
    printf("about: by %d, link %d of %d px; heading %d of %d px\n", om_uf_width(&om_face_label, OM_PA_BY),
           om_uf_width(&om_face_small, OM_PA_LINK), tw, om_uf_width_tracked(&om_face_small, OM_PA_HEADING, OM_PD_TRACK8),
           lw);
    /* the heading under the band, the list under the heading, the footer's rule under the list */
    CHECK(OM_PA_HEAD_Y > OM_PA_HDR_Y1 + 1);
    CHECK(OM_PA_HEAD_Y + 1 + ss + sd <= OM_PA_LIST_Y0);
    CHECK(OM_PA_ROW_H >= om_uf_height(&om_face_small) && OM_PA_ROW_H >= om_uf_height(&om_face_label));
    CHECK(OM_PA_LIST_Y0 + (int)OM_PA_ROWS * OM_PA_ROW_H - 1 < f->y0 - 1);
    CHECK(OM_PA_LIST_X0 > p->x0 && OM_PA_THUMB_X > OM_PA_LIST_X1 && OM_PA_THUMB_X + 1 < p->x1);
    /* the footer's hints fit */
    CHECK(om_pd_footer_w(&om_pd_footers[PF_ABOUT]) <= f->x1 - f->x0 + 1);
}

static void model(void)
{
    static om_pa a;
    int lw = OM_PA_LIST_X1 - OM_PA_LIST_X0 + 1, i;
    uint16_t c, k, j, last;
    om_pa_open(&a, lw, OM_PA_ROWS, wfn);
    printf("about: %u credits, %u lines, %u shown\n", om_pa_ncredits, a.n, a.rows);
    CHECK(a.n > a.rows);
    CHECK(a.n < OM_PA_LINES_MAX);                                      /* none cut off by the table */
    CHECK_INT(a.top, 0);
    CHECK_INT(a.rows, OM_PA_ROWS);
    for (i = 0; i < a.n; i++) {
        CHECK(a.line[i].len > 0);
        CHECK(a.line[i].s[0] != ' ');
        CHECK(a.line[i].s[a.line[i].len - 1] != '|');                  /* the licence's bar starts its line */
        if (wfn(a.line[i].kind, a.line[i].s, a.line[i].len) > lw)
            printf("line %d too wide: %.*s\n", i, a.line[i].len, a.line[i].s);
        CHECK(wfn(a.line[i].kind, a.line[i].s, a.line[i].len) <= lw);
    }
    /* credit by credit: its name lines, then its project lines, together the whole text */
    for (c = 0, k = 0; c < om_pa_ncredits; c++) {
        for (j = 0; j < 2; j++) {
            const char *src = j ? om_pa_credits[c].what : om_pa_credits[c].who;
            char got[512] = "", want[512] = "";
            CHECK(k < a.n && a.line[k].kind == (j ? OM_PA_WHAT : OM_PA_NAME));
            while (k < a.n && a.line[k].kind == (j ? OM_PA_WHAT : OM_PA_NAME) && a.line[k].s >= src &&
                   a.line[k].s < src + strlen(src)) {
                squeeze(a.line[k].s, a.line[k].len, got);
                k++;
            }
            squeeze(src, (int)strlen(src), want);
            CHECK_STR(got, want);
        }
    }
    CHECK_INT(k, a.n);
    /* the scroll */
    last = om_pa_max_top(&a);
    CHECK_INT(last, a.n - a.rows);
    CHECK_INT(om_pa_key(&a, OM_PA_K_U), 0);                            /* at the top: stays */
    CHECK_INT(om_pa_key(&a, OM_PA_K_U | OM_PA_K_REP), 0);
    CHECK_INT(om_pa_key(&a, OM_PA_K_L), 0);
    CHECK_INT(a.top, 0);
    CHECK_INT(om_pa_key(&a, OM_PA_K_D), OM_PA_MOVED);
    CHECK_INT(a.top, 1);
    CHECK_INT(om_pa_key(&a, OM_PA_K_U), OM_PA_MOVED);
    CHECK_INT(a.top, 0);
    CHECK_INT(om_pa_key(&a, OM_PA_K_R), OM_PA_MOVED);                  /* a page */
    CHECK_INT(a.top, a.rows < last ? a.rows : last);
    CHECK_INT(om_pa_key(&a, OM_PA_K_L), OM_PA_MOVED);
    CHECK_INT(a.top, 0);
    for (i = 0; i < 200; i++) om_pa_key(&a, OM_PA_K_D | OM_PA_K_REP);  /* held: stops at the last page */
    CHECK_INT(a.top, last);
    CHECK_INT(om_pa_key(&a, OM_PA_K_D), 0);
    CHECK_INT(om_pa_key(&a, OM_PA_K_R), 0);
    CHECK_INT(a.top, last);
    for (i = 0; i < 20; i++) om_pa_key(&a, OM_PA_K_L);
    CHECK_INT(a.top, 0);
    CHECK_INT(om_pa_key(&a, OM_PA_K_R), OM_PA_MOVED);
    CHECK_INT(om_pa_key(&a, OM_PA_K_B), OM_PA_CLOSE);                  /* B closes, from anywhere */
    CHECK_INT(om_pa_key(&a, 0), 0);
    /* a list shorter than its rows does not scroll */
    {
        static om_pa b;
        om_pa_open(&b, lw, OM_PA_LINES_MAX, wfn);
        CHECK_INT(om_pa_max_top(&b), 0);
        CHECK_INT(om_pa_key(&b, OM_PA_K_D), 0);
        CHECK_INT(om_pa_key(&b, OM_PA_K_R), 0);
        CHECK_INT(b.top, 0);
    }
    /* a narrow list wraps more and still loses nothing */
    {
        static om_pa n;
        om_pa_open(&n, 120, OM_PA_ROWS, wfn);
        CHECK(n.n > a.n);
        for (i = 0; i < n.n; i++) CHECK(wfn(n.line[i].kind, n.line[i].s, n.line[i].len) <= 120);
    }
}

int main(void)
{
    layout();
    model();
    credits_md();
    return th_done("test_pabout");
}
