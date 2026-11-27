#include "th.h"
#include "tga.h"
#include "omlayout.h"

static uint8_t f[200000];
static uint16_t cv[OM_HERO_PIXELS];

static uint32_t hdr(uint8_t type, uint8_t idlen, uint16_t cmlen, uint8_t cmbits, uint16_t w, uint16_t h, uint8_t bpp, uint8_t desc)
{
    memset(f, 0, 18);
    f[0] = idlen; f[1] = cmlen ? 1 : 0; f[2] = type;
    f[5] = (uint8_t)cmlen; f[6] = (uint8_t)(cmlen >> 8); f[7] = cmbits;
    f[12] = (uint8_t)w; f[13] = (uint8_t)(w >> 8); f[14] = (uint8_t)h; f[15] = (uint8_t)(h >> 8);
    f[16] = bpp; f[17] = desc;
    memset(f + 18, 'I', idlen);
    return 18u + idlen;
}

#define PX(x, y, cw) cv[(y) * (cw) + (x)]

int main(void)
{
    om_tga_info in;
    uint32_t o, i;
    /* 24 bpp, 2x2, bottom-left origin, ID field 5 bytes: row 0 in the file is the BOTTOM row */
    o = hdr(2, 5, 0, 0, 2, 2, 24, 0x00);
    { const uint8_t px[] = { 255,0,0,  0,255,0,   0,0,255,  255,255,255 };   /* B,G,R triples */
      memcpy(f + o, px, sizeof px); o += sizeof px; }
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(in.layout, OM_TGA_SQUARE); CHECK_INT(in.scale, 2);
    /* 2x2 doubled = 4x4 centred in 160x160: top-left at (78,78) */
    CHECK_INT(PX(78, 78, 160), 0x8000 | 0x001F);              /* top row = file row 1: first pixel R=255 */
    CHECK_INT(PX(80, 78, 160), 0xFFFF);                       /* white */
    CHECK_INT(PX(78, 80, 160), 0x8000 | (31 << 10));          /* bottom row: blue */
    CHECK_INT(PX(80, 80, 160), 0x8000 | (31 << 5));           /* green */
    CHECK_INT(PX(0, 0, 160), 0x0000);                         /* transparent padding */
    /* top-left origin flag (bit 5) */
    f[17] = 0x20;
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(PX(78, 78, 160), 0x8000 | (31 << 10));
    /* 32 bpp alpha: a < 128 is transparent */
    o = hdr(2, 0, 0, 0, 1, 1, 32, 0x28);
    f[o++] = 0; f[o++] = 0; f[o++] = 255; f[o++] = 127;
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(PX(79, 79, 160), 0);
    f[o - 1] = 128;
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(PX(79, 79, 160), 0x801F);
    /* 16 bpp ARRRRRGGGGGBBBBB little-endian, opaque */
    o = hdr(2, 0, 0, 0, 1, 1, 16, 0x20);
    f[o++] = 0x1F; f[o++] = 0x00;                             /* B=31 */
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(PX(79, 79, 160), 0x8000 | (31 << 10));
    /* type 1, 8 bpp, 24-bit colour map of 2 entries */
    o = hdr(1, 0, 2, 24, 1, 1, 8, 0x20);
    f[o++] = 0; f[o++] = 0; f[o++] = 0;  f[o++] = 0; f[o++] = 255; f[o++] = 0;   /* entry 1 = green */
    f[o++] = 1;
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(PX(79, 79, 160), 0x8000 | (31 << 5));
    f[o - 1] = 2;
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_TGA_E_DATA);
    /* Rings-sized tall cover: 64x100 -> doubled to fill 128x200 exactly */
    o = hdr(2, 0, 0, 0, 64, 100, 24, 0x20);
    for (i = 0; i < 64 * 100; i++) { f[o++] = 0; f[o++] = 0; f[o++] = 255; }
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_OK);
    CHECK_INT(in.layout, OM_TGA_TALL); CHECK_INT(in.scale, 2);
    CHECK_INT(PX(0, 0, 128), 0x801F); CHECK_INT(PX(127, 199, 128), 0x801F);
    /* refusals */
    CHECK_INT(om_tga_decode(f, o - 1, cv, &in), OM_TGA_E_SHORT);
    o = hdr(10, 0, 0, 0, 2, 2, 24, 0x20);                     /* RLE truecolour */
    CHECK_INT(om_tga_decode(f, o + 64, cv, &in), OM_TGA_E_TYPE);
    o = hdr(2, 0, 0, 0, 200, 200, 24, 0x20);                  /* larger than the hero canvas */
    CHECK_INT(om_tga_decode(f, o + 120000, cv, &in), OM_TGA_E_SIZE);
    o = hdr(2, 0, 0, 0, 2, 2, 15, 0x20);
    CHECK_INT(om_tga_decode(f, o + 16, cv, &in), OM_TGA_E_BPP);
    o = hdr(2, 0, 0, 0, 2, 2, 24, 0x30);                      /* right-to-left origin */
    CHECK_INT(om_tga_decode(f, o + 12, cv, &in), OM_TGA_E_TYPE);
    CHECK_STR(om_tga_strerror(OM_TGA_E_TYPE), "TGA type not supported");

    /* --- hostile-input RED tests (lessons from earlier reviews) --- */

    /* a zero width (and, separately, zero height) must be refused, not divide-by-zero or wrap */
    o = hdr(2, 0, 0, 0, 0, 4, 24, 0x20);
    CHECK_INT(om_tga_decode(f, o + 48, cv, &in), OM_TGA_E_SIZE);
    o = hdr(2, 0, 0, 0, 4, 0, 24, 0x20);
    CHECK_INT(om_tga_decode(f, o + 48, cv, &in), OM_TGA_E_SIZE);

    /* a declared width*height*bpp that overflows a 32-bit product if computed naively: w=h=46341, bpp=16
     * gives w*h*2 = 4297064962 = 97666 (mod 2^32) -- a tiny wrapped "need" a short file would satisfy.
     * Both w and h are already far over the hero canvas (160x160 / 128x200), so this must come back
     * OM_TGA_E_SIZE, not a false OM_OK from a wrapped size check (and must not crash or read OOB; this
     * whole test file runs clean under ASan, see the Task 8 report). */
    o = hdr(2, 0, 0, 0, 46341, 46341, 16, 0x20);
    CHECK_INT(om_tga_decode(f, o + 64, cv, &in), OM_TGA_E_SIZE);

    /* image-ID length past EOF: idlen says 40 bytes of ID follow the 18-byte header, but the buffer handed
     * to the decoder ends 10 bytes into that span. */
    memset(f, 0, 18);
    f[0] = 40; f[2] = 2; f[12] = 2; f[14] = 2; f[16] = 24; f[17] = 0x20;
    CHECK_INT(om_tga_decode(f, 18 + 30, cv, &in), OM_TGA_E_SHORT);

    /* colour map past EOF: type 1, 8 bpp, a 100-entry 24-bit colour map (300 bytes) declared, but the
     * buffer stops 1 byte short of the full map -- must be rejected before any colour-map byte is read,
     * not just before the (nonexistent) pixel data. */
    o = hdr(1, 0, 100, 24, 1, 1, 8, 0x20);
    CHECK_INT(om_tga_decode(f, o + 299, cv, &in), OM_TGA_E_SHORT);

    /* an 8-bit image with an out-of-range palette index, isolated from the earlier colour-map test above:
     * a 1-entry colour map (cmfirst defaults to 0, cmlen=1, so index 0 is the only valid index) and a
     * pixel byte of 5. */
    o = hdr(1, 0, 1, 24, 1, 1, 8, 0x20);
    f[o++] = 0; f[o++] = 0; f[o++] = 0;                       /* the one colour-map entry */
    f[o++] = 5;                                               /* pixel index, out of range */
    CHECK_INT(om_tga_decode(f, o, cv, &in), OM_TGA_E_DATA);

    return th_done("test_tga");
}
