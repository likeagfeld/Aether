/* menu/ui/v1.c -- see v1.h. Command layouts per VDP1_Manual.txt (ST-013-R3) chapter 6-7: CMDCTRL bits 3-0 = 0 normal
 * sprite, 1 scaled sprite, 4 polygon, 8 user clipping coordinate set; a scaled sprite with zoom point 0 takes its
 * upper-left (XA,YA) and lower-right (XC,YC) corners. CMDSRCA = texture address / 8, CMDSIZE = (w / 8) << 8 | h
 * (HWF-06), CMDCOLR = LUT address / 8 or an RGB code, CMDGRDA = Gouraud table address / 8. BSD-3-Clause.
 *
 * aligned(4) and in sbuild's SGL_LONG_TABLES: SGL reads the SPRITE it is given with long loads; a table at 2 mod 4
 * hung slSynch once (debug-folders-vdp1-hang.md). One entry per command a frame, so SGL may keep a pointer until
 * slSynch without a later command overwriting it. */
#include "sgl.h"
#include "omlayout.h"
#include "v1.h"

#define OM_V1_MAX 240u              /* < MAX_POLYGONS 256 (menu/runtime/workarea.c); marquee bands use ~20 each */
#define OX 160                      /* SGL's local-coordinate origin is the screen centre (HWF-42, emulator-tier) */
#define OY 112

SPRITE om_v1_cmd[OM_V1_MAX] __attribute__((aligned(4)));
static uint32_t s_n, s_dropped;

/* M6 Task 5: the safe-area transform for the commands that follow (NULL = identity). While it is set, every rectangle
 * goes through om_safe_rect_u and every vertex through om_safe_xu/om_safe_yu (unclamped: a part partly off screen keeps
 * its size), and a normal sprite becomes a scaled sprite of its mapped rectangle (HWF-59: two vertices A and C, zoom
 * point 0000B, nearest-dot sampling with HSS = 0). */
static const om_safe *s_safe;
void om_v1_set_safe(const om_safe *t) { s_safe = t && !t->ident ? t : 0; }

static void map_rect(int *x0, int *y0, int *x1, int *y1)
{
    int r[4];
    om_safe_rect_u(s_safe, *x0, *y0, *x1, *y1, r);
    *x0 = r[0]; *y0 = r[1]; *x1 = r[2]; *y1 = r[3];
}

static void map_xy(const int16_t in[8], int16_t out[8])
{
    int i;
    for (i = 0; i < 8; i += 2) {
        out[i] = (int16_t)om_safe_xu(s_safe, in[i]);
        out[i + 1] = (int16_t)om_safe_yu(s_safe, in[i + 1]);
    }
}

void om_v1_begin(void) { s_n = 0; }
uint32_t om_v1_dropped(void) { return s_dropped; }
uint32_t om_v1_count(void) { return s_n; }

static SPRITE *next(int z, uint16_t ctrl)
{
    SPRITE *c;
    if (s_n >= OM_V1_MAX) {
        s_dropped++;                                   /* counted and named by the view, never silent */
        return 0;
    }
    c = &om_v1_cmd[s_n++];
    c->CTRL = ctrl;
    c->LINK = 0;
    c->PMOD = 0;
    c->COLR = 0;
    c->SRCA = 0;
    c->SIZE = 0;
    c->XA = c->YA = c->XB = c->YB = c->XC = c->YC = c->XD = c->YD = 0;
    c->GRDA = 0;
    c->DMMY = (Uint16)z;
    return c;
}

static void submit(SPRITE *c) { slSetSprite(c, toFIXED((Sint32)c->DMMY)); }

void om_v1_sprite(int x, int y, uint32_t tex, int w, int h, uint32_t lut, uint16_t pmod, int z)
{
    SPRITE *c;
    if (s_safe) {                                 /* the scaled sprite of its mapped rectangle (om_v1_scaled maps) */
        om_v1_scaled(x, y, x + w - 1, y + h - 1, tex, w, h, lut, 0, pmod, z);
        return;
    }
    c = next(z, 0x0000);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | (lut ? CL16Look : CL32KRGB) | pmod);
    c->COLR = (Uint16)(lut >> 3);
    c->SRCA = (Uint16)(tex >> 3);
    c->SIZE = (Uint16)(((w >> 3) << 8) | h);
    c->XA = (Sint16)(x - OX);
    c->YA = (Sint16)(y - OY);
    submit(c);
}

void om_v1_sprite_bank(int x, int y, uint32_t tex, int w, int h, uint16_t bank, int z)
{   /* identity only: its one caller (bootplay.c, the boot animation) draws with no safe area set */
    SPRITE *c = next(z, 0x0000);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | SPdis | CL256Bnk);
    c->COLR = (Uint16)(bank & 0xFF00u);
    c->SRCA = (Uint16)(tex >> 3);
    c->SIZE = (Uint16)(((w >> 3) << 8) | h);
    c->XA = (Sint16)(x - OX);
    c->YA = (Sint16)(y - OY);
    submit(c);
}

void om_v1_scaled(int x0, int y0, int x1, int y1, uint32_t tex, int w, int h, uint32_t lut, uint32_t gour,
                  uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0001);
    if (!c) return;
    if (s_safe) map_rect(&x0, &y0, &x1, &y1);
    c->PMOD = (Uint16)(ECdis | (lut ? CL16Look : CL32KRGB) | (gour ? CL_Gouraud : 0) | pmod);
    c->COLR = (Uint16)(lut >> 3);
    c->SRCA = (Uint16)(tex >> 3);
    c->SIZE = (Uint16)(((w >> 3) << 8) | h);
    c->XA = (Sint16)(x0 - OX);
    c->YA = (Sint16)(y0 - OY);
    c->XC = (Sint16)(x1 - OX);
    c->YC = (Sint16)(y1 - OY);
    c->GRDA = (Uint16)(gour >> 3);
    submit(c);
}

void om_v1_quad(int x0, int y0, int x1, int y1, uint16_t color, uint32_t gour, uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0004);
    if (!c) return;
    if (s_safe) map_rect(&x0, &y0, &x1, &y1);
    c->PMOD = (Uint16)(ECdis | SPdis | (gour ? CL_Gouraud : 0) | pmod);
    c->COLR = color;
    c->XA = (Sint16)(x0 - OX); c->YA = (Sint16)(y0 - OY);
    c->XB = (Sint16)(x1 - OX); c->YB = (Sint16)(y0 - OY);
    c->XC = (Sint16)(x1 - OX); c->YC = (Sint16)(y1 - OY);
    c->XD = (Sint16)(x0 - OX); c->YD = (Sint16)(y1 - OY);
    c->GRDA = (Uint16)(gour >> 3);
    submit(c);
}

static void corners(SPRITE *c, const int16_t xy0[8])
{
    int16_t m[8];
    const int16_t *xy = xy0;
    if (s_safe) {
        map_xy(xy0, m);
        xy = m;
    }
    c->XA = (Sint16)(xy[0] - OX); c->YA = (Sint16)(xy[1] - OY);
    c->XB = (Sint16)(xy[2] - OX); c->YB = (Sint16)(xy[3] - OY);
    c->XC = (Sint16)(xy[4] - OX); c->YC = (Sint16)(xy[5] - OY);
    c->XD = (Sint16)(xy[6] - OX); c->YD = (Sint16)(xy[7] - OY);
}

void om_v1_distorted(const int16_t xy[8], uint32_t tex, int w, int h, uint32_t lut, uint32_t gour, uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0002);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | (lut ? CL16Look : CL32KRGB) | (gour ? CL_Gouraud : 0) | pmod);
    c->COLR = (Uint16)(lut >> 3);
    c->SRCA = (Uint16)(tex >> 3);
    c->SIZE = (Uint16)(((w >> 3) << 8) | h);
    corners(c, xy);
    c->GRDA = (Uint16)(gour >> 3);
    submit(c);
}

void om_v1_distorted_bank(const int16_t xy[8], uint32_t tex, int w, int h, uint16_t bank, uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0002);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | CL256Bnk | pmod);
    c->COLR = bank;
    c->SRCA = (Uint16)(tex >> 3);
    c->SIZE = (Uint16)(((w >> 3) << 8) | h);
    corners(c, xy);
    submit(c);
}

void om_v1_poly4(const int16_t xy[8], uint16_t color, uint32_t gour, uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0004);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | SPdis | (gour ? CL_Gouraud : 0) | pmod);
    c->COLR = color;
    corners(c, xy);
    c->GRDA = (Uint16)(gour >> 3);
    submit(c);
}

void om_v1_line(int xa, int ya, int xb, int yb, uint16_t color, uint16_t pmod, int z)
{
    SPRITE *c = next(z, 0x0006);
    if (!c) return;
    c->PMOD = (Uint16)(ECdis | SPdis | pmod);
    c->COLR = color;
    if (s_safe) {
        xa = om_safe_xu(s_safe, xa); ya = om_safe_yu(s_safe, ya);
        xb = om_safe_xu(s_safe, xb); yb = om_safe_yu(s_safe, yb);
    }
    c->XA = (Sint16)(xa - OX); c->YA = (Sint16)(ya - OY);
    c->XB = (Sint16)(xb - OX); c->YB = (Sint16)(yb - OY);
    submit(c);
}

void om_v1_clip(int x0, int y0, int x1, int y1, int z)
{
    SPRITE *c = next(z, 0x0008);
    if (!c) return;
    if (s_safe) {   /* clamped, not map_rect: the fields are unsigned (VDP1_Manual.txt:4639-4680, 7.2: XA 10 bits, YA 9),
                     * so Off's design edge 0 -> -8 would wrap and clip everything away */
        int r[4];
        om_safe_rect(s_safe, x0, y0, x1, y1, r);
        x0 = r[0]; y0 = r[1]; x1 = r[2]; y1 = r[3];
    }
    c->XA = (Sint16)x0; c->YA = (Sint16)y0;            /* clipping coordinates are not local (HWF-42) */
    c->XC = (Sint16)x1; c->YC = (Sint16)y1;
    submit(c);
}

void om_v1_write(uint32_t off, const uint16_t *src, uint32_t words)
{
    volatile Uint16 *d = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + off);
    uint32_t i;
    for (i = 0; i < words; i++)
        d[i] = src[i];
}

void om_v1_write_bytes(uint32_t off, const uint8_t *src, uint32_t bytes)
{
    volatile Uint16 *d = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + off);
    uint32_t i;
    for (i = 0; i + 1 < bytes; i += 2)
        d[i >> 1] = (Uint16)((src[i] << 8) | src[i + 1]);
}

uint16_t om_v1_gour_delta(int dr, int dg, int db)
{
    int v[3], i;
    v[0] = dr; v[1] = dg; v[2] = db;
    for (i = 0; i < 3; i++) {
        v[i] += 16;
        if (v[i] < 0) v[i] = 0;
        if (v[i] > 31) v[i] = 31;
    }
    return (uint16_t)((v[2] << 10) | (v[1] << 5) | v[0]);
}

void om_v1_gouraud(uint32_t off, uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    uint16_t t[4];
    t[0] = a; t[1] = b; t[2] = c; t[3] = d;
    om_v1_write(off, t, 4);
}
