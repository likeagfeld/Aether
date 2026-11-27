/* menu/launcher/lgfx.c -- see lgfx.h. Registers: VDP2_Manual (HWF-09, -28, -29, -30, -49), SCU DMA level 0 (HWF-50);
 * the background player follows menu/views/loading.c (one frame decoded in place in HWRAM, moved by DMA to the hidden
 * bitmap, the bitmap base flipped in a V-blank). BSD-3-Clause. */
#include <string.h>
#include "lgfx.h"
#include "lgap.h"
#include "lgread.h"
#include "loadbg.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "satiator.h"
#include "uifont.h"

extern char font8x8_basic[128][8];      /* vdp2txt.c's (public domain) */

#define REG(o)     (*(volatile uint16_t *)(0x25F80000u + (o)))
#define TVSTAT     REG(0x004)
#define VRAM16     ((volatile uint16_t *)0x25E00000u)
#define VRAM8      ((volatile uint8_t *)0x25E00000u)
#define CRAM16     ((volatile uint16_t *)0x25F00000u)
#define VDP1VRAM16 ((volatile uint16_t *)0x25C00000u)
#define BMP_B0     0x40000u
#define BMP_A1     0x20000u
#define BMP_B1     0x60000u
#define SCU_REG(o) ((volatile uint32_t *)(0x25FE0000u + (o)))
#define SCU_D0W    SCU_REG(0x04)
#define SCU_D0AD   SCU_REG(0x0C)
#define SCU_D0EN   SCU_REG(0x10)
#define SCU_D0MD   SCU_REG(0x14)
#define SCU_DSTA   SCU_REG(0x7C)
#define DSTA_D0    0x00010030u
#define FILE8      ((uint8_t *)OM_LG_FILE)
#define PIX8       ((uint8_t *)OM_LG_PIX)
#define COVER16    ((uint16_t *)OM_LG_COVER)
#define RAST8      ((uint8_t *)OM_LG_RAST)
#define W          320
#define H          224
#define CHUNK      2048u
#define DEC_US     4000                 /* a pump's decoding budget */

om_lg_sty om_lg_c;
volatile uint8_t om_lg_on, om_lg_used, om_lg_disp;

static struct {
    om_lbg g;
    om_lbg_play p;
    om_lbg_dec d;
    om_lgr c;                           /* LOADBG.BIN's reads (lgread.h): every s_read word-aligned */
    int ok, ph, disp, decoding;
    int32_t credit;                     /* the reads' share of the time (lgread.h om_lgr_credit) */
    uint32_t us, dma_spins;
    uint32_t art_w, art_h;
} s;
enum { PH_IDLE, PH_DEC, PH_DMA, PH_READY };

static uint32_t s_name[76];
static const char *al(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n > 255) n = 255;
    memcpy(s_name, p, n);
    ((char *)s_name)[n] = 0;
    return (const char *)s_name;
}

uint16_t om_lg_mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t r = (a & 31u) + ((((b & 31u) - (a & 31u)) * t + 128u) >> 8);
    uint32_t g = ((a >> 5) & 31u) + (((((b >> 5) & 31u) - ((a >> 5) & 31u)) * t + 128u) >> 8);
    uint32_t bl = ((a >> 10) & 31u) + (((((b >> 10) & 31u) - ((a >> 10) & 31u)) * t + 128u) >> 8);
    return (uint16_t)(0x8000u | ((bl & 31u) << 10) | ((g & 31u) << 5) | (r & 31u));
}
static uint16_t c555(uint32_t rgb)
{
    return (uint16_t)(0x8000u | (((rgb >> 3) & 31u) << 10) | (((rgb >> 11) & 31u) << 5) | ((rgb >> 19) & 31u));
}

/* --- time and V-blank ---------------------------------------------------------------------------------------------- */
static uint16_t frc(void)
{
    uint8_t hi = *(volatile uint8_t *)0xFFFFFE12;
    uint8_t lo = *(volatile uint8_t *)0xFFFFFE13;
    return (uint16_t)((hi << 8) | lo);
}
static uint32_t ticks_us(uint16_t t)
{
    static const uint32_t div[4] = { 8, 32, 128, 128 };
    return (uint32_t)t * (div[*(volatile uint8_t *)0xFFFFFE16 & 3u] * 125u) / 3359u;   /* no overflow at clock/128 */
}
static uint16_t s_frc_last;
static uint32_t clock_step(void)
{   /* microseconds since the last call, added to s.us (a gap over one FRC wrap, ~19 ms at clock/8, ~312 ms at the save
     * swap's clock/128, counts short) */
    uint16_t t = frc();
    uint32_t us = ticks_us((uint16_t)(t - s_frc_last));
    s.us += us;
    s_frc_last = t;
    return us;
}
static int in_vblank(void) { return (TVSTAT & 0x0008) != 0; }
static void wait_vblank(void)
{   /* the next V-blank's start, bounded (~20 ms) */
    uint16_t t0 = frc();
    int low = !in_vblank();
    while (ticks_us((uint16_t)(frc() - t0)) < 18000u) {
        if (!in_vblank()) low = 1;
        else if (low) return;
    }
}

/* --- DMA (HWF-50) -------------------------------------------------------------------------------------------------- */
static uint32_t phys(uint32_t a) { return a & 0x07FFFFFFu; }
static int dma_busy(void) { return (*SCU_DSTA & DSTA_D0) != 0u; }
static void dma_frame(uint32_t vram)
{   /* the frame rows -> the bitmap at vram (512 B a row) */
    uint32_t r, *t = (uint32_t *)OM_CT(OM_LG_TBL), base = phys(0x25E00000u + vram);
    for (r = 0; r < H; r++, t += 3) {
        t[0] = W;
        t[1] = base + r * 512u;
        t[2] = phys(OM_LG_PIX + r * W);
    }
    t[-1] |= 0x80000000u;
    *SCU_D0EN = 0;
    *SCU_D0W = phys(OM_LG_TBL);
    *SCU_D0AD = 0x101u;
    *SCU_D0MD = 0x01000007u;
    *SCU_D0EN = 0x101u;
}
static void dma_wait(void)
{
    uint16_t t0 = frc();
    uint32_t us = 0;
    while (dma_busy() && us < 500000u) {
        uint16_t t = frc();
        us += ticks_us((uint16_t)(t - t0));
        t0 = t;
    }
}
static void flip(int b)
{   /* NBG1's bitmap base, B0 or B1 (map offset in 0x20000 units, HWF-28); called in a V-blank */
    REG(0x03C) = (uint16_t)((b ? 3u : 2u) << 4);
    s.disp = b;
    om_lg_disp = (uint8_t)b;
}

/* --- the file ------------------------------------------------------------------------------------------------------ */
static int entry_in(uint32_t e)
{
    const uint8_t *x = s.g.index + 12u * e;
    return om_lgr_has(&s.c, (uint32_t)(s.g.index - FILE8) + 12u * (s.g.n + 1u), om_be32(x), om_be32(x + 4));
}
static int read_some(void)
{   /* one read of up to 2 KB at rp; 1 = more to read, 0 = the whole file is in, < 0 = an error */
    return om_lgr_some(&s.c, CHUNK);
}
static void file_close(void)
{
    if (s.c.fd >= 0) s_close(s.c.fd);
    s.c.fd = -1;
}

/* the head (header, palette, index) read at once; 0 ok (else the file is closed) */
static int file_head(void);
static int file_open(void)
{
    int rc = file_head();
    if (rc) file_close();
    return rc;
}
static int file_head(void)
{
    static uint32_t st[76];
    uint32_t head;
    int n;
    s.c.fd = -1;
    if (s_stat(al("/aether/LOADBG.BIN"), (s_stat_t *)st, (int)sizeof st - 4) < 0) return -1;
    s.c.size = ((s_stat_t *)st)->size;
    if (s.c.size < 600u || s.c.size > OM_LG_FILE_MAX) return -1;
    if ((s.c.fd = s_open(al("/aether/LOADBG.BIN"), FA_READ | FA_OPEN_EXISTING)) < 0) return -1;
    if ((n = s_read(s.c.fd, FILE8, 2048)) < 64) return -1;
    head = om_lbg_head_bytes(FILE8, (uint32_t)n);
    s.c.rp = (uint32_t)n;
    while (s.c.rp < head && s.c.rp < s.c.size)
        if (om_lgr_read(&s.c, head, CHUNK) <= 0) return -1;
    if (om_lbg_open(&s.g, FILE8, s.c.rp, s.c.size) != OM_OK || s.g.w != W || s.g.h != H || s.g.scale != 1u) return -1;
    s.c.s1 = 0;
    s.c.e1 = s.c.rp;
    return 0;
}

/* the menu's last frame (bitmap B0 or A1, whichever holds a frame of this loop): 1 and s.p at that frame */
static int adopt(void)
{
    static const uint32_t base[2] = { BMP_B0, BMP_A1 };
    uint32_t b, r, f, crc;
    for (b = 0; b < 2; b++) {
        volatile const uint32_t *v;
        uint32_t *o = (uint32_t *)OM_CT(OM_LG_PIX);
        for (r = 0; r < H; r++) {
            v = (volatile const uint32_t *)(0x25E00000u + base[b] + r * 512u);
            for (f = 0; f < W / 4; f++) *o++ = v[f];
        }
        crc = om_crc32(0, (const void *)OM_CT(OM_LG_PIX), W * H);
        for (f = 0; f < s.g.n; f++)
            if (om_be32(s.g.index + 12u * (f + 1u) + 8u) == crc) {
                s.p.pix = PIX8;
                s.p.frame = f;
                return 1;
            }
    }
    return 0;
}

/* --- the player ---------------------------------------------------------------------------------------------------- */
static void palette(void)
{
    uint32_t i;
    for (i = 0; i < 256; i++) CRAM16[256 + i] = om_lbg_color(&s.g, i);
}

/* the player's phases, as far as they go now: at most one flip, then the next frame's decode (DEC_US a call) and its
 * DMA started. The launch calls this from its yield hook (lseq.c), between two whole Satiator calls, so one call takes
 * every step it can (hardware 2026-11-27: one phase a call took three calls a frame, ~30 ms apart in the swap) */
static void play(void)
{
    uint32_t period = s.g.fps == 60u ? 16000u : 32000u;
    for (;;) switch (s.ph) {
    case PH_IDLE: {
        uint32_t nx = om_lbg_next(&s.p, &s.g), off, len;
        if (!entry_in(nx + 1u)) return;
        om_lbg_entry_of(&s.g, nx, &off, &len);
        if (om_lbg_dec_begin(&s.d, &s.p, &s.g, FILE8 + off, len, 0) != OM_OK) { s.ok = 0; return; }
        s.ph = PH_DEC;
    }   /* fall through */
    case PH_DEC: {
        uint16_t t0 = frc();
        int rc;
        do rc = om_lbg_dec_run(&s.d, &s.p, &s.g, 64);
        while (rc == 1 && ticks_us((uint16_t)(frc() - t0)) < DEC_US);
        if (rc < 0) { s.ok = 0; return; }
        if (rc == 1) return;
        dma_frame(s.disp ? BMP_B0 : BMP_B1);
        s.ph = PH_DMA;
    }   /* fall through */
    case PH_DMA:
        if (dma_busy()) return;
        s.ph = PH_READY;
        /* fall through */
    default:                                                    /* PH_READY */
        switch (om_lgr_flip(s.us, period, in_vblank())) {
        case OM_LGR_LATER: return;
        case OM_LGR_WAIT: wait_vblank(); break;                 /* a late frame: the next V-blank, bounded */
        default: break;
        }
        flip(!s.disp);
        s.us = 0;
        s.ph = PH_IDLE;
        break;                                                  /* on to the next frame's decode */
    }
}

static void pump(int reads)
{   /* reads: 0 none, 1 one 2 KB read (a caller every V-blank: the editors), 2 the launch's share (lgread.h) */
    uint32_t dt;
    if (!om_lg_on) return;
    dt = clock_step();
    if (s.ok) play();                                           /* first: a pump that lands in the V-blank flips in it */
    s.credit = om_lgr_credit(s.credit, dt);
    if (!s.ok || s.c.fd < 0 || !reads) return;
    if (reads == 1) {
        if (read_some() <= 0) file_close();
        return;
    }
    while (s.credit > 0 && om_lgr_ahead(&s.c, &s.g, s.p.frame, OM_LGR_AHEAD) < OM_LGR_AHEAD) {
        uint16_t t0 = frc();
        int rc = read_some();
        s.credit -= (int32_t)ticks_us((uint16_t)(frc() - t0));
        if (rc <= 0) { file_close(); break; }
    }
}
void om_lg_pump(int may_read) { pump(may_read ? 1 : 0); }
void om_lg_pace(void) { pump(2); }

void om_lg_wait(uint32_t vblanks)
{
    while (vblanks--) {
        om_lgap();
        om_lg_pump(1);
    }
}

void om_lg_quiet(void) { file_close(); }

void om_lg_stop(void)
{
    if (!om_lg_on) return;
    dma_wait();
    file_close();
    s.ok = 0;
    om_lg_on = 0;
}

/* --- NBG0 drawing -------------------------------------------------------------------------------------------------- */
static int clip(int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > W - 1) *x1 = W - 1;
    if (*y1 > H - 1) *y1 = H - 1;
    return *x0 <= *x1 && *y0 <= *y1;
}
void om_lg_fill(int x0, int y0, int x1, int y1, uint16_t c)
{
    int x, y;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) VRAM16[y * 512 + x] = c;
}
void om_lg_clear(int x0, int y0, int x1, int y1) { om_lg_fill(x0, y0, x1, y1, 0); }
void om_lg_hbar(int x0, int y0, int x1, int y1, uint16_t c0, uint16_t c1)
{
    int x, y, n = x1 - x0 > 0 ? x1 - x0 : 1, ox = x0;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (x = x0; x <= x1; x++) {
        uint16_t c = om_lg_mix(c0, c1, (uint32_t)((x - ox) * 256 / n));
        for (y = y0; y <= y1; y++) VRAM16[y * 512 + x] = c;
    }
}
uint16_t om_lg_under(int x0, int y0, int x1, int y1)
{   /* the frame now on screen, every 4th pixel each way */
    uint32_t r = 0, g = 0, b = 0, n = 0;
    int x, y;
    if (!s.ok || !clip(&x0, &y0, &x1, &y1)) return c555(0x04081A);
    for (y = y0; y <= y1; y += 4)
        for (x = x0; x <= x1; x += 4) {
            uint16_t c = om_lbg_color(&s.g, PIX8[y * W + x]);
            r += c & 31u; g += (c >> 5) & 31u; b += (c >> 10) & 31u; n++;
        }
    return (uint16_t)(0x8000u | ((b / n) << 10) | ((g / n) << 5) | (r / n));
}

static const om_face *face(int f) { return om_face_get(f); }
int om_lg_width(int f, const char *s_) { return om_uf_width(face(f), s_); }

static void blit4(int x, int top, int w, int h, int stride, uint16_t fg, uint16_t bg, int solid)
{   /* solid: the box's empty pixels become transparent in the same pass (moving text never blinks) */
    int i, j;
    for (j = 0; j < h; j++) {
        int y = top + j;
        if (y < 0 || y >= H) continue;
        for (i = 0; i < w; i++) {
            int xx = x + i;
            uint8_t b = RAST8[j * (stride >> 1) + (i >> 1)];
            uint32_t c = (i & 1) ? (b & 15u) : (b >> 4);
            if (xx < 0 || xx >= W) continue;
            if (!c) { if (solid) VRAM16[y * 512 + xx] = 0; continue; }
            VRAM16[y * 512 + xx] = c == 15u ? fg : om_lg_mix(bg, fg, c * 17u);
        }
    }
}

/* the raster of str in a box of boxw px, scrolled by off (a copy every width + 24 px when it does not fit) */
static int raster(const om_face *fc, const char *str, int boxw, int32_t off, int fade)
{
    int w = om_uf_width(fc, str), h = fc->asc + fc->desc, stride = (boxw + 1) & ~1;
    if (boxw <= 0 || stride / 2 * h > (int)OM_LG_RAST_MAX) return 0;
    memset(RAST8, 0, (uint32_t)(stride / 2 * h));
    if (w <= boxw) off = 0;
    else off %= w + 24;
    om_uf_draw(fc, str, RAST8, stride, boxw, h, -off, fc->asc);
    if (off && w - off < boxw) om_uf_draw(fc, str, RAST8, stride, boxw, h, w + 24 - off, fc->asc);
    if (!off && w > boxw && fade) om_uf_fade(RAST8, stride, h, boxw - 12, 12);
    return stride;
}

void om_lg_bartext(int x0, int y0, int x1, int y1, uint16_t c0, uint16_t c1, uint16_t edge, int f, int tx, int base,
                   int boxw, const char *str, uint16_t fg, int32_t off)
{
    const om_face *fc = face(f);
    int h = fc->asc + fc->desc, top = base - fc->asc, stride = raster(fc, str, boxw, off, 1), x, y, n = x1 - x0 > 0 ? x1 - x0 : 1;
    for (y = y0; y <= y1; y++) {
        if (y < 0 || y >= H) continue;
        for (x = x0; x <= x1; x++) {
            uint16_t c = x < x0 + 2 ? edge : om_lg_mix(c0, c1, (uint32_t)((x - x0) * 256 / n));
            int i = x - tx, j = y - top;
            if (stride && i >= 0 && i < boxw && j >= 0 && j < h) {
                uint8_t b = RAST8[j * (stride >> 1) + (i >> 1)];
                uint32_t k = (i & 1) ? (b & 15u) : (b >> 4);
                if (k) c = k == 15u ? fg : om_lg_mix(c, fg, k * 17u);
            }
            if (x >= 0 && x < W) VRAM16[y * 512 + x] = c;
        }
    }
}

int om_lg_text(int f, int x, int base, int maxw, const char *str, uint16_t fg, uint16_t bg, int flags)
{
    const om_face *fc = face(f);
    int w = om_uf_width(fc, str), h = fc->asc + fc->desc, bw, stride;
    if (maxw > 0 && w > maxw) bw = maxw; else bw = w;
    if (bw <= 0) return 0;
    stride = (bw + 1) & ~1;
    if (stride / 2 * h > (int)OM_LG_RAST_MAX) return 0;
    memset(RAST8, 0, (uint32_t)(stride / 2 * h));
    om_uf_draw(fc, str, RAST8, stride, bw, h, 0, fc->asc);
    if (w > bw && (flags & OM_LG_FADE)) om_uf_fade(RAST8, stride, h, bw - 12, 12);
    if (flags & OM_LG_RIGHT) x -= bw;
    if (!bg) bg = om_lg_under(x, base - fc->asc, x + bw - 1, base + fc->desc);
    blit4(x, base - fc->asc, bw, h, stride, fg, bg, flags & OM_LG_SOLID);
    return bw;
}

int om_lg_marquee(int f, int x, int base, int boxw, const char *str, uint16_t fg, uint16_t bg, int32_t off)
{   /* the whole box in one pass: a scrolled name never blinks */
    const om_face *fc = face(f);
    int stride = raster(fc, str, boxw, off, 1);
    if (!stride) return 0;
    if (!bg) bg = om_lg_under(x, base - fc->asc, x + boxw - 1, base + fc->desc);
    blit4(x, base - fc->asc, boxw, fc->asc + fc->desc, stride, fg, bg, 1);
    return boxw;
}

int om_lg_tracked(int x, int base, const char *str, uint16_t fg)
{
    const om_face *fc = face(OM_LGF_LABEL);
    int w = om_uf_width_tracked(fc, str, 13), h = fc->asc + fc->desc, stride = (w + 1) & ~1;
    if (stride / 2 * h > (int)OM_LG_RAST_MAX) return 0;
    memset(RAST8, 0, (uint32_t)(stride / 2 * h));
    om_uf_draw_tracked(fc, str, RAST8, stride, w, h, 0, fc->asc, 13);
    blit4(x, base - fc->asc, w, h, stride, fg, om_lg_under(x, base - fc->asc, x + w - 1, base), 0);
    return w;
}

void om_lg_mono(int x, int y, const char *str, uint16_t fg, uint16_t bg)
{   /* bg 0: transparent around the glyphs */
    int i, j;
    for (; *str && x <= W - 8; str++, x += 8)
        for (j = 0; j < 8; j++) {
            uint8_t row = (uint8_t)font8x8_basic[(uint8_t)*str & 0x7Fu][j];
            for (i = 0; i < 8; i++)
                VRAM16[(y + j) * 512 + x + i] = (row >> i) & 1u ? fg : bg;
        }
}

/* the cover: the loading screen's box (layout.h LD_COVER_TALL / LD_COVER_SQ), scaled to its width, a soft shadow
 * pre-blended against the background under it, a lit top edge */
void om_lg_cover(int dim, int compact)
{
    int tall = s.art_w ? s.art_h > s.art_w : 1, x0 = compact ? 200 : 190, y0 = tall ? (compact ? 16 : 22) : (compact ? 40 : 57),
        dw = compact ? 96 : 108, dh = tall ? (compact ? 150 : 169) : dw, x, y;
    uint16_t sh = om_lg_mix(om_lg_under(x0 + 4, y0 + 4, x0 + dw + 4, y0 + dh + 4), 0x8000u, 150);
    om_lg_fill(x0 + 4, y0 + 5, x0 + dw + 3, y0 + dh + 4, sh);
    if (!s.art_w) {
        uint16_t fill = c555(0x2A3046), edge = c555(0x46506A);
        om_lg_fill(x0, y0, x0 + dw - 1, y0 + dh - 1, edge);
        om_lg_fill(x0 + 1, y0 + 1, x0 + dw - 2, y0 + dh - 2, fill);
        om_lg_text(OM_LGF_SMALL, x0 + (dw - om_lg_width(OM_LGF_SMALL, "NO ART")) / 2, y0 + dh / 2 + 4, 0, "NO ART",
                   c555(0xC8C8D2), fill, 0);
        return;
    }
    for (y = 0; y < dh; y++) {
        const uint16_t *src = COVER16 + (uint32_t)(y * (int)s.art_h / dh) * s.art_w;
        for (x = 0; x < dw; x++) {
            uint16_t c = src[x * (int)s.art_w / dw];
            c = (c & 0x8000u) ? c : 0x8000u;
            VRAM16[(y0 + y) * 512 + x0 + x] = dim ? om_lg_mix(c, 0x8421u, 160) : c;
        }
    }
    for (x = 0; x < dw; x++)
        VRAM16[y0 * 512 + x0 + x] = om_lg_mix(VRAM16[y0 * 512 + x0 + x], c555(0x9AA6B8), 90);
}

__asm__(".section .lui.logo,\"a\"\n.align 2\n_s_logo: .incbin \"menu/app/assets/footlogo.bin\"\n_s_logo_end:\n.text\n");
extern const uint8_t s_logo[], s_logo_end[];
void om_lg_brand(void)
{   /* the footer: its 22x15 logo (24 wide), then the product name (the Aether rename: one word, the text colour) */
    int x, y;
    if (s_logo_end - s_logo == 24 * 15 * 2)
        for (y = 0; y < 15; y++)
            for (x = 0; x < 24; x++) {
                uint16_t c = om_be16(s_logo + 2u * (uint32_t)(y * 24 + x));
                if (c & 0x8000u) VRAM16[(200 + y) * 512 + 16 + x] = c;
            }
    (void)om_lg_text(OM_LGF_LABEL, 42, 211, 0, "AETHER", om_lg_c.txt, 0, 0);
}

/* --- start ---------------------------------------------------------------------------------------------------------- */
static void vdp2_setup(int keep)
{   /* in a V-blank: NBG1 the eye (B0 shown), NBG0 the RGB overlay (cleared before), nothing else */
    REG(0x00E) = 0x1300;                                         /* RAMCTL: CRAM as SGL left it, banks partitioned */
    REG(0x010) = 0x4444; REG(0x012) = 0xEEEE;                    /* A0: NBG0 bitmap x4 (32,768 colors), CPU */
    REG(0x014) = 0x4444; REG(0x016) = 0xEEEE;                    /* A1: the same */
    REG(0x018) = 0x55EE; REG(0x01A) = 0xEEEE;                    /* B0: NBG1 bitmap x2 (256 colors), CPU */
    REG(0x01C) = 0x55EE; REG(0x01E) = 0xEEEE;                    /* B1: the same */
    REG(0x028) = 0x1232;                                         /* CHCTLA: NBG0 RGB bitmap 512x256, NBG1 256 colors */
    REG(0x02C) = 0x0100;                                         /* BMPNA: NBG1 palette 1 (CRAM 256-511) */
    REG(0x03C) = 0x0020;                                         /* MPOFN: NBG0 at 0, NBG1 at B0 */
    REG(0x070) = 0; REG(0x072) = 0; REG(0x074) = 0; REG(0x076) = 0;
    REG(0x078) = 1; REG(0x07A) = 0; REG(0x07C) = 1; REG(0x07E) = 0;
    REG(0x080) = 0; REG(0x082) = 0; REG(0x084) = 0; REG(0x086) = 0;
    REG(0x088) = 1; REG(0x08A) = 0; REG(0x08C) = 1; REG(0x08E) = 0;
    REG(0x098) = 0;                                              /* ZMCTL */
    REG(0x09A) = 0;                                              /* SCRCTL */
    REG(0x0D0) = 0; REG(0x0D2) = 0;                              /* windows off */
    REG(0x0E0) = 0;                                              /* SPCTL */
    REG(0x0E4) = 0;                                              /* CRAOFA */
    REG(0x0EC) = 0;                                              /* CCCTL: no color calculation */
    REG(0x0F0) = 0; REG(0x0F2) = 0; REG(0x0F4) = 0; REG(0x0F6) = 0;   /* sprites (the menu's VDP1) hidden */
    REG(0x0F8) = 0x0607;                                         /* PRINA: NBG0 7, NBG1 6 */
    REG(0x0FA) = 0;                                              /* NBG2, NBG3 off */
    REG(0x110) = 0;                                              /* no color offset (no fade) */
    REG(0x020) = keep ? 0x0203 : 0x0001;                         /* BGON: NBG0 (+ NBG1, its dot 0 opaque) */
    REG(0x000) = 0x8000;                                         /* TVMD: on, 320 x 224 */
}

int om_lg_start(const om_handoff *h, int adopt_frame)
{
    uint32_t i;
    memset(&s, 0, sizeof s);
    s.c.file = FILE8;
    s.c.fd = -1;
    om_lg_c.txt = (uint16_t)(h && h->sty[0] ? h->sty[0] | 0x8000u : c555(0xE8EEF8));
    om_lg_c.acc2 = (uint16_t)(h && h->sty[1] ? h->sty[1] | 0x8000u : c555(0x78C8FF));
    om_lg_c.acc = (uint16_t)(h && h->sty[2] ? h->sty[2] | 0x8000u : c555(0x409CFF));
    om_lg_c.dim = om_lg_mix(c555(0x04081A), om_lg_c.txt, 160);   /* as the loading screen derives them */
    om_lg_c.faint = om_lg_mix(c555(0x04081A), om_lg_c.txt, 110);
    om_lg_c.err = c555(0xFFA08C);
    om_lg_c.gold = c555(0xFFD65A);
    om_lg_c.ok = c555(0x5FE39A);
    if (h && h->art && h->art_w && h->art_h && h->art_w <= 160u && h->art_h <= 200u &&
        (uint32_t)h->art_w * h->art_h <= 25600u &&                 /* omlayout.h OM_HERO_PIXELS: what fits here */
        h->art + 2u * h->art_w * h->art_h <= 0x80000u) {         /* the menu's cover, before step 5 resets VDP1 */
        uint16_t *o = (uint16_t *)OM_CT(OM_LG_COVER);
        for (i = 0; i < (uint32_t)h->art_w * h->art_h; i++) o[i] = VDP1VRAM16[h->art / 2u + i];
        s.art_w = h->art_w;
        s.art_h = h->art_h;
    }
    if (h && (h->flags & OM_HO_TEST_NO_SATIATOR)) {             /* the L1 harness: no card; a copy it put in RAM */
        if (!memcmp(FILE8, "OMSATLBG", 8)) {
            s.c.size = om_be32(FILE8 + 0x18) + om_be32(FILE8 + 0x1C);
            if (s.c.size <= OM_LG_FILE_MAX && om_lbg_open(&s.g, FILE8, s.c.size, s.c.size) == OM_OK && s.g.w == W &&
                s.g.h == H) {
                s.c.e1 = s.c.rp = s.c.size;
                s.ok = om_lbg_start(&s.p, &s.g, PIX8) == OM_OK;
            }
        }
    } else if (file_open() == 0) {
        s.ok = 1;
        if (adopt_frame && adopt()) {
            uint32_t nx = om_lbg_next(&s.p, &s.g), off, len;
            om_lbg_entry_of(&s.g, nx, &off, &len);               /* read on from the next frame's entry, then wrap */
            if (om_lgr_from(&s.c, off) < 0) { s.ok = 0; file_close(); }   /* from off's word (lgread.h) */
            om_lbg_entry_of(&s.g, 0, &s.c.w0, &len);             /* the wrap from frame 0's entry: never the keyframe */
        } else {
            uint32_t need = om_lbg_key_need(&s.g);
            while (s.ok && s.c.rp < need)
                if (read_some() <= 0) { s.ok = 0; file_close(); }
            if (s.ok && om_lbg_start(&s.p, &s.g, PIX8) != OM_OK) { s.ok = 0; file_close(); }
        }
    }
    if (!s.ok) {                                                 /* no LOADBG.BIN: the loading screen's dark blue */
        for (i = 0; i < W * H / 4; i++) ((uint32_t *)OM_CT(OM_LG_PIX))[i] = 0;
        CRAM16[256] = 0x1880;                                    /* (4, 8, 26) */
    } else
        palette();
    /* the frame into B0 (hidden while the menu's stays up, or nothing shows yet), the overlay cleared, then on */
    dma_wait();
    dma_frame(BMP_B0);
    dma_wait();
    for (i = 0; i < 0x40000u / 4u; i++) ((volatile uint32_t *)0x25E00000u)[i] = 0;
    wait_vblank();
    vdp2_setup(1);
    s.disp = 0;
    om_lg_disp = 0;
    s.ph = PH_IDLE;
    s_frc_last = frc();
    s.credit = OM_LGR_CREDIT_MAX;
    om_lg_on = om_lg_used = 1;
    return 0;
}
