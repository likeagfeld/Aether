/* menu/pop/pdraw.c -- see pdraw.h. Drawn every frame the popup is open, in front of the menu (Z below the menu's
 * frontmost band, Z_RC); a text slot re-rasterises only when its string changes (uitext.h). BSD-3-Clause. */
#include <string.h>
#include "anim.h"
#include "art.h"
#include "chkai.h"
#include "marquee.h"
#include "omlayout.h"
#include "pdraw.h"
#include "uifont.h"
#include "uiline.h"
#include "uitext.h"
#include "v1.h"
#include "version.h"

#define B(i) (&om_pd_box[i])

/* Z: in front of every band the menu uses (uiview.h: Z_RC 143 is its frontmost) */
enum { Z_PCLIP = 141, Z_PBODY = 140, Z_PHDR = 139, Z_PRING = 138, Z_PBAR = 137, Z_PICON = 136, Z_PTXT = 135,
       Z_PMQ = 128 };

#define NROW   10
#define NSIDE  10
#define NFOOT  6
/* text slots */
static om_ut s_label, s_title, s_meter, s_tab[3], s_row[NROW], s_rval[NROW], s_side[NSIDE], s_foot[NFOOT],
    s_badge[NFOOT], s_msg, s_star, s_up, s_dn, s_big;
static uint32_t s_pool, s_mq_key, s_mq_t;
static int16_t s_mq_row;                    /* .bss (om_pd_open sets it): the image's .data never changes */
static om_style S;
#define ANROW  OM_PA_ROWS                   /* About: a list line's slot is its index mod ANROW (one raster a step) */
static om_ut s_aby, s_alink, s_ahead, s_arow[ANROW];
static uint32_t s_logo;                     /* About: the logo's LUT, its texture 32 B on */
#define LOGO_BYTES (OM_PA_LOGO_W * OM_PA_LOGO_H / 2u)
static int about_open(om_pop_state *g);

#define POOL0 OM_VRAM_SAVER
#define POOL1 OM_VRAM_VERTAG_L              /* the saver / boot range ends where the version tag's LUT starts */

static int alloc(om_ut *t, int cap, int h)
{   /* uitext.c om_ut_alloc's layout, from this range */
    uint32_t bytes;
    cap = (cap + 7) & ~7;
    bytes = 32u + (((uint32_t)cap / 2u * (uint32_t)h + 31u) & ~31u);
    if (cap >= 128) bytes += 96u;
    if (s_pool + bytes > POOL1) return OM_E_FULL;
    memset(t, 0, sizeof *t);
    t->lut = s_pool;
    t->blut = cap >= 128 ? s_pool + bytes - 96u : 0;
    t->tex = s_pool + 32u;
    s_pool += bytes;
    t->cap = (uint16_t)cap;
    t->h = (uint16_t)h;
    t->w8 = 8;
    return 0;
}

int om_pd_open(om_pop_state *g)
{
    int i, hs = om_uf_height(&om_face_small), ht = om_uf_height(&om_face_title), e = 0;
    S = *g->req.sty;
    s_pool = POOL0;
    s_mq_row = -1;
    if (g->kind == OM_POP_ABOUT) return about_open(g);
    e |= alloc(&s_label, 88, hs);
    e |= alloc(&s_title, 256, ht);
    e |= alloc(&s_meter, 128, hs);
    for (i = 0; i < 3; i++) e |= alloc(&s_tab[i], 56, hs);
    for (i = 0; i < NROW; i++) { e |= alloc(&s_row[i], 168, hs); e |= alloc(&s_rval[i], 48, hs); }
    for (i = 0; i < NSIDE; i++) e |= alloc(&s_side[i], 112, hs);
    for (i = 0; i < NFOOT; i++) { e |= alloc(&s_foot[i], 72, hs); e |= alloc(&s_badge[i], 16, 11); }
    e |= alloc(&s_msg, 288, hs);
    e |= alloc(&s_star, 16, om_icon_star.h);
    e |= alloc(&s_up, 8, hs);
    e |= alloc(&s_dn, 8, hs);
    e |= alloc(&s_big, 248, ht);                                 /* the name and code pages' line, title face */
    if (e) return OM_E_FULL;
    om_ut_icon(&s_star, &om_icon_star);
    return 0;
}

/* --- small helpers --------------------------------------------------------------------------------------------- */
static uint16_t mix(uint16_t a, uint16_t b, uint32_t t) { return om_ui_mix(a, b, t); }
static uint16_t hdr_bg(void) { return mix(S.ov_eff, S.acc, 70); }
static uint16_t bar_bg(void) { return mix(S.ov_eff, S.acc, 120); }
#define GREEN ((uint16_t)(0x8000u | (6u << 10) | (24u << 5) | 9u))   /* RGB555 (72, 196, 80) */

static void quad(int x0, int y0, int x1, int y1, uint16_t c, int z) { om_v1_quad(x0, y0, x1, y1, c | 0x8000u, 0, 0, z); }

/* rasterise (clipw > 0: cut with a fade at clipw px) and draw with the baseline at base; right: x is the right edge */
static void text(om_ut *t, int face, const char *s, int clipw, uint16_t fg, uint16_t bg, uint16_t vis, int x,
                 int base, int right)
{
    om_ut_text(t, face, s, clipw);
    om_ut_colors(t, fg, bg, vis);
    om_ut_draw_base(t, right ? x + 1 - t->w : x, base, 0, Z_PTXT);
}
static void tracked(om_ut *t, const char *s, uint16_t fg, uint16_t bg, uint16_t vis, int x, int base)
{
    om_ut_text_tracked(t, OM_FACE_SMALL, s, OM_PD_TRACK8);
    om_ut_colors(t, fg, bg, vis);
    om_ut_draw_base(t, x, base, 0, Z_PTXT);
}
static int sbase(int y) { return y + 1 + om_face_small.asc; }   /* a small-face line whose box starts at y */

static void put_u(char **o, uint32_t v)
{
    char d[12];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *(*o)++ = d[--k];
}

/* --- frame, header, footer ------------------------------------------------------------------------------------- */
static void ring(uint32_t frame, int clip_x0)
{   /* the Aether orbit, faint and tilted -17 degrees, its comet once round every 4 s; clipped to the header below
     * the meter row and right of the title (clip_x0), so it never crosses text */
    static const int16_t c16[24] = { 1024, 989, 887, 724, 512, 265, 0, -265, -512, -724, -887, -989,
                                     -1024, -989, -887, -724, -512, -265, 0, 265, 512, 724, 887, 989 };
    const om_pbox *h = B(PB_HDR);
    int cx = h->x1 - 70, cy = h->y0 + 27, rx = 88, ry = 16, i, px = 0, py = 0;
    uint16_t c = mix(hdr_bg(), S.acc2, 70);
    om_v1_clip(clip_x0, B(PB_METER_T)->y1 + 2, h->x1, h->y1 - 1, Z_PCLIP);
    for (i = 0; i <= 24; i++) {
        int k = i % 24, cs = c16[k], sn = c16[(k + 18) % 24];      /* sin = cos(t - 90) */
        int ex = rx * cs / 1024, ey = ry * sn / 1024;
        int x = cx + (ex * 979 - ey * (-300)) / 1024, y = cy + (ex * (-300) + ey * 979) / 1024;   /* rotate */
        if (i) om_v1_line(px, py, x, y, c | 0x8000u, OM_V1_CLIP, Z_PRING);
        px = x; py = y;
    }
    {
        uint32_t t = frame % 240u, k = t * 24u / 240u, j;
        for (j = 0; j < 3; j++) {
            uint32_t q = (k + 24u - j) % 24u;
            int cs = c16[q], sn = c16[(q + 18u) % 24u], ex = rx * cs / 1024, ey = ry * sn / 1024;
            int x = cx + (ex * 979 + ey * 300) / 1024, y = cy + (ey * 979 - ex * 300) / 1024;
            uint16_t col = mix(hdr_bg(), S.txt, j == 0 ? 255u : j == 1 ? 140u : 70u);
            om_v1_quad(x - (j == 0), y - (j == 0), x, y, col | 0x8000u, 0, OM_V1_CLIP, Z_PRING - 1);
        }
    }
}

static void panel_to(int hdr_y1)
{   /* the frame, the body, the header band down to hdr_y1 with its accent line, the footer's rule */
    const om_pbox *p = B(PB_PANEL), *h0 = B(PB_HDR);
    om_pbox hb = { h0->x0, h0->y0, h0->x1, (int16_t)hdr_y1 };
    const om_pbox *h = &hb;
    uint16_t fr = mix(S.ov_eff, S.txt, 70);
    quad(p->x0 + 1, p->y0 + 1, p->x1 - 1, p->y1 - 1, S.ov_eff, Z_PBODY);
    quad(p->x0, p->y0, p->x1, p->y0, fr, Z_PBODY);
    quad(p->x0, p->y1, p->x1, p->y1, fr, Z_PBODY);
    quad(p->x0, p->y0, p->x0, p->y1, fr, Z_PBODY);
    quad(p->x1, p->y0, p->x1, p->y1, fr, Z_PBODY);
    quad(h->x0, h->y0, h->x1, h->y1, hdr_bg(), Z_PHDR);
    quad(h->x0, h->y1 + 1, h->x1, h->y1 + 1, mix(S.ov_eff, S.acc2, 90), Z_PHDR);
    quad(B(PB_FOOT)->x0, B(PB_FOOT)->y0 - 1, B(PB_FOOT)->x1, B(PB_FOOT)->y0 - 1, mix(S.ov_eff, S.txt, 40), Z_PHDR);
}
static void panel(void) { panel_to(B(PB_HDR)->y1); }

static void header(const om_pop_state *g, const char *label, const char *meter, uint32_t used, uint32_t total,
                   uint16_t fill, uint16_t vis, uint32_t frame)
{
    const om_pbox *cb = B(PB_COVER), *lb = B(PB_LABEL), *tb = B(PB_TITLE), *mt = B(PB_METER_T), *mb = B(PB_METER_B);
    uint16_t bg = hdr_bg();
    uint32_t tex;
    int w, h, tall, lx;
    if (g->kind != OM_POP_SAVES_ALL && !om_art_loading() && om_art_hero(&tex, &w, &h, &tall))
        om_v1_scaled(cb->x0, tall ? cb->y0 : cb->y0 + 4, tall ? cb->x0 + 20 : cb->x1, tall ? cb->y1 : cb->y0 + 27, tex, w, h,
                     0, 0, 0, Z_PICON);                   /* a tall cover 21x32, square art 24x24 */
    else if (g->kind != OM_POP_SAVES_ALL) {                       /* no cover: the panel's NO ART card, smaller */
        quad(cb->x0, cb->y0, cb->x1, cb->y1, mix(bg, S.txt, 60), Z_PICON);
        quad(cb->x0 + 2, cb->y0 + 2, cb->x1 - 2, cb->y1 - 2, mix(bg, 0x8000u, 128), Z_PICON - 1);
    }
    tracked(&s_label, label, S.acc2, bg, vis, lx = lb->x0, sbase(lb->y0));
    text(&s_title, OM_FACE_TITLE, g->req.title[0] ? g->req.title : "Every game", tb->x1 - tb->x0 + 1, S.txt, bg, vis,
         tb->x0, tb->y0 + 1 + om_face_title.asc, 0);
    ring(frame, tb->x0 + s_title.w + 8);
    if (meter) {
        int x0 = lb->x0 + s_label.w + 6 > mt->x0 ? lb->x0 + s_label.w + 6 : mt->x0;   /* never under the label */
        text(&s_meter, OM_FACE_SMALL, meter, mt->x1 - x0 + 1, S.txt, bg, vis, mt->x1, sbase(mt->y0), 1);
        if (total) {
            uint32_t fw = (uint32_t)(mb->x1 - mb->x0 + 1) * (used > total ? total : used) / total;
            quad(mb->x0, mb->y0, mb->x1, mb->y1, mix(bg, 0x8000u, 110), Z_PBAR);
            if (fw) quad(mb->x0, mb->y0, mb->x0 + (int)fw - 1, mb->y1, fill, Z_PBAR - 1);
        }
    }
    (void)lx;
}


static void footer(int which, uint16_t vis)
{   /* badges and their labels from the left; the last one right-aligned */
    const om_pbox *f = B(PB_FOOT);
    int x = f->x0 + 4, i, k = 0, base = f->y0 + 1 + om_face_small.asc, rx;
    uint16_t bg = S.ov_eff;
    const om_pd_footer *ft = &om_pd_footers[which];
    int nl = ft->n;
    for (i = 0; i < nl + 1 && k < NFOOT; i++, k++) {
        const om_pd_hint *h = i < nl ? &ft->left[i] : &ft->right;
        om_ut *b = &s_badge[k], *l = &s_foot[k];
        if (!h) break;
        om_ut_text(l, OM_FACE_SMALL, h->label, 0);
        if (h->ic) {
            if (b->key != (uint32_t)(uintptr_t)h->ic) { b->h = h->ic->h; om_ut_icon(b, h->ic); b->key = (uint32_t)(uintptr_t)h->ic; }
            om_ut_badge_colors(b, bg, mix(bg, S.txt, 210), bg);
        }
        if (i == nl) {                                            /* the right one */
            rx = f->x1 - 4 - l->w;
            if (h->ic) rx -= b->w + 3;
            x = rx;
        }
        if (h->ic) { om_ut_draw(b, x, f->y0 + 2, 0, Z_PTXT); x += b->w + 3; }
        om_ut_colors(l, S.row, bg, vis);
        om_ut_draw_base(l, x, base, 0, Z_PTXT);
        x += l->w + 9;
    }
}

static void message(const char *m, uint16_t fg, uint16_t vis)
{   /* in place of the hints; a marquee when wider than the footer */
    const om_pbox *f = B(PB_FOOT);
    int bw = f->x1 - f->x0 - 7, base = f->y0 + 1 + om_face_small.asc;
    om_ut_text(&s_msg, OM_FACE_SMALL, m, -bw);
    om_ut_colors(&s_msg, fg, S.ov_eff, vis);
    if (s_msg.key != s_mq_key) { s_mq_key = s_msg.key; s_mq_t = 0; }
    if (s_msg.full_w > bw && s_msg.w == s_msg.full_w) {
        int32_t per = om_marquee_period(s_msg.full_w, bw), off = om_marquee_off(s_mq_t++, s_msg.full_w, bw, 5994u);
        om_ut_marquee(&s_msg, f->x0 + 4, f->x0 + 4, f->x0 + 4 + bw, f->y0, f->y1, base, off, per, Z_PMQ);
    } else
        om_ut_draw_base(&s_msg, f->x0 + 4, base, 0, Z_PTXT);
}

/* a list row's bar (the Launch Options panel's: a lit left edge) */
static uint16_t row_bar(int x0, int x1, int y, int sel)
{
    if (!sel) return S.ov_eff;
    quad(x0, y, x1, y + OM_PD_ROW_H - 1, bar_bg(), Z_PBAR);
    quad(x0, y, x0 + 1, y + OM_PD_ROW_H - 1, S.acc2, Z_PBAR - 1);
    return bar_bg();
}

/* a row's name: the selected one scrolls when wider than its box, the others fade at its edge */
static void row_name(int i, const char *s, int x, int x1, int base, int sel, uint16_t fg, uint16_t bg, uint16_t vis)
{
    om_ut *t = &s_row[i];
    int bw = x1 - x + 1;
    om_ut_text(t, OM_FACE_SMALL, s, sel ? -bw : bw);
    om_ut_colors(t, fg, bg, vis);
    if (sel && t->full_w > bw && t->w == t->full_w) {
        static uint32_t k0;
        if (s_mq_row != i || k0 != t->key) { s_mq_row = (int16_t)i; k0 = t->key; s_mq_t = 0; }
        om_ut_marquee(t, x, x, x1, base - om_face_small.asc, base + om_face_small.desc, base,
                      om_marquee_off(s_mq_t++, t->full_w, bw, 5994u), om_marquee_period(t->full_w, bw), Z_PMQ - 2);
    } else
        om_ut_draw_base(t, x, base, 0, Z_PTXT);
}

/* the side panel's lines, one after another, never past its box */
typedef struct { const om_pbox *b; int y, n; uint16_t vis; } side;
static int side_line(side *sd, const char *s, uint16_t fg, int track)
{
    om_ut *t;
    if (sd->n >= NSIDE || sd->y + OM_PD_ROW_H - 1 > sd->b->y1) return 0;
    t = &s_side[sd->n++];
    if (track) tracked(t, s, fg, S.ov_eff, sd->vis, sd->b->x0 + 4, sbase(sd->y));
    else text(t, OM_FACE_SMALL, s, sd->b->x1 - sd->b->x0 - 7, fg, S.ov_eff, sd->vis, sd->b->x0 + 4, sbase(sd->y), 0);
    sd->y += OM_PD_ROW_H;
    return 1;
}
static void side_wrap(side *sd, const char *s, uint16_t fg)
{
    char l1[64], l2[64];
    int n = om_title_wrap(&om_face_small, s, sd->b->x1 - sd->b->x0 - 7, l1, sizeof l1, l2, sizeof l2);
    side_line(sd, l1, fg, 0);
    if (n > 1) side_line(sd, l2, fg, 0);
}
static void divider(const om_pbox *side_box)
{
    quad(side_box->x0 - 3, side_box->y0, side_box->x0 - 3, side_box->y1, mix(S.ov_eff, S.txt, 40), Z_PBAR);
}

/* --- Cheats ---------------------------------------------------------------------------------------------------- */
static const char *ced_msg(const om_ced *e)
{
    switch (e->msg) {
    case OM_CEM_FULL:     return "No room: turn another cheat off first";
    case OM_CEM_BAD:      return om_chk_why(e->why);
    case OM_CEM_READONLY: return "CHEATS.PAK does not read: fix it on a PC";
    case OM_CEM_KAI:      return "A built-in cheat: its codes are read only";
    case OM_CEM_FILEFULL: return "No room for more cheats or codes";
    case OM_CEM_ASK:      return "Press Z again to delete";
    case OM_CEM_NAME:     return "Type a name first";
    case OM_CEM_LABEL:    return "A heading, not a cheat";
    case OM_CEM_LAST:     return "A cheat needs one code: delete the cheat";
    default:              return "";
    }
}

static void code_text(const om_ch_code *c, char out[24])
{
    om_ced t;
    char r[18];
    memset(&t, 0, sizeof t);
    t.addr = c->addr; t.val = c->value; t.cmd = c->cmd;
    om_ced_code_text(&t, r);                                    /* "AAAAAAAA:VVVV:TT" */
    memcpy(out, r, 8); memcpy(out + 8, " : ", 3); memcpy(out + 11, r + 9, 4); memcpy(out + 15, " : ", 3);
    memcpy(out + 18, r + 14, 2); out[20] = 0;
}

static void toggle(int x, int y, int on)
{   /* a pill: green with the knob right when on */
    uint16_t tr = on ? GREEN : mix(S.ov_eff, S.txt, 70);
    quad(x + 1, y, x + 12, y + 6, tr, Z_PBAR - 2);
    quad(x, y + 1, x + 13, y + 5, tr, Z_PBAR - 2);
    if (on) quad(x + 8, y + 1, x + 12, y + 5, S.txt, Z_PICON);
    else quad(x + 1, y + 1, x + 5, y + 5, mix(S.ov_eff, S.txt, 200), Z_PICON);
}

static void cheat_list(om_pop_state *g, uint16_t vis)
{
    const om_ced *e = &g->pc.ed;
    const om_ch_file *f = e->f;
    const om_pbox *lb = B(PB_LIST_C);
    uint32_t top, i, vis_rows = OM_PD_CHEAT_ROWS;
    static uint32_t s_top;
    if (e->row < s_top) s_top = e->row;
    if (e->row >= s_top + vis_rows) s_top = e->row - vis_rows + 1u;
    if (s_top + vis_rows > f->ncheats) s_top = f->ncheats > vis_rows ? f->ncheats - vis_rows : 0;
    top = s_top;
    if (!f->ncheats)
        text(&s_row[0], OM_FACE_SMALL, e->ro ? "CHEATS.PAK does not read" : "No cheats yet: X adds one",
             lb->x1 - lb->x0 - 10, S.dim, S.ov_eff, vis, lb->x0 + 6, sbase(lb->y0 + 1), 0);
    for (i = top; i < f->ncheats && i < top + vis_rows; i++) {
        const om_ch_cheat *c = &f->cheat[i];
        int r = (int)(i - top), y = lb->y0 + r * OM_PD_ROW_H, sel = i == e->row, hd = (c->flags & OM_CH_HEADER) != 0;
        uint16_t bg = row_bar(lb->x0, lb->x1, y, sel);
        if (hd) {
            row_name(r, c->name, lb->x0 + 6, lb->x1 - 4, sbase(y), sel, S.acc2, bg, vis);
            continue;
        }
        toggle(lb->x0 + 6, y + 4, (c->flags & OM_CH_ON) != 0);
        row_name(r, c->name, lb->x0 + 24, lb->x1 - 14, sbase(y), sel, sel ? S.txt : S.row, bg, vis);
        if (c->flags & OM_CH_USER) {
            om_ut_colors(&s_star, S.gold, bg, vis);
            om_ut_draw(&s_star, lb->x1 - 11, y + (OM_PD_ROW_H - s_star.h) / 2, 0, Z_PTXT);
        }
    }
    if (f->ncheats > vis_rows) {                                  /* the scroll thumb */
        int hh = lb->y1 - lb->y0 + 1, ty = lb->y0 + hh * (int)top / f->ncheats, th = hh * (int)vis_rows / f->ncheats;
        quad(lb->x1 - 1, ty, lb->x1, ty + th - 1, S.dim, Z_PBAR);
    }
}

static void cheat_side(om_pop_state *g, uint16_t vis)
{
    const om_ced *e = &g->pc.ed;
    const om_ch_file *f = e->f;
    side sd = { B(PB_SIDE_C), B(PB_SIDE_C)->y0 + 2, 0, vis };
    char l[64], *o;
    uint32_t k;
    divider(sd.b);
    if (!f->ncheats || e->row >= f->ncheats) {
        side_line(&sd, "CHEATS", S.acc2, 1);
        side_wrap(&sd, e->ro ? "This file does not read: nothing can change." : "X adds your own cheat.", S.dim);
    } else {
        const om_ch_cheat *c = &f->cheat[e->row];
        side_line(&sd, "SELECTED", S.acc2, 1);
        side_wrap(&sd, c->name, S.txt);
        if (c->flags & OM_CH_HEADER) side_line(&sd, "A heading", S.dim, 0);
        else {
            o = l;
            strcpy(o, (c->flags & OM_CH_USER) ? "Added by you \xC2\xB7 " : "Built in \xC2\xB7 ");
            o += strlen(o);
            put_u(&o, c->count);
            strcpy(o, c->count == 1 ? " code" : " codes");
            side_line(&sd, l, S.dim, 0);
            for (k = 0; k < c->count && k < 3u; k++) {
                char ct[24];
                if (sd.y + OM_PD_ROW_H - 1 > sd.b->y1 - 2 * OM_PD_ROW_H) break;   /* room for the slots line */
                quad(sd.b->x0 + 2, sd.y, sd.b->x1 - 4, sd.y + OM_PD_ROW_H - 2, mix(S.ov_eff, 0x8000u, 120), Z_PBAR);
                code_text(&f->code[c->first + k], ct);
                if (sd.n < NSIDE) {
                    om_ut *t = &s_side[sd.n++];
                    text(t, OM_FACE_SMALL, ct, sd.b->x1 - sd.b->x0 - 10, S.txt, mix(S.ov_eff, 0x8000u, 120), vis,
                         sd.b->x0 + 6, sbase(sd.y), 0);
                }
                sd.y += OM_PD_ROW_H;
            }
            if (c->count > k) {
                o = l; *o++ = '+'; put_u(&o, c->count - k); strcpy(o, " more");
                side_line(&sd, l, S.dim, 0);
            }
            o = l;
            strcpy(o, (c->flags & OM_CH_ON) ? "On: uses " : "Turning it on uses ");
            o += strlen(o);
            put_u(&o, c->count);
            strcpy(o, c->count == 1 ? " slot." : " slots.");
            side_wrap(&sd, l, S.dim);
        }
    }
    if (g->req.cheat_jhl) {                                       /* the last line: why they will not run */
        sd.y = sd.b->y1 - OM_PD_ROW_H + 1 > sd.y ? sd.b->y1 - OM_PD_ROW_H + 1 : sd.y;
        side_line(&sd, "Needs the CWX loader", S.err, 0);
    }
}

static void cheat_codes(om_pop_state *g, uint16_t vis)
{   /* a user cheat's codes, one a row */
    const om_ced *e = &g->pc.ed;
    const om_ch_cheat *c = &e->f->cheat[e->cheat];
    const om_pbox *lb = B(PB_LIST_C);
    side sd = { B(PB_SIDE_C), B(PB_SIDE_C)->y0 + 2, 0, vis };
    uint32_t i, top = e->ctop;
    if (e->crow < top) top = e->crow;
    if (e->crow >= top + OM_PD_CHEAT_ROWS) top = e->crow - OM_PD_CHEAT_ROWS + 1u;
    for (i = top; i < c->count && i < top + OM_PD_CHEAT_ROWS; i++) {
        int r = (int)(i - top), y = lb->y0 + r * OM_PD_ROW_H, sel = i == e->crow;
        char ct[24];
        uint16_t bg = row_bar(lb->x0, lb->x1, y, sel);
        code_text(&e->f->code[c->first + i], ct);
        row_name(r, ct, lb->x0 + 8, lb->x1 - 4, sbase(y), sel, sel ? S.txt : S.row, bg, vis);
    }
    divider(sd.b);
    side_line(&sd, "CODES", S.acc2, 1);
    side_wrap(&sd, c->name, S.txt);
    side_line(&sd, "Address : value : type", S.dim, 0);
}

static void name_page(om_pop_state *g, uint16_t vis)
{   /* the cheat's name on a character wheel: the letter under the cursor lit, the next and previous above and below */
    static const char wh[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.!%&'()/:+?";   /* chedit.c's wheel */
    const om_ced *e = &g->pc.ed;
    const om_pbox *lb = B(PB_LIST_C);
    char nm[OM_CH_MAX_NAME + 2], pre[OM_CH_MAX_NAME + 2], one[2] = { 0, 0 };
    int x0 = lb->x0 + 10, y = lb->y0 + 30, cx, cw, k, n = (int)strlen(e->name);
    const char *w;
    uint32_t wi, wn = sizeof wh - 1u;
    side sd = { B(PB_SIDE_C), B(PB_SIDE_C)->y0 + 2, 0, vis };
    memcpy(nm, e->name, (size_t)n + 1);
    if (e->npos >= n) { nm[e->npos] = ' '; nm[e->npos + 1] = 0; }
    for (k = 0; k < e->npos; k++) pre[k] = nm[k] == ' ' ? '_' : nm[k];
    pre[k] = 0;
    cx = x0 + om_uf_width(&om_face_title, pre);
    one[0] = nm[e->npos] == ' ' ? 'M' : nm[e->npos];
    cw = om_uf_width(&om_face_title, one);
    quad(cx - 1, y - 2, cx + cw, y + om_uf_height(&om_face_title) - 1, S.acc2, Z_PBAR);
    text(&s_big, OM_FACE_TITLE, nm, lb->x1 - x0, S.txt, S.ov_eff, vis, x0, y + om_face_title.asc, 0);
    w = strchr(wh, nm[e->npos]);
    wi = w ? (uint32_t)(w - wh) : 0;
    one[0] = wh[(wi + 1u) % wn];
    text(&s_up, OM_FACE_SMALL, one[0] == ' ' ? "_" : one, 0, S.dim, S.ov_eff, vis, cx, y - 4, 0);
    one[0] = wh[(wi + wn - 1u) % wn];
    text(&s_dn, OM_FACE_SMALL, one[0] == ' ' ? "_" : one, 0, S.dim, S.ov_eff, vis, cx,
         y + om_uf_height(&om_face_title) + 1 + om_face_small.asc, 0);
    divider(sd.b);
    side_line(&sd, e->is_new ? "NEW CHEAT" : "RENAME", S.acc2, 1);
    side_wrap(&sd, "Up/Down change the letter, Left/Right move.", S.dim);
    side_wrap(&sd, e->is_new ? "A goes on to its first code." : "A keeps the name.", S.dim);
}

static void code_page(om_pop_state *g, uint16_t vis)
{   /* ADDRESS : VALUE : TYPE, the digit being changed lit */
    static const char *const tn[4] = { "00 write once at boot", "01 keep writing 16 bits", "03 keep writing 8 bits",
                                       "0D only while this word matches" };
    const om_ced *e = &g->pc.ed;
    const om_pbox *lb = B(PB_LIST_C);
    char r[18], show[24], pre[24];
    int cur = e->field + (e->field >= 8) + (e->field >= 12), k, o = 0, x0 = lb->x0 + 10, y = lb->y0 + 26, cx, cw, ti;
    side sd = { B(PB_SIDE_C), B(PB_SIDE_C)->y0 + 2, 0, vis };
    om_ced_code_text(e, r);
    for (k = 0; r[k]; k++) show[o++] = r[k] == ':' ? ' ' : r[k];
    show[o] = 0;
    memcpy(pre, show, (size_t)cur);
    pre[cur] = 0;
    cx = x0 + om_uf_width(&om_face_title, pre);
    {
        char one[3] = { show[cur], e->field >= 12 ? show[cur + 1] : 0, 0 };
        cw = om_uf_width(&om_face_title, one);
    }
    quad(cx - 1, y - 2, cx + cw, y + om_uf_height(&om_face_title) - 1, S.acc2, Z_PBAR);
    text(&s_big, OM_FACE_TITLE, show, lb->x1 - x0, S.txt, S.ov_eff, vis, x0, y + om_face_title.asc, 0);
    text(&s_row[1], OM_FACE_SMALL, "Address  Value  Type", 0, S.dim, S.ov_eff, vis, x0, y - 6, 0);
    for (ti = 0; ti < 4; ti++)
        if (om_ced_types[ti] == e->cmd)
            text(&s_row[2], OM_FACE_SMALL, tn[ti], lb->x1 - x0, S.acc2, S.ov_eff, vis, x0, sbase(y + 24), 0);
    text(&s_row[3], OM_FACE_SMALL, "Game RAM 06004000-060FFFFF,", lb->x1 - x0, S.dim, S.ov_eff, vis, x0,
         sbase(y + 24 + OM_PD_ROW_H), 0);
    text(&s_row[4], OM_FACE_SMALL, "00200000-002FFFFF; 16-bit codes even.", lb->x1 - x0, S.dim, S.ov_eff, vis, x0,
         sbase(y + 24 + 2 * OM_PD_ROW_H), 0);
    divider(sd.b);
    side_line(&sd, "CODE FOR", S.acc2, 1);
    side_wrap(&sd, e->is_new == 1 ? e->name : e->f->cheat[e->cheat].name, S.txt);
    side_wrap(&sd, "Left/Right pick a digit, Up/Down change it.", S.dim);
    side_wrap(&sd, "A checks it first.", S.dim);
}

static void draw_cheats(om_pop_state *g, uint16_t vis, uint32_t frame)
{
    const om_ced *e = &g->pc.ed;
    char m[32], *o = m;
    uint32_t act = om_ced_active(e);
    strcpy(o, "Code slots ");
    o += strlen(o);
    put_u(&o, act);
    strcpy(o, " / ");
    o += 3;
    put_u(&o, e->cap);
    *o = 0;
    panel();
    header(g, "CHEATS", m, act, e->cap, GREEN, vis, frame);
    switch (e->page) {
    case OM_CE_CODES: cheat_codes(g, vis); break;
    case OM_CE_NAME:  name_page(g, vis); break;
    case OM_CE_CODE:  code_page(g, vis); break;
    default:          cheat_list(g, vis); cheat_side(g, vis); break;
    }
    if (g->saving) message("Saving CHEATS.PAK - keep the power on", S.gold, vis);
    else if (g->msg[0]) message(g->save_failed ? "Not saved \xC2\xB7 B again leaves without saving" : g->msg,
                                g->save_failed ? S.err : S.txt, vis);
    else if (e->msg != OM_CEM_NONE) message(ced_msg(e), e->msg == OM_CEM_ASK ? S.gold : S.err, vis);
    else if (e->page == OM_CE_LIST) footer(e->dirty ? PF_CHEAT_LIST_SAVE : PF_CHEAT_LIST, vis);
    else if (e->page == OM_CE_CODES) footer(PF_CHEAT_CODES, vis);
    else footer(e->page == OM_CE_NAME ? PF_CHEAT_NAME : PF_CHEAT_CODE, vis);
}

/* --- Manage saves ---------------------------------------------------------------------------------------------- */
static void tabs(const om_ps *p, uint16_t vis)
{
    static const char *const k_tab[3] = { "Console", "Cartridge", "Card" };
    const om_pbox *tb = B(PB_TABS);
    int x = tb->x0, i;
    for (i = 0; i < 3; i++) {
        om_ut *t = &s_tab[i];
        int sel = p->tab == i, w;
        uint16_t bg = sel ? S.acc : mix(S.ov_eff, S.txt, 30);
        om_ut_text(t, OM_FACE_SMALL, k_tab[i], 0);
        w = t->w + 14;
        quad(x, tb->y0, x + w - 1, tb->y1, bg, Z_PBAR);
        om_ut_colors(t, sel ? S.txt : S.row, bg, vis);
        om_ut_draw_base(t, x + 7, tb->y0 + om_face_small.asc, 0, Z_PTXT);
        x += w + 4;
    }
}

static void save_icon(int x, int y, int set, uint16_t bg)
{   /* a small card: a save (filled centre) or a set (an open frame) */
    uint16_t c = mix(bg, S.acc2, 200);
    quad(x, y, x + 8, y + 8, c, Z_PICON + 1);
    quad(x + 1, y + 1, x + 7, y + 7, mix(bg, S.acc, 160), Z_PICON);
    if (!set) quad(x + 3, y + 3, x + 5, y + 5, S.txt, Z_PICON - 1);
    else quad(x + 3, y + 2, x + 4, y + 5, S.txt, Z_PICON - 1);
}

static const char *folder_title(const om_pop_state *g, const char *fold, char *out, uint32_t cap)
{   /* "_GENERAL" reads "General saves"; a game's folder its GAMES.DB title when the product is known */
    om_game gm;
    char key[24];
    uint32_t k;
    if (!strcmp(fold, "_GENERAL")) return "General saves";
    for (k = 0; fold[k] && fold[k] != '[' && k + 1 < sizeof key; k++) key[k] = fold[k];
    key[k] = 0;
    if (g->req.db && om_gamesdb_find(g->req.db, key, &gm) == 1 && gm.title_en && gm.title_en[0]) {
        strncpy(out, gm.title_en, cap - 1);
        out[cap - 1] = 0;
        return out;
    }
    return fold;
}

static void save_rows(om_pop_state *g, uint16_t vis)
{
    const om_ps *p = &g->ps;
    const om_pbox *lb = B(PB_LIST_S);
    uint16_t i;
    for (i = 0; i < OM_PD_SAVE_ROWS && p->top + i < p->nrow; i++) {
        const om_sv_item *it;
        const om_ps_set *st;
        int y = lb->y0 + i * OM_PD_ROW_H, sel = p->top + i == p->row && p->page == OM_PS_LIST;
        int hi = p->top + i == p->row;
        uint16_t bg = row_bar(lb->x0, lb->x1, y, hi), fg = hi ? S.txt : S.row;
        char nm[40], val[16];
        om_ps_row(p, (uint16_t)(p->top + i), &it, &st);
        if (it) {
            memcpy(nm, it->d.name, 11);
            nm[11] = 0;
            om_pd_size(it, val);
        } else {
            if (st->alt) strcpy(nm, "Kept copies");
            else if (st->n) om_pd_bup_date(st->newest, nm);
            else strcpy(nm, "Empty set");
            if (st->set && !st->alt) {
                char *o = nm + strlen(nm);
                strcpy(o, " \xC2\xB7 slot ");
                o += strlen(o);
                *o++ = (char)('0' + g->sv.set[st->set].slot);
                *o = 0;
            }
            if (st->loaded) strcpy(val, "loaded");
            else if (st->alt) strcpy(val, "alt");
            else { char *o = val; put_u(&o, st->n); strcpy(o, st->n == 1 ? " save" : " saves"); }
        }
        save_icon(lb->x0 + 6, y + 2, st != 0, bg);
        om_ut_text(&s_rval[i], OM_FACE_SMALL, val, 0);
        om_ut_colors(&s_rval[i], st && st->loaded ? GREEN : st && st->alt ? S.gold : S.dim, bg, vis);
        om_ut_draw_base(&s_rval[i], lb->x1 - 4 - s_rval[i].w, sbase(y), 0, Z_PTXT);
        row_name(i, nm, lb->x0 + 19, lb->x1 - 8 - s_rval[i].w, sbase(y), sel, fg, bg, vis);
    }
    if (!p->nrow) {
        const char *why = p->list_rc ? om_sv_why(p->list_rc) : p->tab == OM_SV_SD && p->sdlev ? "This set is empty"
                        : p->tab == OM_SV_SD ? "No saves on the card for this game" : "No saves here";
        text(&s_row[0], OM_FACE_SMALL, why, lb->x1 - lb->x0 - 10, S.dim, S.ov_eff, vis, lb->x0 + 6,
             sbase(lb->y0 + 1), 0);
    }
}

static void save_side(om_pop_state *g, uint16_t vis)
{
    om_ps *p = &g->ps;
    side sd = { B(PB_SIDE_S), B(PB_SIDE_S)->y0 + 2, 0, vis };
    const om_sv_item *it;
    const om_ps_set *st;
    char l[64], d[12], *o;
    divider(sd.b);
    if (p->page == OM_PS_ACT) {
        const om_sv_item *x = &p->sel;
        uint16_t i;
        memcpy(l, x->d.name, 11);
        l[11] = 0;
        side_line(&sd, l, S.acc2, 1);
        o = l;
        if (x->d.comment[0]) { *o++ = '"'; memcpy(o, x->d.comment, 10); o[10] = 0; o += strlen(o); *o++ = '"'; *o = 0; }
        else *o = 0;
        side_line(&sd, l, S.dim, 0);
        for (i = 0; i < p->nact; i++) {
            const om_pbox *b = sd.b;
            int sel = i == p->act, del = p->acts[i] == OM_PSA_DELETE;
            uint16_t bg = sel ? bar_bg() : S.ov_eff;
            if (sd.n >= NSIDE || sd.y + OM_PD_ROW_H - 1 > b->y1) break;
            if (sel) quad(b->x0 + 1, sd.y, b->x1 - 2, sd.y + OM_PD_ROW_H - 1, bg, Z_PBAR);
            text(&s_side[sd.n++], OM_FACE_SMALL, om_ps_act_label(p, i), b->x1 - b->x0 - 9,
                 del ? (p->armed && sel ? S.gold : S.err) : S.txt, bg, vis, b->x0 + 5, sbase(sd.y), 0);
            sd.y += OM_PD_ROW_H;
        }
        if (sd.y <= sd.b->y1 - 2 * OM_PD_ROW_H + 1) sd.y = sd.b->y1 - 2 * OM_PD_ROW_H + 1;   /* at the bottom */
        side_wrap(&sd, x->dev == OM_SV_SD ? "Delete asks twice; the older set keeps a copy."
                                          : "Delete asks twice and backs up first.", S.dim);
        return;
    }
    om_ps_row(p, p->row, &it, &st);
    if (it) {
        side_line(&sd, "DETAILS", S.acc2, 1);
        memcpy(l, it->d.name, 11);
        l[11] = 0;
        side_line(&sd, l, S.txt, 0);
        if (it->d.comment[0]) {
            o = l; *o++ = '"'; memcpy(o, it->d.comment, 10); o[10] = 0; o += strlen(o); *o++ = '"'; *o = 0;
            side_line(&sd, l, S.dim, 0);
        }
        om_pd_size(it, l);
        side_line(&sd, l, S.dim, 0);
        if (it->d.date) { om_pd_bup_date(it->d.date, d); strcpy(l, "Saved "); strcat(l, d); side_line(&sd, l, S.dim, 0); }
        if (it->alt) side_wrap(&sd, "A kept copy: another version of a save in this set.", S.dim);
        side_wrap(&sd, "A shows what you can do with it.", S.dim);
    } else if (st) {
        uint32_t k, n = 0;
        side_line(&sd, "THIS SET", S.acc2, 1);
        if (st->n) { om_pd_bup_date(st->newest, d); strcpy(l, "Newest "); strcat(l, d); side_line(&sd, l, S.txt, 0); }
        o = l;
        *o = 0;
        for (k = 0; k < g->sv.n && n < 4u; k++)               /* its saves' names */
            if (g->sv.it[k].set == st->set && g->sv.it[k].alt == st->alt) {
                char nm[12];
                memcpy(nm, g->sv.it[k].d.name, 11);
                nm[11] = 0;
                if (n++) strcat(l, ", ");
                strcat(l, nm);
            }
        if (n) side_wrap(&sd, l, S.dim);
        if (st->loaded) side_wrap(&sd, "In the console now: this game's set.", S.dim);
        else if (st->alt) side_wrap(&sd, "Kept copies: other versions, never loaded.", S.dim);
        else if (g->sv.set[st->set].general) side_wrap(&sd, "Loads for games with no set of their own.", S.dim);
        else if (!g->sv.set[st->set].slot) side_wrap(&sd, "Loads next time you launch with slot 0.", S.dim);
        else {
            strcpy(l, "Loads when you launch with slot 0.");
            l[sizeof "Loads when you launch with slot " - 1] = (char)('0' + g->sv.set[st->set].slot);
            side_wrap(&sd, l, S.dim);
        }
    } else
        side_line(&sd, p->tab == OM_SV_SD ? "THE CARD" : p->tab == OM_SV_CART ? "CARTRIDGE" : "CONSOLE", S.acc2, 1);
}

static void games_page(om_pop_state *g, uint16_t vis)
{
    const om_ps *p = &g->ps;
    const om_pbox *lb = B(PB_LIST_S);
    side sd = { B(PB_SIDE_S), B(PB_SIDE_S)->y0 + 2, 0, vis };
    char t[64], l[40], *o;
    uint16_t i;
    for (i = 0; i < OM_PD_SAVE_ROWS && p->gtop + i <= p->nfold; i++) {   /* row 0: the import (psave.h) */
        int y = lb->y0 + i * OM_PD_ROW_H, sel = p->gtop + i == p->grow;
        uint16_t bg = row_bar(lb->x0, lb->x1, y, sel);
        if (p->gtop + i == 0) {
            row_name(i, "Import from Rings / Kaiâ¦", lb->x0 + 19, lb->x1 - 6, sbase(y), sel, sel ? S.txt : S.row,
                     bg, vis);
            continue;
        }
        save_icon(lb->x0 + 6, y + 2, 1, bg);
        row_name(i, folder_title(g, om_ps_folder(p, (uint16_t)(p->gtop + i - 1u)), t, sizeof t), lb->x0 + 19, lb->x1 - 6,
                 sbase(y), sel, sel ? S.txt : S.row, bg, vis);
    }
    if (!p->nfold && p->gtop + 1u < OM_PD_SAVE_ROWS)
        text(&s_row[1], OM_FACE_SMALL, "No saves on the card yet", lb->x1 - lb->x0 - 10, S.dim, S.ov_eff, vis,
             lb->x0 + 6, sbase(lb->y0 + OM_PD_ROW_H + 1), 0);
    divider(sd.b);
    side_line(&sd, "GAMES", S.acc2, 1);
    o = l;
    put_u(&o, p->nfold);
    strcpy(o, p->nfold == 1 ? " folder of saves" : " folders of saves");
    side_line(&sd, l, S.txt, 0);
    if (!p->grow) {
        side_line(&sd, "A picks Rings and Kai saves", S.dim, 0);
        side_line(&sd, "to copy into Aether.", S.dim, 0);
        return;
    }
    side_line(&sd, om_ps_folder(p, (uint16_t)(p->grow - 1u)), S.dim, 0);
    side_line(&sd, "A opens its saves.", S.dim, 0);
    side_wrap(&sd, "Console and Cartridge list every save.", S.dim);
}

static void draw_saves(om_pop_state *g, uint16_t vis, uint32_t frame)
{
    om_ps *p = &g->ps;
    char m[40], *o = m;
    uint32_t used = 0, total = 0;
    *o = 0;
    if (p->page != OM_PS_GAMES && p->tab != OM_SV_SD && !p->list_rc) {
        /* the device's blocks: asked of the BIOS only when the tab, the list or an operation's result changed (a
         * cartridge's BUP_Stat is slow; review minor 3) */
        static uint32_t key, u0, t0;                                 /* .bss: zeroed at every open */
        uint32_t k = (uint32_t)p->tab | (uint32_t)p->nrow << 2 | (uint32_t)p->msgk << 12 | (uint32_t)(uint8_t)p->msg[0] << 16
                     | (uint32_t)p->page << 24 | 1u << 31;
        if (k != key) {
            om_bram_info bi;
            uint32_t fb = 0, bs = 0;
            key = k;
            u0 = t0 = 0;
            if (om_bram_use(p->tab == OM_SV_CART ? 1u : 0u) == 0 && om_bram_info_get(0, &bi) == 0 &&
                om_bram_stat(&fb, &bs) == 0) {
                t0 = bi.totalblock;
                u0 = t0 > fb ? t0 - fb : 0;
            }
        }
        used = u0;
        total = t0;
        if (total) { put_u(&o, used); strcpy(o, " / "); o += 3; put_u(&o, total); strcpy(o, " blocks"); }
    } else if (p->page != OM_PS_GAMES && p->tab == OM_SV_SD) {
        put_u(&o, p->sdlev ? p->nrow : p->nsets);
        strcpy(o, p->sdlev ? (p->nrow == 1 ? " save" : " saves") : (p->nsets == 1 ? " save set" : " save sets"));
    }
    panel();
    header(g, "MANAGE SAVES", m[0] ? m : 0, used, total, S.acc2, vis, frame);
    if (p->page == OM_PS_GAMES) {
        games_page(g, vis);

        if (p->msgk != OM_PSM_NONE) message(p->msg, p->msgk == OM_PSM_ERR ? S.err : S.txt, vis);
        else footer(PF_SAVE_GAMES, vis);
        return;
    }
    tabs(p, vis);
    save_rows(g, vis);
    save_side(g, vis);
    if (om_ps_pending(p)) message("Working - keep the power on", S.gold, vis);
    else if (p->msgk != OM_PSM_NONE)
        message(p->msg, p->msgk == OM_PSM_ERR ? S.err : p->msgk == OM_PSM_ASK ? S.gold : S.txt, vis);
    else if (p->page == OM_PS_ACT) footer(PF_SAVE_ACT, vis);
    else {
        int back = (p->tab == OM_SV_SD && p->sdlev) || p->global, sets = p->tab == OM_SV_SD && !p->sdlev;
        footer(sets ? (back ? PF_SAVE_SETS_B : PF_SAVE_SETS) : (back ? PF_SAVE_LIST_B : PF_SAVE_LIST), vis);
    }
}

/* --- About (2026-11-27: the Card Manager's About window) -------------------------------------------------------- */
static int about_w(int kind, const char *s, int len)
{   /* pabout.h om_pa_width: a name in the label face (9 px SemiBold), its project line in the small face */
    char b[256];
    if (len > 255) len = 255;
    memcpy(b, s, (size_t)len);
    b[len] = 0;
    return om_uf_width(kind == OM_PA_NAME ? &om_face_label : &om_face_small, b);
}

static int about_open(om_pop_state *g)
{   /* its own slots, the logo (LUT and texture, written once), the credits wrapped to the list */
    int i, h = om_uf_height(&om_face_small), e = 0;
    if (om_uf_height(&om_face_label) > h) h = om_uf_height(&om_face_label);
    e |= alloc(&s_label, 88, h);
    e |= alloc(&s_title, 128, om_uf_height(&om_face_title));
    e |= alloc(&s_aby, 192, h);
    e |= alloc(&s_alink, 192, h);
    e |= alloc(&s_ahead, 288, h);
    for (i = 0; i < (int)ANROW; i++) e |= alloc(&s_arow[i], 288, h);
    for (i = 0; i < NFOOT; i++) { e |= alloc(&s_foot[i], 72, h); e |= alloc(&s_badge[i], 16, 11); }
    if (e || s_pool + 32u + LOGO_BYTES > POOL1) return OM_E_FULL;
    s_logo = s_pool;
    s_pool += 32u + ((LOGO_BYTES + 31u) & ~31u);
    om_v1_write(s_logo, om_pa_logo_lut, 16);
    om_v1_write_bytes(s_logo + 32u, om_pa_logo, LOGO_BYTES);
    om_pa_open(&g->pa, OM_PA_LIST_X1 - OM_PA_LIST_X0 + 1, OM_PA_ROWS, about_w);
    return 0;
}

static void draw_about(om_pop_state *g, uint16_t vis)
{
    const om_pa *a = &g->pa;
    uint16_t bg = hdr_bg(), i;
    int x = OM_PA_TEXT_X, w = OM_PA_TEXT_X1 - OM_PA_TEXT_X + 1, lw = OM_PA_LIST_X1 - OM_PA_LIST_X0 + 1;
    char b[256];
    panel_to(OM_PA_HDR_Y1);
    om_v1_sprite(OM_PA_LOGO_X, OM_PA_LOGO_Y, s_logo + 32u, OM_PA_LOGO_W, OM_PA_LOGO_H, s_logo, 0, Z_PICON);
    tracked(&s_label, "ABOUT", S.acc2, bg, vis, x, OM_PA_BASE_LABEL);
    text(&s_aby, OM_FACE_LABEL, OM_PA_BY, w, S.txt, bg, vis, x, OM_PA_BASE_BY, 0);
    text(&s_alink, OM_FACE_SMALL, OM_PA_LINK, w, S.acc2, bg, vis, x, OM_PA_BASE_LINK, 0);
    quad(x, OM_PA_BASE_LINK + 2, x + s_alink.w - 1, OM_PA_BASE_LINK + 2, mix(bg, S.acc2, 150), Z_PBAR);   /* a link */
    text(&s_title, OM_FACE_TITLE, OM_VERSION_TAG, w, S.txt, bg, vis, x, OM_PA_BASE_VER, 0);
    tracked(&s_ahead, OM_PA_HEADING, S.acc2, S.ov_eff, vis, OM_PA_LIST_X0, sbase(OM_PA_HEAD_Y));
    for (i = 0; i < a->rows && a->top + i < a->n; i++) {
        const om_pa_line *l = &a->line[a->top + i];
        int name = l->kind == OM_PA_NAME;
        memcpy(b, l->s, l->len);
        b[l->len] = 0;
        text(&s_arow[(a->top + i) % ANROW], name ? OM_FACE_LABEL : OM_FACE_SMALL, b, lw, name ? S.txt : S.row, S.ov_eff,
             vis, OM_PA_LIST_X0, sbase(OM_PA_LIST_Y0 + i * OM_PA_ROW_H), 0);
    }
    if (a->n > a->rows) {                                         /* the scroll thumb */
        int hh = (int)a->rows * OM_PA_ROW_H, ty = OM_PA_LIST_Y0 + hh * a->top / a->n, th = hh * a->rows / a->n;
        quad(OM_PA_THUMB_X, OM_PA_LIST_Y0, OM_PA_THUMB_X + 1, OM_PA_LIST_Y0 + hh - 1, mix(S.ov_eff, S.txt, 40), Z_PBAR);
        quad(OM_PA_THUMB_X, ty, OM_PA_THUMB_X + 1, ty + th - 1, S.dim, Z_PBAR - 1);
    }
    footer(PF_ABOUT, vis);
}

void om_pd_draw(om_pop_state *g, uint16_t vis, uint32_t frame)
{
    om_ut_budget_us(6000);                                        /* the list behind is still: the popup's frame */
    if (g->kind == OM_POP_CHEATS) draw_cheats(g, vis, frame);
    else if (g->kind == OM_POP_ABOUT) draw_about(g, vis);
    else draw_saves(g, vis, frame);
}
