/* menu/views/ovdraw.c -- see ovdraw.h. Every position comes from the layout table (menu/src/layout.h): one centered
 * panel (OVERLAY) for Settings, Filter and the game's Launch Options, rows of 14 px in OV_BAR (the game panel's from
 * OV_GBAR, right of its cover), labels 4 px into the row, values right-aligned at OV_VALUE.x1. The panel is a VDP1 body
 * and 1 px frame drawn over the dimmed menu (folders.c dims what lies behind it), then the highlight and the text,
 * inside a user clip of the (sliding) panel. Values wider than their box marquee (addendum 3); fixed labels and hints
 * always fit (menu/host-tests/test_layout.c). BSD-3-Clause. */
#include <string.h>
#include "anim.h"
#include "art.h"
#include "layout.h"
#include "marquee.h"
#include "omlayout.h"
#include "ovdraw.h"
#include "safearea.h"
#include "settings.h"
#include "uiline.h"
#include "v1.h"
#include "vdp2theme.h"

/* Every fixed string has its own slot, rasterised once at boot (om_ovdraw_prewarm), so opening a panel rasterises only
 * its values. The settings list has more rows (OM_OVR_COUNT) than fit (OM_OV_VIS): row()'s `i` is the VISIBLE
 * position, used for the row's y and the value caches (s_value, s_vkey, s_mq). The Controls page has its own
 * labels (s_clabel); the game panel has seven (s_glabel; Settings borrows Saves) and reuses the
 * Loader and In Game Reset labels; s_gtitle is the game's name after the panel's title, rasterised at open. "Press A
 * again" borrows the armed Reset row's value slot. */
static om_ut s_title[4], s_label[OM_OVR_COUNT], s_clabel[OM_OVC_COUNT], s_flabel, s_value[OM_OV_VIS], s_hint[4],
    s_rflag[5], s_rname[5], s_glabel[7], s_gtitle;
static const char *const k_title[4] = { "Settings", "Filter", "Controls", "Launch Options" };
/* keyed by the enum, so a row inserted in overlay.h cannot shift the labels below it; SAVES borrows the
 * game panel's labels */
static const char *const k_label[OM_OVR_COUNT] = {
    [OM_OVR_THEME] = "Theme", [OM_OVR_CLOCK] = "Clock", [OM_OVR_MUSIC] = "Music", [OM_OVR_SFX] = "Sounds",
    [OM_OVR_ANIM] = "Animations", [OM_OVR_SAFE] = "Overscan", [OM_OVR_LOADER] = "Loader",
    [OM_OVR_IGR] = "In Game Reset", [OM_OVR_RESELECT] = "Start on Last Game",
    [OM_OVR_SAVER] = "Screensaver", [OM_OVR_CONTROLS] = "Controls",
    [OM_OVR_SAVEMGR] = "Manage / Import Saves", [OM_OVR_ABOUT] = "About" };
enum { GL_SAVES, GL_SLOT, GL_CHEATS, GL_PAD, GL_RESET, GL_LAUNCH, GL_SAVEMGR };
static const char *const k_glabel[7] = { "Saves Per Game", "Slot", "Cheats", "Pad", "Reset to defaults", "Launch",
                                         "Manage saves" };
static const uint8_t k_glcap[7] = { 72, 24, 32, 24, 80, 40, 64 };   /* each label's own slot width (test_ovpages) */
/* The UI pool is VDP1 VRAM the cover cache's third slot shares (art.c om_art_cache_claim), so the slots are only as wide
 * as their text: a value can be long (a marquee) only at visible position 0 (Settings' Theme, row 0; the Filter's
 * genre) and at the game panel's Pad row (a compat note of up to 46 characters); its Loader row reads at most "Auto
 * (CWX, database: may fail)", 133 px; every other value fits OV_VALUE (116 px). test_ovpages checks the widths. */
#define VAL_CAP(i) ((i) == 0 || (i) == OM_OGR_PAD ? 256 : (i) == OM_OGR_LOADER ? 136 : 120)
static const uint8_t k_hintcap[4] = { 80, 96, 96, 88 };   /* the hint templates with their widest letter */
typedef char game_rows_visible[OM_OGR_PAD < OM_OV_VIS && OM_OGR_LOADER < OM_OV_VIS ? 1 : -1];
static const char *const k_pg3[3] = { "Default", "On", "Off" };                  /* OM_PG_DEFAULT, ON, OFF */
#define ARROW "\xE2\x80\xBA"
static uint32_t s_mq[OM_OV_VIS];
static char s_htxt_filter[OM_OVHINT_MAX];
static uint32_t s_vkey[OM_OV_VIS];                         /* the value texture's key its marquee clock started on */
static om_style Sty;
static uint16_t s_bar_base;
static int s_bx0;                                          /* this panel's row left edge: OV_BAR's, or OV_GBAR's */

void om_ovdraw_style(const om_style *s)
{
    int i;
    Sty = *s;
    s_bar_base = s->ovbar_base;                            /* Gouraud table OM_G_OVBAR, written by the view */
    for (i = 0; i < 4; i++) s_title[i].lut_ok = 0;
    s_flabel.lut_ok = 0;
    for (i = 0; i < OM_OVR_COUNT; i++) s_label[i].lut_ok = 0;
    for (i = 0; i < OM_OVC_COUNT; i++) s_clabel[i].lut_ok = 0;
    for (i = 0; i < OM_OV_VIS; i++) s_value[i].lut_ok = 0;
    for (i = 0; i < 7; i++) s_glabel[i].lut_ok = 0;
    s_gtitle.lut_ok = 0;
}

void om_ovdraw_alloc(void (*alloc)(om_ut *t, int cap, int h))
{
    int i, hs = om_uf_height(&om_face_small);
    for (i = 0; i < 4; i++) alloc(&s_title[i], i == 3 ? 104 : 56, om_uf_height(&om_face_title));   /* 98, 53 px */
    for (i = 0; i < OM_OVR_COUNT; i++)                              /* rows in the 9 px small face */
        if (k_label[i])   /* the widths test_ovpages checks: 102 px, 86 px, 65 px, the rest <= 56 px */
            alloc(&s_label[i], i == OM_OVR_SAVEMGR ? 104 : i == OM_OVR_RESELECT ? 88 : i == OM_OVR_IGR ? 72 : 56, hs);
    for (i = 0; i < OM_OVC_COUNT; i++) alloc(&s_clabel[i], 56, hs);
    for (i = 0; i < OM_OV_VIS; i++) alloc(&s_value[i], VAL_CAP(i), hs);
    alloc(&s_flabel, 56, hs);
    for (i = 0; i < 4; i++) alloc(&s_hint[i], k_hintcap[i], hs);
    for (i = 0; i < 7; i++) alloc(&s_glabel[i], k_glcap[i], hs);
    alloc(&s_gtitle, 104, hs);
    for (i = 0; i < 5; i++) {
        alloc(&s_rflag[i], OM_FLAG_TEX_W, OM_FLAG_H);
        alloc(&s_rname[i], 48, hs);
    }
}

void om_ovdraw_prewarm(void)
{
    static const char L[5] = { 'J', 'T', 'U', 'E', 'B' };
    int i;
    om_remap d;
    for (i = 0; i < 4; i++) om_ut_text(&s_title[i], OM_FACE_TITLE, k_title[i], 0);
    for (i = 0; i < OM_OVR_COUNT; i++) if (k_label[i]) om_ut_text(&s_label[i], OM_FACE_SMALL, k_label[i], 0);
    for (i = 0; i < OM_OVC_COUNT; i++)
        om_ut_text(&s_clabel[i], OM_FACE_SMALL, i < OM_OVC_RESET ? om_act_label(om_ovc_act((uint16_t)i)) : "Reset all", 0);
    om_ut_text(&s_flabel, OM_FACE_SMALL, "Genre", 0);
    for (i = 0; i < 7; i++) om_ut_text(&s_glabel[i], OM_FACE_SMALL, k_glabel[i], 0);
    om_remap_default(&d);
    om_ovdraw_hints(&d);
    for (i = 0; i < 5; i++) {
        om_ut_text(&s_rname[i], OM_FACE_SMALL, om_area_name(L[i]), 0);
        om_ut_flag(&s_rflag[i], om_flag_for(L[i]));
    }
}

void om_ovdraw_hints(const om_remap *m)
{
    char h[OM_OVHINT_MAX];
    uint8_t i;
    om_ovhint_text(s_htxt_filter, 2, m);
    for (i = 0; i < 4; i++) {
        om_ovhint_text(h, i, m);
        om_ut_text(&s_hint[i], OM_FACE_SMALL, h, 0);
    }
}

static uint16_t bar_at(int x)
{
    const om_box *b = OM_BX(OV_BAR);
    return om_c555_mix(Sty.ovbar_l, Sty.ovbar_r, (uint32_t)(x - s_bx0) * 256u / (uint32_t)(b->x1 - s_bx0));
}

void om_ovdraw_game_title(const char *name)
{   /* at open, not per frame: the game's name after "Launch Options", cut with a fade at the panel's edge */
    om_ut_text(&s_gtitle, OM_FACE_SMALL, name, om_box_w(OM_B_OV_TITLE) - s_title[3].w - 6);
}

/* dim: drawn in the dim colour even when selected (a game with no id: only Launch applies); it still edits */
static void row_d(int i, int sel, om_ut *lab, const char *value, int dx, uint16_t vis, uint32_t fps, int marquee,
                  int clipx0, int clipx1, int dim)
{
    const om_box *bar = OM_BX(OV_BAR), *vb = OM_BX(OV_VALUE);
    int y = bar->y0 + OM_OV_ROW_H * i, base = OM_OV_ROW0_MID + OM_OV_ROW_H * i + om_face_small.mid, lx = s_bx0 + 4;
    if (sel) {
        om_v1_quad(s_bx0 + dx, y, bar->x1 + dx, y + OM_OV_ROW_H - 1, s_bar_base, OM_VRAM_GOUR + 8u * OM_G_OVBAR,
                   OM_V1_CLIP, Z_OVBAR);
        om_v1_quad(s_bx0 + dx, y, s_bx0 + 1 + dx, y + OM_OV_ROW_H - 1, Sty.acc2, 0, OM_V1_CLIP, Z_OVBAR);
    }
    om_ut_colors(lab, dim ? Sty.dim : sel ? Sty.txt : Sty.row, sel ? bar_at(lx) : Sty.ov_eff, vis);
    om_ut_draw_base(lab, lx + dx, base, OM_V1_CLIP, Z_OVTXT);
    if (!value)
        return;
    om_ut_text(&s_value[i], OM_FACE_SMALL, value, marquee ? -om_box_w(OM_B_OV_VALUE) : om_box_w(OM_B_OV_VALUE));
    if (s_value[i].key != s_vkey[i]) {                     /* a new value: its marquee starts over */
        s_vkey[i] = s_value[i].key;
        s_mq[i] = 0;
    }
    om_ut_colors(&s_value[i], dim ? Sty.dim : Sty.txt, sel ? bar_at(vb->x1 - 20) : Sty.ov_eff, vis);
    if (s_value[i].w == s_value[i].full_w && s_value[i].full_w > om_box_w(OM_B_OV_VALUE)) {
        int32_t per = om_marquee_period(s_value[i].full_w, om_box_w(OM_B_OV_VALUE));
        int32_t off = om_marquee_off(s_mq[i]++, s_value[i].full_w, om_box_w(OM_B_OV_VALUE), fps);
        int x0 = vb->x0 + dx > clipx0 ? vb->x0 + dx : clipx0, x1 = vb->x1 + dx < clipx1 ? vb->x1 + dx : clipx1;
        int yy = base - s_value[i].asc;
        om_ut_marquee(&s_value[i], vb->x0 + dx, x0, x1, yy, yy + s_value[i].h - 1, base, off, per, Z_OVVAL - 2 * i);
    } else
        om_ut_draw_base(&s_value[i], vb->x1 + 1 - s_value[i].w + dx, base, OM_V1_CLIP, Z_OVTXT);
}

static void row(int i, int sel, om_ut *lab, const char *value, int dx, uint16_t vis, uint32_t fps, int marquee,
                int clipx0, int clipx1)
{
    row_d(i, sel, lab, value, dx, vis, fps, marquee, clipx0, clipx1, 0);
}

static void thumb(int top, int nrows, int dx)
{   /* the scroll thumb: 1 px at OV_BAR's right edge, only when the list has more rows than fit */
    const om_box *bar = OM_BX(OV_BAR);
    int hh = OM_OV_VIS * OM_OV_ROW_H, ty0 = bar->y0 + hh * top / nrows, th = hh * OM_OV_VIS / nrows;
    if (nrows > OM_OV_VIS)
        om_v1_quad(bar->x1 + dx, ty0, bar->x1 + dx, ty0 + th - 1, Sty.dim, 0, OM_V1_CLIP, Z_OVBAR);
}

/* drawn only while the game panel is open; cold (size over speed, a handful of rows) */
static int dec3(char *o, unsigned v)
{   /* v (0..999) in decimal, NUL-terminated: its length */
    char t[4];
    int n = 0, i;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 3);
    for (i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
    return n;
}

static __attribute__((cold, noinline, optimize("Os"))) void draw_game(const om_overlay *o, const om_ovgame *pg, int dx, uint16_t vis,
                                                      uint32_t fps, int marquee, int cx0, int cx1)
{
    /* the per-game panel: the game's cover at the left, its rows beside it. Manage saves and Cheats end in an arrow
     * (A opens them); Reset to defaults asks twice: armed, its row reads "Press A again". */
    const om_pergame *g = pg->g;
    const om_box *cb = OM_BX(OV_COVER);
    char sl[2], cv[24];
    uint32_t tex;
    int ridx, w, h, tall;
    if (!om_art_loading() && om_art_hero(&tex, &w, &h, &tall))     /* the list's cover, scaled into the panel */
        om_v1_scaled(cb->x0 + dx, cb->y0, cb->x1 + dx, tall ? cb->y1 : cb->y0 + (cb->x1 - cb->x0), tex, w, h, 0, 0,
                     OM_V1_CLIP, Z_OVBAR);
    else {                                                         /* no art: the list's NO ART card, without text */
        om_v1_quad(cb->x0 + dx, cb->y0, cb->x1 + dx, cb->y1, om_c555_rgb(0x78788C), 0, OM_V1_CLIP, Z_OVBAR);
        om_v1_quad(cb->x0 + 2 + dx, cb->y0 + 2, cb->x1 - 2 + dx, cb->y1 - 2, om_c555_rgb(0x282830), 0, OM_V1_CLIP,
                   Z_OVTXT);                                       /* in front of the edge: no equal-Z order */
    }
    s_bx0 = OM_BX(OV_GBAR)->x0;
    for (ridx = o->top; ridx < o->top + OM_OV_VIS && ridx < OM_OGR_COUNT; ridx++) {
        om_ut *lab = &s_glabel[GL_RESET];
        const char *value = NULL;
        int dim = 0, v = ridx - o->top;
        switch (ridx) {
        case OM_OGR_LAUNCH: lab = &s_glabel[GL_LAUNCH]; break;     /* row 0: A starts the game */
        case OM_OGR_LOADER: lab = &s_label[OM_OVR_LOADER]; value = pg->loader; break;
        /* 2026-11-25: Saves and Cheats show On or Off; with no choice of the game's own, the global value, dimmed */
        case OM_OGR_SAVES:  lab = &s_glabel[GL_SAVES];
                            dim = g->saves != OM_PG_ON && g->saves != OM_PG_OFF;
                            value = k_pg3[dim ? ((o->defs & OM_OVD_SAVES) ? OM_PG_ON : OM_PG_OFF) : g->saves]; break;
        case OM_OGR_SLOT:   lab = &s_glabel[GL_SLOT];
                            if (g->slot == OM_PG_SLOT_ASK) value = "Ask";
                            else { sl[0] = (char)('0' + (g->slot <= 9 ? g->slot : 0)); sl[1] = 0; value = sl; }
                            break;
        case OM_OGR_SAVEMGR: lab = &s_glabel[GL_SAVEMGR]; value = ARROW; break;
        case OM_OGR_CHEATS: lab = &s_glabel[GL_CHEATS];             /* A opens the list (LAUNCHER.BIN) */
                            dim = g->cheats != OM_PG_ON && g->cheats != OM_PG_OFF;
                            if (pg->cheat_jhl) { strcpy(cv, "Needs CWX loader"); dim = 0; }
                            else if (pg->ch_ok && !pg->ch_n) strcpy(cv, "None");
                            else if (!(dim ? (o->defs & OM_OVD_CHEATS) != 0 : g->cheats == OM_PG_ON)) strcpy(cv, "Off");
                            else if (!pg->ch_ok) strcpy(cv, "On");
                            else {                                  /* "<n> of <m> on" */
                                char *q = cv;
                                q += dec3(q, pg->ch_on);
                                strcpy(q, " of ");
                                q += dec3(q + 4, pg->ch_n) + 4;
                                strcpy(q, " on");
                            }
                            strcat(cv, " " ARROW);
                            value = cv;
                            break;
        /* "Default" shows what it resolves to, dimmed */
        case OM_OGR_IGR:    lab = &s_label[OM_OVR_IGR];
                            dim = g->igr != OM_PG_ON && g->igr != OM_PG_OFF;
                            value = k_pg3[dim ? (pg->igr_def ? OM_PG_ON : OM_PG_OFF) : g->igr]; break;
        /* static (never edits); a real value so a long note gets the value marquee */
        case OM_OGR_PAD:    lab = &s_glabel[GL_PAD]; value = pg->pad_note ? pg->pad_note : k_pg3[0]; break;
        default:                                               /* OM_OGR_RESET */
                            if (o->rcur) {                     /* the row's value slot holds the question */
                                lab = &s_value[v];
                                om_ut_text(lab, OM_FACE_SMALL, "Press A again", 0);
                            }
                            break;
        }
        if (!g->id[0] && ridx != OM_OGR_LAUNCH) dim = 1;   /* no game id: Launch alone applies */
        row_d(v, o->row == ridx, lab, value, dx, vis, fps, marquee, cx0, cx1, dim);
    }
    thumb(o->top, OM_OGR_COUNT, dx);
}

/* Os like draw_game: per frame only while a panel is open, and it only sequences the rows (row_d, the per-row work,
 * stays -O2) */
__attribute__((optimize("Os")))
void om_ovdraw(const om_overlay *o, uint8_t kind, const om_settings *s, const char *theme_name, const char *genre_name,
               const om_ovgame *pg, int dx, uint16_t vis, uint32_t frame, uint32_t fps, int marquee)
{
    const om_box *ov = OM_BX(OVERLAY), *hb = OM_BX(OV_HINT);
    int cx0 = ov->x0 + dx, cx1 = ov->x1 + dx > 319 ? 319 : ov->x1 + dx, i, tb = OM_OV_TITLE_MID + om_face_title.mid;
    uint16_t frame_col = om_ui_mix((uint16_t)(om_v2_bg_at(250, 120) | 0x8000u), 0xFFFF, 60);
    om_ut *h0, *h1;
    (void)frame;
    if (cx0 > 319)
        return;
    s_bx0 = OM_BX(OV_BAR)->x0;
    om_v1_clip(cx0, ov->y0, cx1, ov->y1, Z_OVCLIP);
    /* the panel: an opaque body over the dimmed menu, in a 1 px light frame (the cover card's) */
    om_v1_quad(ov->x0 + 1 + dx, ov->y0 + 1, ov->x1 - 1 + dx, ov->y1 - 1, Sty.ov_eff, 0, OM_V1_CLIP, Z_OVFRAME);
    om_v1_quad(ov->x0 + dx, ov->y0, ov->x1 + dx, ov->y0, frame_col, 0, OM_V1_CLIP, Z_OVFRAME);
    om_v1_quad(ov->x0 + dx, ov->y1, ov->x1 + dx, ov->y1, frame_col, 0, OM_V1_CLIP, Z_OVFRAME);
    om_v1_quad(ov->x0 + dx, ov->y0, ov->x0 + dx, ov->y1, frame_col, 0, OM_V1_CLIP, Z_OVFRAME);
    om_v1_quad(ov->x1 + dx, ov->y0, ov->x1 + dx, ov->y1, frame_col, 0, OM_V1_CLIP, Z_OVFRAME);
    {
        om_ut *ti = &s_title[kind == OM_OV_GAME ? 3 : kind != OM_OV_SETTINGS ? 1 : o->page == OM_OVP_CONTROLS ? 2 : 0];
        om_ut_colors(ti, Sty.txt, Sty.ov_eff, vis);
        om_ut_draw_base(ti, OM_BX(OV_TITLE)->x0 + dx, tb, OM_V1_CLIP, Z_OVTXT);
        if (kind == OM_OV_GAME) {                                  /* the game's name, dimmed, after the title */
            om_ut_colors(&s_gtitle, Sty.dim, Sty.ov_eff, vis);
            om_ut_draw_base(&s_gtitle, OM_BX(OV_TITLE)->x0 + ti->w + 6 + dx, tb, OM_V1_CLIP, Z_OVTXT);
        }
    }
    h0 = &s_hint[0];
    h1 = &s_hint[1];
    if (kind == OM_OV_GAME) {
        if (pg) draw_game(o, pg, dx, vis, fps, marquee, cx0, cx1);   /* folders.c always passes the data */
    } else if (kind == OM_OV_SETTINGS && o->page == OM_OVP_CONTROLS) {
        /* the Controls page -- each action's mapped letter, then "Reset all" ("Done" when the map is the
         * default already). Same scrolling window and thumb as the main page. */
        om_remap d;
        int ridx, isdef;
        om_remap_default(&d);
        isdef = !memcmp(&d, &s->pad, sizeof d);
        for (ridx = o->top; ridx < o->top + OM_OV_VIS && ridx < OM_OVC_COUNT; ridx++) {
            char lt[2];
            lt[0] = om_btn_letter(ridx < OM_OVC_RESET ? s->pad.btn[om_ovc_act((uint16_t)ridx)] : 0);
            lt[1] = 0;
            row(ridx - o->top, o->row == ridx, &s_clabel[ridx], ridx < OM_OVC_RESET ? lt : isdef ? "Done" : ARROW, dx,
                vis, fps, marquee, cx0, cx1);
        }
        thumb(o->top, OM_OVC_COUNT, dx);
    } else if (kind == OM_OV_SETTINGS) {
        /* the approved wording and capitalisation (the motion preview) */
        static const char *const k_anim[3] = { "Full", "Reduced", "Off" };
        char tn[32], mv[8] = "Vol ", sv[8] = "5 min";
        uint32_t k;
        uint8_t v = s->music_vol >= 1 && s->music_vol <= 10 ? s->music_vol : 10;
        int ridx;
        for (k = 0; theme_name[k] && k + 1 < sizeof tn; k++) tn[k] = theme_name[k];
        tn[k] = 0;
        if (tn[0] >= 'a' && tn[0] <= 'z') tn[0] = (char)(tn[0] - 32);   /* "saturn" -> "Saturn" */
        if (v == 10) { mv[4] = '1'; mv[5] = '0'; mv[6] = 0; } else { mv[4] = (char)('0' + v); mv[5] = 0; }
        /* a scrolling list: only the rows o->top..o->top+OM_OV_VIS-1 are drawn, at visible position ridx - o->top */
        for (ridx = o->top; ridx < o->top + OM_OV_VIS && ridx < OM_OVR_COUNT; ridx++) {
            const char *value;
            om_ut *lab = &s_label[ridx];
            switch (ridx) {
            case OM_OVR_THEME: value = tn; break;
            case OM_OVR_CLOCK: value = s->clock24 ? "24h" : "12h"; break;
            case OM_OVR_MUSIC: value = s->music ? mv : "Off"; break;         /* switch + volume, one row */
            case OM_OVR_SFX:   value = s->sfx ? "On" : "Off"; break;
            case OM_OVR_ANIM:  value = k_anim[s->anim < 3 ? s->anim : 0]; break;
            case OM_OVR_SAFE:  value = om_safe_label(s->overscan); break;
            case OM_OVR_LOADER: value = om_loader_label(s->loader); break;
            case OM_OVR_RESELECT: value = s->reselect ? "Select" : "Off"; break;   /* "Start on Last Game" */
            case OM_OVR_SAVER:  if (!s->saver_min) value = "Off";
                                else { sv[0] = om_saver_key(s->saver_min)[0]; value = sv; }
                                break;
            case OM_OVR_IGR:    value = s->igr ? "On" : "Off"; break;         /* M6 Task 20a */
            case OM_OVR_SAVES:  value = s->saves ? "On" : "Off"; lab = &s_glabel[GL_SAVES]; break;
            case OM_OVR_SAVEMGR: value = ARROW; break;            /* its own, wider label (not the panel's) */
            default:            value = ARROW; break;              /* Controls, About: open a page */
            }
            row(ridx - o->top, o->row == ridx, lab, value, dx, vis, fps, marquee, cx0, cx1);
        }
        thumb(o->top, OM_OVR_COUNT, dx);
    } else {
        static const char L[5] = { 'J', 'T', 'U', 'E', 'B' };     /* GAMES.DB region bits 0..4 */
        const om_box *bar = OM_BX(OV_BAR), *rf = OM_BX(OV_RFLAG), *rn = OM_BX(OV_RNAME);
        row(0, o->row == 0, &s_flabel, o->genre < 0 ? "All" : genre_name, dx, vis, fps, marquee, cx0, cx1);
        for (i = 0; i < 5; i++) {                                  /* flag + name; an off region is dimmed 50 % */
            const om_flag *fl = om_flag_for(L[i]);
            int y = OM_OV_RROW0 + OM_OV_RROW_H * i, cur = o->row == 1 && o->rcur == i;
            int shown = !s->filter_region || ((s->filter_region >> i) & 1);
            uint16_t lvl = (uint16_t)(shown ? vis : vis / 2), bgc;
            if (cur) {
                om_v1_quad(bar->x0 + dx, y, bar->x1 + dx, y + OM_OV_RROW_H - 1, s_bar_base,
                           OM_VRAM_GOUR + 8u * OM_G_OVBAR, OM_V1_CLIP, Z_OVBAR);
                om_v1_quad(bar->x0 + dx, y, bar->x0 + 1 + dx, y + OM_OV_RROW_H - 1, Sty.acc2, 0, OM_V1_CLIP, Z_OVBAR);
            }
            bgc = cur ? bar_at(rf->x0) : Sty.ov_eff;
            om_ut_flag_colors(&s_rflag[i], fl, bgc, lvl);
            om_ut_draw(&s_rflag[i], rf->x1 + 1 - OM_FLAG_TEX_W + dx, y + 3, OM_V1_CLIP, Z_OVTXT);
            om_ut_colors(&s_rname[i], cur ? Sty.txt : Sty.row, cur ? bar_at(rn->x0) : Sty.ov_eff, lvl);
            om_ut_draw_base(&s_rname[i], rn->x0 + dx, y + 7 + om_face_small.mid, OM_V1_CLIP, Z_OVTXT);
        }
        om_ut_text(&s_hint[2], OM_FACE_SMALL, s_htxt_filter, 0);   /* the list may have borrowed it */
        h0 = &s_hint[2];
        h1 = &s_hint[3];
    }
    {   /* one hint line: the pair side by side (the second starts with its own separator dot) */
        int base = OM_OV_HINT_MID + om_face_small.mid;
        om_ut_colors(h0, Sty.dim, Sty.ov_eff, vis);
        om_ut_colors(h1, Sty.dim, Sty.ov_eff, vis);
        om_ut_draw_base(h0, hb->x0 + dx, base, OM_V1_CLIP, Z_OVTXT);
        om_ut_draw_base(h1, hb->x0 + h0->w + 4 + dx, base, OM_V1_CLIP, Z_OVTXT);
    }
}
