/* menu/views/folders.c -- the Folders view (spec §7.4), the only view, on om_browse: Favourites, Recently Played, A-Z,
 * Genre and Region above the card tree; X toggles a favourite (FAVS.TXT written at once, §7.6); RECENT.TXT is written
 * after LAUNCHER.BIN loads and before the hand-off (§7.1). Pad through SGL (HWF-14). A enters/launches, B goes up,
 * L/R page; A+B+C+Start resets the console (HWF-21, HWF-58). Y cycles the sort, Z opens the filter overlay, Start the
 * settings overlay (menu/src/overlay.c, §7.5); changes save to SETTINGS.INI on close. The SMPC RTC clock (HWF-27).
 *
 * drawn like the approved mockup (.superpowers/sdd/2026-11-18-m3-library-themes-music/mockup_render.py)
 * with every position from the one layout table (menu/src/layout.h). Text is proportional anti-aliased VDP1 sprites
 * (menu/ui/uitext.c) whose LUTs are pre-blended against what lies under them; panels are pixel-exact NBG2 rectangles with
 * colour calculation (decision D1); the cursor bar, hairline, frames and scrollbar are opaque VDP1 Gouraud quads,
 * pre-blended the same way, because VDP1 cannot blend with VDP2 (HWF-32). NBG0 text is left to the boot status and
 * fatal screens (and the fake build's diagnostics). Motion: every state change is immediate and only
 * the visuals ease toward it (menu/src/anim.c), 60 fps locked to slSynch, scaled by anim= (full / reduced / off).
 * BSD-3-Clause. */
#include <string.h>
#include "anim.h"
#include "art.h"
#include "audio.h"
#include "artsched.h"
#include "browse.h"
#include "clock.h"
#include "diag.h"
#include "fade.h"
#include "boottime.h"
#include "interstitial.h"
#include "fatal.h"
#include "folders.h"
#include "fwcheck.h"
#include "idle.h"
#include "saverview.h"
#include "info.h"
#include "launch.h"
#include "layout.h"
#include "logo.h"
#include "libsort.h"
#include "loader.h"
#include "loading.h"
#include "cheats.h"
#include "discset.h"
#include "marquee.h"
#include "msgfmt.h"
#include "omlayout.h"
#include "overlay.h"
#include "ovdraw.h"
#include "pad.h"
#include "padkind.h"
#include "persist.h"
#include "popup.h"
#include "hang.h"
#include "pergame.h"
#include "pgsplan.h"
#include "savegen.h"
#include "impscan.h"
#include "match.h"
#include "rtc.h"
#include "safearea.h"
#include "satiator.h"
#include "satfs.h"
#include "sd.h"
#include "slotpick.h"
#include "text.h"
#include "themes.h"
#include "title.h"
#include "uiline.h"
#include "uitext.h"
#include "uiview.h"
#include "v1.h"
#include "vdp2theme.h"
#ifdef OMSAT_FAKECARD
#include "fakepad.h"
#include "fakesat.h"
#endif

/* code that runs once per event (a launch, a key press, a list load), never per frame,
 * is optimized for size, out of line (folders.c is otherwise -O2; docs/ledger.md) */
#define OM_COLD __attribute__((cold, noinline, optimize("Os")))
#define NSLOT 12                 /* rows top-2 .. top+9: 8 visible, one sliding in at each end, one prefetched */
#define LIST_SLIDE 60            /* px a new list slides in from on folder enter / back */
#define OM_RASTER_BUDGET_US 2500 /* review-13a fix-plan 2: rasterisation time a frame (the first is always allowed) */
#define OM_RASTER_BUDGET_OV_US 1000 /* while the overlay slides, its NBG2 cells come first (N1) */

static char s_status[OM_TOAST_MSG], s_status_next[OM_TOAST_MSG];
/* the body may not reach LOADBG.BIN's LW region, which the loading screen is still
 * playing from while the body is read (a larger LAUNCHER.BIN on the card fails with OM_E_RANGE, launch.c).
 * The 4 KB header goes in the art canvas, lent for the launch (hdr set by launch_bufs). */
static om_launch_bufs s_lb = { NULL, (uint8_t *)OM_CT(OM_LAUNCHER_LOAD), OM_LAUNCHER_BODY_CAP,
                               (om_handoff *)OM_CT(OM_LW_HANDOFF) };   /* HWF-12 */
typedef char lhdr_fits[OM_LNCH_HDR <= OM_ART_LEND_BYTES ? 1 : -1];
static const om_launch_bufs *launch_bufs(void)
{
    s_lb.hdr = om_art_lend();
    return &s_lb;
}
static om_sortctx s_sort;
static om_browse s_b;
static om_persist s_ps;
extern om_retblk g_om_ret;                /* main.c: the return from a tool (handoff.h), taken at the start */
extern int g_om_ret_ok;
static om_overlay s_ov;
static om_clock s_clock;

/* --- view state ------------------------------------------------------------------------------------------------ */
/* Row slots always hold the plain (item-face, fade-cut) look, keyed by item number mod NSLOT; a slot is rewritten only
 * for an item two or more rows off screen, so VDP1 never reads a row texture while it changes. The
 * selected row has its own double-buffered slot (fix-plan 3/4): first its box-width look, then -- during the 1 s hold,
 * never while a direction is held -- the whole title for the marquee. */
typedef struct { om_ut title, flag; int32_t item; uint8_t has_flag; } rowslot;
static rowslot s_row[NSLOT];
static om_ut2 s_sel, s_crumb, s_dtitle, s_dmeta, s_reason;
static int32_t s_sel_item = -1;
static om_ut s_brand1, s_clk, s_count, s_noart, s_rc, s_selflag, s_pillstar;
static om_ut s_hint[OM_NHINTS], s_badge[OM_NHINTS], s_folder[2], s_star[2];
static uint32_t s_status_serial;
static uint8_t s_status_auto;
static int16_t s_badge_x[OM_NHINTS], s_label_x[OM_NHINTS];
static om_style Sty;
static om_toast s_toast;
static om_tween s_cur_y, s_scroll, s_list_dx, s_list_fade, s_cover_in, s_ov_slide, s_star_t, s_thumb;
static uint32_t s_fps, s_frame, s_mq_sel, s_mq_dt, s_mq_dm, s_mq_crumb, s_mq_reason;
static uint32_t s_content_serial;                  /* bumped when favourites or the card data under the view change */
/* Review-13a I6: the WHOLE loop iteration timed in phases (FRT, corrected for its wrap by the V-blank count). A
 * missed frame (more than one V-blank between two slSynch returns) is charged to the iteration's costliest phase. */
enum { PH_INPUT, PH_THEME, PH_SAVE, PH_INFO, PH_ART, PH_RASTER, PH_DRAW, PH_N };
static uint32_t s_ph[PH_N], s_ph_max[PH_N], s_miss[PH_N];
typedef struct { uint16_t frc; uint32_t vb; } ph_mark;
static ph_mark ph_now(void) { ph_mark m; m.frc = om_ut_frc(); m.vb = om_v2_vblanks(); return m; }
static uint32_t ph_since(ph_mark m)
{   /* FRT us, plus whole FRT wraps until it is at least the V-blanks seen minus one frame */
    uint32_t us = om_ut_frc_us((uint16_t)(om_ut_frc() - m.frc)), wrap = om_ut_frc_us(0xFFFFu) + 1u;
    uint32_t vb = om_v2_vblanks() - m.vb, floor_us = vb > 1u ? (vb - 1u) * 16683u : 0u;
    while (us < floor_us) us += wrap;
    return us;
}
static void ph_add(int ph, ph_mark m) { s_ph[ph] += ph_since(m); }
static uint32_t s_art_serial = 0xFFFFFFFFu, s_cost_acc, s_cost_max, s_v1_max;
static uint8_t s_v1_named;
static int32_t s_star_item = -1;
static uint8_t s_star_add, s_ov_kind_shown, s_held;
static uint8_t s_safe_flush;     /* the Safe area changed: NBG2 redraws every panel cell next frame */
static uint16_t s_last_top, s_last_cur;
static char s_dt_text[160], s_dm_text[200], s_crumb_text[200], s_reason_text[OM_TOAST_MSG];

void om_folders_status(const char *msg)
{   /* every post gets a new serial, so the same rc error twice is shown twice */
    strncpy(s_status, msg, sizeof s_status - 1);
    s_status[sizeof s_status - 1] = 0;
    s_status_serial++;
    s_status_auto = 0;
}

/* a read-only getter so the diagnostics screen (menu/views/diag.c) can show "frame misses since
 * boot" without this view handing out its own s_miss[] array (which is sized by the PH_* enum above, a detail
 * diag.c has no reason to know). s_miss[] is never reset after the fake-HUD/s_vb_restart resync (frame_out just
 * stops counting a gap once, it does not zero the running total), so this really is "since boot". */
void om_folders_miss_total(uint32_t *total, char *worst_tag, uint32_t *worst_count)
{
    static const char tag[PH_N] = { 'I', 'T', 'S', 'N', 'A', 'R', 'D' };
    uint32_t i, t = 0, w = 0, wi = 0;
    for (i = 0; i < PH_N; i++) {
        t += s_miss[i];
        if (s_miss[i] > w) { w = s_miss[i]; wi = i; }
    }
    *total = t;
    *worst_tag = tag[wi];
    *worst_count = w;
}

static void status_auto(const char *msg)
{   /* a message not caused by input (art, clock) never replaces a visible rc error -- nor (N2) an
     * input-driven rc error posted earlier in the same iteration that the toast has not been offered yet */
    if (s_status_serial != s_toast.serial && !s_status_auto && om_msg_is_error(s_status))
        return;
    om_folders_status(msg);
    s_status_auto = 1;
}

void om_folders_status_soft(const char *msg)
{
    if ((s_status_serial != s_toast.serial || om_msg_is_error(s_status)) && s_status[0]) {
        if (!s_status_next[0]) {
            strncpy(s_status_next, msg, sizeof s_status_next - 1);
            s_status_next[sizeof s_status_next - 1] = 0;
        }
        return;
    }
    status_auto(msg);
}

static void sd_quiet(void)
{   /* review-sound I2 ruling: at most 2 files open -- before a synchronous SETTINGS/FAVS/RECENT write the music file is
     * closed (reopened and re-sought by om_audio_tick) and an art job in flight is canceled (re-requested next frame) */
    om_audio_sd_pause();
    om_art_invalidate();                                   /* also an idle prefetch's file (the cache stays) */
}

/* --- colours ------------------------------------------------------------------------------------------------------ */
uint16_t om_ui_mix(uint16_t a, uint16_t b, uint32_t a255)
{
    return (uint16_t)(om_c555_mix((uint16_t)(a | 0x8000u), (uint16_t)(b | 0x8000u), (a255 * 256u + 127u) / 255u) | 0x8000u);
}

static uint16_t eff(uint16_t panel, int x, int y)
{   /* a panel pixel as the screen shows it: NBG2 over NBG1 at the theme ratio (HWF-31, D1) */
    return (uint16_t)(om_c555_cc(panel, om_v2_bg_at(x, y), Sty.ratio) | 0x8000u);
}

static void gouraud_lr(int slot, uint16_t l, uint16_t r, uint16_t *base)
{   /* a horizontal Gouraud fade l -> r: COLR = the midpoint, the table +-half the difference (HWF-43) */
    int ch[3][2], i;
    uint16_t m = 0x8000u, gl, gr;
    for (i = 0; i < 3; i++) {
        int a = (l >> (5 * i)) & 31, b = (r >> (5 * i)) & 31, mid = (a + b + 1) / 2;
        m = (uint16_t)(m | (mid << (5 * i)));
        ch[i][0] = a - mid; ch[i][1] = b - mid;
    }
    gl = om_v1_gour_delta(ch[0][0], ch[1][0], ch[2][0]);
    gr = om_v1_gour_delta(ch[0][1], ch[1][1], ch[2][1]);
    om_v1_gouraud(OM_VRAM_GOUR + 8u * (uint32_t)slot, gl, gr, gr, gl);
    *base = m;
}

static uint16_t s_bar_base, s_hair_base[2];
/* the start-up interstitial's sequenced fade-in (interstitial.h), 0..256, per frame: the wordmark, clock and
 * hairline (s_word), then the panels (NBG2's ratio, interstitial.c), then everything else (s_in). 256 = drawn as always. */
static uint16_t s_word = 256, s_in = 256;
/* An open panel dims the menu behind it (the approved mockup's 55 % veil): its VDP2 layers through colour offset A, per
 * channel from the backdrop's own colour (an additive offset that darkens the theme's background by about OV_VEIL/256
 * and keeps its hue), its sprites through the same fade as the start-up's (s_word, s_in, to OV_DIM/256), the header
 * logo (blended on the backdrop) through a Gouraud table (one grey step, the offset's mean). s_in_full: s_in before
 * that (the toast keeps it). s_veil: the offset now wanted (R, G, B; 0 = none). */
#define OV_DIM    120
#define OV_VEIL   140
#define VEIL_MASK (NBG1ON | NBG2ON | BACKON)
static uint16_t s_in_full = 256;
static int16_t s_veil[3];
static uint16_t s_veil_op = 0xFFFFu;    /* the panel's opening (0..256) s_veil was made for; style_update forgets it */

static OM_COLD void veil_update(uint32_t op)
{   /* only while the panel slides, or after a theme change: the offset from the backdrop's colour, and the logo's
     * Gouraud table with it */
    uint16_t bg = op ? om_v2_bg_at(160, 112) : 0, g;
    int k;
    for (k = 0; k < 3; k++) s_veil[k] = (int16_t)-(int)((((uint32_t)bg >> (5 * k)) & 31u) * 8u * OV_VEIL * op >> 16);
    k = (s_veil[0] + s_veil[1] + s_veil[2]) / 24;             /* the logo: one grey step (per channel tints it) */
    g = om_v1_gour_delta(k, k, k);
    om_v1_gouraud(OM_VRAM_GOUR + 8u * OM_G_DIM, g, g, g, g);
    s_veil_op = (uint16_t)op;
}
static uint32_t a255_of(uint16_t f256) { return (f256 * 255u) >> 8; }
static uint16_t list_fade(void)
{
    uint16_t f = (uint16_t)om_tween_value(&s_list_fade);
    return f < s_in ? f : s_in;
}

static void hair_update(uint16_t f256)
{   /* the hairline: accent at 200 * (1 - |x - 110| / 200) / 255 over the background at y 33 (f256: Task 13d's fade) */
    uint16_t hair[3];
    hair[0] = om_ui_mix(om_v2_bg_at(OM_BX(HAIR)->x0, OM_BX(HAIR)->y0), Sty.acc, (100u * f256) >> 8);
    hair[1] = om_ui_mix(om_v2_bg_at(OM_HAIR_PEAK, OM_BX(HAIR)->y0), Sty.acc, (200u * f256) >> 8);
    hair[2] = (uint16_t)(om_v2_bg_at(OM_BX(HAIR)->x1, OM_BX(HAIR)->y0) | 0x8000u);
    gouraud_lr(OM_G_HAIR_L, hair[0], hair[1], &s_hair_base[0]);
    gouraud_lr(OM_G_HAIR_R, hair[1], hair[2], &s_hair_base[1]);
}

static void bar_update(uint16_t f256)
{   /* the cursor bar's Gouraud quad toward the list panel (Task 13d's fade); 256 = as style_update set it */
    gouraud_lr(OM_G_BAR, (uint16_t)(om_c555_mix(Sty.list_eff, Sty.bar_l, f256) | 0x8000u),
               (uint16_t)(om_c555_mix(Sty.list_eff, Sty.bar_r, f256) | 0x8000u), &s_bar_base);
}

static void style_update(void)
{
    const om_theme *t = om_v2_theme();
    uint16_t panel = (uint16_t)(t->panel | 0x8000u), txt = (uint16_t)(t->text | 0x8000u);
    om_v2_set_safe(&g_om_safe);                                   /* M6 Task 5: sample the backdrop where it shows */
    Sty.panel = panel;
    s_veil_op = 0xFFFFu;                                          /* a new backdrop: the veil follows it */
    Sty.ratio = t->panel_alpha >= 100 ? 0 : om_cc_ratio(t->panel_alpha);
    Sty.txt = txt;
    Sty.acc = (uint16_t)(t->cursor | 0x8000u);
    Sty.acc2 = (uint16_t)(t->text_hi | 0x8000u);
    Sty.row = om_ui_mix(txt, panel, 38);                          /* mockup (198,206,222) */
    Sty.dim = om_ui_mix(txt, panel, 97);                          /* mockup (140,152,175) */
    Sty.faint = om_ui_mix(txt, panel, 158);                       /* mockup (85,96,120) */
    Sty.gold = om_c555_rgb(0xFFD65A);                             /* the favourite star (brief) */
    Sty.folder = om_c555_rgb(0xF0BE5A);
    Sty.err = om_c555_rgb(0xFFA08C);
    Sty.list_eff = t->panel_alpha >= 100 ? panel : eff(panel, 95, 107);
    Sty.det_eff = t->panel_alpha >= 100 ? panel : eff(panel, 95, 185);
    Sty.ov_eff = t->panel_alpha >= 100 ? panel : eff(panel, 250, 120);
    Sty.bar_l = om_ui_mix(Sty.list_eff, Sty.acc, 235);                /* mockup: 235 * (1 - 0.55 t) over the panel */
    Sty.bar_r = om_ui_mix(Sty.list_eff, Sty.acc, 106);
    Sty.ovbar_l = om_ui_mix(Sty.ov_eff, Sty.acc, 235);
    Sty.ovbar_r = om_ui_mix(Sty.ov_eff, Sty.acc, 106);
    bar_update(s_in);
    gouraud_lr(OM_G_OVBAR, Sty.ovbar_l, Sty.ovbar_r, &Sty.ovbar_base);
    hair_update(256);
    om_art_jp_lut(Sty.txt, Sty.det_eff);
    om_ovdraw_style(&Sty);
    s_content_serial++;
    {   /* every LUT is rewritten with the new colours on the next draw */
        int i;
        for (i = 0; i < NSLOT; i++) { s_row[i].title.lut_ok = s_row[i].flag.lut_ok = 0; s_row[i].item = -1; }
    s_sel_item = -1;
    }
    om_v2_set_safe(NULL);
}

/* --- data helpers (unchanged behaviour) ------------------------------------------------------------------------- */
static int game_of(const om_card *c, uint16_t idx, om_lib_entry *e, om_game *g)
{
    om_libidx_entry(&c->lib, idx, e);
    return !c->db_rc && e->key[0] && om_gamesdb_find(&c->db, e->key, g);
}

static uint16_t genre_id(const om_card *c, const char *name)
{
    uint16_t i;
    if (c->db_rc || !name[0])
        return OM_FILTER_ANY;
    for (i = 0; i < s_sort.ngenre; i++) {
        const char *g = om_gamesdb_genre(&c->db, (uint8_t)i);
        if (g && !strcmp(g, name))
            return i;
    }
    return OM_FILTER_ANY;            /* a name no longer in GAMES.DB filters nothing */
}

static void rows_invalidate(void)
{   /* the list's contents changed under the same item numbers: every row slot is rebuilt (they are keyed by number) */
    uint32_t i;
    for (i = 0; i < NSLOT; i++)
        s_row[i].item = -1;
    s_sel_item = -1;
}

static void apply_view(const om_card *c)
{
    om_browse_set(&s_b, s_ps.set.sort, genre_id(c, s_ps.set.filter_genre), s_ps.set.filter_region);
    rows_invalidate();
}

static uint32_t visible_n(const om_plist *p)
{   /* the count a virtual root opens to, i.e. under the active filter (browse.c rebuild) */
    uint32_t i, n = 0;
    for (i = 0; i < p->n; i++)
        if (p->idx[i] < s_sort.n && om_entry_passes(&s_sort, p->idx[i], s_b.filter_genre, s_b.filter_region))
            n++;
    return n;
}

static void row_label(const om_card *c, const om_item *it, char *out, uint32_t cap)
{   /* a game's row: its folder or file name, never a title (title.h om_entry_raw); the info line shows the title */
    om_lib_entry e;
    if (it->kind != OM_IT_GAME) {
        om_browse_label(&s_b, it, out, cap);
        return;
    }
    om_libidx_entry(&c->lib, it->idx, &e);
    om_title_raw(&c->lib, &e, out, cap);
}

static int is_fav(const om_item *it) { return it->kind == OM_IT_GAME && om_plist_find(&s_ps.favs, it->idx) >= 0; }

/* --- layout helpers --------------------------------------------------------------------------------------------- */
static int base_of(int mid, int face) { return mid + om_face_get(face)->mid; }
static int anim_marquee(int selected_or_error)
{   /* addendum 3: full marquees everything that overflows; reduced only the selected row and errors; off nothing */
    return s_ps.set.anim == OM_ANIM_FULL || (s_ps.set.anim == OM_ANIM_REDUCED && selected_or_error);
}
static uint16_t frames(uint16_t ms) { return om_anim_frames(ms, s_ps.set.anim, s_held, s_fps); }

/* --- the list ------------------------------------------------------------------------------------------------- */
static rowslot *slot_of(uint32_t item) { return &s_row[item % NSLOT]; }

static int row_label_of(const om_card *c, uint32_t i, char *buf, uint32_t cap, int *game)
{
    uint16_t n;
    const om_item *it = &om_browse_items(&s_b, &n)[i];
    *game = it->kind == OM_IT_GAME;
    row_label(c, it, buf, cap);
    return *game ? OM_B_ROW_TITLE : OM_B_ROW_FTITLE;
}

static const om_flag *flag_of(const om_card *c, const om_item *it)
{   /* one flag: the game's primary area (addendum 6) -- the Region sort's group (2026-11-20 hardware report) */
    om_lib_entry e;
    om_game g;
    if (it->kind != OM_IT_GAME)
        return 0;
    return om_flag_for(om_primary_area(&e, game_of(c, it->idx, &e, &g) ? &g : NULL, om_info_find(it->idx)));
}

static int row_prepare(const om_card *c, uint32_t i, uint16_t fade)
{   /* the plain look of item i in its slot; 0 when the frame's raster budget is spent */
    uint16_t n;
    const om_item *it = &om_browse_items(&s_b, &n)[i];
    rowslot *r = slot_of(i);
    char buf[160];
    int game, box = row_label_of(c, i, buf, sizeof buf, &game);
    const om_flag *fl;
    if (om_ut_text(&r->title, OM_FACE_ITEM, buf, om_box_w(box)) < 0)
        return 0;
    r->title.lut_ok = 0;
    om_ut_colors(&r->title, game ? Sty.row : Sty.txt, Sty.list_eff, fade);
    r->has_flag = 0;
    if ((fl = flag_of(c, it)) != 0) {
        om_ut_flag(&r->flag, fl);
        om_ut_flag_colors(&r->flag, fl, Sty.list_eff, fade);
        r->has_flag = 1;
    }
    r->item = (int32_t)i;
    return 1;
}

static uint16_t bar_at(int x) { return om_c555_mix(Sty.bar_l, Sty.bar_r, (uint32_t)(x - OM_BX(BAR)->x0) * 256u / 172u); }

static void prepare_selected(const om_card *c, int full)
{   /* fix-plan 3: the box-width look first (one raster per cursor move); full = the whole title for the marquee */
    char buf[160];
    int game, box;
    if (!om_browse_selected(&s_b))
        return;
    box = row_label_of(c, s_b.cur, buf, sizeof buf, &game);
    if ((int32_t)s_b.cur != s_sel_item) {
        if (om_ut2_text(&s_sel, OM_FACE_SEL, buf, om_box_w(box)) < 0)
            return;
        s_sel_item = (int32_t)s_b.cur;
        {
            uint16_t n;
            const om_flag *fl = flag_of(c, &om_browse_items(&s_b, &n)[s_b.cur]);
            s_selflag.key = 0;
            if (fl) {
                om_ut_flag(&s_selflag, fl);
                om_ut_flag_colors(&s_selflag, fl, bar_at(OM_BX(ROW_FLAG)->x0 + 8), 256);
            }
        }
    } else if (full && !s_held && anim_marquee(1) && om_ut2_front(&s_sel)->w < om_ut2_front(&s_sel)->full_w)
        om_ut2_text(&s_sel, OM_FACE_SEL, buf, -om_box_w(box));
    om_ut_colors(om_ut2_front(&s_sel), Sty.txt, bar_at(om_boxes[box].x0), s_in);
    if (s_in < 256 && s_selflag.key) {                             /* Task 13d's fade-in */
        uint16_t n;
        const om_flag *fl = flag_of(c, &om_browse_items(&s_b, &n)[s_b.cur]);
        if (fl) om_ut_flag_colors(&s_selflag, fl, bar_at(OM_BX(ROW_FLAG)->x0 + 8), s_in);
    }
}

static void prepare_rows(const om_card *c, int prefetch)
{   /* visible rows top-1..top+8; prefetch (idle budget only): top-2 and top+9 (fix-plan 5) */
    uint16_t n;
    int i, lo = (int)s_b.top - (prefetch ? 2 : 1), hi = (int)s_b.top + (int)OM_ROWS + (prefetch ? 1 : 0);
    uint16_t fade = list_fade();
    (void)om_browse_items(&s_b, &n);
    for (i = lo; i <= hi && om_ut_budget_left() > 0; i++)
        if (i >= 0 && i < n && slot_of((uint32_t)i)->item != i)
            row_prepare(c, (uint32_t)i, fade);
}

static void draw_list(void)
{   /* draws only: everything was rasterised by the prepare pass (frame_out) */
    uint16_t n;
    const om_item *items = om_browse_items(&s_b, &n);
    int32_t dx = om_tween_value(&s_list_dx), sc = om_tween_value(&s_scroll);
    uint16_t fade = list_fade();
    int y0 = OM_BX(ROWS)->y0, i;
    const om_box *L = OM_BX(LIST);
    if (!s_in)
        return;                                                    /* not faded in yet */
    /* the list's clip: the rows' band inside the panel (rows slide under its edges) */
    om_v1_clip(L->x0 + 1, OM_BX(ROWS)->y0, L->x1 - 1, OM_BX(ROWS)->y1, Z_LISTCLIP);
    if (n && om_browse_selected(&s_b)) {                           /* the cursor bar, gliding; 2 px bright edge */
        int y = om_tween_value(&s_cur_y);
        om_v1_quad(OM_BX(BAR)->x0 + dx, y, OM_BX(BAR)->x1 + dx, y + OM_ROW_H - 1, s_bar_base,
                   OM_VRAM_GOUR + 8u * OM_G_BAR, OM_V1_CLIP, Z_BAR);
        om_v1_quad(OM_BX(BAR)->x0 + dx, y, OM_BX(BAR)->x0 + 1 + dx, y + OM_ROW_H - 1,
                   s_in < 256 ? om_ui_mix(Sty.list_eff, Sty.acc2, a255_of(s_in)) : Sty.acc2, 0, OM_V1_CLIP, Z_BAR);
    }
    {   /* scrollbar: a 1 px track and a 2 px accent thumb that glides with the list */
        const om_box *sb = OM_BX(SCROLL);
        om_v1_quad(sb->x1, sb->y0, sb->x1, sb->y1, om_ui_mix(Sty.list_eff, 0xFFFF, (25u * s_in) >> 8), 0, 0, Z_SCROLL);
        if (n > OM_ROWS) {
            uint32_t len = (uint32_t)(sb->y1 - sb->y0 + 1) * OM_ROWS / n;
            int ty = om_tween_value(&s_thumb);
            if (len < 6) len = 6;
            om_v1_quad(sb->x0, ty, sb->x1, ty + (int)len - 1, om_ui_mix(Sty.list_eff, Sty.acc2, (200u * s_in) >> 8), 0,
                       0, Z_SCROLL);
        }
    }
    if (!n) {
        om_ut_text(&s_row[0].title, OM_FACE_ITEM, "(empty)", 0);   /* N3: rasterised once, kept by its key */
        om_ut_colors(&s_row[0].title, Sty.dim, Sty.list_eff, s_in);
        s_row[0].item = -1;
        om_ut_draw_base(&s_row[0].title, OM_BX(ROW_TITLE)->x0, y0 + 7 + om_face_item.mid, 0, Z_ROWS);
        return;
    }
    for (i = (int)s_b.top - 1; i <= (int)s_b.top + (int)OM_ROWS; i++) {
        const om_item *it;
        rowslot *r;
        int y, sel, game, usesel;
        if (i < 0 || i >= n)
            continue;
        y = y0 + (i - (int)s_b.top) * OM_ROW_H + sc;
        if (y + OM_ROW_H <= OM_BX(ROWS)->y0 || y > OM_BX(ROWS)->y1)
            continue;
        it = &items[i];
        r = slot_of((uint32_t)i);
        sel = i == s_b.cur;
        game = it->kind == OM_IT_GAME;
        usesel = sel && s_sel_item == i;
        if (r->item != i && !usesel)
            continue;                                              /* not rasterised yet: drawn from the next frame */
        if (r->item == i && (r->title.fade != fade || !r->title.lut_ok)) {   /* a fade or a theme: only the LUTs */
            uint16_t fg = r->title.fg, bg = r->title.bg;
            r->title.lut_ok = 0;
            om_ut_colors(&r->title, fg, bg, fade);
            if (r->has_flag) {
                const om_flag *fl = om_flag_for((char)r->flag.fg);
                if (fl) om_ut_flag_colors(&r->flag, fl, r->flag.bg, fade);
            }
        }
        if (!game) {                                               /* folder icon + name */
            om_ut *ic = &s_folder[sel];
            om_ut_draw(ic, OM_BX(ROW_ICON)->x0 + dx, y + 3, OM_V1_CLIP, Z_ROWS);
        } else {
            if (usesel ? s_selflag.key != 0 : (r->item == i && r->has_flag))   /* right-aligned, where the letter was */
                om_ut_draw(usesel ? &s_selflag : &r->flag, OM_BX(ROW_FLAG)->x1 + 1 - OM_FLAG_TEX_W + dx, y + 3,
                           OM_V1_CLIP, Z_ROWS);
            if (is_fav(it) || s_star_item == i) {                  /* the favourite star; pops on add, shrinks away */
                int32_t p = s_star_item == i ? om_tween_value(&s_star_t) : OM_Q;
                int32_t sc256 = 256;
                if (s_star_item == i && om_tween_active(&s_star_t))
                    sc256 = s_star_add ? (p < OM_Q / 2 ? 256 + (p * 154 >> 12) : 333 - ((p - OM_Q / 2) * 154 >> 12))
                                       : 256 - (p * 256 >> 12);
                else if (s_star_item == i && !s_star_add)
                    sc256 = 0;
                if (sc256 > 0) {
                    int cx = (OM_BX(ROW_STAR)->x0 + OM_BX(ROW_STAR)->x1 + 1) / 2 + dx, cy = y + 7;
                    int hs = (8 * sc256 + 255) / 512;
                    if (sc256 == 256)
                        om_ut_draw(&s_star[sel], cx - 4, cy - 4, OM_V1_CLIP, Z_ROWS);
                    else
                        om_ut_draw_scaled(&s_star[sel], cx - hs, cy - hs, cx + hs - 1, cy + hs - 1, OM_V1_CLIP, Z_ROWS);
                }
            }
        }
        {
            const om_box *tb = game ? OM_BX(ROW_TITLE) : OM_BX(ROW_FTITLE);
            int bw = om_box_w(game ? OM_B_ROW_TITLE : OM_B_ROW_FTITLE);
            const om_ut *tt = usesel ? om_ut2_front(&s_sel) : &r->title;
            int base = y + 7 + om_face_get(usesel ? OM_FACE_SEL : OM_FACE_ITEM)->mid;
            int cy0 = y > OM_BX(ROWS)->y0 ? y : OM_BX(ROWS)->y0;
            int cy1 = y + OM_ROW_H - 1 < OM_BX(ROWS)->y1 ? y + OM_ROW_H - 1 : OM_BX(ROWS)->y1;
            if (usesel && tt->w == tt->full_w && tt->full_w > bw && anim_marquee(1)) {
                int32_t per = om_marquee_period(tt->full_w, bw);  /* the selected row marquees in its title box */
                int32_t off = om_marquee_off(s_mq_sel, tt->full_w, bw, s_fps);
                om_ut_marquee(tt, tb->x0 + dx, tb->x0 + dx, tb->x1 + dx, cy0, cy1, base, off, per, Z_SEL);
            } else if (usesel)                                     /* never over the star / flag */
                om_ut_marquee(tt, tb->x0 + dx, tb->x0 + dx, tb->x1 + dx, cy0, cy1, base, 0, 0, Z_SEL);
            else
                om_ut_draw_base(tt, tb->x0 + dx, base, OM_V1_CLIP, Z_ROWS);
        }
    }
}

/* --- header, breadcrumb, details, cover, footer ---------------------------------------------------------------- */
static void draw_header(void)
{
    const om_box *b = OM_BX(BRAND), *ck = OM_BX(CLOCK);
    char t[8];
    uint16_t bg = (uint16_t)(om_v2_bg_at(80, OM_HDR_MID) | 0x8000u);
    int glide;
    om_v1_set_safe(NULL);                                          /* the interstitial draws at identity */
    om_v2_set_safe(NULL);
    glide = om_istl_draw_logo();                                   /* the logo glides in from the center */
    om_v1_set_safe(&g_om_safe);
    om_v2_set_safe(&g_om_safe);
    if (glide)
        ;
    else if (s_veil[0] | s_veil[1] | s_veil[2])                    /* dimmed with the backdrop it is blended on */
        om_v1_scaled(OM_BX(LOGO)->x0, OM_BX(LOGO)->y0, OM_BX(LOGO)->x0 + OM_LOGO_W - 1, OM_BX(LOGO)->y0 + OM_LOGO_H - 1,
                     OM_VRAM_LOGO, OM_LOGO_W, OM_LOGO_H, 0, OM_VRAM_GOUR + 8u * OM_G_DIM, 0, OM_Z_HEADER);
    else
        om_logo_draw();
    if (!s_word)
        return;                                                    /* the wordmark waits until the logo has cleared it */
    om_v1_quad(OM_BX(HAIR)->x0, OM_BX(HAIR)->y0, OM_HAIR_PEAK, OM_BX(HAIR)->y1, s_hair_base[0],
               OM_VRAM_GOUR + 8u * OM_G_HAIR_L, 0, Z_HAIR);
    om_v1_quad(OM_HAIR_PEAK, OM_BX(HAIR)->y0, OM_BX(HAIR)->x1, OM_BX(HAIR)->y1, s_hair_base[1],
               OM_VRAM_GOUR + 8u * OM_G_HAIR_R, 0, Z_HAIR);
    /* the Aether rename (user decision D2, supersedes addendum 8's two-tone "VibeMenu"): the wordmark is the logo's
     * "AETHER" in Montserrat Light, the 4 bpp icon uploaded once (om_icon_wordmark: 1 px margin, cap rows 1..11), its
     * capitals centred on the header's text line, faded in with s_word like the text it replaces */
    om_ut_colors(&s_brand1, Sty.txt, bg, s_word);
    om_ut_draw(&s_brand1, b->x0, OM_HDR_MID - 6, 0, Z_HDR);
    om_clock_fmt(&s_clock, s_ps.set.clock24, t);
    om_ut_text(&s_clk, OM_FACE_TITLE, t, 0);
    om_ut_colors(&s_clk, Sty.txt, (uint16_t)(om_v2_bg_at(290, OM_HDR_MID) | 0x8000u), s_word);
    om_ut_draw_box(&s_clk, ck->x0, ck->x1, base_of(OM_HDR_MID, OM_FACE_TITLE), 1, 0, 0, 0, 0, Z_HDR);
}

static void set_marquee_text(om_ut2 *t, uint32_t *timer, char *keep, uint32_t keepcap, int face, const char *s, int box,
                             int marquee)
{   /* MARQUEE elements: the whole string (or, past the raster cap, fade-cut at the box); else FADE */
    if (strcmp(keep, s)) {
        strncpy(keep, s, keepcap - 1);
        keep[keepcap - 1] = 0;
        *timer = 0;
    }
    om_ut2_text(t, face, s, marquee ? -om_box_w(box) : om_box_w(box));
}

static void draw_box_text(om_ut2 *d, uint32_t *timer, int box, int base, int right, int zclip, int z)
{
    const om_box *b = &om_boxes[box];
    const om_ut *t = om_ut2_front(d);
    int32_t per = om_marquee_period(t->full_w, om_box_w(box));
    int32_t off = per && t->w == t->full_w ? om_marquee_off(*timer, t->full_w, om_box_w(box), s_fps) : 0;
    om_ut_draw_box(t, b->x0, b->x1, base, right, off, t->w == t->full_w ? per : 0, 0, zclip, z);
    (*timer)++;
}

static uint32_t view_key(void)
{   /* what the breadcrumb and the details card depend on: rebuilt only when this changes (measured: rebuilding them
     * every frame cost ~2-4 ms of the frame) */
    uint16_t n;
    const om_item *it = om_browse_selected(&s_b);
    uint32_t h = 2166136261u, v[9], i;
    (void)om_browse_items(&s_b, &n);
    v[0] = s_b.cur; v[1] = n; v[2] = s_b.depth; v[3] = (uint32_t)s_b.loc.kind << 16 | s_b.loc.arg;
    v[4] = it ? (uint32_t)it->kind << 16 | it->idx : 0xFFFFFFFFu;
    v[5] = it && it->kind == OM_IT_GAME && om_info_find(it->idx) ? 1u : 0u;
    v[6] = s_ps.set.anim; v[7] = s_content_serial; v[8] = s_b.top;
    for (i = 0; i < 9; i++)
        h = (h ^ v[i]) * 16777619u;
    return h;
}

static char s_crumb_str[200], s_count_str[24];

static void prepare_crumb(void)
{
    static uint32_t key = 1;
    if (key != view_key()) {
        char p[300];
        uint16_t n, i, games = 0;
        const om_item *it = om_browse_items(&s_b, &n);
        key = view_key();
        om_browse_path(&s_b, p, sizeof p);
        om_crumb(p, s_crumb_str, sizeof s_crumb_str);
        for (i = 0; i < n; i++)
            if (it[i].kind == OM_IT_GAME) games++;
        if (s_b.loc.kind == OM_LOC_CARD && s_b.loc.arg == 0)       /* the top level counts the whole */
            games = (uint16_t)om_browse_games(&s_b);              /* card's games, as the interstitial does */
        om_count_line(games, n, s_count_str, sizeof s_count_str);
    }
    set_marquee_text(&s_crumb, &s_mq_crumb, s_crumb_text, sizeof s_crumb_text, OM_FACE_SMALL, s_crumb_str,
                     OM_B_CRUMB_PATH, anim_marquee(0));
    om_ut_text(&s_count, OM_FACE_SMALL, s_count_str, 0);
}

static void draw_crumb(void)
{
    uint16_t bg = (uint16_t)(om_v2_bg_at(60, OM_CRUMB_MID) | 0x8000u);
    if (!s_in)
        return;
    om_ut_colors(om_ut2_front(&s_crumb), Sty.dim, bg, s_in);
    draw_box_text(&s_crumb, &s_mq_crumb, OM_B_CRUMB_PATH, base_of(OM_CRUMB_MID, OM_FACE_SMALL), 0, Z_CRUMBCLIP, Z_CRUMB);
    om_ut_colors(&s_count, Sty.faint, (uint16_t)(om_v2_bg_at(160, OM_CRUMB_MID) | 0x8000u), s_in);
    om_ut_draw_box(&s_count, OM_BX(CRUMB_COUNT)->x0, OM_BX(CRUMB_COUNT)->x1, base_of(OM_CRUMB_MID, OM_FACE_SMALL), 1,
                   0, 0, 0, 0, Z_COUNT);
}

static char s_det_title[160], s_det_meta[200];
static int s_det_wrap;
static uint8_t s_det_padnote;   /* meta is the pad-compat note, in Sty.acc2 (theme text_hi), not Sty.dim */

/* the GAMES.DB compat note for key/area (gamesdb.h om_gdb_compat), the same guard om_loader_db_hint
 * uses. 0 = no compat row for this disc's regions (db not loaded, no key, or none on record). */
/* which game's saves the console holds -- ACTIVE.TXT's first two fields (SPEC §5.17), read at boot and
 * after every launch that comes back (a successful one never does: the way back is a fresh boot). "" = the general
 * set, no file, or a file that does not parse. */
static char s_act_gid[OM_GID_MAX];
static uint8_t s_act_slot, s_slot_ask;
static OM_COLD void read_active(void)
{
    uint32_t buf[80], len = 0;
    if (om_sdw_read_head(OM_PATH_ACTIVE, (uint8_t *)buf, sizeof buf, &len) != 0 ||   /* 24b fix: line 1 is enough */
        om_active_parse((const char *)buf, len, s_act_gid, &s_act_slot) != 0)
        s_act_gid[0] = 0;
}
static OM_COLD void gid_of(const om_sysid *ip, char gid[OM_GID_MAX]);

static OM_COLD int gdb_compat_note(const om_card *c, const char *key, uint8_t area, const char **note, uint8_t *flags)
{
    uint32_t rec;
    om_compat cp;
    if (c->db_rc || !key || !key[0] || !om_gamesdb_rec(&c->db, key, &rec) || !om_gdb_compat(&c->db, rec, area, &cp))
        return 0;
    *note = cp.note;
    if (flags) *flags = cp.flags;
    return 1;
}

/* review-task-19.md Important: the old single OM_COLD prepare_details was called every
 * frame from frame_draw (not "once per selection change" as its own comment claimed) -- its raster: tail (the two
 * set_marquee_text calls below, which drive the live marquee) ran every frame too, under -Os. Split: this half is
 * the truly once-per-selection-change content build (title/meta/the compat note, the anim=off wrap), gated by
 * view_key() in the caller below, and stays -Os; prepare_details itself, which runs every frame unconditionally,
 * does not. */
static OM_COLD void prepare_details_build(const om_card *c, const om_tree *t, const om_item *it)
{
    char *title = s_det_title, *meta = s_det_meta;
    int wrap = 0;
    const char *note = NULL;
    title[0] = meta[0] = 0;
    if (it && it->kind == OM_IT_GAME) {
        om_lib_entry e;
        om_game g;
        int have = game_of(c, it->idx, &e, &g);
        const om_sysid *ip = om_info_find(it->idx);
        om_title(&c->lib, &e, have ? &g : NULL, title, sizeof s_det_title);
        om_meta_line(&e, have ? &g : NULL, have ? om_gamesdb_genre(&c->db, g.genre) : NULL, ip, meta, sizeof s_det_meta);
        gdb_compat_note(c, e.key, ip ? ip->area_flags : e.area, &note, NULL);
        if (s_act_gid[0] && ip) {                          /* this game's saves are in the console */
            char gid[OM_GID_MAX], held[32];
            gid_of(ip, gid);
            om_saves_held_line(held, sizeof held, s_act_slot);
            if (!strcmp(gid, s_act_gid) && strlen(meta) + sizeof OM_SEP_DOT + strlen(held) < sizeof s_det_meta) {
                if (meta[0]) strcat(meta, OM_SEP_DOT);
                strcat(meta, held);
            }
        }
    } else if (it) {
        uint32_t k;
        om_browse_label(&s_b, it, title, sizeof s_det_title);
        k = (uint32_t)strlen(title);
        if (k && title[k - 1] == '/') title[k - 1] = 0;
        if (it->kind == OM_IT_FOLDER) {
            om_count_line(0, t->count[it->idx], meta, sizeof s_det_meta);
        } else if (it->kind == OM_IT_VROOT && (it->idx == OM_VR_FAVS || it->idx == OM_VR_RECENT)) {
            uint32_t vn = visible_n(it->idx == OM_VR_FAVS ? &s_ps.favs : &s_ps.recent);
            om_count_line(vn, vn, meta, sizeof s_det_meta);
        }
    }
    if (s_ps.set.anim == OM_ANIM_OFF && om_uf_width(&om_face_title, title) > om_box_w(OM_B_DET_TITLE)) {
        /* anim=off: no motion, so the details card carries the whole title on two lines (meta hidden) */
        uint32_t k = (uint32_t)strlen(title), cut = 0, j;
        char l1[160];
        for (j = 1; j < k; j++)
            if (title[j] == ' ') {
                memcpy(l1, title, j); l1[j] = 0;
                if (om_uf_width(&om_face_title, l1) <= om_box_w(OM_B_DET_TITLE)) cut = j;
            }
        if (cut) {
            strcpy(meta, title + cut + 1);
            title[cut] = 0;
            wrap = 1;
        }
    }
    if (note && note[0]) {   /* the pad-compat note wins over the anim=off title-wrap above (rare to meet
                               * both at once: a long title and one of the two known pad-sensitive games) -- it cancels
                               * the wrap's TITLE face/position but, if wrap also fired, the title stays cut at the
                               * word the wrap chose; the overflow word is not restored. */
        strcpy(meta, "PAD: ");
        strcat(meta, note);
        wrap = 0;
    }
    s_det_padnote = (uint8_t)(note && note[0]);
    s_det_wrap = wrap;
}

static void prepare_details(const om_card *c, const om_tree *t)
{
    static uint32_t key = 1;
    if (key != view_key()) {
        key = view_key();
        prepare_details_build(c, t, om_browse_selected(&s_b));
    }
    /* 2026-11-20 hardware report: the details card describes the selected row, so its title and meta line marquee
     * whenever the selected row does (anim full or reduced) -- with anim_marquee(0) (full only) they were fade-cut
     * at the box under anim=reduced, the long text never shown whole. Runs every frame (frame_draw) -- -O2, with the
     * rest of folders.c. */
    set_marquee_text(&s_dtitle, &s_mq_dt, s_dt_text, sizeof s_dt_text, OM_FACE_TITLE, s_det_title, OM_B_DET_TITLE,
                     anim_marquee(1));
    set_marquee_text(&s_dmeta, &s_mq_dm, s_dm_text, sizeof s_dm_text, s_det_wrap ? OM_FACE_TITLE : OM_FACE_SMALL,
                     s_det_meta, OM_B_DET_META, anim_marquee(1));
}

static void draw_details(void)
{
    const om_item *it = om_browse_selected(&s_b);
    uint32_t jtex;
    int jw, jh, wrap = s_det_wrap, have_jp = it && it->kind == OM_IT_GAME && om_art_jp(&jtex, &jw, &jh);
    if (!s_in)
        return;
    om_ut_colors(om_ut2_front(&s_dtitle), Sty.txt, Sty.det_eff, s_in);
    /* the pad-compat note in the theme's text_hi (Sty.acc2), over wrap/dim */
    om_ut_colors(om_ut2_front(&s_dmeta), s_det_padnote ? Sty.acc2 : wrap ? Sty.txt : Sty.dim, Sty.det_eff, s_in);
    if (have_jp && s_in < 256)
        ;                                                          /* a JP title is a bitmap: shown once faded in */
    else if (have_jp)                                              /* JP titles keep their bitmap (brief) */
        om_v1_sprite(OM_BX(DET_TITLE)->x0, OM_BX(DET_TITLE)->y0 - 1, jtex, jw, jh, OM_VRAM_LUT, 0, Z_DTITLE);
    else
        draw_box_text(&s_dtitle, &s_mq_dt, OM_B_DET_TITLE, base_of(OM_DET_TITLE_MID, OM_FACE_TITLE), 0, Z_DTCLIP, Z_DTITLE);
    draw_box_text(&s_dmeta, &s_mq_dm, OM_B_DET_META,
                  wrap ? base_of(OM_DET_META_MID, OM_FACE_TITLE) - 1 : base_of(OM_DET_META_MID, OM_FACE_SMALL), 0,
                  Z_DMCLIP, Z_DMETA);
}

static int placeholder_square(const om_card *c)
{   /* addendum 2: NO ART takes the shape of the selected item's region when known (primary area Japan -> square, the
     * shape JP covers are stored in), else tall */
    const om_item *it = om_browse_selected(&s_b);
    om_lib_entry e;
    om_game g;
    if (!it || it->kind != OM_IT_GAME)
        return 0;
    return om_primary_area(&e, game_of(c, it->idx, &e, &g) ? &g : NULL, om_info_find(it->idx)) == 'J';
}

static void cover_box(const om_card *c, const om_box **b, int *tall, int *ph)
{
    uint32_t tex;
    int w, h, t;
    *ph = !om_art_hero(&tex, &w, &h, &t);
    *tall = *ph ? !placeholder_square(c) : t;
    *b = *tall ? OM_BX(COVER_TALL) : OM_BX(COVER_SQ);
}

static void draw_cover(const om_card *c, int32_t vis, int ov_on, int ov_dx)
{   /* vis: 0..256, the overlay's fade-out (addendum 4); the art fades up from dark and rises 4 px when it changes.
     * Review-13a I3: while the overlay slides, every cover part is drawn OUTSIDE a user clip of the overlay panel
     * (Clip = 1, Cmod = 1; VDP1_Manual.txt:3480-3500), so the panel's leading edge covers the card -- no pop. */
    const om_box *b;
    int tall, ph, rise, dark;
    uint32_t tex;
    int w, h, t;
    int32_t in = om_tween_value(&s_cover_in);
    uint16_t g;
    uint16_t pm = ov_on ? OM_V1_CLIP_OUT : 0;
    if (vis > s_in) vis = s_in;                                    /* Task 13d's fade-in, as the overlay's fade-out */
    if (vis <= 0)
        return;
    if (ov_on) {
        const om_box *o = OM_BX(OVERLAY);
        om_v1_clip(o->x0 + ov_dx, o->y0, o->x1 + ov_dx, o->y1, Z_FRAME);
    }
    cover_box(c, &b, &tall, &ph);
    om_art_hero(&tex, &w, &h, &t);
    if (in > vis) in = vis;
    rise = (int)((256 - om_tween_value(&s_cover_in)) * om_anim_dist(4, s_ps.set.anim) / 256);
    dark = (int)(-16 * (256 - in) / 256);                          /* Gouraud floor is -16 of 31 (HWF-43) */
    g = om_v1_gour_delta(dark, dark, dark);
    om_v1_gouraud(OM_VRAM_GOUR + 8u * OM_G_COVER, g, g, g, g);
    om_v1_quad(b->x0 - 1, b->y0 - 1 + rise, b->x1 + 1, b->y1 + 1 + rise,
               om_ui_mix((uint16_t)(om_v2_bg_at(250, 120) | 0x8000u), 0xFFFF, 60), OM_VRAM_GOUR + 8u * OM_G_COVER, pm,
               Z_FRAME);
    if (ph) {                                                      /* NO ART: the placeholder card, vector-drawn */
        uint16_t fill = om_c555_rgb(0x282830), edge = om_c555_rgb(0x78788C);
        om_v1_quad(b->x0, b->y0 + rise, b->x1, b->y1 + rise, fill, OM_VRAM_GOUR + 8u * OM_G_COVER, pm, Z_COVER);
        om_v1_quad(b->x0 + 3, b->y0 + 3 + rise, b->x1 - 3, b->y1 - 3 + rise, edge, OM_VRAM_GOUR + 8u * OM_G_COVER, pm,
                   Z_COVER);
        om_v1_quad(b->x0 + 5, b->y0 + 5 + rise, b->x1 - 5, b->y1 - 5 + rise, fill, OM_VRAM_GOUR + 8u * OM_G_COVER, pm,
                   Z_COVER);
        om_ut_text(&s_noart, OM_FACE_SMALL, "NO ART", 0);
        om_ut_colors(&s_noart, om_c555_rgb(0xC8C8D2), fill, (uint16_t)in);
        om_ut_draw_base(&s_noart, (b->x0 + b->x1 + 1 - s_noart.w) / 2, (b->y0 + b->y1) / 2 + 4 + rise, pm, Z_NOART);
    } else
        om_v1_scaled(b->x0, b->y0 + rise, b->x1, b->y1 + rise, tex, w, h, 0, OM_VRAM_GOUR + 8u * OM_G_COVER, pm, Z_COVER);
    if (!ov_on && om_art_loading() && s_ps.set.anim != OM_ANIM_OFF) {        /* a soft shimmer sweeps the slot while art loads */
        int span = b->x1 - b->x0 + 1 + 24, x = b->x0 - 12 + (int)((s_frame * 2u) % (uint32_t)span);
        om_v1_clip(b->x0, b->y0 + rise, b->x1, b->y1 + rise, Z_SHIMCLIP);
        om_v1_quad(x, b->y0 + rise, x + 11, b->y1 + rise, 0xFFFF, 0, OM_V1_CLIP | OM_V1_HALF, Z_SHIMMER);
    }
}

static int pill_w(int text_w, int star) { return text_w + (star ? 22 : 12); }

static void prepare_toast(void)
{   /* first in the raster order: a status line, and above all an rc, is never starved */
    const om_box *f = OM_BX(FOOTER);
    const char *rc;
    int star, whole;
    if (!s_toast.msg[0] || !om_toast_progress(&s_toast))
        return;
    rc = om_split_rc(s_toast.msg);
    star = !s_toast.sticky;
    whole = pill_w(om_uf_width(&om_face_small, s_toast.msg), star) <= om_box_w(OM_B_TOAST);
    if (whole || !rc) {                                            /* N5: cut at the pill's text width */
        int tw = om_box_w(OM_B_TOAST) - (star ? 18 : 6) - 6;
        if (strcmp(s_reason_text, s_toast.msg)) {
            strncpy(s_reason_text, s_toast.msg, sizeof s_reason_text - 1);
            s_reason_text[sizeof s_reason_text - 1] = 0;
            s_mq_reason = 0;
        }
        om_ut2_text(&s_reason, OM_FACE_SMALL, s_toast.msg, anim_marquee(s_toast.sticky) ? -tw : tw);
    }
    else {                                                         /* the reason only; the rc is its own sprite */
        char reason[OM_TOAST_MSG];
        uint32_t k = (uint32_t)(rc - s_toast.msg);
        int rw;
        memcpy(reason, s_toast.msg, k);
        reason[k] = 0;
        om_ut_text(&s_rc, OM_FACE_SMALL, rc + 1, 0);
        rw = f->x1 - 6 - s_rc.w - 4 - (f->x0 + 6) + 1;
        if (strcmp(s_reason_text, reason)) {
            strncpy(s_reason_text, reason, sizeof s_reason_text - 1);
            s_reason_text[sizeof s_reason_text - 1] = 0;
            s_mq_reason = 0;
        }
        om_ut2_text(&s_reason, OM_FACE_SMALL, reason, anim_marquee(1) ? -rw : rw);   /* M1: one raster, final width */
    }
}

static void pill(int x0, int y0, int x1, int y1, uint16_t col, int z)
{   /* the approved preview's toast pill -- an opaque pre-blended rounded bar (3 quads, D1) */
    om_v1_quad(x0 + 2, y0, x1 - 2, y1, col, 0, OM_V1_CLIP, z);
    om_v1_quad(x0 + 1, y0 + 1, x1 - 1, y1 - 1, col, 0, OM_V1_CLIP, z);
    om_v1_quad(x0, y0 + 2, x1, y1 - 2, col, 0, OM_V1_CLIP, z);
}

static void draw_footer(void)
{
    const om_box *f = OM_BX(FOOTER);
    int32_t p = om_toast_progress(&s_toast);
    int h = f->y1 - f->y0 + 1, dy_h = (int)((p * h) >> 12), dy_t = h - dy_h, i;
    int base = base_of(OM_FOOT_MID, OM_FACE_SMALL);
    uint16_t bg = (uint16_t)(om_v2_bg_at(160, OM_FOOT_MID) | 0x8000u);
    if (!s_in)
        return;
    om_v1_clip(f->x0, f->y0, f->x1, f->y1, Z_FOOTCLIP);           /* M3: descenders end inside FOOTER (203-215) */
    if (dy_h < h)
        for (i = 0; i < OM_NHINTS; i++) {
            om_ut_badge_colors(&s_badge[i], bg, om_ui_mix(bg, om_c555_rgb(0xE1E8F5), (235u * s_in) >> 8),
                               s_in < 256 ? om_ui_mix(bg, om_c555_rgb(0x0C1222), a255_of(s_in)) : om_c555_rgb(0x0C1222));
            om_ut_draw(&s_badge[i], s_badge_x[i], f->y0 + 1 + dy_h, OM_V1_CLIP, Z_FOOT);
            om_ut_text(&s_hint[i], OM_FACE_SMALL, om_ui_hint_label[i], 0);
            om_ut_colors(&s_hint[i], Sty.dim, bg, s_in);
            om_ut_draw_base(&s_hint[i], s_label_x[i], base + dy_h, OM_V1_CLIP, Z_FOOT);
        }
    if (dy_t < h && s_toast.msg[0] && s_in_full >= 256) {                              /* the toast pill slides up over the hints */
        const om_ut *rs = om_ut2_front(&s_reason);
        int star = !s_toast.sticky, tx = f->x0 + (star ? 18 : 6);
        uint16_t pc = s_toast.sticky ? om_c555_rgb(0x5A1E1E) : om_c555_rgb(0x1E4A2C);
        int whole = pill_w(om_uf_width(&om_face_small, s_toast.msg), star) <= om_box_w(OM_B_TOAST);
        int px1 = whole ? f->x0 + pill_w(rs->w, star) - 1 : f->x1;
        if (px1 > f->x1) px1 = f->x1;
        pill(f->x0, f->y0 + dy_t, px1, f->y1 + dy_t, pc, Z_FOOT);
        if (star) {
            om_ut_colors(&s_pillstar, Sty.gold, pc, 256);
            om_ut_draw(&s_pillstar, f->x0 + 6, f->y0 + dy_t + 2, OM_V1_CLIP, Z_REASON);
        }
        om_ut_colors((om_ut *)rs, Sty.txt, pc, 256);
        if (whole || !om_split_rc(s_toast.msg)) {                 /* the whole line (it fits, or it has no rc) */
            int32_t per = rs->w == rs->full_w ? om_marquee_period(rs->full_w, px1 - 6 - tx + 1) : 0;
            int32_t off = per ? om_marquee_off(s_mq_reason, rs->full_w, px1 - 6 - tx + 1, s_fps) : 0;
            int y = base + dy_t - rs->asc;
            om_ut_marquee(rs, tx, tx, px1 - 6, f->y0 > y ? f->y0 : y, f->y1, base + dy_t, off, per, Z_REASON);
        } else {                                                   /* rc fixed and whole at the right; reason scrolls */
            int x1 = f->x1 - 6 - s_rc.w - 4;
            int32_t per = rs->w == rs->full_w ? om_marquee_period(rs->full_w, x1 - tx + 1) : 0;
            int32_t off = per ? om_marquee_off(s_mq_reason, rs->full_w, x1 - tx + 1, s_fps) : 0;
            int y = base + dy_t - rs->asc;
            om_ut_marquee(rs, tx, tx, x1, f->y0 > y ? f->y0 : y, f->y1, base + dy_t, off, per, Z_REASON);
            om_ut_colors(&s_rc, Sty.txt, pc, 256);
            om_v1_clip(f->x0, f->y0, f->x1, f->y1, Z_RCCLIP);
            om_ut_draw_base(&s_rc, f->x1 - 6 + 1 - s_rc.w, base + dy_t, OM_V1_CLIP, Z_RC);
        }
        s_mq_reason++;
    }
}

/* --- panels (NBG2) ------------------------------------------------------------------------------------------- */
static void draw_panels(const om_card *c, int ov_on)
{
    const om_box *L = OM_BX(LIST), *D = OM_BX(DETAILS);
    om_v2_rects_begin();
    om_v2_rect(L->x0, L->y0, L->x1, L->y1, OM_V2_COL_PANEL, 0);
    om_v2_rect(D->x0, D->y0, D->x1, D->y1, OM_V2_COL_PANEL, 0);
    /* the cover's soft drop shadow follows its shape; with an overlay open it is empty (the overlay's panel is VDP1,
     * ovdraw.c) */
    if (!ov_on) {
        const om_box *b;
        int tall, ph;
        cover_box(c, &b, &tall, &ph);
        om_v2_rect(b->x0 + OM_SHADOW_DX, b->y0 + OM_SHADOW_DY, b->x1 + OM_SHADOW_DX, b->y1 + OM_SHADOW_DY,
                   OM_V2_COL_SHADOW, 1);
    } else
        om_v2_rect(1, 1, 0, 0, 0, 0);
    om_v2_rects_end();
    if (s_safe_flush) {             /* a Safe area change moved every panel: all its cells at once (one */
        s_safe_flush = 0;           /* long frame), not 8 a frame behind the VDP1 picture for about a second */
        om_v2_rects_flush();
    }
}

/* --- actions --------------------------------------------------------------------------------------------------- */
static OM_COLD void toggle_fav(const om_card *c)
{
    const om_item *it = om_browse_selected(&s_b);
    char msg[OM_TOAST_MSG];
    int r, w;
    if (!it || it->kind != OM_IT_GAME)
        return;
    if ((r = om_plist_toggle(&s_ps.favs, it->idx)) == OM_E_FULL) {
        om_folders_status("Favorites full (256)");                 /* addendum 7: American spelling */
        om_sfx(OM_SFX_ERROR);
        return;
    }
    if (s_ps.set.anim == OM_ANIM_FULL) {                           /* the star pops on add, shrinks away on removal */
        s_star_item = s_b.cur;
        s_star_add = (uint8_t)(r != 0);
        om_tween_run(&s_star_t, 0, OM_Q, om_anim_frames(r ? 200 : 150, OM_ANIM_FULL, 0, s_fps));
    }
    om_sfx(r ? OM_SFX_FAV_ADD : OM_SFX_FAV_REMOVE);
    {
        ph_mark pm = ph_now();
        sd_quiet();
        w = om_persist_save_list(&s_ps.favs, &c->lib, OM_PATH_FAVS);
        ph_add(PH_SAVE, pm);
    }
    if (w != 0) {
        om_msg_rc(msg, sizeof msg, "FAVS.TXT NOT SAVED: ", om_strerror(w), w);
        om_folders_status(msg);
    } else
        om_folders_status(r ? "Added to favorites" : "Removed from favorites");
    om_browse_refresh(&s_b);
    rows_invalidate();
    s_content_serial++;
}

static void frame_draw(const om_card *c, const om_tree *t);
static void show_theme(uint16_t i);

/* --- the launch: menu fade-out (LOADBG.BIN read meanwhile), the loading screen, and back ------------- */
#define MENU_FADE_MASK   (NBG1ON | NBG2ON | SPRON | BACKON)   /* NBG0 stays: the fake build's diagnostics */
#define MENU_FADE_MS     250u
#define PRELOAD_END_US   12500      /* LOADBG.BIN is read after the menu's drawing; a 2 KB step starts only if it ends by this */
typedef struct { const om_card *c; const om_tree *t; int preload; uint32_t vb_last, misses; } fade_ctx;
static uint16_t s_launch_idx;
static uint8_t s_sr_latch;       /* A+B+C+Start canceled a launch: no return to the official menu until released */
static uint8_t s_vb_restart;     /* the launch (loading screen and back) is not a missed menu frame */
#ifdef OMSAT_FAKECARD
static uint8_t s_diag_reset;     /* the loading screen wrote NBG0 rows 24-27: the tour label is rewritten */
#endif

static void fade_draw(void *ctx)
{
    fade_ctx *f = (fade_ctx *)ctx;
    uint16_t t0 = om_ut_frc();
    uint32_t vb = om_v2_vblanks();
    if (f->vb_last && vb - f->vb_last > 1u)                    /* the fades' missed frames, for the report */
        f->misses += vb - f->vb_last - 1u;
    f->vb_last = vb;
    frame_draw(f->c, f->t);
    if (f->preload)
        om_loading_preload_tick(t0, PRELOAD_END_US);
}

static void tool_loaded(void *ctx, char *msg, uint32_t cap)
{   /* a tool's hand-off (the cheat editor, Manage saves, the import): nothing enters Recently Played; the music's
     * resume point is kept for the return */
    (void)ctx; (void)msg; (void)cap;
    sd_quiet();
    om_audio_save_state();
}

static void recent_written(void *ctx, char *msg, uint32_t cap)
{   /* spec §7.1/§7.6: RECENT.TXT at the loading screen's commit point -- LAUNCHER.BIN has loaded and
     * passed its CRC, no cancel is possible any more, and High WRAM (this list, the
     * LIBRARY.IDX path pool in s_libbuf) is forfeit once it runs. The write touches only HWRAM (sd.c s_wbuf, the path
     * pool), never the launcher body in LWRAM. A failed write shows for one second, then the launch goes on. */
    const om_card *c = (const om_card *)ctx;
    int w;
    om_plist_push_front(&s_ps.recent, s_launch_idx);
    sd_quiet();                                            /* both are closed by now (FADE_OUT, the launch); cheap */
    if ((w = om_persist_save_list(&s_ps.recent, &c->lib, OM_PATH_RECENT)) != 0)
        om_msg_rc(msg, cap, "RECENT.TXT: ", om_strerror(w), w);
    om_audio_save_state();                                 /* the music's resume point (MUSICPOS.TXT) */
}

/* the loader and the per-game options of this launch (menu/src/loader.h): the game id from its IP (Task
 * 15, gameid.h), its PERGAME.INI section (read into OM_LW_STAGE: the caller has stopped the art job, the only other
 * user, and nothing else touches the stage before LAUNCHER.BIN's body is read over it), the GAMES.DB compat hint for
 * this disc's regions, Settings > Loader. The choice and why are appended to the loading screen's meta line.
 * Saves, slot, cheats, NetLink and save-tool bytes stay 0 until Tasks 23-27. */
/* the panel's code runs on a key press, never per frame: GCC's cold attribute optimizes it for size
 * (folders.c is otherwise -O2; docs/ledger.md, measures the saving) */
#define PG_COLD OM_COLD
static void save_settings(void);

static OM_COLD void gid_of(const om_sysid *ip, char gid[OM_GID_MAX])
{   /* the game id from its IP (gameid.h); "" when there is none. om_sysid fields are trimmed: pad them back */
    uint8_t prod[OM_GID_PRODUCT], areas[10];
    uint32_t n;
    memset(prod, ' ', sizeof prod);
    memset(areas, ' ', sizeof areas);
    n = (uint32_t)strlen(ip->product);
    memcpy(prod, ip->product, n < sizeof prod ? n : sizeof prod);
    n = (uint32_t)strlen(ip->area);
    memcpy(areas, ip->area, n < sizeof areas ? n : sizeof areas);
    if (om_gameid_ini(prod, areas, gid) != 0)
        gid[0] = 0;
}

static PG_COLD const om_sysid *game_gid(const om_card *c, const om_lib_entry *e, uint16_t idx, char gid[OM_GID_MAX])
{   /* the game id from its IP (gameid.h), read now if the details card has not; "" when there is none. A disc
     * of a multi-disc set takes its set's lead disc's id (discset.h; one more IP read): one game, one set of saves and
     * one PERGAME.INI section, whichever disc starts it. Returns idx's own IP. */
    uint16_t at = (uint16_t)om_discset_lead(&c->lib, idx);
    om_lib_entry le;
    const om_sysid *ip;
    gid[0] = 0;
    for (;;) {                                             /* the lead first (another disc of idx's set), then idx */
        const om_lib_entry *x = e;
        if (at != idx) { om_libidx_entry(&c->lib, at, &le); x = &le; }
        ip = om_info_find(at);
        if (!ip && om_info_load(x, at) == 0)
            ip = om_info_find(at);
        if (ip && !gid[0])
            gid_of(ip, gid);
        if (at == idx)
            return ip;
        at = idx;
    }
}

static OM_COLD void launch_opts(const om_card *c, uint16_t idx, const om_lib_entry *e, om_load_game *g)
{
    const om_sysid *ip;
    char *ini = (char *)OM_CT(OM_LW_STAGE);
    const char *note = NULL;
    uint8_t per = OM_LDR_AUTO, per_igr = OM_PG_DEFAULT, per_saves = OM_PG_DEFAULT, per_ch = OM_PG_DEFAULT, area,
            cflags = 0;
    uint32_t len = 0;
    om_pergame pg;
    om_loader_choice ch;
    memset(&g->opts, 0, sizeof g->opts);
    s_slot_ask = 0;
    ip = game_gid(c, e, idx, g->opts.gid);
    g->opts.set_lib = &c->lib;                             /* a multi-disc set's discs go with it (launch.c) */
    g->opts.set_idx = idx;
    if (g->opts.gid[0] && om_sdw_read_file(OM_PATH_PERGAME, (uint8_t *)ini, OM_PERGAME_MAX_BYTES + 4u, &len) == 0
        && om_pergame_get(ini, len, g->opts.gid, &pg) == 1) {
        per = pg.loader;                                    /* absent or corrupt file: no per-game choice */
        per_igr = pg.igr;
        per_saves = pg.saves;
        per_ch = pg.cheats;
        s_slot_ask = pg.slot == OM_PG_SLOT_ASK;            /* launch_selected's picker asks */
        g->opts.slot = s_slot_ask ? 0 : pg.slot;
    }
    area = ip ? ip->area_flags : e->area;
    gdb_compat_note(c, e->key, area, &note, &cflags);
    /* a save tool (GAMES.DB's OM_CF_SAVETOOL, or its IP product / title, save_tools.json) never gets
     * per-game saves: the launcher gives it the console's own (general) set, and the loading screen says so */
    g->opts.savetool = (uint8_t)((cflags & OM_CF_SAVETOOL) || (ip && om_savetool_ip(ip->product, ip->title)));
    /* the per-game Saves row over the global setting (on from the start); Move or Share is saves_first */
    if (g->opts.gid[0] && om_saves_want(s_ps.set.saves, per_saves, g->opts.savetool) && !om_card_old_folder)
        g->opts.saves = s_ps.set.saves_first == OM_SVF_MOVE ? OM_HO_SAVES_MOVE : OM_HO_SAVES_SHARE;
    if (s_slot_ask && g->opts.saves)                       /* the picker starts on the slot the console holds */
        g->opts.slot = om_slot_first(g->opts.gid, s_act_gid, s_act_slot);
    else
        s_slot_ask = 0;
    ch = om_loader_pick(per, s_ps.set.loader, om_loader_db_hint(&c->db, !c->db_rc, e->key, area), OM_CWX_AVAILABLE);
    g->opts.loader = ch.loader;
    /* om_igr_want's inputs, fetched once here (never again at the OM_HO_NO_IGR site below: the caller
     * mirrors this result instead of re-deriving it, so the two cannot drift) */
    g->opts.igr = (uint8_t)om_igr_want(s_ps.set.igr, per_igr, cflags);
    /* the Cheats row over SETTINGS.INI cheats=; LAUNCHER.BIN reads the game's CHEATS.PAK row (ckey) */
    g->opts.cheats = (uint8_t)(per_ch == OM_PG_ON || (per_ch != OM_PG_OFF && s_ps.set.cheats));   /* om_ch_want */
    memcpy(g->opts.ckey, e->key, sizeof g->opts.ckey - 1);
    if (g->meta2[0] && strlen(g->meta2) + sizeof OM_SEP_DOT < sizeof g->meta2)
        strcat(g->meta2, OM_SEP_DOT);
    if (strlen(g->meta2) + strlen(om_loader_why(&ch)) < sizeof g->meta2)
        strcat(g->meta2, om_loader_why(&ch));
    if (g->opts.savetool && strlen(g->meta2) + sizeof OM_SEP_DOT + 34u < sizeof g->meta2) {
        strcat(g->meta2, OM_SEP_DOT);
        strcat(g->meta2, "Save tool: per-game saves paused");
    }
}

/* --- M6 Task 18: the per-game panel (A on a game) -------------------------------------------------------------------
 * LWRAM ownership (docs/ledger.md): while the panel is open, OM_LW_STAGE holds the file as read (PG_OLD) and
 * the file to write (PG_NEW). The art job, the stage's everyday tenant, is not run while it is open (pg_busy); the
 * screensaver's logo decode may still borrow the stage and hands it back zeroed, so the panel re-reads the file when it
 * saves. (2026-11-23: the Settings > Per-game list is gone, and with it its entries at +0x8800.) */
#define PG_OLD_OFF   0x0000u
#define PG_NEW_OFF   0x4400u
#define PG_CAP       (OM_PERGAME_MAX_BYTES + 4u)    /* om_sd_read_all wants size + 4 */
typedef char pg_stage_fits[(PG_OLD_OFF + PG_CAP <= PG_NEW_OFF && PG_NEW_OFF + PG_CAP <= OM_LW_STAGE_MAX) ? 1 : -1];
/* review R1: SETTINGS.INI's merge output (om_persist_save_settings), live only inside
 * save_settings(), which first runs sd_quiet() (the art job, the stage's in-flight tenant, canceled). Placed above
 * every PG region, so it cannot meet the panel's data either (docs/ledger.md). */
#define SET_OUT_OFF  0x1C000u
#define SET_IN_OFF   0x19000u           /* the file as it is on the card, read again at each save (persist.c) */
typedef char set_out_fits[(PG_NEW_OFF + PG_CAP <= SET_IN_OFF && SET_IN_OFF + OM_SETTINGS_IN <= SET_OUT_OFF &&
                           SET_OUT_OFF + OM_SETTINGS_MAX <= OM_LW_STAGE_MAX) ? 1 : -1];
/* PG_OLD/PG_NEW (pg_save at the panel's close) and launch_opts' PERGAME.INI (+0, PG_CAP) stay below
 * LOADBG.BIN's LW region (OM_LW_LBG), which an adopted read-ahead still holds through the launch */
typedef char pg_below_lbg[(PG_NEW_OFF + PG_CAP <= OM_LW_LBG - OM_LW_STAGE) ? 1 : -1];
#define SET_OUT  ((char *)OM_CT(OM_LW_STAGE + SET_OUT_OFF))
#define SET_IN   ((uint8_t *)OM_CT(OM_LW_STAGE + SET_IN_OFF))
#define PG_OLD   ((char *)OM_CT(OM_LW_STAGE + PG_OLD_OFF))
#define PG_NEW   ((char *)OM_CT(OM_LW_STAGE + PG_NEW_OFF))
static om_pergame s_pg;                 /* the panel's game, edited in place by om_overlay_key_game */
static char s_pg_id[OM_GID_MAX];        /* its id (Reset to defaults clears s_pg.id) */
static char s_pg_loader[40];            /* the Loader row's value */
static om_loader_choice s_pg_auto;      /* what Auto resolves to for it: the launch's own om_loader_pick */
static uint8_t s_pg_auto_ok;            /* s_pg_auto is known (the panel was opened on the game in the list) */
static char s_pg_key[17];               /* the game's GAMES.DB key, its CHEATS.PAK row (the editor's) */
static uint8_t s_pg_preload;            /* opened on the selected game: LOADBG.BIN is read meanwhile (loading.h) */
static uint16_t s_loop_t0;              /* this loop iteration's start (om_ut_frc): the panel preload's deadline */
static uint32_t s_loop_vb0;             /* and its V-blank (the static panel's read budget) */
static uint8_t s_pg_quiet;              /* iterations since the last press while the panel is open */
static uint16_t s_pg_idx;               /* the game the panel was opened on (its LW bytes' owner) */
#define PANEL_READ_VBL 6u               /* a static panel iteration reads until it has filled 6 V-blanks (10 fps); a
                                         * press ends it at the next 2 KB step (om_pad_peek), the music is topped up
                                         * between steps (om_audio_tick_low): neither waits for the iteration */
#define PANEL_QUIET    15u              /* after a press the panel runs at full rate for 15 iterations (the cursor) */
#ifdef OMSAT_FAKECARD
static uint32_t s_pg_mus0, s_pg_und0;   /* the music's bytes and underruns at the panel's open */
static uint32_t s_pg_presses;           /* and the presses the open panel was given (none lost) */
static uint32_t panel_read_vbl(void)
{   /* fakecard --pglong-script forces 8 V-blank iterations (7.5 it/s: far below the music's refill need at one tick
     * each, 3 x 2 KB, 46 KB/s against 88.2) */
    static int16_t slow = -1;
    if (slow < 0) slow = (int16_t)om_fakesat_has("aether/FAKESCRIPT.PGLONG");
    return slow ? 8u : PANEL_READ_VBL;
}
#else
#define panel_read_vbl() PANEL_READ_VBL
#endif
#ifdef OMSAT_FAKECARD
static uint8_t s_pg_fdmax;              /* the panel's FDMAX, iterations and V-blanks while it was open */
static uint32_t s_pg_it0, s_pg_vb0;
#endif
static om_ovgame s_ovg = { &s_pg, s_pg_loader };

static int pg_busy(void)
{
    return s_ov.kind == OM_OV_GAME || om_pop_active();   /* a popup runs over OM_LW_STAGE (popup.h) */
}

static PG_COLD void pg_msg(const char *what, int rc)
{
    char msg[OM_TOAST_MSG];
    om_msg_rc(msg, sizeof msg, what, om_strerror(rc), rc);
    om_folders_status(msg);
    om_sfx(OM_SFX_ERROR);
}

static PG_COLD int pg_read(uint32_t *len)
{   /* PERGAME.INI into PG_OLD: 0 (a missing file reads as empty), or the rc (OM_E_FULL: over the 16 KB cap). M6 Task
     * 21: through sdwrite.h's recovery rule (PERGAME.INI.NEW when an interrupted replace left only that) */
    int rc = om_sdw_read_file(OM_PATH_PERGAME, (uint8_t *)PG_OLD, PG_CAP, len);
    if (rc == OM_SD_NOFILE) { *len = 0; return OM_OK; }
    return rc;
}

static PG_COLD void pg_loader_text(void)
{   /* "Auto (CWX)", "Auto (JHL, database)", "JHL" ...; opened from the list the disc is not known: plain "Auto" */
    if (s_pg_auto_ok || s_pg.loader != OM_LDR_AUTO)
        om_loader_panel(s_pg.loader, &s_pg_auto, s_pg_loader, sizeof s_pg_loader);
    else
        strcpy(s_pg_loader, "Auto");
    /* Kai's cheats need CWX -- the Cheats row says so while the loader resolves to JHL */
    s_ovg.cheat_jhl = (uint8_t)((s_pg.loader == OM_LDR_AUTO ? (s_pg_auto_ok ? s_pg_auto.loader : OM_LDR_CWX)
                                                            : s_pg.loader) == OM_LDR_JHL);
}

static PG_COLD void pg_show(uint8_t kind)
{
    s_ov_kind_shown = kind;
    om_tween_to(&s_ov_slide, 256, frames(150));
}

static PG_COLD void pg_open(const char *gid, const char *seed_title)
{   /* the game's section (a missing file or section: every choice Default); a corrupt or oversize file reads as empty
     * and says so (the ruling carried from Task 15); the save then backs a corrupt file up, or refuses an oversize one.
     * No game id (no product code): Launch alone, nothing read or saved. */
    uint32_t len = 0;
    int rc = OM_OK;
    om_art_invalidate();                                   /* its fd closed: the stage is ours (the two-files rule) */
    om_pergame_default(&s_pg, gid);
    if (gid[0] && (rc = pg_read(&len)) == OM_OK && (rc = om_pergame_get(PG_OLD, len, gid, &s_pg)) > 0)
        rc = OM_OK;
    if (rc < 0) {
        om_pergame_default(&s_pg, gid);
        pg_msg("PER-GAME: PERGAME.INI READ AS EMPTY ", rc);
    }
    if (!s_pg.title[0]) {                                  /* written with the first change (the Card Manager shows it) */
        strncpy(s_pg.title, seed_title, OM_PG_TITLE_MAX);
        s_pg.title[OM_PG_TITLE_MAX] = 0;
    }
    strcpy(s_pg_id, gid);
    pg_loader_text();
    om_ovdraw_game_title(seed_title);                      /* after the panel's title, dimmed */
    om_overlay_open_game(&s_ov, &s_pg);
    pg_show(OM_OV_GAME);
}

/* 2026-11-25: Saves and Cheats show On or Off (overlay.h OM_OVD_*), Cheats with the game's count from CHEATS.PAK,
 * read into PG_NEW's slot (free while the panel is open: pg_save fills it only to write). At the panel's open, and again
 * after the Cheats popup saved CHEATS.PAK (pop_closed). */
static PG_COLD void pg_cheat_line(void)
{
    uint32_t on = 0, n = 0;
    int rc = s_pg_key[0] && !s_ovg.cheat_jhl ? om_cheats_count(s_pg_key, (uint8_t *)OM_CT(OM_LW_STAGE + PG_NEW_OFF),
                                                               PG_CAP, &on, &n) : OM_E_FIELD;
    s_ov.defs = (uint8_t)((om_saves_want(s_ps.set.saves, OM_PG_DEFAULT, 0) ? OM_OVD_SAVES : 0u) |
                          (s_ps.set.cheats ? OM_OVD_CHEATS : 0u));
    s_ovg.ch_ok = (uint8_t)(rc >= 0 || rc == OM_SD_NOFILE);
    s_ovg.ch_n = (uint8_t)(rc == 1 ? n : 0u);
    s_ovg.ch_on = (uint8_t)(rc == 1 ? on : 0u);
}

/* A on a game (user decision 2026-11-23: every launch goes through this panel, "Launch" its highlighted first row); on
 * a folder: nothing. 1 = the panel opened, for every game: one with no IP.BIN product number (or an unreadable header,
 * which a status line names) gets Launch alone (hardware report on 3c64908). 0 = not a game. C no longer opens it. */
static PG_COLD int pg_open_selected(const om_card *c, const om_card_bufs *bufs)
{
    const om_item *it = om_browse_selected(&s_b);
    const om_sysid *ip;
    om_lib_entry e;
    om_game gm;
    char gid[OM_GID_MAX], title[OM_PG_TITLE_MAX + 1];
    int have;
    if (!it || it->kind != OM_IT_GAME)
        return 0;
    om_art_invalidate();   /* an art job's file closed before game_gid may open the image (two-files rule) */
    have = game_of(c, it->idx, &e, &gm);
    ip = game_gid(c, &e, it->idx, gid);
    if (!ip) om_folders_status("DISC HEADER UNREADABLE: LAUNCH ONLY");
    om_title(&c->lib, &e, have ? &gm : NULL, title, sizeof title);
    s_pg_auto = om_loader_pick(OM_LDR_AUTO, s_ps.set.loader,
                               om_loader_db_hint(&c->db, !c->db_rc, e.key, ip ? ip->area_flags : e.area),
                               OM_CWX_AVAILABLE);
    s_pg_auto_ok = 1;
    memcpy(s_pg_key, e.key, sizeof s_pg_key - 1);
    s_pg_key[sizeof s_pg_key - 1] = 0;
    /* the panel's static Pad row, whatever pad is connected -- unknown (NULL) if this key has no compat row */
    {
        const char *note = NULL;
        uint8_t cf = 0;
        gdb_compat_note(c, e.key, ip ? ip->area_flags : e.area, &note, &cf);
        s_ovg.pad_note = note;
        s_ovg.igr_def = (uint8_t)om_igr_want(s_ps.set.igr, OM_PG_DEFAULT, cf);
    }
    om_sfx(OM_SFX_SELECT);
#ifdef OMSAT_FAKECARD
    if (om_boot_diag()) {   /* the most SD files open at once so far (the two-files rule) */
        char line[41], h[12];
        strcpy(line, "PANEL FDMAX ");
        strcat(line, om_itoa(h, om_fakesat_fd_max()));
        om_text_put(0, 21, line, 40);
        s_pg_fdmax = (uint8_t)om_fakesat_fd_max();
        s_pg_it0 = s_frame;
        s_pg_vb0 = om_v2_vblanks();
        s_pg_mus0 = om_audio_stream_bytes();
        s_pg_presses = 0;
        s_pg_und0 = om_audio_underruns();
    }
#endif
    pg_open(gid, title);
    pg_cheat_line();
    /* the user directive of 2026-11-20: the panel's time on screen reads the loading screen's background ahead */
    om_loading_preload_early((uint8_t *)bufs->work, (uint32_t)sizeof *bufs->work);
    s_pg_preload = 1;
    s_pg_idx = it->idx;
    return 1;
}

static PG_COLD void pg_save(void)
{   /* on close after a change: the file read again (the stage may have been borrowed meanwhile), the section put or
     * deleted, and the whole file written back -- sdwrite.h's write-then-replace (PERGAME.INI.NEW written
     * and verified, then renamed over PERGAME.INI), so a power cut keeps the previous file */
    uint32_t len = 0, n = 0;
    int rc;
    sd_quiet();                                            /* the music's and any art job's files closed */
    om_loading_preload_pause();
    if ((rc = pg_read(&len)) != OM_OK) { pg_msg("PER-GAME: NOT SAVED ", rc); return; }
    if (om_pergame_count(PG_OLD, len) < 0) {               /* corrupt: kept as PERGAME.BAK, then a clean file */
        if ((rc = om_sd_write_buf(OM_PATH_PERGAME_BAK, PG_OLD, len)) != 0) { pg_msg("PER-GAME: BACKUP FAILED ", rc); return; }
#ifdef OMSAT_FAKECARD
        if (om_boot_diag()) {   /* the backup's bytes, for a byte-exact check against the seed */
            char line[41], h[12];
            uint32_t crc = om_crc32(0, PG_OLD, len), i;
            static const char hexd[] = "0123456789ABCDEF";
            strcpy(line, "BAK LEN ");
            strcat(line, om_itoa(h, (int)len));
            strcat(line, " CRC ");
            for (i = 0; i < 8; i++) h[i] = hexd[(crc >> (28 - 4 * i)) & 15];
            h[8] = 0;
            strcat(line, h);
            om_text_put(0, 20, line, 40);
        }
#endif
        len = 0;
    }
    rc = s_pg.id[0] ? om_pergame_put(PG_OLD, len, &s_pg, PG_NEW, OM_PERGAME_MAX_BYTES, &n)
                    : om_pergame_del(PG_OLD, len, s_pg_id, PG_NEW, OM_PERGAME_MAX_BYTES, &n);
    if (rc != OM_OK) { pg_msg("PER-GAME: NOT SAVED ", rc); return; }
    if ((rc = om_sd_replace(OM_PATH_PERGAME, PG_NEW, n)) != 0) { pg_msg("PER-GAME: WRITE FAILED ", rc); return; }
    om_folders_status(s_pg.id[0] ? "Per-game options saved" : "Options reset to defaults");
#ifdef OMSAT_FAKECARD
    if (om_boot_diag()) {   /* the fake card's file, for a byte-exact check against omsat.pergame */
        char line[41], h[12];
        uint32_t crc = om_crc32(0, PG_NEW, n), i;
        static const char hexd[] = "0123456789ABCDEF";
        strcpy(line, "PG LEN ");
        strcat(line, om_itoa(h, (int)n));
        strcat(line, " CRC ");
        for (i = 0; i < 8; i++) h[i] = hexd[(crc >> (28 - 4 * i)) & 15];
        h[8] = 0;
        strcat(line, h);
        om_text_put(0, 22, line, 40);          /* row 23 is the audio line (audio.c) */
    }
#endif
}

static PG_COLD int pg_after_key(uint8_t kind, int f)
{   /* the game panel's close: its read-ahead paused, its changes saved; returns the flags left */
    if ((f & OM_OV_CLOSED) && kind == OM_OV_GAME) {
#ifdef OMSAT_FAKECARD
        if (om_boot_diag() && s_pg_preload) {   /* the panel's frame rate and read-ahead (KB) */
            char line[41], h[12];
            strcpy(line, "PANEL FDMAX ");
            strcat(line, om_itoa(h, s_pg_fdmax));
            strcat(line, " I");
            strcat(line, om_itoa(h, (int)(s_frame - s_pg_it0)));
            strcat(line, " V");
            strcat(line, om_itoa(h, (int)(om_v2_vblanks() - s_pg_vb0)));
            strcat(line, " K");
            strcat(line, om_itoa(h, (int)(om_loading_preload_have() / 1024u)));
            strcat(line, " P");
            strcat(line, om_itoa(h, (int)s_pg_presses));
            om_text_put(0, 21, line, 40);
            {   /* the music's refill while the panel was open, B/s against its 88,200 B/s
                 * (22,050 Hz, 2 channels, 16-bit; SPEC 5.12) at 60 V-blanks/s; PASS = no underrun and >= 95 % over
                 * 10 s or more (a shorter dwell is dominated by the ring's own fill level: SHORT). M6 Task 24 (18b
                 * review m1): a 5 % deficit over 15 s is about 66 KB, the room between a full ring and tick_low's
                 * floor, so the rate alone cannot see a slow drain -- PASS also needs the ring's lowest lead over the
                 * last 5 s (A<n>, blocks) to be at least 2 */
                uint32_t vb = om_v2_vblanks() - s_pg_vb0, b = om_audio_stream_bytes() - s_pg_mus0;
                uint32_t r = vb ? b / vb * 60u : 0, u = om_audio_underruns() - s_pg_und0, a5 = om_audio_min_ahead5();
                strcpy(line, "MUSR ");
                strcat(line, om_itoa(h, (int)r));
                strcat(line, " U");
                strcat(line, om_itoa(h, (int)u));
                strcat(line, " A");
                strcat(line, om_itoa(h, (int)a5));
                strcat(line, u ? " FAIL" : vb < 600u ? " SHORT" : r >= 88200u * 95u / 100u && a5 >= 2u ? " PASS"
                       : " FAIL");
                om_text_put(0, 3, line, 40);
            }
        }
#endif
        om_loading_preload_pause();                        /* its file closed; the bytes stay for the launch */
        s_pg_preload = 0;
        if (f & OM_OV_PGSAVE) pg_save();
    }
    return f;
}

/* 2026-11-24 hardware report (Settings > Import): the NBG0 text pages below drew over the live menu -- the last menu
 * frame stayed up while the import scan ran (no frame), so "IMPORT SAVES FROM KAI / RINGS" lay across the list and the
 * Settings panel. A page now starts on a clean screen: every menu layer (VDP1, NBG1/NBG2, back) black through colour
 * offset A, as the launch fade leaves it (MENU_FADE_MASK; NBG0 is not in it), an empty VDP1 list, and one slSynch so
 * the screen is clean before any blocking work. page_end gives the layers back; the menu's next frame redraws them. */
static OM_COLD void page_begin(void)
{
    om_text_set_inset(om_safe_margin(g_om_safe.level) / 8);
    om_text_clear();
    om_fade_black(MENU_FADE_MASK);
    om_v1_begin();
    slSynch();
}

static OM_COLD void page_end(void)
{
    om_text_clear();
    om_text_set_inset(0);
    om_fade_clear(MENU_FADE_MASK);                         /* an open panel's veil comes back with the next frame */
}

/* fwcheck.c om_fw_decide's notice() style -- plain NBG0 text at the safe-area inset, a physical
 * A/B wait, A+B+C+Start still resets. 1 = A: launch anyway, 0 = B: back, nothing written. Unlike fwcheck.c's own
 * continuing A, the loading screen that follows reads only the A+B+C+Start combo and ST_FAILED's "any press" (never
 * a bare A), and om_pad_poll's edge detection already tracks a held A continuously across the call -- om_pad_prime()
 * covers the "not a fresh press" rule with no extra wait. */
static OM_COLD int pad_warn(const char *note)
{
    uint32_t n = (uint32_t)strlen(note), cut = 0, j;
    Uint16 p = 0;
    page_begin();
    om_text_put(0, 8, "CONTROLLER WARNING", 34);
    if (n > 34u) {
        for (j = 1; j < n && j < 34u; j++)
            if (note[j] == ' ') cut = j;
        if (!cut) cut = 34;
        om_text_put(0, 10, note, (int)cut);           /* om_text_put itself caps at width: no copy needed */
        om_text_put(0, 11, note + cut + (note[cut] == ' ' ? 1 : 0), 34);
    } else
        om_text_put(0, 10, note, 34);
    om_text_put(0, 13, "A: LAUNCH ANYWAY   B: BACK", 34);
    om_pad_prime();
    for (;;) {
        Uint16 held;
        p = om_pad_poll();
        held = om_pad_held();
        if (om_pad_soft_reset(held)) om_return_official();
        if (p & (PER_DGT_TA | PER_DGT_TB)) break;
        om_audio_tick();                               /* the music's refill: a page is no reason to starve it */
        om_v1_begin();
        slSynch();
    }
    page_end();
    om_pad_prime();
    return (p & PER_DGT_TA) ? 1 : 0;
}

/* M6 Task 24: a slot folder's saves -- the "*.BUP" of /aether/<dir>'s current set (Task 24b, savegen.h: its newest
 * complete generation, else the folder itself; one listing at a time, each drained in full: satfs.c,
 * HWF-22); 0 for an absent folder, < 0 when the card does not answer. Kai's product-only folder is the game's while
 * the region folder is absent (pgsplan.h want_po, the launcher's own rule). */
static OM_COLD int slot_saves(const char *gid, uint8_t slot)
{
    char d[OM_PGS_DIR], p[80], c[80];
    om_gen_scr g;                                          /* savegen.h's listing buffers */
    uint32_t *st = g.st, size, gen;
    int len, n = 0, po = 0;
    if (!om_pgs_product_dir(d, gid)) {
        strcpy(p, "/aether/"); strcat(p, d);
        if (om_sd_size(p, &size) == 0) {
            om_pgs_slot_dir(d, gid, 0);
            strcpy(p, "/aether/"); strcat(p, d);
            po = om_sd_size(p, &size) != 0;
        }
    }
    if (om_pgs_slot_dir_po(d, gid, slot, po)) return -1;
    strcpy(p, "/aether/"); strcat(p, d);
    if (om_gen_cur(p, c, &gen, &g)) return -1;             /* the current set (its newest generation) */
    if ((len = s_opendir(om_sd_path(c))) < 0)
        return len == -FR_NO_PATH || len == -FR_NO_FILE ? 0 : -1;
    while ((len = s_stat(NULL, (s_stat_t *)st, (int)sizeof g.st - 4)) > 0) {
        s_stat_t *e = (s_stat_t *)st;
        const char *nm = e->name;
        if (len > 4 && nm[len - 4] == '.' && (nm[len - 3] | 32) == 'b' && (nm[len - 2] | 32) == 'u' &&
            (nm[len - 1] | 32) == 'p' && !(e->attrib & AM_DIR))
            n++;
    }
    return len == SAT_ETIMEDOUT ? -1 : n;
}

/* Slot "Ask" -- pad_warn's style (NBG0 text at the safe-area inset, A+B+C+Start still resets).
 * Left / Right pick 0-9 (each slot's status read as the cursor lands), A or Start launches, B goes back.
 * 1 = launch with *slot, 0 = back, nothing written. */
static OM_COLD int slot_pick(const char *gid, uint8_t *slot)
{
    char line[40], sel[16];
    int r = 0, shown = -1;
    page_begin();
    om_text_put(0, 8, "PER-GAME SAVES", 34);
    om_text_put(0, 14, "LEFT/RIGHT: SLOT", 34);
    om_text_put(0, 15, "A: LAUNCH   B: BACK", 34);
    om_pad_prime();
    for (;;) {
        Uint16 p, held;
        uint16_t k = 0;
        if (shown != *slot) {
            shown = *slot;
            strcpy(sel, "SAVE SLOT: < 0 >");
            sel[13] = (char)('0' + *slot);
            om_text_put(0, 10, sel, 34);
            om_slot_line(line, sizeof line, *slot, slot_saves(gid, *slot),
                         !strcmp(gid, s_act_gid) && s_act_slot == *slot);
            om_text_put(0, 12, "                                  ", 34);
            om_text_put(0, 12, line, 34);
        }
        p = om_pad_poll();
        held = om_pad_held();
        if (om_pad_soft_reset(held)) om_return_official();
        if (p & PER_DGT_KL) k |= OM_K_LEFT;
        if (p & PER_DGT_KR) k |= OM_K_RIGHT;
        if (p & PER_DGT_TA) k |= OM_K_A;
        if (p & PER_DGT_TB) k |= OM_K_B;
        if (p & PER_DGT_ST) k |= OM_K_START;
        if ((r = om_slot_key(slot, k)) != 0) break;
        om_audio_tick();                               /* the music's refill: a page is no reason to starve it */
        om_v1_begin();
        slSynch();
    }
    page_end();
    om_pad_prime();
    return r == OM_SP_LAUNCH;
}

/* the return block's place, at the menu's start (ret_write): the list replayed level by level (browse.h), then the
 * panel on its row -- a game's only if the entry is still that game (its GAMES.DB key); anything gone: the list as an
 * ordinary start leaves it, no message */
static OM_COLD void ret_place(void)
{   /* before the list is drawn: the list's place */
    const om_retblk *r = &g_om_ret;
    om_loc path[OM_RET_LOCS];
    uint32_t k;
    for (k = 0; k <= r->depth; k++) memcpy(&path[k], &r->loc[k], sizeof path[k]);
    if (!om_browse_replay(&s_b, path, r->depth, r->cur, r->top) && r->entry != 0xFFFFu)
        om_browse_reveal(&s_b, r->entry);            /* the game in its card folder instead */
}

static uint8_t s_ret_wait;              /* > 0: a tool's return waits for the game's cover before its panel */
static OM_COLD void ret_reopen(om_card *c, const om_card_bufs *bufs)
{   /* once the start-up screen has handed over (its toasts and the art job are the menu's by then) */
    const om_retblk *r = &g_om_ret;
    const om_item *sel;
    om_lib_entry e;
    om_game gm;
    if (r->ov_kind == OM_OV_GAME) {
        sel = om_browse_selected(&s_b);
        if (!sel || sel->kind != OM_IT_GAME || sel->idx != r->entry) return;
        (void)game_of(c, sel->idx, &e, &gm);
        if (strncmp(e.key, r->key, sizeof r->key) || !pg_open_selected(c, bufs)) return;
        if (r->ov_row < s_ov.count) {
            s_ov.row = r->ov_row;
            s_ov.top = r->ov_top <= r->ov_row ? r->ov_top : 0;
        }
    } else if (r->ov_kind == OM_OV_SETTINGS) {
        int ti = om_themes_find(s_ps.set.theme);
        om_overlay_open(&s_ov, OM_OV_SETTINGS, ti < 0 ? 0 : (uint16_t)ti, om_themes_count(), -1, 0);
        if ((r->ov_page == OM_OVP_MAIN && r->ov_row < OM_OVR_COUNT) ||
            (r->ov_page == OM_OVP_CONTROLS && r->ov_row < OM_OVC_COUNT)) {   /* else its first row */
            s_ov.page = r->ov_page;
            s_ov.row = r->ov_row;
            s_ov.top = r->ov_top <= r->ov_row ? r->ov_top : 0;
        }
        s_ov_kind_shown = OM_OV_SETTINGS;
        om_tween_to(&s_ov_slide, 256, frames(150));
    }
}

/* user decision 2026-11-21: the Kai/Rings import is optional, never automatic: Settings >
 * Import saves asks once, then hands off to LAUNCHER.BIN with OM_HO_IMPORT and no game. The launcher runs the copy-only
 * import with its progress and result on its own screen and resets the console back to Aether (pgsbak.h). It runs
 * there, not here: it reuses the save-swap arena and code the launcher already carries, so the menu pays only this
 * page (HWRAM), and it is a launcher step, never part of the in-game reset (which is a SYSRES with no SD write). */
/* M6 Task 24c (user decision 2026-11-23: the import is picked per game). Settings > Import scans Kai's and Rings'
 * folders here (impscan.h: one row per game, its save count; Kai's LATEST as "General saves") and lists them, every
 * one ticked: A ticks / unticks, Start imports the ticked ones, B goes back with nothing written. The rows and names
 * live in bufs->work (LWRAM scratch, as the diagnostics and the loading preload borrow it); only this code is HWRAM.
 * Start writes IMPORT.SEL (sdwrite) and puts its CRC in the hand-off's gid ("SEL<8 hex>"): the launcher's import mode
 * refuses any other file (pgsbak.h om_pgs_import_sel). 1 = import with o filled, 0 = back. */
#define PICK_ROWS 192u
#define PICK_VIS  16                     /* rows 4-19; row 21 is the last one at a 24 px safe area */
static OM_COLD int import_pick(om_card *c, const om_card_bufs *bufs, om_launch_opts *o)
{
    om_isc_row *rows = (om_isc_row *)(void *)bufs->work;
    om_isc_name *names = (om_isc_name *)(void *)(rows + PICK_ROWS);
    char *sel = (char *)(names + 256);
    uint32_t n = 0, cur = 0, top = 0, i, crc;
    uint32_t al[64], st[80];
    int len, rc, dirty = 1;
    Uint16 p;
    om_text_put(0, 7, "IMPORT SAVES FROM KAI / RINGS", 34);
    om_text_put(0, 9, "READING THE CARD...", 34);
    om_audio_tick();
    om_v1_begin();
    slSynch();                                             /* the text is up before the scan's blocking reads */
    /* no refill inside the scan: a listing is read in full before any other SD call (satfs.c, HWF-22); the ring's
     * ~1 s ahead covers the Kai/Rings folders' listings */
    if (om_isc_scan(rows, PICK_ROWS, &n, names, 256, al, st) || !n) {
        om_text_put(0, 9, n ? "THE CARD DOES NOT ANSWER." : "NO KAI OR RINGS SAVES ON THE CARD.", 34);
        om_text_put(0, 11, "B: BACK", 34);
        om_pad_prime();
        do { p = om_pad_poll(); if (om_pad_soft_reset(om_pad_held())) om_return_official(); om_audio_tick(); om_v1_begin(); slSynch(); }
        while (!(p & (PER_DGT_TA | PER_DGT_TB)));
        return 0;
    }
    om_text_clear();
    om_text_put(0, 1, "IMPORT SAVES: PICK THE GAMES", 34);
    om_text_put(0, 2, "A: TICK  START: IMPORT  B: BACK", 34);
    om_text_put(0, 21, "THEIR FOLDERS STAY AS THEY ARE.", 34);
    om_pad_prime();
    for (;;) {
        if (dirty) {
            for (i = 0; i < PICK_VIS; i++) {
                char line[40], t[8];
                uint32_t r = top + i, k;
                om_game g;
                const char *title;
                memset(line, ' ', 34);
                line[34] = 0;
                if (r < n) {
                    char key[17];
                    om_normalise(rows[r].key, (uint32_t)strlen(rows[r].key), key);
                    title = !strcmp(rows[r].key, OM_ISC_GENERAL) ? "GENERAL (KAI LATEST)"
                          : (!c->db_rc && om_gamesdb_find(&c->db, key, &g) && g.title_en && g.title_en[0]) ? g.title_en
                          : rows[r].key;
                    line[0] = r == cur ? '>' : ' ';
                    line[1] = '['; line[2] = rows[r].on ? 'X' : ' '; line[3] = ']';
                    k = 0;
                    { uint32_t v = rows[r].n; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 5); }
                    {   /* the title up to one space before the count */
                        uint32_t j;
                        for (j = 0; j < 28u - k && title[j]; j++) line[5 + j] = title[j];
                        for (j = 0; j < k; j++) line[33 - j] = t[j];
                    }
                }
                om_text_put(0, 4 + (int)i, line, 34);
            }
            dirty = 0;
        }
        p = om_pad_poll();
        if (om_pad_soft_reset(om_pad_held())) om_return_official();
        if (p & PER_DGT_TB) return 0;
        if (p & PER_DGT_KU) { if (cur) cur--; dirty = 1; }
        if (p & PER_DGT_KD) { if (cur + 1 < n) cur++; dirty = 1; }
        if (p & PER_DGT_TA) { rows[cur].on = (uint8_t)!rows[cur].on; dirty = 1; }
        if (cur < top) top = cur;
        if (cur >= top + PICK_VIS) top = cur - PICK_VIS + 1;
        if (p & PER_DGT_ST) {
            for (i = 0; i < n && !rows[i].on; i++) ;
            if (i == n) return 0;                          /* nothing ticked: as B */
            if ((len = om_isel_fmt(rows, n, sel, 4096, &crc)) < 0) return 0;
            om_audio_sd_pause();                           /* the write's files only (sd.c: the 2-file rule) */
            /* SAVES is made here too: on a card no game has launched from it is not there yet */
            if ((rc = om_sd_replace_in("/aether/SAVES", OM_ISC_SEL_PATH, sel, (uint32_t)len)) != 0) {
                char m[36];
                om_msg_rc(m, 35, "IMPORT.SEL: ", rc == OM_E_SHORT ? "CARD FULL?"
                          : rc == -100 - FR_NO_PATH ? "NO SAVES DIR" : "NOT WRITTEN", rc);
                om_text_put(0, 21, m, 34);
                continue;
            }
            memset(o, 0, sizeof *o);
            strcpy(o->gid, "SEL");
            for (i = 0; i < 8; i++) o->gid[3 + i] = "0123456789ABCDEF"[(crc >> (28 - 4 * i)) & 15u];
            o->gid[11] = 0;
            return 1;
        }
        om_audio_tick();                               /* the music's refill: a page is no reason to starve it */
        om_v1_begin();
        slSynch();
    }
}

/* The hand-offs that boot no game. mode 0: Settings > Manage / Import Saves > Import from Rings / Kai (the popup's
 * first row; the picker first). 2: Launch Options > Cheats (OM_HO_CHEATS, the game's key): LAUNCHER.BIN runs the
 * cheat editor and saves CHEATS.PAK, then resets. 3: Launch Options > Manage saves (OM_HO_SAVEMGR with the game's
 * id): that game's saves; 4: Settings > Manage / Import Saves (no id): every save. 2 and 3 pass the game's shown name
 * as the image ("x/<name>"). */
/* the launcher's screens continue the loading screen's look: the cover on screen now (VDP1 VRAM, intact until its
 * step 5) and the theme's colors */
static OM_COLD void hand_look(om_launch_opts *o)
{
    uint32_t tex;
    int w, h, tall;
    if (!om_art_loading() && om_art_hero(&tex, &w, &h, &tall)) {
        o->art = tex;
        o->art_w = (uint8_t)w;
        o->art_h = (uint8_t)h;
    }
    o->sty[0] = Sty.txt;
    o->sty[1] = Sty.acc2;
    o->sty[2] = Sty.acc;
}

static OM_COLD void load_run(om_card *c, const om_tree *t, const om_card_bufs *bufs, om_load_game *g);

/* 2026-11-25 (hardware: B in the cheat editor reset the console, the user sat through the whole boot): the return
 * block (handoff.h om_retblk) -- written here, unarmed, just before a tool's hand-off: the list's place (the browse
 * stack, the cursor) and the panel open on its row. The tool arms it on its way back; main.c takes it at the next
 * start, which skips the intro, and ret_reopen puts the list and the panel back. */
typedef char om_ret_loc_size[sizeof(om_loc) == sizeof(om_ret_loc) ? 1 : -1];
typedef char om_ret_locs_fit[sizeof s_b.stack / sizeof s_b.stack[0] < OM_RET_LOCS ? 1 : -1];
static OM_COLD void ret_write(void)
{
    om_retblk *r = (om_retblk *)OM_CT(OM_LW_RETURN);
    uint32_t k;
    memset(r, 0, sizeof *r);
    r->ov_kind = s_ov.kind;
    r->ov_page = s_ov.page;
    r->ov_row = s_ov.row;
    r->ov_top = s_ov.top;
    r->depth = s_b.depth;
    for (k = 0; k < s_b.depth; k++) memcpy(&r->loc[k], &s_b.stack[k], sizeof r->loc[k]);
    memcpy(&r->loc[s_b.depth], &s_b.loc, sizeof r->loc[0]);
    r->cur = s_b.cur;
    r->top = s_b.top;
    r->entry = s_ov.kind == OM_OV_GAME ? s_pg_idx : 0xFFFFu;
    if (s_ov.kind == OM_OV_GAME) memcpy(r->key, s_pg_key, sizeof r->key - 1);
    om_ret_seal(r);
}

/* 2026-11-25 (hardware: a black page with "STARTING..." before the cheat editor): a tool's hand-off runs the loading
 * screen as a launch does -- the menu fades out, the eye comes up with the tool's name, the game's title and cover,
 * LAUNCHER.BIN is read under it, and the tool continues the eye's last frame. No NBG0 text. */
static OM_COLD void import_request(om_card *c, const om_tree *t, Uint8 area, const om_card_bufs *bufs, int mode)
{
    static om_load_game g;
    static char path[OM_PG_TITLE_MAX + 4];                 /* g.path: read by the loading screen */
    static const char *const k_label[5] = { "IMPORT SAVES", "", "CHEATS", "MANAGE SAVES", "MANAGE SAVES" };
    if (om_card_old_folder && mode != 2) {                 /* the Aether rename: saves live in two folders until the
                                                            * Card Manager moves /vibemenu; cheats (mode 2) touch none */
        om_folders_status(OM_OLD_FOLDER_MSG);
        return;
    }
    if (mode == 2 || mode == 3) {                          /* the panel saved first if changed, as Launch does; under
                                                            * JHL (or with no key) the Cheats row only says so */
        om_loading_preload_pause();
        s_pg_preload = 0;
        if (s_ov.dirty) pg_save();
        if (mode == 2 && (s_ovg.cheat_jhl || !s_pg_key[0])) return;
    } else if (s_ov.dirty)
        save_settings();
    memset(&g, 0, sizeof g);
    if (!mode) {
        int go;
        page_begin();
        go = import_pick(c, bufs, &g.opts);                /* B, or nothing to import: back, nothing written */
        page_end();
        om_pad_prime();
        if (!go) return;
    }
    om_art_invalidate();                                   /* an in-flight art job closes its file */
    g.opts.loader = OM_LDR_CWX;                            /* a valid block; these boot no game */
    hand_look(&g.opts);
    if (mode == 2 || mode == 3) {
        uint32_t tex;
        int w, h, tall;
        char *q;
        g.art = !om_art_loading() && om_art_hero(&tex, &w, &h, &tall);
        g.square = g.art ? !tall : placeholder_square(c);
        strcpy(path, "x/");
        strcat(path, s_pg.title[0] ? s_pg.title : s_pg_key);
        for (q = path + 2; *q; q++) if (*q == '/') *q = '-';
        strncpy(g.title, path + 2, sizeof g.title - 1);
        if (mode == 2) strcpy(g.opts.ckey, s_pg_key);
        else strcpy(g.opts.gid, s_pg_id);
    } else {
        g.opts.art = 0; g.opts.art_w = g.opts.art_h = 0;   /* Settings: no game, no cover */
        strcpy(path, mode ? "aether/SAVES" : "aether/IMPORT");
        strcpy(g.title, mode ? "All saves" : "Kai and Rings saves");
    }
    g.label = k_label[mode];
    g.path = path;
    g.area = area;
    g.flags = mode == 2 ? OM_HO_CHEATS : mode ? OM_HO_SAVEMGR : OM_HO_IMPORT;
    ret_write();                                           /* where to come back to */
    om_audio_event(OM_AEV_MUSIC_FADE_OUT);                 /* as a launch: the music fades with the menu */
    load_run(c, t, bufs, &g);
}

/* 2026-11-25 (user-approved mockup, popups.html): Cheats and Manage saves open as large popups over the dimmed list
 * (popup.h, POPUP.BIN in LWRAM) instead of handing off. mode as import_request's (2 Cheats, 3 a game's saves, 4 every
 * save), or 5: Settings > About (2026-11-27, no hand-off: a failed open is only named). The card-scan work area (the
 * panel's LOADBG read-ahead dropped) and the art job's decode canvas are lent while it is open. 1: handled (open, or
 * refused here as import_request would); 0: no popup -- hand off instead. */
static OM_COLD void pop_closed(const om_card_bufs *bufs, int saved);
static void idle_rearm(void);
static OM_COLD int pop_try(const om_card *c, const om_card_bufs *bufs, int mode)
{
    om_pop_req r;                                          /* copied by the overlay's open */
    char why[OM_TOAST_MSG];
    int rc;
    if (om_card_old_folder && mode != 2 && mode != 5) { om_folders_status(OM_OLD_FOLDER_MSG); return 1; }
    if (mode == 2 || mode == 3) {
        om_loading_preload_pause();
        s_pg_preload = 0;
        if (s_ov.dirty) pg_save();
        if (mode == 2 && (s_ovg.cheat_jhl || !s_pg_key[0])) return 1;
    } else if (s_ov.dirty)
        save_settings();
    om_loading_preload_drop();                             /* its HWRAM and LW bytes are the popup's now */
    om_loading_preload_lw_drop();
    memset(&r, 0, sizeof r);
    r.kind = mode == 2 ? OM_POP_CHEATS : mode == 3 ? OM_POP_SAVES : mode == 5 ? OM_POP_ABOUT : OM_POP_SAVES_ALL;
    r.cheat_jhl = s_ovg.cheat_jhl;
    if (mode == 2 || mode == 3) {
        strncpy(r.gid, s_pg_id, sizeof r.gid - 1);
        strncpy(r.key, s_pg_key, sizeof r.key - 1);
        strncpy(r.title, s_pg.title[0] ? s_pg.title : s_pg_key, sizeof r.title - 1);
    }
    r.sty = &Sty;
    r.db = c->db_rc ? NULL : &c->db;
    r.arena = (uint8_t *)bufs->work;
    r.arena_cap = (uint32_t)sizeof *bufs->work;
    r.tables = om_art_lend();                              /* the art job dropped; finished covers stay on screen */
    r.tables_cap = OM_ART_LEND_BYTES;
    r.fps = s_fps;
    if ((rc = om_pop_open(&r, (uint8_t *)bufs->work, why, sizeof why)) == 0) return 1;
    pop_closed(bufs, 0);
    {   /* named, then the launcher's tool runs as before */
        char msg[OM_TOAST_MSG];
        om_msg_rc(msg, sizeof msg, why[0] ? why : "Popup", "", rc);
        om_folders_status(msg);
    }
    return 0;
}

/* after a popup (or a failed open): the stage and the work area are the panel's again */
static OM_COLD void pop_closed(const om_card_bufs *bufs, int saved)
{
    om_art_invalidate();                                   /* the canvas was lent: the selection's art re-requested */
    if (s_ov.kind == OM_OV_GAME) {                         /* the panel's read-ahead starts over */
        if (saved) pg_cheat_line();                        /* the Cheats row's count, from the file just written */
        om_loading_preload_early((uint8_t *)bufs->work, (uint32_t)sizeof *bufs->work);
        s_pg_preload = 1;
    }
    idle_rearm();
}

/* the loading screen and the hand-off, or back to the menu (a canceled or failed launch): a game's launch and, with
 * g->label set, a tool that boots no game (the cheat editor, Manage saves, the import) -- the same fade, the same eye */
static OM_COLD void load_run(om_card *c, const om_tree *t, const om_card_bufs *bufs, om_load_game *g)
{
    om_load_env env;
    om_load_out out;
    fade_ctx fc;
    int ti;
    char theme_msg[OM_TOAST_MSG];
    /* a per-game panel's early read of LOADBG.BIN (its HWRAM part) is adopted here, not read again */
#ifdef OMSAT_FAKECARD
    {   /* the adoption's cost (lw_verify hashes up to 92 KB of LWRAM), before the fade-out */
        uint32_t v = om_v2_vblanks();
        uint16_t t = om_ut_frc();
        om_loading_preload_begin((uint8_t *)bufs->work, (uint32_t)sizeof *bufs->work);
        if (om_boot_diag()) {
            char line[41], h[12];
            strcpy(line, "ADOPT US");
            strcat(line, om_itoa(h, (int)om_ut_frc_us((uint16_t)(om_ut_frc() - t))));
            strcat(line, " VB");
            strcat(line, om_itoa(h, (int)(om_v2_vblanks() - v)));
            om_text_put(0, 20, line, 40);
        }
    }
#else
    om_loading_preload_begin((uint8_t *)bufs->work, (uint32_t)sizeof *bufs->work);
#endif
    fc.c = c; fc.t = t; fc.preload = 1; fc.vb_last = 0; fc.misses = 0;
    om_fade_out(MENU_FADE_MASK, MENU_FADE_MS, s_ps.set.anim, fade_draw, &fc);
    env.sty = &Sty;
    env.anim = s_ps.set.anim;
    env.fps = s_fps;
    env.loaded = g->label ? tool_loaded : recent_written;   /* a tool: no RECENT.TXT, the music's resume point */
    env.ctx = c;
    om_loading_run(g, launch_bufs(), &env, &out);
    om_diag_record_load(&out);   /* the diag screen's "last run" page -- out goes out of scope below */
    if (out.result == OM_LOAD_HANDOFF) om_audio_handoff();    /* every slot off, common word restored */
#ifndef OMSAT_FAKECARD
    if (out.result == OM_LOAD_HANDOFF)
        om_launch_jump();                                  /* does not return; the screen is black (faded) */
#else
    if (out.result == OM_LOAD_HANDOFF && g->label && om_fakesat_has("aether/FAKESCRIPT.RETURN")) {
        /* the emulator tier's round trip: the block armed and carried in sound RAM as the tool's B does (lmain.c
         * om_go_home), then the menu started again from its first instruction (SGL_Start, 0x06004000) through the
         * hand-off's own jump (the slave stopped, interrupts masked, the cache purged) -- the BIOS's CD boot is the
         * one step this leaves out (the L1 harnesses cover it); a console reset would play the BIOS's start-up chime
         * over sound RAM. The next start must skip the intro and reopen here. */
        static const uint16_t stub[6] = { 0xD001, 0x402B, 0x0009, 0x0009, 0x0600, 0x4000 };   /* mov.l @(4,pc),r0;
                                                                                               * jmp @r0; .long */
        uint32_t n;
        if (om_ret_arm((om_retblk *)OM_CT(OM_LW_RETURN)))
            om_ret_copy16((volatile void *)OM_SND_RETURN, (const volatile void *)OM_CT(OM_LW_RETURN));
        om_audio_handoff();
        for (n = 0; n < 6u; n++) ((volatile uint16_t *)OM_CT(OM_LAUNCHER_LOAD))[n] = stub[n];
        om_launch_jump();
    }
#endif
    /* back to the menu, still black: its LWRAM data, theme and colors, then it fades up */
    read_active();                                         /* the details card's "Saves in console" */
    om_art_invalidate();
    if (out.li.clobbered)
        om_card_reload_lw(c, bufs);
    ti = om_themes_find(s_ps.set.theme);
    {
        uint32_t ser = s_status_serial;
        show_theme(ti < 0 ? OM_THEME_SATURN : (uint16_t)ti);   /* the NBG1 bitmap and its palette (CRAM 256-511) */
        theme_msg[0] = 0;
        if (s_status_serial != ser) {                      /* a theme / BG.BIN error is kept */
            strncpy(theme_msg, s_status, sizeof theme_msg - 1);
            theme_msg[sizeof theme_msg - 1] = 0;
        }
    }
    rows_invalidate();
    s_content_serial++;
    draw_panels(c, 0);                                     /* the panels back at once, while black (not 8 cells a frame) */
    om_v2_rects_flush();
    {   /* the launch's outcome. Review-13c M7: a theme / BG.BIN reload error is never overwritten -- after a failure
         * the launch's rc shows first and the theme error is queued for the next press; otherwise the theme error wins */
        if (out.result == OM_LOAD_FAILED) {
            om_folders_status(out.msg);
            om_sfx(OM_SFX_ERROR);
            if (theme_msg[0])
                strcpy(s_status_next, theme_msg);
        } else if (theme_msg[0])
            om_folders_status(theme_msg);
        else if (out.result == OM_LOAD_CANCEL)
            om_folders_status("Launch canceled");
#ifdef OMSAT_FAKECARD                                      /* fake-only wording stays out of --app */
        else                                               /* the fake build stops short of the hand-off */
            om_folders_status("Fake build: would hand off now");
#endif
    }
    om_audio_event(OM_AEV_MUSIC_RESUME);                   /* Task 15 */
    s_sr_latch = 1;
#ifdef OMSAT_FAKECARD
    s_diag_reset = 1;
    if (om_boot_diag()) {   /* only with `fakecard --diag` (M3-R40/M3-R45) */
        char line[41], n[12];
        strcpy(line, "LOAD R");
        strcat(line, om_itoa(n, out.result));
        strcat(line, " F");
        strcat(line, om_itoa(n, (int)out.frames));
        strcat(line, " M");
        strcat(line, om_itoa(n, (int)out.misses));
        strcat(line, " BG");
        strcat(line, om_itoa(n, (int)out.bg_frames));
        strcat(line, " L");
        strcat(line, om_itoa(n, (int)out.bg_wraps));
        strcat(line, " RC");
        strcat(line, om_itoa(n, out.rc));
        strcat(line, " FO");
        strcat(line, om_itoa(n, (int)fc.misses));               /* the menu's fade-out (preload) misses */
        strcat(line, " X");
        strcat(line, om_itoa(n, (int)out.crc_misses));          /* the one wrap CRC check (fake only) */
        strcat(line, " LT");
        strcat(line, om_itoa(n, (int)out.bg_late));             /* background V-blanks late */
        strcat(line, " PV");
        strcat(line, om_itoa(n, (int)out.pre_vbl));             /* LOADBG.BIN preload, V-blanks */
        om_text_put(0, 24, line, 40);
    }
#endif
    fc.preload = 0;
    fc.vb_last = 0;
    om_fade_in(MENU_FADE_MASK, MENU_FADE_MS, s_ps.set.anim, fade_draw, &fc);
    s_vb_restart = 1;
    memset(s_ph, 0, sizeof s_ph);                          /* the fades' frame_draw time is not one menu frame's */
}

static OM_COLD void launch_selected(om_card *c, const om_tree *t, Uint8 area, const om_card_bufs *bufs)
{
    const om_item *it = om_browse_selected(&s_b);
    static om_load_game g;
    om_lib_entry e, e1;
    om_game gm;
    int have;
    uint8_t rbits;
    g.label = NULL;                                        /* a game: "NOW LOADING" */
    {   /* what the loading screen shows: the details card's data, in the mockup's two meta lines */
        const om_sysid *ip = om_info_find(it->idx);
        uint32_t tex;
        int w, h, tall;
        have = game_of(c, it->idx, &e, &gm);
        e1 = e;
        e1.disc_total = 1;                                 /* "Disc n of m" is on the second meta line instead */
        om_title(&c->lib, &e1, have ? &gm : NULL, g.title, sizeof g.title);
        om_load_meta(&e, have ? &gm : NULL, have ? om_gamesdb_genre(&c->db, gm.genre) : NULL, ip, g.meta1,
                     sizeof g.meta1, g.meta2, sizeof g.meta2);
        g.art = !om_art_loading() && om_art_hero(&tex, &w, &h, &tall);   /* never the previous game's cover */
        g.square = g.art ? !tall : placeholder_square(c);
        g.path = e.path;                                   /* in the LIBRARY.IDX pool (HWRAM s_libbuf) */
        g.area = area;
        rbits = ip ? ip->area_flags : e.area;              /* the compat lookup's region bits */
    }
    s_launch_idx = it->idx;
    om_audio_event(OM_AEV_SELECT);                         /* Task 15 */
    om_audio_event(OM_AEV_MUSIC_FADE_OUT);                 /* the music fades out here */
    /* the card-scan work area is free once the card has loaded (main.c); nothing the launch needs lives there: the
     * hand-off block and LAUNCHER.BIN go to LWRAM (s_lb), RECENT.TXT is written from s_ps and the path pool */
    om_art_invalidate();                                   /* an in-flight art job closes its file: one SD file at a time */
    launch_opts(c, it->idx, &e, &g);                       /* after the art job, which stages in OM_LW_STAGE */
    hand_look(&g.opts);
    /* the hand-off flag mirrors launch_opts' own om_igr_want result (g.opts.igr) -- not a second,
     * independently-fed om_igr_want call -- so this and the per-game options byte cannot disagree. */
    g.flags = g.opts.igr ? 0u : OM_HO_NO_IGR;
    {   /* after the loader choice, before anything is written (no SD write has happened yet; the
         * audio events above are not writes). B: back goes no further. */
        const char *note = NULL;
        uint8_t flags = 0;
        gdb_compat_note(c, e.key, rbits, &note, &flags);
        if (om_pad_conflict(om_pad_kind(om_pad_port1_id(), 0), flags) && !pad_warn(note ? note : "Use a standard pad")) {
            om_audio_event(OM_AEV_MUSIC_RESUME);
            om_folders_status("Launch canceled");          /* the same text the loading screen's own cancel posts */
            return;
        }
    }
    if (s_slot_ask && !slot_pick(g.opts.gid, &g.opts.slot)) {   /* Slot "Ask"; nothing written yet */
        om_audio_event(OM_AEV_MUSIC_RESUME);
        om_folders_status("Launch canceled");
        return;
    }
    load_run(c, t, bufs, &g);
}

/* --- the idle screensaver (M3.5 decision 8) ----------------------------------------------------------- */
static om_idle s_idle_t;
static uint32_t s_saver_t;       /* saver frames since it started: the logo's own fade-up */

static void idle_rearm(void)
{   /* a new timeout from Settings, or a blocking screen just ended (diagnostics, a launch): count from now, or the
     * saver would start at once */
    om_idle_set(&s_idle_t, om_idle_vblanks(s_ps.set.saver_min, s_fps), om_v2_vblanks());
}

static OM_COLD void saver_start(const om_card *c, const om_tree *t)
{   /* the menu to black as the launch path fades it (color offset A on every menu layer), then only the logo is lit:
     * the sprite layer moves to offset B (ST-058-R2 color offset select, as interstitial.c does), which ramps up */
    fade_ctx fc;
    om_saver_begin(om_v2_vblanks(), om_safe_margin(s_ps.set.overscan));
    fc.c = c; fc.t = t; fc.preload = 0; fc.vb_last = 0; fc.misses = 0;
    om_fade_out(MENU_FADE_MASK, MENU_FADE_MS, s_ps.set.anim, fade_draw, &fc);
    slColOffsetB(-255, -255, -255);
    slColOffsetBUse(SPRON);
    s_saver_t = 0;
}

static void saver_frame(void)
{
    int v = s_saver_t >= 15u ? 0 : -255 + 17 * (int)s_saver_t;   /* 250 ms up from black (FADE_IN, interstitial.c) */
    s_saver_t++;
    slColOffsetB((Sint16)v, (Sint16)v, (Sint16)v);
    om_v1_begin();
    om_saver_draw();
    slSynch();                                                 /* one per loop iteration (HWF-16) */
}

static OM_COLD void saver_stop(const om_card *c, const om_tree *t)
{   /* the sprites back on offset A (still -255), then the menu fades in as after a launch */
    fade_ctx fc;
    slColOffsetB(0, 0, 0);
    slColOffsetAUse(SPRON);
    fc.c = c; fc.t = t; fc.preload = 0; fc.vb_last = 0; fc.misses = 0;
    om_fade_in(MENU_FADE_MASK, MENU_FADE_MS, s_ps.set.anim, fade_draw, &fc);
    s_vb_restart = 1;                                          /* the saver's frames are not missed menu frames */
    memset(s_ph, 0, sizeof s_ph);
}

/* the remappable buttons, in remap.h's OM_BTN_* order (SL_DEF.H:1250-1262, HWF-14). The D-pad, Start, the
 * L+R+Start diagnostics combo and A+B+C+Start stay physical (safety); every other button press goes through
 * s_ps.set.pad. */
static const uint16_t k_btn_mask[OM_BTN_COUNT] = { PER_DGT_TA, PER_DGT_TB, PER_DGT_TC, PER_DGT_TX, PER_DGT_TY,
                                                   PER_DGT_TZ, PER_DGT_TL, PER_DGT_TR };
#define ACT(a) (1u << (a))

static uint16_t keys_of(Uint16 p, uint16_t a)
{   /* a = om_remap_acts(p): OK / Back / Filter come through the map */
    uint16_t k = 0;
    if (p & PER_DGT_KU) k |= OM_K_UP;
    if (p & PER_DGT_KD) k |= OM_K_DOWN;
    if (p & PER_DGT_KL) k |= OM_K_LEFT;
    if (p & PER_DGT_KR) k |= OM_K_RIGHT;
    if (a & ACT(OM_ACT_OK)) k |= OM_K_A;
    if (a & ACT(OM_ACT_BACK)) k |= OM_K_B;
    if (a & ACT(OM_ACT_FILTER)) k |= OM_K_Z;
    if (p & PER_DGT_ST) k |= OM_K_START;
    return k;
}

static const om_icon *badge_of(uint8_t btn)
{
    static const om_icon *const k[OM_BTN_COUNT] = { &om_icon_badge_a, &om_icon_badge_b, &om_icon_badge_c,
        &om_icon_badge_x, &om_icon_badge_y, &om_icon_badge_z, &om_icon_badge_l, &om_icon_badge_r };
    return btn < OM_BTN_COUNT ? k[btn] : &om_icon_badge_a;
}

static void pad_map_changed(void)
{   /* the footer badges, the overlay hints, the auto-repeat (paging) and the launch hold-back follow the map */
    int i;
    for (i = 0; i < OM_NHINTS; i++)
        om_ut_icon(&s_badge[i], om_ui_hint_act[i] < OM_ACT_COUNT ? badge_of(s_ps.set.pad.btn[om_ui_hint_act[i]])
                                                                  : &om_icon_badge_st);
    om_ovdraw_hints(&s_ps.set.pad);
    om_pad_set_repeat((Uint16)(PER_DGT_KU | PER_DGT_KD | om_remap_mask(&s_ps.set.pad, OM_ACT_PGUP, k_btn_mask) |
                               om_remap_mask(&s_ps.set.pad, OM_ACT_PGDN, k_btn_mask)));
    om_istl_set_ok(om_remap_mask(&s_ps.set.pad, OM_ACT_OK, k_btn_mask));
}

static void save_settings(void)
{
    char msg[OM_TOAST_MSG];
    ph_mark pm = ph_now();
    int rc;
    sd_quiet();                                            /* also frees SET_IN/SET_OUT's stage slots (the art job) */
    rc = om_persist_save_settings(&s_ps, SET_IN, SET_OUT);
    om_audio_save_state();                                 /* a safe moment for MUSICPOS.TXT too */
    ph_add(PH_SAVE, pm);
    if (rc) {
        om_msg_rc(msg, sizeof msg, "SETTINGS.INI NOT SAVED: ", om_strerror(rc), rc);
        om_folders_status(msg);
    }
}

static int16_t genre_rank_of(const om_card *c)
{
    uint16_t id = genre_id(c, s_ps.set.filter_genre);
    return id == OM_FILTER_ANY ? -1 : (int16_t)s_sort.genre_rank[id];
}

static const char *genre_name_at(const om_card *c, int16_t rank)
{
    const char *g = rank < 0 || c->db_rc ? NULL : om_gamesdb_genre(&c->db, s_sort.genre_by_rank[rank]);
    return g ? g : "all";
}

static void show_theme(uint16_t i)
{
    char msg[41];
    int rc;
    ph_mark pm = ph_now();
    om_art_invalidate();                                     /* themes stage through OM_LW_STAGE (HWF-12) */
    rc = om_themes_apply(i, (uint8_t *)OM_CT(OM_LW_STAGE), OM_LW_STAGE_MAX, msg);
    om_art_theme_changed();                                  /* the virtual-folder tiles take the new colors */
    if (rc && !msg[0])                                       /* Task 11 hand-off: a failure never blanks the line */
        om_msg_rc(msg, sizeof msg, "THEME: ", om_strerror(rc), rc);
    if (rc || msg[0])
        om_folders_status(msg);
    style_update();
    ph_add(PH_THEME, pm);
}

static void apply_theme(uint16_t i)
{
    show_theme(i);
    om_logo_rebind();                                        /* header logo edges over the new background */
    strncpy(s_ps.set.theme, om_themes_name(i), sizeof s_ps.set.theme - 1);
    s_ps.set.theme[sizeof s_ps.set.theme - 1] = 0;
}

static void clock_status(void)
{   /* a bad RTC byte is named once per rc change; it clears only while the status line still shows it */
    static char shown[41];
    int rc = om_rtc_rc();
    char msg[41];
    if (rc) {
        om_msg_rc(msg, sizeof msg, "CLOCK: BAD RTC", "", rc);
        if (strcmp(msg, shown)) {
            status_auto(msg);
            strcpy(shown, msg);
        }
    } else if (shown[0]) {
        if (!strcmp(s_status, shown))
            status_auto("");
        shown[0] = 0;
    }
}

static void close_overlay(const om_card *c, int flags, uint8_t kind)
{
    if (kind == OM_OV_FILTER) {
        const char *g = s_ov.genre < 0 ? "" : genre_name_at(c, s_ov.genre);
        strncpy(s_ps.set.filter_genre, g, sizeof s_ps.set.filter_genre - 1);
        s_ps.set.filter_genre[sizeof s_ps.set.filter_genre - 1] = 0;
        apply_view(c);
        om_folders_status(s_ps.set.filter_region || s_ps.set.filter_genre[0] ? "FILTER ON" : "FILTER OFF");
    }
    if (flags & OM_OV_CHANGED)
        save_settings();
}

static OM_COLD void load_lists(om_card *c, const om_tree *t)
{
    char msg[OM_TOAST_MSG], num[12];
    int ti;
    const om_gamesdb *db = c->db_rc ? NULL : &c->db;
    /* boot steps 4 (settings, favorites, recent), 3 (themes, BG.BIN) and 5 (sort contexts, browse lists),
     * timed (boottime.h). The sort build moved after the other two (it reads neither) so step 5 is one span. */
    om_boot_step_begin(OM_BT_PERSIST);
    /* OM_LW_STAGE is free here: no art job has started yet (HWF-12). s_b's scratch arrays are only borrowed. */
    om_persist_load(&s_ps, &c->lib, s_b.scratch, s_b.tmp, (uint8_t *)OM_CT(OM_LW_STAGE), OM_LW_STAGE_MAX);
    om_safe_set(&g_om_safe, s_ps.set.overscan);                    /* before the logo rebind below */
    read_active();                                                 /* M6 Task 24 */
    om_boot_step_end(OM_BT_PERSIST);
    om_boot_step_begin(OM_BT_BG);
    om_themes_scan();
    ti = om_themes_find(s_ps.set.theme);
    /* Task 11 hand-off: an unknown theme= shows the built-in but keeps the user's value in s_ps.set.theme, so the next
     * SETTINGS.INI save does not overwrite it; only choosing a theme in the settings overlay (apply_theme) does. */
    om_istl_bg_dip(ti >= 0 && (uint16_t)ti != OM_THEME_SATURN);   /* no pop under the interstitial */
    show_theme(ti < 0 ? OM_THEME_SATURN : (uint16_t)ti);
    /* the logos' edges blended with the background now shown (OM_LW_STAGE is free again after show_theme) */
    om_istl_rebind((uint8_t *)OM_CT(OM_LW_STAGE), OM_LW_STAGE_MAX);
    om_logo_rebind();
    om_boot_step_end(OM_BT_BG);
    if (s_ps.set_rc) {
        om_msg_rc(msg, sizeof msg, "SETTINGS.INI: ", om_strerror(s_ps.set_rc), s_ps.set_rc);
        om_folders_status(msg);
    } else if (ti < 0) {
        om_msg_rc(msg, sizeof msg, "THEME NOT FOUND: ", s_ps.set.theme, OM_E_FIELD);
        om_folders_status(msg);
    } else if (s_ps.set.anim_bad[0]) {                             /* like theme= */
        om_msg_rc(msg, sizeof msg, "ANIMATIONS: UNKNOWN anim=", s_ps.set.anim_bad, OM_E_FIELD);
        om_folders_status(msg);
    } else if (s_ps.set.pad_bad) {                                 /* pad_* named a button twice */
        om_folders_status("CONTROLS: BUTTON USED TWICE, DEFAULTS");
    } else if (s_ps.set.bad_key[0]) {                              /* the first unrecognized new key */
        om_msg_rc(msg, sizeof msg, "SETTINGS: BAD VALUE ", s_ps.set.bad_key, OM_E_FIELD);
        om_folders_status(msg);
    } else if (s_ps.favs_rc || s_ps.recent_rc) {
        int rc = s_ps.favs_rc ? s_ps.favs_rc : s_ps.recent_rc;
        om_msg_rc(msg, sizeof msg, s_ps.favs_rc ? "FAVS.TXT: " : "RECENT.TXT: ", om_strerror(rc), rc);
        om_folders_status(msg);
    } else if (s_ps.favs.dropped || s_ps.recent.dropped) {
        om_itoa(num, s_ps.favs.dropped + s_ps.recent.dropped);
        strcpy(msg, "FAVS/RECENT: ");
        strcat(msg, num);
        strcat(msg, " not on card, dropped");
        om_folders_status(msg);
    }
    om_boot_step_begin(OM_BT_SORT);
    om_sortctx_build(&s_sort, &c->lib, db);
    om_browse_init(&s_b, t, &c->lib, &s_sort, db, &s_ps.favs, &s_ps.recent);
    om_boot_step_end(OM_BT_SORT);
}

/* --- slots ----------------------------------------------------------------------------------------------------- */
static void alloc_or_fatal(om_ut *t, int cap, int h)
{
    int rc = om_ut_alloc(t, cap, h);
    if (rc)
        om_fatal("UI: VDP1 VRAM POOL FULL (uitext)", rc);
}

static OM_COLD void alloc_slots(void)
{
    static const om_icon *const k_badge[OM_NHINTS] = { &om_icon_badge_a, &om_icon_badge_b, &om_icon_badge_x,
                                                       &om_icon_badge_y, &om_icon_badge_z, &om_icon_badge_st };
    int i, hi = om_uf_height(&om_face_item), hs = om_uf_height(&om_face_small), ht = om_uf_height(&om_face_title);
    om_ut_reset_pool();
    for (i = 0; i < NSLOT; i++) {                                  /* plain rows: box width (fade-cut) */
        alloc_or_fatal(&s_row[i].title, 176, hi);
        alloc_or_fatal(&s_row[i].flag, OM_FLAG_TEX_W, OM_FLAG_H);
        s_row[i].item = -1;
    }
    if (om_ut2_alloc(&s_sel, 504, hi))                             /* the selected row, double-buffered */
        om_fatal("UI: VDP1 VRAM POOL FULL (uitext)", OM_E_FULL);
    alloc_or_fatal(&s_selflag, OM_FLAG_TEX_W, OM_FLAG_H);
    alloc_or_fatal(&s_pillstar, 8, 8);
    om_ut_icon(&s_pillstar, &om_icon_star);
    alloc_or_fatal(&s_brand1, om_icon_wordmark.w, om_icon_wordmark.h);
    om_ut_icon(&s_brand1, &om_icon_wordmark);
    alloc_or_fatal(&s_clk, 72, ht);
    if (om_ut2_alloc(&s_crumb, 504, hs) || om_ut2_alloc(&s_dtitle, 504, ht) || om_ut2_alloc(&s_dmeta, 504, ht) ||
        om_ut2_alloc(&s_reason, 504, hs))                          /* rewritten while on screen */
        om_fatal("UI: VDP1 VRAM POOL FULL (uitext)", OM_E_FULL);
    alloc_or_fatal(&s_count, 56, hs);
    alloc_or_fatal(&s_noart, 40, hs);
    alloc_or_fatal(&s_rc, 72, hs);
    for (i = 0; i < OM_NHINTS; i++) {
        alloc_or_fatal(&s_hint[i], 48, hs);
        alloc_or_fatal(&s_badge[i], 24, 11);
        om_ut_icon(&s_badge[i], k_badge[i]);
    }
    for (i = 0; i < 2; i++) {
        alloc_or_fatal(&s_folder[i], 16, 8);
        om_ut_icon(&s_folder[i], &om_icon_folder);
        alloc_or_fatal(&s_star[i], 8, 8);
        om_ut_icon(&s_star[i], &om_icon_star);
    }
    om_ovdraw_alloc(alloc_or_fatal);
    om_loading_alloc(alloc_or_fatal);                              /* the loading screen's own slots */
    om_art_cache_claim();                                          /* the pool's unused top -> cover slot C2 */
    om_ut_budget_us(1 << 30);                                      /* boot: no frame to protect yet */
    om_ovdraw_prewarm();
    om_ui_hint_layout(s_badge_x, s_label_x);
}

static void icon_colors(void)
{   /* the icons' LUTs: 0 transparent, ramp from the backdrop to the icon colour (normal row / selected row) */
    uint16_t bsel_icon = om_c555_mix(Sty.bar_l, Sty.bar_r, 10u * 256u / 172u);
    uint16_t bsel_star = om_c555_mix(Sty.bar_l, Sty.bar_r, 143u * 256u / 172u);
    om_ut_colors(&s_folder[0], Sty.folder, Sty.list_eff, s_in);
    om_ut_colors(&s_folder[1], Sty.folder, bsel_icon, s_in);
    om_ut_colors(&s_star[0], Sty.gold, Sty.list_eff, s_in);
    om_ut_colors(&s_star[1], Sty.gold, bsel_star, s_in);
}

/* --- per frame ------------------------------------------------------------------------------------------------- */
static void track_motion(void)
{
    int target = OM_BX(ROWS)->y0 + ((int)s_b.cur - (int)s_b.top) * OM_ROW_H;
    uint16_t n;
    const om_box *sb = OM_BX(SCROLL);
    (void)om_browse_items(&s_b, &n);
    if (s_b.top != s_last_top) {                                   /* rows slide one row height, eased */
        int32_t d = s_b.top > s_last_top ? OM_ROW_H : -OM_ROW_H;
        int32_t from = om_tween_value(&s_scroll) + om_anim_dist(d, s_ps.set.anim);
        if (from > OM_ROW_H) from = OM_ROW_H;                      /* rows top-1..top+8 cover one row of offset */
        if (from < -OM_ROW_H) from = -OM_ROW_H;
        om_tween_run(&s_scroll, from, 0, frames(120));
    }
    om_tween_to(&s_cur_y, target, frames(80));                     /* the cursor glides, 80 ms ease-out */
    if (n > OM_ROWS) {
        int span = sb->y1 - sb->y0 + 1, len = span * OM_ROWS / n;
        if (len < 6) len = 6;
        om_tween_to(&s_thumb, sb->y0 + (int)((uint32_t)(span - len) * s_b.top / (uint32_t)(n - OM_ROWS)), frames(120));
    }
    if (s_b.cur != s_last_cur || s_b.top != s_last_top)
        s_mq_sel = 0;                                              /* the newly selected row marquees after its hold */
    s_last_top = s_b.top;
    s_last_cur = s_b.cur;
}

static void list_changed(int dir)
{   /* folder enter (+1) / back (-1): the new list slides in from the right / left with a fade-in (150 ms) */
    om_tween_run(&s_list_dx, om_anim_dist(dir > 0 ? LIST_SLIDE : dir < 0 ? -LIST_SLIDE : 0, s_ps.set.anim), 0,
                 frames(150));
    om_tween_run(&s_list_fade, s_ps.set.anim == OM_ANIM_OFF ? 256 : 0, 256, frames(150));
    om_tween_jump(&s_cur_y, OM_BX(ROWS)->y0 + ((int)s_b.cur - (int)s_b.top) * OM_ROW_H);
    om_tween_jump(&s_scroll, 0);
    rows_invalidate();
    s_last_top = s_b.top;
    s_last_cur = s_b.cur;
    s_star_item = -1;
}

static void step_tweens(void)
{
    om_tween_step(&s_cur_y); om_tween_step(&s_scroll); om_tween_step(&s_list_dx); om_tween_step(&s_list_fade);
    om_tween_step(&s_cover_in); om_tween_step(&s_ov_slide); om_tween_step(&s_star_t); om_tween_step(&s_thumb);
    om_toast_tick(&s_toast);
    s_mq_sel++;                                                    /* the selected row's marquee clock */
    if (s_star_item >= 0 && !om_tween_active(&s_star_t) && s_star_add)
        s_star_item = -1;
}

static void frame_out(const om_card *c, const om_tree *t)
{
    frame_draw(c, t);
    om_fade_veil(VEIL_MASK, s_veil[0], s_veil[1], s_veil[2]);      /* the main loop's frames only: a fade owns its own */
    if (s_pg_preload) {                                            /* after the drawing, as fade_draw */
        if (s_ov.kind == OM_OV_GAME && s_pg_quiet >= PANEL_QUIET && !om_tween_active(&s_ov_slide))
            om_loading_preload_tick_vb(s_loop_t0, s_loop_vb0, panel_read_vbl());   /* drops frames */
        else
            om_loading_preload_tick(s_loop_t0, PRELOAD_END_US);
    }
    slSynch();
    {   /* misses: V-blanks beyond one since the last slSynch return, charged to the costliest phase */
        static uint32_t vb_last;
        uint32_t vb = om_v2_vblanks(), i, worst = 0;
        if (s_vb_restart) { s_vb_restart = 0; vb_last = 0; }
        for (i = 0; i < PH_N; i++) {
            if (s_ph[i] > s_ph_max[i]) s_ph_max[i] = s_ph[i];
            if (s_ph[i] > s_ph[worst]) worst = i;
        }
        if (vb_last && vb - vb_last > 1u)
            s_miss[worst] += vb - vb_last - 1u;
        vb_last = vb;
        for (i = 0; i < PH_N; i++) s_ph[i] = 0;
    }
}

static void frame_draw(const om_card *c, const om_tree *t)
{   /* everything a frame submits before its slSynch (the fades call it once a frame). Drawn
     * through the safe-area transform (g_om_safe), restored to identity at the end: the interstitial, the loading
     * screen, the boot animation and the diagnostics draw between view groups */
    ph_mark pm = ph_now();
    int32_t ovp = om_tween_value(&s_ov_slide);                     /* 0 closed .. 256 open */
    int ov_on = s_ov.kind != OM_OV_NONE || ovp > 0;
    int ov_dx = (int)((256 - ovp) * (OM_BX(OVERLAY)->x1 - OM_BX(OVERLAY)->x0 + 12) / 256);
    if (om_art_serial() != s_art_serial) {                         /* new art: fade up from dark, rise 4 px (150 ms) */
        s_art_serial = om_art_serial();
        om_tween_run(&s_cover_in, s_ps.set.anim == OM_ANIM_OFF ? 256 : 0, 256,
                     om_anim_frames(150, s_ps.set.anim, 0, s_fps));
    }
    uint16_t t0 = om_ut_frc();
    om_v1_set_safe(&g_om_safe);                                    /* M6 Task 5 (see above); hair_update samples too */
    om_v2_set_safe(&g_om_safe);
    {   /* the interstitial's glide and sequenced fade-in (its own V-blank clock) */
        uint16_t w0 = s_word, i0 = s_in;
        uint32_t op = ovp < 0 ? 0u : ovp > 256 ? 256u : (uint32_t)ovp, dk = 256u - op * (256u - OV_DIM) / 256u;
        om_istl_menu_tick();
        s_word = (uint16_t)(om_istl_in(OM_ISTL_WORDMARK) * dk >> 8);
        s_in_full = om_istl_in(OM_ISTL_CONTENT);
        s_in = (uint16_t)(s_in_full * dk >> 8);
        if (op != s_veil_op) veil_update(op);
        if (s_word != w0) hair_update(s_word);
        if (s_in != i0) bar_update(s_in);
    }
    if (s_ps.set.anim == OM_ANIM_OFF)
        ov_dx = 0;
    om_toast_offer(&s_toast, s_status, s_status_serial, s_status_auto);   /* by serial, every post */
    /* review-13a fix-plan 2: rasterise by priority within the frame's time budget */
    om_ut_budget_us(om_tween_active(&s_ov_slide) ? OM_RASTER_BUDGET_OV_US : OM_RASTER_BUDGET_US);
    prepare_toast();
    prepare_selected(c, 0);
    if (!s_held)
        prepare_details(c, t);
    prepare_rows(c, 0);
    prepare_crumb();
    if (s_held)                                                    /* fast scrolling: the rows first, details after */
        prepare_details(c, t);
    prepare_selected(c, 1);                                        /* the marquee texture, during the hold */
    prepare_rows(c, 1);                                            /* prefetch, idle time only */
    om_v1_begin();
    draw_footer();                                                 /* first, so never dropped */
    draw_panels(c, ov_on);
    draw_header();
    draw_crumb();
    icon_colors();
    draw_list();
    draw_details();
    draw_cover(c, 256 - ovp, ov_on, ov_dx);
    if (om_pop_active())                                           /* 2026-11-25: the popup in place of the panel */
        om_pop_draw(256, s_frame);
    else if (ov_on)
        om_ovdraw(&s_ov, s_ov.kind != OM_OV_NONE ? s_ov.kind : s_ov_kind_shown, &s_ps.set,
                  om_themes_name(s_ov.theme), genre_name_at(c, s_ov.genre), &s_ovg, ov_dx,
                  (uint16_t)(ovp < 0 ? 0 : ovp), s_frame, s_fps, anim_marquee(0));
    om_v1_set_safe(NULL);
    om_v2_set_safe(NULL);
    if (om_v1_dropped() && !s_v1_named) {                          /* M4: a full command list is a named error */
        char msg[OM_TOAST_MSG];
        om_msg_rc(msg, sizeof msg, "UI: VDP1 COMMAND LIST FULL", "", OM_E_FULL);
        status_auto(msg);
        s_v1_named = 1;
    }
    if (om_v1_count() > s_v1_max) s_v1_max = om_v1_count();
    step_tweens();
    s_frame++;
    {
        uint32_t all = ph_since(pm), ras = om_ut_take_frame_us();
        s_ph[PH_RASTER] += ras;
        s_ph[PH_DRAW] += all > ras ? all - ras : 0;
    }
    {   /* the frame's CPU cost before slSynch, worst over the last second (fake build diagnostics, report) */
        uint32_t us = om_ut_frc_us((uint16_t)(om_ut_frc() - t0));
        if (us > s_cost_acc) s_cost_acc = us;
        if (s_frame % 60u == 0) { s_cost_max = s_cost_acc; s_cost_acc = 0; }
    }
}

#ifdef OMSAT_FAKECARD
#endif

static const om_card *s_pf_c;
static const om_tree *s_pf_t;

static void art_idle(void)
{   /* during the interstitial's minimum time, the first screen's art is read into the cache (the selection,
     * then +1, -1, +2, -2), so it shows at once when the panel fades in */
    uint16_t n;
    const om_item *it = om_browse_items(&s_b, &n);
    om_art_tick(s_pf_c, s_pf_t, om_browse_selected(&s_b));
    om_art_prefetch(s_pf_c, s_pf_t, it, n, s_b.cur);
}

/* /aether/DIAG.TXT (app/setup.h), once a menu boot (not on the way back from a tool): POPUP.BIN reads the console
 * setup into the card-scan work area and writes the file only when it changed; a press during its load stops it
 * (tried again at the next idle moment, the press kept for the poll). Its load overwrites OM_LW_LBG. */
static uint8_t s_setup_done;
static int setup_abort(void) { return om_pad_peek() != 0; }
static void setup_run(om_card *c, const om_card_bufs *bufs)
{
    int rc;
    sd_quiet();
    om_loading_preload_drop();
    rc = om_pop_setup((uint8_t *)bufs->work, (uint32_t)sizeof *bufs->work, om_libidx_games(&c->lib),
                      s_ps.set.loader, setup_abort);
    s_setup_done = rc != OM_POP_E_ABORT;
    om_art_invalidate();                                   /* the art job re-requests its stage (popup agent) */
}

void om_folders_run(om_card *c, om_tree *t, Uint8 area, const om_card_bufs *bufs)
{
    uint16_t held_n = 0;
    s_fps = om_tv_is_pal() ? 5000u : 5994u;                       /* HWF-10, HWF-11 */
    alloc_slots();
    load_lists(c, t);
    {   /* M3.5 decisions 1, 2: the firmware against the tested minimum; a blocking notice only when it is
         * older and fw_check is on, else at most a status line (queued behind one already showing) */
        char fwmsg[OM_TOAST_MSG];
        om_fw_decide(s_ps.set.fw_check, fwmsg, sizeof fwmsg);
        if (fwmsg[0]) { if (s_status[0]) strcpy(s_status_next, fwmsg); else om_folders_status(fwmsg); }
        if (om_card_old_folder) {                                  /* the Aether rename's banner (satfs.h) */
            if (!s_status[0]) om_folders_status(OM_OLD_FOLDER_MSG);
            else if (!s_status_next[0]) strcpy(s_status_next, OM_OLD_FOLDER_MSG);
        }
    }
    pad_map_changed();                                             /* SETTINGS.INI's button map */
    om_audio_settings(s_ps.set.music, s_ps.set.music_vol, s_ps.set.sfx);   /* Sounds off honoured by buffered presses */
    apply_view(c);
    if (s_ps.set.reselect && s_ps.recent.n)          /* the last-played game, in its folder (before the */
        om_browse_reveal(&s_b, s_ps.recent.idx[0]);  /* first art read); hidden or gone: the top, no message (M3.5 7) */
    if (g_om_ret_ok)                                 /* back from a tool: the list as it was (the panel below) */
        ret_place();
    om_toast_init(&s_toast, s_ps.set.anim, om_anim_frames(150, s_ps.set.anim, 0, s_fps));
    om_info_reset();
    om_text_clear();                                               /* NBG0 is left to fatal screens from here */
    list_changed(0);
    om_tween_jump(&s_list_dx, 0); om_tween_jump(&s_list_fade, 256);
    OM_EXC_INSTALL();                               /* an exception names itself on screen (hang.h) */
    slIntFunction(OM_HANG_VBL);                     /* om_audio_vblank (om_v2_vblank, the music's fades), then the hang
                                                     * watchdog (hang.h) */
    s_pf_c = c;
    s_pf_t = t;
    om_istl_set_idle(art_idle);
    om_istl_ready(s_ps.set.anim);                                  /* the interstitial hands over */
    om_istl_set_idle(0);
    if (g_om_ret_ok) {                                             /* back from a tool: its panel, on its row */
        if (g_om_ret.ov_kind == OM_OV_GAME) s_ret_wait = 1;         /* the panel once its cover is in (below) */
        else ret_reopen(c, bufs);
    }
    om_boot_step_begin(OM_BT_RASTER);                              /* step 6: this loop's first frame, shown */
    om_v2_hwstat_reset();                                          /* the diag screen's NBG2/VDP1 counters: the menu's */
    om_idle_init(&s_idle_t, om_idle_vblanks(s_ps.set.saver_min, s_fps), om_v2_vblanks());   /* M6 Task 14 */
    for (;;) {
        ph_mark pm = ph_now();
        Uint16 p, held;
        OM_HANG(OM_HT_LOOP);
        s_loop_t0 = om_ut_frc();                                   /* the panel preload's deadline */
        s_loop_vb0 = om_v2_vblanks();
        p = om_istl_take_press(om_pad_poll());                     /* + boot's presses; no launch unseen */
        held = om_pad_held();
        uint16_t a = om_remap_acts(&s_ps.set.pad, p, k_btn_mask);   /* this frame's mapped actions */
        const om_item *sel;
        if (s_sr_latch && !om_pad_soft_reset(held))
            s_sr_latch = 0;                                        /* released since a canceled launch */
        if (om_pad_soft_reset(held) && !s_sr_latch) {
            om_pop_abort();                                        /* a popup's working folder back to "/" */
            sd_quiet();                                            /* no SD file left open (music, an art job) */
            om_return_official();                                  /* review-final-m3 I1: om_audio_handoff first,
                                                                    * through fatal.h's silence hook (main.c) */
        }
        {   /* M3.5 decision 8: the idle screensaver, after the soft-reset check (safety first); any button
             * wakes it and does nothing else; the music keeps playing (its 2 KB refills, the clock) */
            int ev = om_idle_step(&s_idle_t, om_v2_vblanks(), p != 0 || held != 0 || om_pop_active());   /* no saver over a popup */
            if (ev == OM_IDLE_START) saver_start(c, t);
            if (ev == OM_IDLE_WAKE) { saver_stop(c, t); om_pad_prime(); p = 0; a = 0; }
            if (s_idle_t.active) {
                om_audio_tick();
                om_rtc_tick(&s_clock);
                saver_frame();
                continue;
            }
        }
        held_n = (held & (PER_DGT_KU | PER_DGT_KD)) ? (uint16_t)(held_n + 1) : 0;
        s_held = held_n > OM_PAD_REPEAT_DELAY;                     /* a held direction shortens the glides */
        if (p)
            om_toast_input(&s_toast);                              /* an rc error stays until the next action */
        if (p && s_status_next[0]) {                               /* a queued status, after it */
            om_folders_status(s_status_next);
            s_status_next[0] = 0;
        }
        if (p || held) s_pg_quiet = 0;                             /* a press, or a held button */
        else if (s_pg_quiet < 255u) s_pg_quiet++;
        if (s_pg_quiet == 45u && !s_setup_done && !g_om_ret_ok && s_ov.kind == OM_OV_NONE && !s_pg_preload &&
            !om_pop_active())
            setup_run(c, bufs);                                    /* /aether/DIAG.TXT, after 0.75 s idle */
#ifdef OMSAT_FAKECARD                                              /* outside the if/else (18b review m3) */
        if (p && s_ov.kind == OM_OV_GAME) s_pg_presses++;
#endif
        if (s_ret_wait) {   /* hardware 2026-11-25 (b866b47): a tool's return opened the panel before the game's cover
                             * was read, and an open panel holds the art job (pg_busy), so it stayed blank: the panel
                             * now opens once the art has settled (shown or none), after ~2.5 s at most, or at a press */
            if ((s_ret_wait > 2u && !om_art_loading()) || s_ret_wait > 150u || p) {
                s_ret_wait = 0;
                om_ut_budget_us(8000);                           /* the panel's title is rasterised once, at its open */
                ret_reopen(c, bufs);
                p = 0; a = 0;                                      /* the press only hurried it */
            } else
                s_ret_wait++;
        }
        if (om_pop_active()) {                                     /* 2026-11-25: a popup takes every key */
            /* the user's OK and Back (Settings > Controls) are the popup's A and B; its other keys stay physical */
            Uint16 pk = (Uint16)(p & ~(PER_DGT_TA | PER_DGT_TB));
            uint32_t pf;
            if (a & ACT(OM_ACT_OK)) pk = (Uint16)((pk & ~om_remap_mask(&s_ps.set.pad, OM_ACT_OK, k_btn_mask)) | PER_DGT_TA);
            if (a & ACT(OM_ACT_BACK)) pk = (Uint16)((pk & ~om_remap_mask(&s_ps.set.pad, OM_ACT_BACK, k_btn_mask)) | PER_DGT_TB);
            OM_HANG(OM_HT_POPKEY);
            pf = om_pop_key(pk, om_pad_repeated());                /* every frame: it runs an operation a frame late */
            if (pf & OM_POP_SFX_ERR) om_sfx(OM_SFX_ERROR);
            else if (pf & OM_POP_SFX_BACK) om_sfx(OM_SFX_BACK);
            else if (pf & OM_POP_SFX_OK) om_sfx(OM_SFX_SELECT);
            else if (pf & OM_POP_SFX_MOVE) om_sfx(OM_SFX_SCROLL);
            if (pf & OM_POP_CLOSED) {                              /* B: back to the panel or Settings, at once */
                pop_closed(bufs, (pf & OM_POP_SAVED) != 0);
                om_pad_prime();
                if (pf & OM_POP_IMPORT) import_request(c, t, area, bufs, 0);   /* Manage / Import Saves' first row */
            }
            pm = ph_now();                                         /* an operation blocks by design: not charged */
        } else if (s_ov.kind != OM_OV_NONE) {
            uint8_t kind = s_ov.kind;
            om_overlay ov0 = s_ov;                                 /* review-sound M5: a sound only for a change */
            om_settings st0 = s_ps.set;
            om_pergame pg0 = s_pg;
            uint16_t k = keys_of(p, a);
            if (p & om_pad_repeated() & (PER_DGT_KU | PER_DGT_KD)) k |= OM_K_REP;   /* held: the list stops at its ends */
            int f = kind == OM_OV_GAME ? om_overlay_key_game(&s_ov, k, &s_pg) : om_overlay_key(&s_ov, k, &s_ps.set);
            if (kind == OM_OV_GAME && !(f & OM_OV_CLOSED)) pg_loader_text();
            if (s_ov.kind == OM_OV_GAME)                   /* A arms once released (the opening press never launches) */
                om_overlay_game_held(&s_ov, keys_of(held, om_remap_acts(&s_ps.set.pad, held, k_btn_mask)));
            if (f & OM_OV_CLOSED) f = pg_after_key(kind, f);
            if (f & (OM_OV_SAVEMGR | OM_OV_CHEATS)) {   /* saved, then handed off */
                int mode = (f & OM_OV_CHEATS) ? 2 : kind == OM_OV_GAME ? 3 : 4;
                if (!pop_try(c, bufs, mode))               /* the popup, else the launcher's tool (hand-off) */
                    import_request(c, t, area, bufs, mode);
                f &= ~(OM_OV_SAVEMGR | OM_OV_CHEATS);
            }
            if (f & OM_OV_LAUNCH) {                                /* the panel's Launch row (user decision
                                                                     * 2026-11-23): save first if changed (launch_opts
                                                                     * reads PERGAME.INI fresh off SD, never s_pg
                                                                     * directly), then launch exactly as the plain
                                                                     * list did. The panel itself stays open
                                                                     * underneath, the same way Import and Manage
                                                                     * saves leave Settings open through their own
                                                                     * blocking run --
                                                                     * a canceled or failed launch returns to it,
                                                                     * Launch still highlighted, ready to retry. */
                om_loading_preload_pause();                        /* its file closed first (the two-files rule),
                                                                     * same as a normal close before pg_save's write */
                s_pg_preload = 0;
                if (s_ov.dirty) pg_save();
                launch_selected(c, t, area, bufs);
                idle_rearm();                                      /* back from a launch (or a cancel) */
                pm = ph_now();                                     /* a launch blocks by design: not charged */
                f &= ~OM_OV_LAUNCH;
            }
            if (f & OM_OV_ABOUT) {                                 /* Settings > About: its popup over Settings */
                pop_try(c, bufs, 5);                               /* B closes it back to Settings */
                f &= ~OM_OV_ABOUT;
            }
            if (f & OM_OV_THEME) apply_theme(s_ov.theme);
            if (f & OM_OV_SAFE) {                                  /* the whole UI rescales, live */
                om_safe_set(&g_om_safe, s_ps.set.overscan);
                om_logo_rebind();                                  /* the header logo's edges at its new place */
                style_update();                                    /* the panel colors over their new backdrop */
                s_safe_flush = 1;
            }
            if (f & OM_OV_ANIM) om_toast_init(&s_toast, s_ps.set.anim, om_anim_frames(150, s_ps.set.anim, 0, s_fps));
            if (f & OM_OV_SOUND) om_audio_settings(s_ps.set.music, s_ps.set.music_vol, s_ps.set.sfx);
            if (f & OM_OV_PAD) pad_map_changed();                  /* badges and hints, live */
            if (f & OM_OV_SAVER) idle_rearm();                     /* the new timeout, from now */
            if (f & OM_OV_CLOSED) om_sfx(OM_SFX_BACK);
            else if ((memcmp(&ov0, &s_ov, sizeof ov0) || memcmp(&st0, &s_ps.set, sizeof st0) || memcmp(&pg0, &s_pg, sizeof pg0)))
                om_sfx((a & ACT(OM_ACT_OK)) ? OM_SFX_SELECT : OM_SFX_SCROLL);
            if (f & OM_OV_CLOSED) {
                s_ov_kind_shown = kind;
                om_tween_to(&s_ov_slide, 0, frames(150));
                close_overlay(c, f, kind);
            }
        } else {
            uint32_t cur0 = s_b.cur;
            if (p & PER_DGT_KU) om_browse_step(&s_b, -1, !(om_pad_repeated() & PER_DGT_KU));   /* a fresh press wraps */
            if (p & PER_DGT_KD) om_browse_step(&s_b, 1, !(om_pad_repeated() & PER_DGT_KD));
            if (a & ACT(OM_ACT_PGUP)) om_browse_page(&s_b, -1);   /* through the map */
            if (a & ACT(OM_ACT_PGDN)) om_browse_page(&s_b, 1);
            if (s_b.cur != cur0) om_sfx(held_n > OM_PAD_REPEAT_DELAY ? OM_SFX_SCROLL_HELD : OM_SFX_SCROLL);
            if ((a & ACT(OM_ACT_BACK)) && om_browse_back(&s_b)) { list_changed(-1); om_sfx(OM_SFX_BACK); }
            if (a & ACT(OM_ACT_OK)) {
                int r = om_browse_enter(&s_b);
                if (r == 2) {
                    /* user decision 2026-11-23: A opens the launch panel (menu/src/overlay.h OM_OGR_LAUNCH, its
                     * highlighted first row) instead of launching at once -- a second A there (OM_OV_LAUNCH, below)
                     * starts the game. The panel never blocks a launch it has nothing to show for: pg_open_selected
                     * returning 0 (no IP.BIN product number to key PERGAME.INI by) falls straight back to the old
                     * direct launch. */
                    if (!pg_open_selected(c, bufs)) {
                        launch_selected(c, t, area, bufs);
                        idle_rearm();                                  /* back from a launch (or a cancel) */
                        pm = ph_now();                                 /* a launch blocks by design: not charged */
                    }
                }
                else if (r) { list_changed(1); om_sfx(OM_SFX_SELECT); }
            }
            if (a & ACT(OM_ACT_FAV)) toggle_fav(c);
            if (a & ACT(OM_ACT_SORT)) {                                 /* spec §7.5: Y = sort (remappable) */
                char msg[OM_TOAST_MSG];
                s_ps.set.sort = (uint8_t)((s_ps.set.sort + 1u) % OM_SORT_COUNT);
                om_sfx(OM_SFX_SORT);
                apply_view(c);
                strcpy(msg, "SORT: ");
                strcat(msg, om_sort_name(s_ps.set.sort));
                om_folders_status(msg);
                save_settings();
                list_changed(0);
            }
            if (a & ACT(OM_ACT_FILTER)) {                               /* spec §7.5: Z = filter (remappable) */
                om_overlay_open(&s_ov, OM_OV_FILTER, 0, 0, genre_rank_of(c), s_sort.ngenre);
                om_sfx(OM_SFX_SORT);
                s_ov_kind_shown = OM_OV_FILTER;
                om_tween_to(&s_ov_slide, 256, frames(150));
            }
            /* hold L+R, then press Start -- the hidden diagnostics screen. Checked ahead of the plain
             * Start/settings branch below so a fresh Start does not ALSO open Settings the same frame; L and R
             * alone keep doing whatever the button map gives them (Page up/down by default, checked above, so
             * holding them without a fresh Start is unchanged); the combo itself stays on the physical L+R (M6
             * Task 11). `held`, not `p`, for L+R: the combo fires on
             * Start's own edge once both are already down, not on whichever of the three happens to be newest. */
            if ((held & (PER_DGT_TL | PER_DGT_TR)) == (PER_DGT_TL | PER_DGT_TR) && (p & PER_DGT_ST)) {
                om_art_invalidate();   /* as before: any art job canceled, its fd closed */
                om_loading_preload_drop();   /* diag borrows the arena a panel's early read is in */
                sd_quiet();            /* review-final-m3 M1: the music file closed (reopened and re-sought by
                                        * om_audio_tick, the review-sound I2 path, not the starvation fade) and an
                                        * in-flight art job canceled, before diag.c's own SD read/write tests open */
                om_diag_run(area, bufs->work, (uint32_t)sizeof *bufs->work);
                om_pad_prime();            /* the dismiss press is not a fresh press next frame (pad.h:9-11) */
                om_text_clear();           /* diag used the whole NBG0 grid; row 27's fake-HUD label is stale */
#ifdef OMSAT_FAKECARD
                s_diag_reset = 1;          /* same resync the loading screen uses (s_diag_reset's own comment) */
#endif
                s_vb_restart = 1;          /* diag's own slSynch calls must not be charged as missed frames */
                memset(s_ph, 0, sizeof s_ph);
                idle_rearm();              /* the time on that screen was not idle time */
            } else if (p & PER_DGT_ST) {                                 /* spec §7.5: Start = settings */
                int ti = om_themes_find(s_ps.set.theme);
                om_overlay_open(&s_ov, OM_OV_SETTINGS, ti < 0 ? 0 : (uint16_t)ti, om_themes_count(), -1, 0);
                om_sfx(OM_SFX_SELECT);
                s_ov_kind_shown = OM_OV_SETTINGS;
                om_tween_to(&s_ov_slide, 256, frames(150));
            }
        }
        track_motion();
        if (om_rtc_tick(&s_clock))
            clock_status();
        sel = om_browse_selected(&s_b);
        {   /* input = the whole block above minus the theme and save time it contains */
            uint32_t all = ph_since(pm), sub = s_ph[PH_THEME] + s_ph[PH_SAVE];
            s_ph[PH_INPUT] += all > sub ? all - sub : 0;
            pm = ph_now();
        }
        /* big-card stutter (hardware report 2026-11-23): no IP.BIN read when the cursor rests. Its 2 stats + 2 opens
         * each resolved a path from the card's root (O(root names) on the Satiator), 4 in one frame -- with a 703-name
         * root the list froze and the music ran dry. The list and the details card use LIBRARY.IDX and GAMES.DB;
         * IP.BIN is read by game_gid (the launch panel, a launch). */
        ph_add(PH_INFO, pm);
        pm = ph_now();
        {
            static char last[41];
            if (!pg_busy()) {                                      /* the panel / list own the stage */
                uint16_t pn;
                const om_item *pit = om_browse_items(&s_b, &pn);
                if (!sel || sel->kind != OM_IT_GAME || sel->idx != s_pg_idx)
                    om_loading_preload_lw_drop();   /* off the panel's game: the stage is the art's */
                om_art_tick(c, t, sel);
                if (!om_loading_preload_lw_held())                 /* no prefetch over the LW bytes */
                    om_art_prefetch(c, t, pit, pn, s_b.cur);       /* idle frames only (art.c decides) */
            }
            if (om_istl_menu_visible())                            /* Task 14/15 + 13d: the first fade-in starts as */
                om_audio_menu_start(s_ps.set.music, s_ps.set.music_vol, s_ps.set.sfx);   /* the list appears; idempotent */
            OM_HANG(OM_HT_AUDIO);
            om_audio_tick();                                       /* the music's 2 KB refills */
            if (strcmp(last, om_art_message())) {
                /* review-10 Important 1: an art error clears once art loads again, but only while the status line
                 * still shows that art message -- a FAVS/RECENT/launch message stays as the last event. */
                if (om_art_message()[0])
                    status_auto(om_art_message());
                else if (!strcmp(s_status, last))
                    status_auto("");
                strcpy(last, om_art_message());
            }
        }
        ph_add(PH_ART, pm);
#ifdef OMSAT_FAKECARD
        if (om_boot_diag()) {   /* plan D6: every screenshot names its tour step (with FAKESCRIPT.DIAG only). Row 27 of NBG0 (y 216-223), below the footer strip
             * (layout.h FOOTER ends at y 215): L = this loop's frames, V = V-blanks (slIntFunction). L - V constant
             * through the tour proves the loop never misses a frame (60 fps). R = the worst row rasterisation, in us. */
            static const char *shown;
            static const char hexd[] = "0123456789ABCDEF";
            char h[30];
            uint32_t L = s_frame, V = om_v2_vblanks(), R = om_ut_worst_us(), i;
            if (s_diag_reset) {                                    /* the loading screen used rows 25-27 */
                s_diag_reset = 0;
                shown = 0;
            }
            if (shown != om_fakepad_label()) {
                shown = om_fakepad_label();
                om_text_put(0, 27, shown, 15);
            }
            h[0] = 'L';
            for (i = 0; i < 4; i++) h[1 + i] = hexd[(L >> (12 - 4 * i)) & 15];
            h[5] = ' '; h[6] = 'V';
            for (i = 0; i < 4; i++) h[7 + i] = hexd[(V >> (12 - 4 * i)) & 15];
            h[11] = ' '; h[12] = 'C';
            om_itoa(h + 13, (int)(s_cost_max > 99999u ? 99999u : s_cost_max));
            om_text_put(15, 27, h, 19);
            h[0] = 'R';
            om_itoa(h + 1, (int)(R > 99999u ? 99999u : R));
            om_text_put(34, 27, h, 6);
            {   /* row 26: missed frames per phase; row 25: worst iteration per phase in 0.1 ms */
                static const char tag[PH_N] = { 'I', 'T', 'S', 'N', 'A', 'R', 'D' };
                for (i = 0; i < PH_N; i++) {
                    h[0] = tag[i];
                    om_itoa(h + 1, (int)(s_miss[i] > 999u ? 999u : s_miss[i]));
                    om_text_put((int)(5 * i), 26, h, 5);
                    h[0] = (char)(tag[i] + 32);
                    om_itoa(h + 1, (int)(s_ph_max[i] / 100u > 999u ? 999u : s_ph_max[i] / 100u));
                    om_text_put((int)(5 * i), 25, h, 5);
                }
            }
        }
#endif
        OM_HANG(OM_HT_SYNCH);
        frame_out(c, t);
        om_boot_step_end(OM_BT_RASTER);                            /* once: later calls are ignored */
    }
}
