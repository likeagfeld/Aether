/* menu/ui/uitext.c -- see uitext.h. VDP1 VRAM writes are 16-bit (HWF-23); a LUT sits on a 0x20 boundary and a texture
 * on an 8-byte one (VDP1_Manual.txt 5.1-5.2), 4 bpp with the high nibble the left pixel (HWF-05); LUT words are RGB
 * codes with bit 15 set (HWF-01), word 0 is unused because code 0 is transparent with SPdis clear (HWF-02).
 * BSD-3-Clause. */
#include "sgl.h"
#include "anim.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "marquee.h"
#include "uitext.h"
#include "v1.h"

#define RAS_MAX_W 504u                      /* CMDSIZE width field: 6 bits of w / 8 (HWF-06) */
#define RAS_MAX_H 21u                       /* the tallest face (hero, 16 + 5) */
static uint8_t s_ras[RAS_MAX_W / 2 * RAS_MAX_H] __attribute__((aligned(4)));
static uint32_t s_pool = OM_VRAM_UI, s_end = OM_VRAM_UI_END;   /* s_end: lowered by om_ut_pool_cap */
static uint32_t s_last_us, s_worst_us, s_count, s_frame_us;
static int32_t s_budget_us = 1 << 30;
static int s_frame_rasters;

void om_ut_budget_us(int32_t us) { s_budget_us = us; s_frame_rasters = 0; }
int32_t om_ut_budget_left(void) { return s_budget_us; }

/* SH7604 FRT (Hardware Manual ch. 11, SH7604_Hardware.txt:12779-12790): FRC at H'FFFFFE12 (high byte first), TCR at
 * H'FFFFFE16 bits 1-0 = clock /8, /32, /128. CPU clock 26.8741 MHz in 320-dot mode (SMPC_Manual.txt:416-418). Only read. */
static uint16_t frc(void)
{
    uint8_t hi = *(volatile uint8_t *)0xFFFFFE12;
    uint8_t lo = *(volatile uint8_t *)0xFFFFFE13;
    return (uint16_t)((hi << 8) | lo);
}

static uint32_t frc_us(uint16_t ticks)
{
    static const uint32_t div[4] = { 8, 32, 128, 128 };
    uint32_t d = div[*(volatile uint8_t *)0xFFFFFE16 & 3u];
    return (uint32_t)ticks * d * 1000u / 26874u;
}

uint16_t om_ut_frc(void) { return frc(); }
uint32_t om_ut_frc_us(uint16_t ticks) { return frc_us(ticks); }

void om_ut_reset_pool(void) { s_pool = OM_VRAM_UI; }

int om_ut_pool_cap(uint32_t end)
{   /* the art cache takes the pool's unused top; refused if something already lies above end */
    if (s_pool > end) return OM_E_FULL;
    if (end < s_end) s_end = end;
    return OM_OK;
}
uint32_t om_ut_pool_used(void) { return s_pool - OM_VRAM_UI; }
uint32_t om_ut_last_us(void) { return s_last_us; }
uint32_t om_ut_worst_us(void) { return s_worst_us; }
uint32_t om_ut_raster_count(void) { return s_count; }
uint32_t om_ut_take_frame_us(void) { uint32_t v = s_frame_us; s_frame_us = 0; return v; }

int om_ut_alloc(om_ut *t, int cap, int h)
{
    uint32_t bytes;
    cap = (cap + 7) & ~7;
    if (cap > (int)RAS_MAX_W) cap = RAS_MAX_W;
    if (h > (int)RAS_MAX_H) h = RAS_MAX_H;
    bytes = 32u + (((uint32_t)cap / 2u * (uint32_t)h + 31u) & ~31u);
    if (cap >= 128)
        bytes += 96u;                                  /* marquee-capable: three band LUTs (addendum 9) */
    if (s_pool + bytes > s_end)
        return OM_E_FULL;
    t->lut = s_pool;
    t->blut = cap >= 128 ? s_pool + bytes - 96u : 0;
    t->tex = s_pool + 32u;
    s_pool += bytes;
    t->key = 0;
    t->cap = (uint16_t)cap;
    t->h = (uint16_t)h;
    t->w8 = 8;
    t->w = 0;
    t->lut_ok = 0;
    t->face = 0;
    t->asc = 0;
    return OM_OK;
}

static uint32_t hash(int face, const char *s)
{
    uint32_t h = 2166136261u ^ (uint32_t)face;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h ? h : 1u;
}

uint32_t om_ut_key(int face, const char *s, int clipw)
{
    uint32_t k = hash(face, s) ^ ((uint32_t)clipw * 0x9E3779B1u);
    return k ? k : 1u;
}

static int text_impl(om_ut *t, int face, const char *s, int clipw, int track8)
{
    const om_face *f = om_face_get(face);
    uint32_t k = om_ut_key(face, s, clipw) ^ ((uint32_t)track8 * 0x85EBCA6Bu), bytes, i;
    uint16_t t0;
    int w, lim;
    if (k == t->key)
        return 0;
    if (s_budget_us <= 0 && s_frame_rasters)
        return -1;                                     /* this frame's raster time is spent: keep the old texture */
    t0 = frc();
    w = om_uf_width_tracked(f, s, track8);
    if (clipw > 0)
        lim = clipw < t->cap ? clipw : t->cap;         /* a FADE element: cut at its box */
    else if (clipw < 0)                                /* a MARQUEE element in a box of -clipw px */
        lim = om_marquee_mode(w, -clipw, t->cap) == OM_MQ_FADE ? -clipw : t->cap;
    else
        lim = t->cap;
    /* estimate the cost BEFORE rasterising (measured on Mednafen: ~1.3 us a texture byte,
     * e.g. 5,586 us for 504 x 17) and refuse what the frame's budget cannot take -- unless it is the frame's first */
    if (s_frame_rasters) {
        int ww = w > lim ? lim : w;
        int32_t est = (int32_t)(((uint32_t)((ww + 7) & ~7) / 2u * t->h * 13u) / 10u) + 100;
        if (est > s_budget_us) {
            s_budget_us = 0;                           /* the frame is full: later (lower priority) asks stop early */
            return -1;
        }
    }
    s_frame_rasters++;
    t->key = k;
    t->face = (uint8_t)face;
    t->asc = f->asc;
    t->full_w = (uint16_t)w;
    if (w > lim)
        w = lim;                                       /* cut, faded below: never abbreviated (addendum 3) */
    t->w = (uint16_t)w;
    t->w8 = (uint16_t)(w ? (w + 7) & ~7 : 8);
    bytes = (uint32_t)t->w8 / 2u * t->h;
    for (i = 0; i < bytes; i++)
        s_ras[i] = 0;
    om_uf_draw_tracked(f, s, s_ras, t->w8, w, t->h, 0, f->asc, track8);
    if (t->full_w > w)
        om_uf_fade(s_ras, t->w8, t->h, w - OM_UT_FADE, OM_UT_FADE);
    om_v1_write_bytes(t->tex, s_ras, bytes);
    s_last_us = frc_us((uint16_t)(frc() - t0));
    s_frame_us += s_last_us;
    s_budget_us -= (int32_t)s_last_us;
    if (s_last_us > s_worst_us && s_last_us < 1000000u)
        s_worst_us = s_last_us;
    s_count++;
    return 1;
}

int om_ut_text(om_ut *t, int face, const char *s, int clipw) { return text_impl(t, face, s, clipw, 0); }
int om_ut_text_tracked(om_ut *t, int face, const char *s, int track8) { return text_impl(t, face, s, 0, track8); }

int om_ut2_alloc(om_ut2 *d, int cap, int h)
{
    int rc = om_ut_alloc(&d->s[0], cap, h);
    if (!rc) rc = om_ut_alloc(&d->s[1], cap, h);
    d->front = 0;
    return rc;
}

int om_ut2_text(om_ut2 *d, int face, const char *s, int clipw)
{   /* review-13a I7: a slot rewritten while it is on screen is double-buffered -- the new text goes to the back slot
     * (not drawn in the list VDP1 is drawing now) and becomes the front only once it is complete */
    om_ut *b = &d->s[d->front ^ 1u];
    uint32_t k = om_ut_key(face, s, clipw);
    if (d->s[d->front].key == k)
        return 0;
    if (b->key != k && om_ut_text(b, face, s, clipw) < 0)
        return -1;
    d->front ^= 1u;
    d->s[d->front].lut_ok = 0;                         /* its LUT is written before it is first drawn */
    return 1;
}

void om_ut_icon(om_ut *t, const om_icon *ic)
{
    uint32_t bytes, i;
    t->w = t->full_w = ic->w;
    t->w8 = (uint16_t)((ic->w + 7) & ~7);
    t->key = 1;
    t->asc = 0;
    bytes = (uint32_t)t->w8 / 2u * t->h;
    for (i = 0; i < bytes; i++)
        s_ras[i] = 0;
    om_uf_icon(ic, s_ras, t->w8, t->w8, t->h, 0, 0);
    om_v1_write_bytes(t->tex, s_ras, bytes);
}

/* (i * 256 + 7) / 15 for the 4bpp ramp: a table, as SH-2 division is a libgcc call */
static const uint16_t k_ramp[16] = { 0, 17, 34, 51, 68, 85, 102, 119, 137, 154, 171, 188, 205, 222, 239, 256 };

void om_ut_colors(om_ut *t, uint16_t fg, uint16_t bg, uint16_t fade256)
{
    uint16_t lut[16], top;
    uint32_t i;
    if (t->lut_ok && t->fg == fg && t->bg == bg && t->fade == fade256)
        return;
    t->fg = fg; t->bg = bg; t->fade = fade256; t->lut_ok = 1;
    bg |= 0x8000u;
    top = om_c555_mix(bg, (uint16_t)(fg | 0x8000u), fade256);
    lut[0] = 0;
    for (i = 1; i < 16; i++)
        lut[i] = om_c555_mix(bg, top, k_ramp[i]);
    om_v1_write(t->lut, lut, 16);
    t->blut_ok = 0;                                    /* the bands follow lazily, only if this slot marquees */
}

static void band_luts(om_ut *t)
{   /* band b (0 outermost) at (b + 1) / 4 of the text's strength (addendum 9). Lazy: a fading
     * overlay rewrote 4 LUTs a slot a frame for slots that never scrolled */
    uint16_t lut[16], bg = (uint16_t)(t->bg | 0x8000u), top = om_c555_mix(bg, (uint16_t)(t->fg | 0x8000u), t->fade);
    uint32_t b, i;
    if (!t->blut || t->blut_ok)
        return;
    lut[0] = 0;
    for (b = 0; b < 3; b++) {
        uint16_t band = om_c555_mix(bg, top, (b + 1u) * 64u);
        for (i = 1; i < 16; i++)
            lut[i] = om_c555_mix(bg, band, k_ramp[i]);
        om_v1_write(t->blut + 32u * b, lut, 16);
    }
    t->blut_ok = 1;
}

void om_ut_badge_colors(om_ut *t, uint16_t bg, uint16_t fill, uint16_t letter)
{
    uint16_t lut[16];
    uint32_t i;
    if (t->lut_ok && t->fg == fill && t->bg == bg && t->c3 == letter)
        return;
    t->fg = fill; t->bg = bg; t->c3 = letter; t->lut_ok = 1;
    bg |= 0x8000u; fill |= 0x8000u; letter |= 0x8000u;
    lut[0] = 0;
    for (i = 1; i <= 8; i++)
        lut[i] = om_c555_mix(bg, fill, i * 32u);
    for (i = 9; i < 16; i++)
        lut[i] = om_c555_mix(fill, letter, ((i - 8u) * 256u + 3u) / 7u);
    om_v1_write(t->lut, lut, 16);
}

void om_ut_draw(const om_ut *t, int x, int y, uint16_t pmod, int z)
{
    if (t->key && t->w)
        om_v1_sprite(x, y, t->tex, t->w8, t->h, t->lut, pmod, z);
}

void om_ut_draw_base(const om_ut *t, int x, int base, uint16_t pmod, int z)
{
    om_ut_draw(t, x, base - t->asc, pmod, z);
}

void om_ut_draw_scaled(const om_ut *t, int x0, int y0, int x1, int y1, uint16_t pmod, int z)
{
    if (t->key && t->w && x1 >= x0 && y1 >= y0)
        om_v1_scaled(x0, y0, x1, y1, t->tex, t->w8, t->h, t->lut, 0, pmod, z);
}

void om_ut_draw_box(const om_ut *t, int x0, int x1, int base, int right, int32_t off, int32_t period, uint16_t pmod,
                    int zclip, int z)
{
    int y = base - t->asc;
    if (!t->key || !t->w)
        return;
    if (t->full_w <= x1 - x0 + 1 || t->w < t->full_w || period <= 0) {
        int x = right ? x1 + 1 - t->w : x0;            /* fits (or fade-cut to the box): plain */
        if ((pmod & OM_V1_CLIP) || t->w > x1 - x0 + 1) {   /* anything wider than its box is clipped */
            om_v1_clip(x0, y, x1, y + t->h - 1, (pmod & OM_V1_CLIP) ? zclip : z);
            pmod = (uint16_t)(pmod | OM_V1_CLIP);
        }
        om_v1_sprite(x, y, t->tex, t->w8, t->h, t->lut, pmod, z);
        return;
    }
    (void)zclip;
    om_ut_marquee(t, x0, x0, x1, y, y + t->h - 1, base, off, period, z);   /* both edges faded (addendum 9) */
}

void om_ut_flag(om_ut *t, const om_flag *f)
{   /* a region flag (addendum 6): 16x8 4 bpp, uploaded only when the slot shows another flag */
    if (t->key == (uint32_t)f->code && t->w)
        return;
    t->key = (uint32_t)f->code;
    t->w = t->full_w = OM_FLAG_TEX_W;
    t->w8 = OM_FLAG_TEX_W;
    t->asc = 0;
    t->lut_ok = 0;
    om_v1_write_bytes(t->tex, f->bits, OM_FLAG_TEX_W / 2 * OM_FLAG_H);
}

void om_ut_flag_colors(om_ut *t, const om_flag *f, uint16_t bg, uint16_t level256)
{   /* the flag's own palette, faded toward bg by level (256 = full; the filter dims an off region to 128) */
    uint16_t lut[16];
    uint32_t i;
    if (t->lut_ok && t->fg == (uint16_t)f->code && t->bg == bg && t->fade == level256)
        return;
    t->fg = (uint16_t)f->code; t->bg = bg; t->fade = level256; t->lut_ok = 1;
    lut[0] = 0;
    for (i = 0; i < 15; i++)
        lut[i + 1] = (uint16_t)(om_c555_mix((uint16_t)(bg | 0x8000u), f->rgb[i], level256) | 0x8000u);
    om_v1_write(t->lut, lut, 16);
}

static void __attribute__((noinline)) copies(const om_ut *t, int x, int y, int32_t off, int32_t period, int cx0,
                                             int cx1, int cy0, int cy1, uint32_t lut, int z)
{
    if (cx1 < cx0)
        return;
    om_v1_clip(cx0, cy0, cx1, cy1, z);
    if (x - off + t->w > cx0 && x - off <= cx1)
        om_v1_sprite(x - off, y, t->tex, t->w8, t->h, lut, OM_V1_CLIP, z);
    if (period > 0 && x - off + period <= cx1)
        om_v1_sprite(x - off + period, y, t->tex, t->w8, t->h, lut, OM_V1_CLIP, z);
}

void om_ut_marquee(const om_ut *t, int x, int x0, int x1, int y0, int y1, int base, int32_t off, int32_t period, int z)
{
    int y = base - t->asc, b, bw = OM_UT_FADE / 3, lfade = off > 0, rfade = x - off + (int)t->full_w - 1 > x1 || period;
    if (!t->key || !t->w)
        return;
    band_luts((om_ut *)t);
    if (!t->blut) {                                    /* no band LUTs: a plain clipped marquee */
        copies(t, x, y, off, period, x0, x1, y0, y1, t->lut, z);
        return;
    }
    copies(t, x, y, off, period, lfade ? x0 + OM_UT_FADE : x0, rfade ? x1 - OM_UT_FADE : x1, y0, y1, t->lut, z);
    for (b = 0; b < 3; b++) {
        if (lfade)
            copies(t, x, y, off, period, x0 + bw * b, x0 + bw * b + bw - 1, y0, y1, t->blut + 32u * (uint32_t)b, z);
        if (rfade)
            copies(t, x, y, off, period, x1 - bw * b - bw + 1, x1 - bw * b, y0, y1, t->blut + 32u * (uint32_t)b, z);
    }
}
