/* menu/views/interstitial.c -- see interstitial.h. BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#undef pal                     /* SGL.H's old-texture macro: om_logo_img has a pal field (logoblend.h) */
#include "anim.h"
#include "biglogo.h"
#include "bootanim.h"
#include "boottime.h"
#include "interstitial.h"
#include "istlgeom.h"
#include "layout.h"
#include "logoblend.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "pad.h"
#include "text.h"
#include "theme.h"
#include "uifont.h"
#include "uitext.h"
#include "uiview.h"
#include "v1.h"
#include "vdp2theme.h"
#include "vertag.h"
#include "yield.h"

/* VDP1 VRAM, inside the boot-only range (OM_VRAM_BOOTANIM .. +45,056 B, free after the animation): the logo as RGB555,
 * then two lookup-table text textures, each with its 16-entry LUT on a 0x20 boundary (VDP1_Manual.txt 5.2). */
#define LOGO_W   OM_ISTL_LOGO_W
#define LOGO_H   OM_ISTL_LOGO_H
#define V_LOGO   OM_VRAM_BOOTANIM                              /* 30,704 B */
#define V_LAB_L  0x31C20u                                      /* label: LUT, then its texture (<= 1,248 B) */
#define V_LAB_T  0x31C40u
#define V_CNT_L  0x32120u                                      /* counts: LUT, then its texture (<= 2,240 B) */
#define V_CNT_T  0x32140u
#define V_END    0x32A00u
typedef char istl_vram_fits[V_LOGO + LOGO_W * LOGO_H * 2u <= V_LAB_L && V_END <= OM_VRAM_BOOTANIM + 45056u ? 1 : -1];

/* Mockup geometry (boot2menu_mockup_render.py, native coordinates): menu/src/istlgeom.h, all of it inside
 * the 24 px safe area (test_istlgeom), since this screen shows before SETTINGS.INI is read. It is always drawn at
 * identity (no safe-area transform set). */
#define CX OM_ISTL_CX
#define CY OM_ISTL_CY
#define RX OM_ISTL_RX
#define RY OM_ISTL_RY
#define LAB_MID OM_ISTL_LAB_MID
#define CNT_MID OM_ISTL_CNT_MID
#define BAR_X0 OM_ISTL_BAR_X0
#define BAR_X1 OM_ISTL_BAR_X1
#define BAR_Y OM_ISTL_BAR_Y
#define LAB_TRACK8 OM_ISTL_LAB_TRACK8

/* Z: bigger is further back (v1.h); behind every menu band (uiview.h tops out at 199) */
#define Z_ORB_BACK 214
#define Z_COM_BACK 212
#define Z_BIGLOGO  208
#define Z_ORB_FRONT 204
#define Z_COM_FRONT 202
#define Z_TEXT 200

/* V-blank counts (59.94 or 50 Hz alike: these are frames, not ms) */
#define FADE_IN   15u          /* 250 ms: logo + background up from the animation's black */
#define SHOW_AT   30u          /* the loading UI appears only if the start-up is still running ~0.5 s in */
#define UI_RAMP   12u
#define MIN_SHOW  24u          /* anim=off: once shown, at least 0.4 s before the cut (no flash) */
#define MIN_UI_NTSC 240u       /* user, 2026-11-20 (was 3.0 s): the loading UI always shows at least 4.0 s (anim full/reduced); */
#define MIN_UI_PAL  200u       /* the counts tick up and the bar fills over that time */
#define READY_MAX 240u         /* om_istl_ready's own bound */
#define SLOW_MAX  600u

/* sin in Q14 over a quarter wave (65 entries); angles are 0..255 per turn */
static const int16_t k_qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366,
    7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665,
    12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679,
    15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384 };
static int32_t __attribute__((noinline)) qsin(uint32_t a)   /* one copy (HWRAM budget) */
{
    a &= 255u;
    if (a < 64u) return k_qsin[a];
    if (a < 128u) return k_qsin[128u - a];
    if (a < 192u) return -k_qsin[a - 128u];
    return -k_qsin[256u - a];
}
static int ex(uint32_t a) { return (int)((RX * qsin(a + 64u)) >> 14); }
static int ey(uint32_t a) { return (int)((RY * qsin(a)) >> 14); }
static int ox(uint32_t a) { return CX + OM_ISTL_TX(ex(a), ey(a)); }     /* tilted -17 degrees (istlgeom.h) */
static int oy(uint32_t a) { return CY + OM_ISTL_TY(ex(a), ey(a)); }
static int near_half(uint32_t a) { return qsin(a) >= 0; }               /* the half in front of the planet */
static int behind_planet(int x, int y)
{   /* a far-half point the planet's circle would cover (the line-art logo is open in the middle) */
    int dx = x - CX, dy = y - CY;
    return dx * dx + dy * dy < OM_ISTL_DISC_R * OM_ISTL_DISC_R;
}

/* the comet's trail: 22 segments of 2 angle units (1.08 rad, as the mockup's 60 x 0.018), alpha 255 * k^1.8 */
#define TRAIL 22
static const uint8_t k_trail[TRAIL] = { 255, 235, 215, 196, 178, 160, 144, 128, 113, 99, 86, 73, 62, 51, 41, 32, 25, 18,
                                        12, 7, 3, 1 };
/* (i * 256 + 7) / 15: the 4 bpp coverage ramp (uitext.c) */
static const uint16_t k_ramp[16] = { 0, 17, 34, 51, 68, 85, 102, 119, 137, 154, 171, 188, 205, 222, 239, 256 };
static const uint16_t k_bar_q12[OM_BT_N + 1] = { 0, 683, 1365, 2048, 2731, 3413, 4096 };

typedef struct { uint32_t lut, tex; uint16_t cap, w8, h, w, asc; int ok; } istl_txt;
static istl_txt s_lab = { V_LAB_L, V_LAB_T, 192, 0, 0, 0, 0, 0 }, s_cnt = { V_CNT_L, V_CNT_T, 320, 0, 0, 0, 0, 0 };
static int s_stage = 2;        /* 0 before the menu, 1 the menu's glide and fades, 2 over (or never begun) */
static uint32_t s_t0, s_last, s_show_at, s_g0, s_slow;
static uint16_t s_last_frc;    /* the FRC when the last frame's slSynch returned (the pacer's ~9 ms) */
static int32_t s_bar;          /* the bar as shown, Q12 */
static uint32_t s_cnt_shown[3], s_cnt_at;   /* the counts as last rasterized (ticking up), and when */
static uint32_t s_counts[3];
static int s_counts_dirty, s_logo_ok, s_dipped;
static uint8_t s_anim;
static uint16_t s_glide_n, s_word_n, s_pan_n, s_con_n, s_ws;
static uint16_t s_word = 256, s_pan = 256, s_con = 256;
static int32_t s_glide_q = OM_Q;
static Uint16 s_prev, s_fifo[8];
static uint8_t s_fh, s_ft;
/* The background (NBG1 + BACK, color offset A) and the logo (sprites, color offset B) fade up together from the
 * animation's black (2026-11-19, hardware run: review-13d I7's wait for step 3 or SHOW_AT left the screen black for as
 * long as s_mode and the library took on a real card). The theme is not known before SETTINGS.INI (step 4, after the
 * library), so the built-in gradient main.c drew is shown at once; a theme other than Saturn's then dips the background
 * alone (om_istl_bg_dip) and step 3's end brings it back up, with its BG.BIN when it has one. s_bg_at: the V-blank its
 * ramp began (0 = black); s_bg_wait: dipped for a theme change, up again only when step 3 ends. */
static uint32_t s_bg_at;
static uint8_t s_bg_wait, s_bar_full;
static void (*s_vchain)(void) = om_v2_vblank;
static void (*s_idle)(void);
#ifdef OMSAT_FAKECARD
/* boot-gap trace (fake build only): every pre-menu frame's gap -- V-blanks since the last frame's slSynch returned
 * (0 = drawn in time, k = the comet held k more V-blanks) -- and the phase it ran in (1..6 a boot step open, 0x10+n
 * after step n ended, 0x30 the summary and folder tree, 0x20 the ready loop). Read from a Mednafen savestate. */
#define GT_N 1024u
uint32_t om_gt_n;
uint8_t om_gt_ph[GT_N];
uint16_t om_gt_gap[GT_N];
static uint8_t s_gt_phase;
void om_istl_gt_phase(int ph) { s_gt_phase = (uint8_t)ph; }
#endif

void om_istl_set_idle(void (*fn)(void)) { s_idle = fn; }

static uint16_t bg(int x, int y) { return (uint16_t)(om_v2_bg_at(x, y) | 0x8000u); }
static uint16_t mix(uint16_t a, uint16_t b, uint32_t t256) { return (uint16_t)(om_c555_mix(a, b, t256) | 0x8000u); }

/* A text texture, rasterized in bands of rows so one small buffer serves (VDP1 VRAM writes are 16-bit, HWF-23). w is the
 * pen's end: the sum of the advances plus the tracking between glyphs (centering uses it, task-13d brief). */
static void raster(istl_txt *t, const om_face *f, const char *s, int track8)
{
    uint8_t s_ras[640] __attribute__((aligned(4)));              /* on the stack (HWRAM budget): 4+ rows a band */
    int ink = om_uf_width_tracked(f, s, track8), h = om_uf_height(f), w8 = (ink + 7) & ~7, y0, rows;
    if (w8 > t->cap) w8 = t->cap;
    rows = (int)(sizeof s_ras / (uint32_t)(w8 / 2));
    for (y0 = 0; y0 < h; y0 += rows) {
        int n = h - y0 < rows ? h - y0 : rows;
        memset(s_ras, 0, (uint32_t)(w8 / 2 * n));
        t->w = (uint16_t)om_uf_draw_tracked(f, s, s_ras, w8, w8, n, 0, f->asc - y0, track8);
        om_v1_write_bytes(t->tex + (uint32_t)(w8 / 2 * y0), s_ras, (uint32_t)(w8 / 2 * n));
    }
    t->w8 = (uint16_t)w8;
    t->h = (uint16_t)h;
    t->asc = f->asc;
    t->ok = 1;
}

static void txt_draw(const istl_txt *t, const om_face *f, int mid, uint16_t fg, uint32_t a256)
{   /* centered on CX from its pen width; the LUT ramps from the background under it to fg, scaled by a256 */
    uint16_t lut[16], b;
    int x = CX - (t->w >> 1), base = mid + f->mid, i;
    if (!t->ok || !a256) return;
    b = bg(CX, mid);
    lut[0] = 0;
    for (i = 1; i < 16; i++)
        lut[i] = mix(b, fg, (k_ramp[i] * a256) >> 8);
    om_v1_write(t->lut, lut, 16);
    om_v1_sprite(x, base - t->asc, t->tex, t->w8, t->h, t->lut, 0, Z_TEXT);
}

static void poll_input(void)
{   /* presses (new edges) during the interstitial go to the menu's first frames (om_istl_take_press) */
    Uint16 cur = om_pad_held(), n = (Uint16)(cur & ~s_prev);
    s_prev = cur;
    if (n && (uint8_t)(s_ft - s_fh) < 8u)
        s_fifo[s_ft++ & 7u] = n;
}

static Uint16 s_ok_mask = PER_DGT_TA;
void om_istl_set_ok(Uint16 mask) { s_ok_mask = mask; }

Uint16 om_istl_take_press(Uint16 live)
{   /* while the menu fades in, live presses queue behind the buffered ones; a press with A (launch)
     * waits at the head until the list is fully visible; navigation passes at one press a frame */
    Uint16 p;
    if (s_stage == 2 && s_fh == s_ft) return live;
    if (live && (uint8_t)(s_ft - s_fh) < 8u)
        s_fifo[s_ft++ & 7u] = live;
    if (s_fh == s_ft) return 0;
    p = s_fifo[s_fh & 7u];
    if ((p & s_ok_mask) && s_stage != 2) return 0;
    s_fh++;
    return p;
}

static void orbit(uint32_t a256, int front, uint16_t col)
{   /* the faint orbit (mockup (120,180,240) at 46/255), 32 segments; the back half before the logo, the front after */
    uint32_t k;
    if (!a256) return;
    for (k = 0; k < 256u; k += 8u) {
        int xa = ox(k), ya = oy(k), xb = ox(k + 8u), yb = oy(k + 8u);
        if (near_half(k + 4u) != front || (!front && behind_planet((xa + xb) >> 1, (ya + yb) >> 1))) continue;
        om_v1_line(xa, ya, xb, yb, mix(bg((xa + xb) >> 1, (ya + yb) >> 1), col, (46u * a256) >> 8), 0,
                   front ? Z_ORB_FRONT : Z_ORB_BACK);
    }
}

static void comet(uint32_t head, uint32_t a256, int front, uint16_t col)
{   /* tail first so the head is drawn last (equal Z keeps submission order, HWF-42); the first 8 segments 2 px thick */
    int i;
    if (!a256) return;
    for (i = TRAIL - 1; i >= 0; i--) {
        uint32_t a = head - 2u * (uint32_t)i;
        int xa = ox(a), ya = oy(a), xb = ox(a - 2u), yb = oy(a - 2u), z = front ? Z_COM_FRONT : Z_COM_BACK;
        uint16_t c;
        if (near_half(a - 1u) != front || (!front && behind_planet((xa + xb) >> 1, (ya + yb) >> 1))) continue;
        c = mix(bg(xa, ya), col, (k_trail[i] * a256) >> 8);
        om_v1_line(xa, ya, xb, yb, c, 0, z);
        if (i < 8) om_v1_line(xa, ya + 1, xb, yb + 1, c, 0, z);
    }
    {
        int x = ox(head), y = oy(head);
        if (near_half(head) == front && (front || !behind_planet(x, y))) {
            om_v1_quad(x - 2, y - 1, x + 2, y + 2, mix(bg(x, y), col, (230u * a256) >> 8), 0, 0,
                       front ? Z_COM_FRONT : Z_COM_BACK);
            om_v1_quad(x - 1, y, x + 1, y + 1, mix(bg(x, y), 0xFFFF, (220u * a256) >> 8), 0, 0,
                       front ? Z_COM_FRONT : Z_COM_BACK);
        }
    }
}

static uint32_t min_ui(void) { return om_tv_is_pal() ? MIN_UI_PAL : MIN_UI_NTSC; }

static uint32_t ui_frac(uint32_t now)
{   /* Q12: how far through the loading UI's minimum time we are (0 before it shows) */
    uint32_t e;
    if (!s_show_at) return 0;
    e = now - s_show_at;
    return e >= min_ui() ? (uint32_t)OM_Q : (e << 12) / min_ui();
}

/* The loading UI at alpha ui (0..256) and the logo at (x0,y0)-(x1,y1); the comet runs one turn per 64 V-blanks.
 * ver (0..256): the startup "FARKUS 0.1" tag, carried over from the boot animation at full level and faded out with
 * the loading UI over the glide (om_istl_draw_logo); it is never drawn once the interstitial is over. */
static void scene(uint32_t now, uint32_t ui, uint32_t ver, int x0, int y0, int x1, int y1)
{
    const om_theme *th = om_v2_theme();
    uint16_t acc = (uint16_t)(th->cursor | 0x8000u), acc2 = (uint16_t)(th->text_hi | 0x8000u);
    uint16_t txt = (uint16_t)(th->text | 0x8000u), orb = om_c555_rgb(0x78B4F0), com = om_c555_rgb(0xAAE1FF);
    uint32_t head = (now - s_t0) << 2;
    if (s_logo_ok) {
        if (x1 - x0 + 1 == LOGO_W && y1 - y0 + 1 == LOGO_H)
            om_v1_sprite(x0, y0, V_LOGO, LOGO_W, LOGO_H, 0, 0, Z_BIGLOGO);
        else
            om_v1_scaled(x0, y0, x1, y1, V_LOGO, LOGO_W, LOGO_H, 0, 0, 0, Z_BIGLOGO);
    }
    om_vertag_draw(bg(OM_BX(VERTAG)->x0 + 8, OM_BX(VERTAG)->y0 + 6), ver, Z_TEXT);   /* the backdrop under the tag */
    if (!ui) return;
    orbit(ui, 0, orb);
    orbit(ui, 1, orb);
    comet(head, ui, 0, com);
    comet(head, ui, 1, com);
    txt_draw(&s_lab, &om_face_label8, LAB_MID, acc2, ui);
    if (s_counts[0] != 0xFFFFFFFFu) {   /* the counts tick up to the real totals over the minimum time (ui_frac) */
        uint32_t f = ui_frac(now), d[3], k;
        int changed = 0;
        for (k = 0; k < 3; k++) {
            d[k] = s_counts[k] >= 0x80000u ? s_counts[k] : (s_counts[k] * f) >> 12;   /* 32-bit: no libgcc */
            changed |= d[k] != s_cnt_shown[k];
        }
        if (s_counts_dirty || (changed && (now - s_cnt_at >= 3u || f == OM_Q))) {   /* 20 rasters a second at most */
            char line[64], n[12];
            strcpy(line, om_itoa(n, (int)d[0]));
            strcat(line, " games  \xC2\xB7  ");
            strcat(line, om_itoa(n, (int)d[1]));
            strcat(line, " folders  \xC2\xB7  ");
            strcat(line, om_itoa(n, (int)d[2]));
            strcat(line, " covers");
            raster(&s_cnt, &om_face_small, line, 0);
            for (k = 0; k < 3; k++) s_cnt_shown[k] = d[k];
            s_cnt_at = now;
            s_counts_dirty = 0;
        }
    }
    txt_draw(&s_cnt, &om_face_small, CNT_MID, mix(bg(CX, CNT_MID), txt, 158u), ui);
    {   /* the bar: a ~15 % white track and the accent fill, 3 px with the corners cut (mockup radius 1) */
        int32_t target = s_bar_full ? OM_Q : k_bar_q12[om_boot_done_count()], tf = (int32_t)ui_frac(now);
        if (target > tf) target = tf;                           /* never ahead of the minimum time */
        int fx;
        uint16_t tc = mix(bg(CX, BAR_Y + 1), 0xFFFF, (40u * ui) >> 8), fc = mix(bg(CX, BAR_Y + 1), acc, ui);
        s_bar += (target - s_bar + 3) >> 2;
        fx = BAR_X0 + (int)(((BAR_X1 - BAR_X0) * s_bar) >> 12);
        om_v1_quad(BAR_X0 + 1, BAR_Y, BAR_X1 - 1, BAR_Y + 2, tc, 0, 0, Z_TEXT);
        om_v1_quad(BAR_X0, BAR_Y + 1, BAR_X1, BAR_Y + 1, tc, 0, 0, Z_TEXT);
        if (fx > BAR_X0 + 1) {
            om_v1_quad(BAR_X0 + 1, BAR_Y, fx - 1, BAR_Y + 2, fc, 0, 0, Z_TEXT);
            om_v1_quad(BAR_X0, BAR_Y + 1, fx, BAR_Y + 1, fc, 0, 0, Z_TEXT);
        }
    }
}

static uint32_t ui_alpha(uint32_t now)
{
    uint32_t e;
    if (!s_show_at) return 0;
    e = now - s_show_at;
    return e >= UI_RAMP ? 256u : e * 21u + 4u;
}

static void offset(int v)
{
    slColOffsetA((Sint16)v, (Sint16)v, (Sint16)v);
}

static void offset_b(int v)
{
    slColOffsetB((Sint16)v, (Sint16)v, (Sint16)v);
}

static int bg_level(uint32_t now)
{   /* the background's offset A: -255 until its ramp starts, then up over FADE_IN V-blanks */
    uint32_t e;
    if (!s_bg_at) return -255;
    e = now - s_bg_at;
    return e >= FADE_IN ? 0 : -255 + 17 * (int)e;           /* 255 / 15 = 17 */
}

static void bg_up(uint32_t now)
{
    if (!s_bg_at) s_bg_at = now ? now : 1u;
}

static void offsets_off(void)
{   /* every layer back on offset A and no offset (menu/ui/fade.c's fades use A) */
    offset(0);
    offset_b(0);
    slColOffsetOff(SPRON | NBG0ON | NBG1ON | NBG2ON | BACKON);
    slColOffsetAUse(SPRON | NBG1ON | BACKON);
}

/* One pre-menu frame. force = 0: only if a V-blank has passed since the last (the step in between took real time). */
static void frame(int force)
{
    uint32_t now = om_v2_vblanks(), e = now - s_t0;
    if (s_stage != 0 || (!force && now == s_last)) return;
#ifdef OMSAT_FAKECARD
    if (om_gt_n < GT_N) { om_gt_ph[om_gt_n] = s_gt_phase; om_gt_gap[om_gt_n] = (uint16_t)(now - s_last); om_gt_n++; }
#endif
    poll_input();
    if (!s_show_at && e >= SHOW_AT) {
        s_show_at = now;
        om_boot_set_shown();
    }
    if (e >= SHOW_AT && !s_bg_wait) bg_up(now);
    offset_b(e < FADE_IN ? -255 + 17 * (int)e : 0);            /* the logo: up from black at once */
    if (!s_bg_wait || s_bg_at) offset(bg_level(now));
    om_v1_begin();
    scene(now, ui_alpha(now), 256u, CX - LOGO_W / 2, CY - LOGO_H / 2, CX - LOGO_W / 2 + LOGO_W - 1, CY - LOGO_H / 2 + LOGO_H - 1);
    slSynch();
    s_last = om_v2_vblanks();
    s_last_frc = om_ut_frc();
}

void om_istl_status(void) { frame(0); }
void om_istl_frame(void) { frame(1); }

/* The pacer (yield.h): the start-up's loads and CPU loops call it often; it draws a frame once ~9 ms (FRC, the loading
 * screen's own kind of deadline) or a V-blank have passed since the last frame -- its slSynch returns at the next
 * V-blank, so the time-based comet moves every V-blank and the work keeps the rest of each frame. Hardware report
 * (2026-11-25, the 690-game card): it ran during om_card_load only, so the light stopped as the counts appeared
 * (SETTINGS.INI, FAVS.TXT, RECENT.TXT, ACTIVE.TXT: about eight path commands, each a walk of the card's root, plus the
 * path index's sort) and stuttered after (themes, the sort contexts, the first list, the art prefetch). Installed from
 * the fade-in to the hand-over (om_istl_ready). Gate: menu/host-tests/test_bootgaps.c. */
#define PACE_US 9000u
static void pace(void)
{
    if (om_v2_vblanks() == s_last && om_ut_frc_us((uint16_t)(om_ut_frc() - s_last_frc)) < PACE_US)
        return;
    frame(1);
}

static void hook(int step, int ended)
{
    if (ended && step == OM_BT_BG) {                           /* the theme's background is in: bring it up */
        s_bg_wait = 0;
        bg_up(om_v2_vblanks());
    }
    frame(0);
#ifdef OMSAT_FAKECARD
    s_gt_phase = (uint8_t)(ended ? 0x10 + step : 1 + step);    /* steps 1..6 while open, 0x10+ after one ends */
#endif
    if (ended && s_slow && step < OM_BT_RASTER) {              /* fake build: hardware-like step times for captures */
        uint32_t until = om_v2_vblanks() + (s_slow < SLOW_MAX ? s_slow : SLOW_MAX);
        while ((int32_t)(om_v2_vblanks() - until) < 0 && s_stage == 0)
            frame(1);
    }
    /* no boot-timing overlay here; the diagnostics screen (diag.c, page 2) is its one display */
}

void om_istl_slow(uint32_t vblanks) { s_slow = vblanks; }

static uint16_t istl_bg(void *ctx, int x, int y)
{
    (void)ctx;
    return om_v2_bg_at(x, y);
}

/* BIGLOGO.BIN in LWRAM (main.c reads it from the menu disc before API mode); until M6 the same table was HWRAM
 * .rodata (sbuild embed_blob om_biglogo). Decoded and blended by menu/src/biglogo.c, the code logo_upload had. */
static const uint8_t *s_lfile;
static uint32_t s_llen;
static int s_lrc = OM_E_SHORT;

void om_istl_set_logo(const uint8_t *file, uint32_t len, int read_rc)
{
    s_lfile = read_rc ? NULL : file;
    s_llen = read_rc ? 0 : len;
    s_lrc = read_rc;
}

int om_istl_logo(const uint8_t **tbl, uint32_t *tlen)
{
    return s_lrc ? s_lrc : om_biglogo_open(s_lfile, s_llen, tbl, tlen);
}

/* The logo blended with the NBG1 backdrop behind its resting place (menu/src/logoblend.c) into V_LOGO; scratch is
 * handed back zeroed. 0, or the logo's read/open/codec error. */
static int logo_upload(uint8_t *scratch, uint32_t len)
{
    const uint8_t *tbl;
    uint32_t tlen;
    int rc = om_istl_logo(&tbl, &tlen);
    if (!rc)                                                   /* 16-bit VDP1 VRAM writes (HWF-23) */
        rc = om_biglogo_blend(tbl, tlen, scratch, len, CX - LOGO_W / 2, CY - LOGO_H / 2, istl_bg, 0,
                              (volatile uint16_t *)(OM_CT(OM_VDP1_VRAM) + V_LOGO));
    return rc;
}

int om_istl_begin(uint8_t *scratch, uint32_t len)
{
    int rc;
    s_stage = 0;
    s_dipped = 0;
    s_show_at = s_bg_at = 0;
    s_bg_wait = s_bar_full = 0;
    s_bar = 0;
    s_cnt_shown[0] = s_cnt_shown[1] = s_cnt_shown[2] = 0xFFFFFFFFu;
    s_cnt_at = 0;
    s_fh = s_ft = 0;
    s_counts_dirty = 0;
    s_counts[0] = s_counts[1] = s_counts[2] = 0xFFFFFFFFu;    /* nothing known yet: no counts line */
    s_prev = om_pad_held();                                    /* a held skip button is not a press */
    slIntFunction(s_vchain);                                   /* the V-blank counter, or the chain that calls it */
    rc = logo_upload(scratch, len);                            /* over the built-in gradient (main.c, om_v2_bg_gradient) */
    s_logo_ok = rc == 0;
    raster(&s_lab, &om_face_label8, "LOADING YOUR LIBRARY", LAB_TRACK8);
    om_vertag_init();                                          /* already done by the boot animation, if it ran */
    /* from the animation's black: the logo comes up under color offset B, NBG1 (the theme background) under A, both at
     * once (see s_bg_at); NBG0 (boot text, fatal screens) and NBG2 (no panels yet) without an offset */
    offset(-255);
    offset_b(-255);
    slColOffsetOff(NBG0ON | NBG2ON);
    slColOffsetBUse(SPRON);
    slColOffsetAUse(NBG1ON | BACKON);
    slColOffsetOn(SPRON | NBG1ON | BACKON);
    slScrDisp(NBG0ON | NBG1ON | NBG2ON | NBG3OFF | RBG0OFF);   /* as om_v2_init (HWF-41) */
    s_t0 = s_last = om_v2_vblanks();
    bg_up(s_t0);                                               /* the built-in gradient: up with the logo */
    om_boot_begin(hook);
    frame(1);
    return rc;
}

void om_istl_fade_in(void)
{   /* the logo's and the background's FADE_IN, run out before the first SD step (s_mode, the library: no frames while
     * they block, so a ramp left half way would hold the screen dim). At least two slSynch (HWF-16); bounded */
    uint32_t k;
    for (k = 0; k < FADE_IN + 4u; k++) {
        if (k >= 2u && om_v2_vblanks() - s_t0 >= FADE_IN) break;
        frame(1);
    }
    frame(1);                                                  /* the last ramp step's offsets, submitted */
    om_yield_fn = pace;                                        /* the loads from here to om_istl_ready draw frames */
}

void om_istl_counts(uint32_t games, uint32_t folders, uint32_t covers)
{
    if (s_counts[0] == games && s_counts[1] == folders && s_counts[2] == covers)
        return;                                                /* re-rasterized only on a change */
    s_counts[0] = games;
    s_counts[1] = folders;
    s_counts[2] = covers;
    s_counts_dirty = 1;
}

void om_istl_abort(void)
{
    if (om_yield_fn == pace) om_yield_fn = 0;
    s_stage = 2;
    offsets_off();
    om_v1_begin();
}

int om_istl_active(void) { return s_stage != 2; }
void om_istl_set_vblank_chain(void (*fn)(void)) { s_vchain = fn ? fn : om_v2_vblank; }
int om_istl_menu_visible(void) { return s_stage == 2 || (s_stage == 1 && s_con > 0); }

void om_istl_bg_dip(int changes)
{   /* I7: a theme with its own background, while the gradient is already up: the background alone dips to black over 8
     * frames (the logo and text stay lit); show_theme runs under it; step 3's end brings it back up (15 V-blanks) */
    int e;
    if (s_stage != 0 || !changes || !s_bg_at) return;
    s_bg_at = 0;
    s_bg_wait = 1;
    s_dipped = 1;                                              /* om_istl_rebind re-blends the logo's edges */
    for (e = 1; e <= 8; e++) {                                 /* bounded: 8 frames */
        offset(e == 8 ? -255 : -32 * e);
        frame(1);
    }
}

void om_istl_rebind(uint8_t *scratch, uint32_t len)
{   /* after show_theme under a dip, the logo's edges are re-blended with the theme's own background */
    if (s_stage != 0 || !s_dipped || !s_logo_ok) return;
    s_dipped = 0;
    s_logo_ok = logo_upload(scratch, len) == 0;
}

void om_istl_ready(uint8_t anim)
{
    uint32_t fps = om_tv_is_pal() ? 5000u : 5994u, limit = om_v2_vblanks() + READY_MAX + MIN_UI_NTSC;
    uint32_t hold = anim == OM_ANIM_OFF ? MIN_SHOW : min_ui();
    if (s_stage != 0) return;
    s_bg_wait = 0;
    bg_up(om_v2_vblanks());                                    /* step 3 has ended by now */
    s_bar_full = 1;                                            /* M1: the bar reads full before the glide */
    if (anim != OM_ANIM_OFF && !s_show_at) {                   /* final M3 round (user): the loading UI ALWAYS shows, */
        s_show_at = om_v2_vblanks();                           /* even when the library loaded sooner; anim=off keeps */
        om_boot_set_shown();                                   /* the instant cut */
    }
#ifdef OMSAT_FAKECARD
    s_gt_phase = 0x20;                                         /* the ready loop (the art prefetch) */
#endif
    for (;;) {                                                 /* bounded: READY_MAX + 4 s of V-blanks */
        uint32_t now = om_v2_vblanks();
        if ((now - s_t0 >= FADE_IN && now - s_bg_at >= FADE_IN && (!s_show_at || (now - s_show_at >= hold &&
             s_bar >= OM_Q - 16))) || (int32_t)(now - limit) >= 0)
            break;
        if (s_idle) s_idle();                                  /* the art cache's prefetch, bounded per frame */
        frame(1);
    }
    if (om_yield_fn == pace) om_yield_fn = 0;                  /* the menu's own loop draws from here */
    offsets_off();
    s_anim = anim;
    s_glide_n = om_anim_frames(300, anim, 0, fps);
    s_word_n = om_anim_frames(150, anim, 0, fps);
    s_pan_n = om_anim_frames(130, anim, 0, fps);
    s_con_n = om_anim_frames(180, anim, 0, fps);
    s_ws = (uint16_t)((s_glide_n * 251u) >> 8);                /* the wordmark starts at ~98 % of the glide */
    s_word = s_pan = s_con = 0;
    s_glide_q = 0;
    s_g0 = 0;
    s_stage = 1;
    poll_input();
    om_pad_prime();                                            /* what is held now was buffered already */
}

static uint16_t ramp(uint32_t e, uint32_t start, uint32_t n)
{
    if (e < start) return 0;
    if (!n || e - start >= n) return 256;
    return (uint16_t)(((e - start) << 8) / n);
}

static void nbg2_ratio(uint16_t pan)
{   /* NBG2 at the theme's ratio scaled by pan: 31 (almost clear) -> the theme's; a 100 % theme turns CC off at the end */
    const om_theme *t = om_v2_theme();
    uint8_t r = t->panel_alpha >= 100 ? 0 : om_cc_ratio(t->panel_alpha);
    slColRateNbg2((Uint16)(31 - (((31 - r) * pan) >> 8)));
    slColorCalcOn(pan >= 256 && t->panel_alpha >= 100 ? 0 : NBG2ON);   /* as om_v2_apply_colors (HWF-31) */
}

void om_istl_menu_tick(void)
{
    uint32_t now = om_v2_vblanks(), e, ps, cs;
    if (s_stage != 1) return;
    if (!s_g0) s_g0 = now ? now : 1u;
    e = now - s_g0;
    s_glide_q = s_glide_n ? (e >= s_glide_n ? OM_Q : (int32_t)((e << 12) / s_glide_n)) : OM_Q;
    s_word = ramp(e, s_ws, s_word_n);
    ps = (uint32_t)s_ws + s_word_n;
    s_pan = ramp(e, ps, s_pan_n);
    cs = ps + s_pan_n;
    s_con = ramp(e, cs, s_con_n);
    nbg2_ratio(s_pan);
    if (s_con >= 256 && s_glide_q >= OM_Q) s_stage = 2;        /* over: the theme's ratio and CC are back */
}

int om_istl_draw_logo(void)
{
    int32_t p;
    int x0, y0, x1, y1, h[4];
    uint32_t now, ui;
    if (s_stage != 1 || s_glide_q >= OM_Q) return 0;
    p = om_ease_out(s_glide_q);
    /* the glide ends where the menu draws the header logo: its LOGO box through the safe area in force */
    om_safe_rect(&g_om_safe, OM_BX(LOGO)->x0, OM_BX(LOGO)->y0, OM_BX(LOGO)->x1, OM_BX(LOGO)->y1, h);
    x0 = CX - LOGO_W / 2 + (int)(((h[0] - (CX - LOGO_W / 2)) * p) >> 12);
    y0 = CY - LOGO_H / 2 + (int)(((h[1] - (CY - LOGO_H / 2)) * p) >> 12);
    x1 = CX - LOGO_W / 2 + LOGO_W - 1 + (int)(((h[2] - (CX - LOGO_W / 2 + LOGO_W - 1)) * p) >> 12);
    y1 = CY - LOGO_H / 2 + LOGO_H - 1 + (int)(((h[3] - (CY - LOGO_H / 2 + LOGO_H - 1)) * p) >> 12);
    now = om_v2_vblanks();
    ui = ui_alpha(now);                                        /* the loading UI fades out over the glide's first half, */
    ui = p >= OM_Q / 2 ? 0 : (ui * (uint32_t)(OM_Q / 2 - p)) >> 11;   /* by its eased position */
    scene(now, ui, p >= OM_Q / 2 ? 0u : (256u * (uint32_t)(OM_Q / 2 - p)) >> 11, x0, y0, x1, y1);   /* the tag too */
    return 1;
}

uint16_t om_istl_in(int part)
{
    if (s_stage == 2) return 256;
    if (s_stage == 0) return 0;
    return part == OM_ISTL_WORDMARK ? s_word : part == OM_ISTL_PANELS ? s_pan : s_con;
}
