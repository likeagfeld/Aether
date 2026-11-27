/* menu/launcher/lorb.c -- see lorb.h. The geometry, colours and timing are the interstitial's (menu/src/istlgeom.h,
 * menu/views/interstitial.c); its VDP1 lines become CPU-drawn pixels on NBG0. NBG0 is single-buffered, so the comet is
 * moved by erasing its last position (each pixel back to the logo or transparent), redrawing the orbit under it and
 * drawing the new one. LWRAM, so every byte here comes off the heap: -Os. BSD-3-Clause. */
#pragma GCC optimize("Os")
#include <string.h>
#include "istlgeom.h"
#include "lgap.h"
#include "lgfx.h"
#include "lorb.h"
#include "om_fmt.h"

extern char font8x8_basic[128][8];      /* vdp2txt.c's (public domain) */

#define REG(o)   (*(volatile uint16_t *)(0x25F80000u + (o)))
#define VRAM16   ((volatile uint16_t *)0x25E00000u)
#define VRAM8    ((volatile uint8_t *)0x25E00000u)
#define W        320
#define H        224
#define ST_MAGIC 0x4F524231u            /* "ORB1" */
#define ST_LOGO  16u                    /* the half logo, 76 x 50 RGB555 */
#define ST_FACE  (ST_LOGO + 76u * 50u * 2u)
#define TRAIL    22
#define US_UNIT  4171u                  /* 1/256 turn: 64 V-blanks a turn at 59.94 Hz */

volatile uint8_t om_orb_armed;

static struct {
    int cx, cy, rx, ry, lx, ly, lw, lh, ls, hw, thick, disc;
    uint32_t lsrc, head, t0;
    uint16_t bg, orb;
} g;

static const int16_t k_qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366,
    7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665,
    12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679,
    15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384 };
static const uint8_t k_trail[TRAIL] = { 255, 235, 215, 196, 178, 160, 144, 128, 113, 99, 86, 73, 62, 51, 41, 32, 25, 18,
                                        12, 7, 3, 1 };

static int32_t qsin(uint32_t a)
{
    a &= 255u;
    if (a < 64u) return k_qsin[a];
    if (a < 128u) return k_qsin[128u - a];
    if (a < 192u) return -k_qsin[a - 128u];
    return -k_qsin[256u - a];
}
enum { P_BACK, P_FRONT, P_ERASE, P_SKIP };
static int ex(uint32_t a) { return (int)((g.rx * qsin(a + 64u)) >> 14); }
static int ey(uint32_t a) { return (int)((g.ry * qsin(a)) >> 14); }
static int ox(uint32_t a) { return g.cx + OM_ISTL_TX(ex(a), ey(a)); }  /* tilted as the menu's (istlgeom.h, D9) */
static int oy(uint32_t a) { return g.cy + OM_ISTL_TY(ex(a), ey(a)); }
/* the far half (qsin < 0) is drawn behind the logo, and not at all inside the planet's circle: the line-art logo is
 * open in the middle (interstitial.c behind_planet) */
static int far_mode(uint32_t a, int x, int y)
{
    int dx = x - g.cx, dy = y - g.cy;
    if (qsin(a) >= 0) return P_FRONT;
    return dx * dx + dy * dy < g.disc * g.disc ? P_SKIP : P_BACK;
}

static uint16_t mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t r = 0, k;
    for (k = 0; k < 15; k += 5) {
        int32_t x = (a >> k) & 31, y = (b >> k) & 31;
        r |= (uint32_t)((x + (((y - x) * (int32_t)t + 128) >> 8)) & 31) << k;
    }
    return (uint16_t)(0x8000u | r);
}
static uint16_t c555(uint32_t rgb)
{
    return (uint16_t)(0x8000u | (((rgb >> 3) & 31u) << 10) | (((rgb >> 11) & 31u) << 5) | ((rgb >> 19) & 31u));
}

/* the logo's pixel at screen (x, y), 0 outside it or where it is transparent */
static uint16_t logo_at(int x, int y)
{
    uint16_t v;
    x -= g.lx; y -= g.ly;
    if (x < 0 || y < 0 || x >= g.lw || y >= g.lh) return 0;
    v = VRAM16[g.lsrc + (uint32_t)(y * g.ls + x)];
    return (v & 0x8000u) ? v : 0;
}

static void plot(int x, int y, uint16_t c, int mode)
{
    if (mode == P_SKIP || x < 0 || y < 0 || x >= W || y >= H) return;
    if (mode == P_ERASE) c = logo_at(x, y);
    else if (mode == P_BACK && logo_at(x, y)) return;          /* behind the logo */
    VRAM16[y * 512 + x] = c;
}
static void line(int x0, int y0, int x1, int y1, uint16_t c, int mode)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        plot(x0, y0, c, mode);
        int e2 = 2 * e;
        if (x0 == x1 && y0 == y1) return;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void box(int x0, int y0, int x1, int y1, uint16_t c, int mode)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) plot(x, y, c, mode);
}

/* the orbit's segments k (8 angle units each) from a to b: the back half behind the logo */
static void orbit(uint32_t a, uint32_t b)
{
    uint32_t k;
    for (k = a & ~7u; (int32_t)(k - b) <= 0; k += 8u) {
        int xa = ox(k), ya = oy(k), xb = ox(k + 8u), yb = oy(k + 8u);
        line(xa, ya, xb, yb, g.orb, far_mode(k + 4u, (xa + xb) >> 1, (ya + yb) >> 1));
    }
}

static void comet(uint32_t head, int erase)
{   /* the tail first, then the head (interstitial.c comet); the first segments a pixel thicker */
    uint16_t col = c555(0xAAE1FF);
    int i, x, y;
    for (i = TRAIL - 1; i >= 0; i--) {
        uint32_t a = head - 2u * (uint32_t)i;
        int xa = ox(a), ya = oy(a), xb = ox(a - 2u), yb = oy(a - 2u);
        int mode = erase ? P_ERASE : far_mode(a - 1u, (xa + xb) >> 1, (ya + yb) >> 1);
        uint16_t c = mix(g.bg, col, k_trail[i]);
        line(xa, ya, xb, yb, c, mode);
        if (i < g.thick) line(xa, ya + 1, xb, yb + 1, c, mode);
    }
    x = ox(head); y = oy(head);
    i = erase ? P_ERASE : far_mode(head, x, y);
    box(x - g.hw, y - 1, x + g.hw, y + g.hw, mix(g.bg, col, 230), i);
    box(x - g.hw + 1, y, x + g.hw - 1, y + g.hw - 1, mix(g.bg, 0xFFFFu, 220), i);
}

void om_orb_clear(int x0, int y0, int x1, int y1)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) VRAM16[y * 512 + x] = 0;
}

void om_orb_scene(int compact, uint16_t bg)
{
    int x, y;
    g.cx = OM_ISTL_CX;
    g.cy = compact ? 38 : OM_ISTL_CY;
    g.rx = compact ? OM_ISTL_RX / 2 : OM_ISTL_RX;
    g.ry = compact ? OM_ISTL_RY / 2 : OM_ISTL_RY;
    g.lw = compact ? OM_ISTL_LOGO_W / 2 : OM_ISTL_LOGO_W;
    g.lh = compact ? OM_ISTL_LOGO_H / 2 : OM_ISTL_LOGO_H;
    g.lx = g.cx - g.lw / 2;
    g.ly = g.cy - g.lh / 2;
    g.ls = compact ? g.lw : 512;
    g.lsrc = compact ? (OM_ORB_STASH + ST_LOGO) / 2u : OM_ORB_LOGO_X;
    g.hw = compact ? 1 : 2;
    g.thick = compact ? 4 : 8;
    g.disc = compact ? OM_ISTL_DISC_R / 2 : OM_ISTL_DISC_R;
    g.bg = bg;
    g.orb = mix(bg, c555(0x78B4F0), 46);
    REG(0x112) = 0;                                            /* CLOFSL: NBG1 on offset A, which darkens it */
    REG(0x114) = (uint16_t)(-OM_ORB_DIM & 0x1FF);
    REG(0x116) = (uint16_t)(-OM_ORB_DIM & 0x1FF);
    REG(0x118) = (uint16_t)(-OM_ORB_DIM & 0x1FF);
    REG(0x110) = 0x0002;                                       /* CLOFEN: NBG1 only */
    om_orb_clear(0, 0, W - 1, H - 1);
    orbit(0, 248);                                             /* the back half stays behind the logo */
    for (y = 0; y < g.lh; y++)
        for (x = 0; x < g.lw; x++) {
            uint16_t v = logo_at(g.lx + x, g.ly + y);
            if (v) VRAM16[(g.ly + y) * 512 + g.lx + x] = v;
        }
    orbit(0, 248);                                             /* and the front half over it */
    g.t0 = om_lclock_us();
    g.head = 0;
    comet(0, 0);
}

void om_orb_tick(void)
{
    uint32_t head = (om_lclock_us() - g.t0) / US_UNIT;
    if (head == g.head) return;
    comet(g.head, 1);
    orbit(g.head - 2u * TRAIL - 8u, g.head + 8u);              /* the orbit under where it was */
    comet(head, 0);
    g.head = head;
}

void om_orb_text(const om_face *f, int x0, int x1, int base, const char *s, uint16_t fg, int flags)
{   /* rasterized a band of rows at a time into a small stack buffer (as interstitial.c raster) */
    uint8_t buf[640] __attribute__((aligned(4)));
    int bw = x1 - x0 + 1, stride = (bw + 1) & ~1, rows = 640 / (stride / 2), asc = f->asc, h = asc + f->desc, y0, i, j;
    int track = (flags & OM_ORB_TRACK) ? 13 : 0, w = om_uf_width_tracked(f, s, track), tx = 0, solid = flags & OM_ORB_SOLID;
    if (bw <= 0 || bw > W) return;
    if (flags & OM_ORB_CENTER) tx = (bw - w) / 2;
    else if (flags & OM_ORB_RIGHT) tx = bw - w;
    if (tx < 0) tx = 0;
    for (y0 = 0; y0 < h; y0 += rows) {
        int n = h - y0 < rows ? h - y0 : rows;
        memset(buf, 0, (uint32_t)(stride / 2 * n));
        om_uf_draw_tracked(f, s, buf, stride, bw, n, tx, asc - y0, track);
        if (w > bw) om_uf_fade(buf, stride, n, bw - 12, 12);
        for (j = 0; j < n; j++) {
            int y = base - asc + y0 + j;
            if (y < 0 || y >= H) continue;
            for (i = 0; i < bw; i++) {
                uint32_t k = buf[j * (stride >> 1) + (i >> 1)];
                k = (i & 1) ? (k & 15u) : (k >> 4);
                if (x0 + i < 0 || x0 + i >= W || (!k && !solid)) continue;
                VRAM16[y * 512 + x0 + i] = !k ? 0 : k == 15u ? fg : mix(g.bg, fg, k * 17u);
            }
        }
    }
}

void om_orb_mono(int x, int y, const char *s, uint16_t fg)
{
    int i, j;
    for (; *s && x <= W - 8; s++, x += 8)
        for (j = 0; j < 8; j++) {
            uint8_t row = (uint8_t)font8x8_basic[(uint8_t)*s & 0x7Fu][j];
            if (y + j < 0 || y + j >= H) continue;
            for (i = 0; i < 8; i++) VRAM16[(y + j) * 512 + x + i] = (row >> i) & 1u ? fg : 0;
        }
}

int om_orb_stash_ok(void)
{
    uint32_t len = *(volatile uint32_t *)(0x25E00000u + OM_ORB_STASH + 4u);
    if (*(volatile uint32_t *)(0x25E00000u + OM_ORB_STASH) != ST_MAGIC || len <= ST_FACE || len > 0x8000u) return 0;
    return om_crc32(0, (const void *)(0x25E00000u + OM_ORB_STASH + ST_LOGO), len - ST_LOGO) ==
           *(volatile uint32_t *)(0x25E00000u + OM_ORB_STASH + 8u);
}

uint16_t om_orb_stash_bg(void) { return (uint16_t)(*(volatile uint32_t *)(0x25E00000u + OM_ORB_STASH + 12u)); }

const om_face *om_orb_face(int i)
{
    return (const om_face *)(0x25E00000u + OM_ORB_STASH + ST_FACE + (uint32_t)i * sizeof(om_face));
}
