/* menu/launcher/vdp2txt.c -- see vdp2txt.h. Every register and value is a row of docs/decisions/m2-hw-facts.md
 * "HWF-09 register table" (per ST-058-R2); a CORRECTED value there overrides the value here. Layout: pattern-name map
 * at VDP2 VRAM 0x00000 (1-word, 64x64 page), 4-bpp cells at 0x04000 (char no. = addr/0x20), back colour word at
 * 0x7FFFE. Font: font8x8_basic (public domain; bit0 = leftmost pixel). BSD-3-Clause. */
#include <stdint.h>
#include "font8x8_basic.h"
#include "vdp2txt.h"

#define REG(o)    (*(volatile uint16_t *)(0x25F80000u + (o)))     /* HWF-09 */
#define VRAM16    ((volatile uint16_t *)0x25E00000u)
#define CRAM16    ((volatile uint16_t *)0x25F00000u)
#define MAP_OFF   0x00000u
#define CELL_OFF  0x04000u
#define CHAR_BASE (CELL_OFF / 0x20u)

int om_txt_ready;      /* om_txt_init has run: before it, VDP2 is still the menu's (or lgfx.h's) */

void om_txt_display(int on) { REG(0x000) = on ? 0x8000 : 0x0000; }   /* TVMD DISP, 320x224 (HWF-09) */

void om_txt_clear(void)
{
    uint32_t i;
    for (i = 0; i < 64u * 64u; i++)
        VRAM16[MAP_OFF / 2 + i] = (uint16_t)(CHAR_BASE + ' ');
}

void om_txt_init(void)
{
    uint32_t i, g, y, x;
    om_txt_ready = 1;
    om_txt_display(0);
    for (i = 0x00E; i <= 0x11E; i += 2)                         /* clear RAMCTL..the last register (HWF-09) */
        REG(i) = 0;
    REG(0x010) = 0x04FF;                                        /* CYCA0L: T0 NBG0 PN, T1 NBG0 CG */
    REG(0x012) = 0xFFFF;
    for (i = 0x014; i <= 0x01E; i += 2)
        REG(i) = 0xFFFF;                                        /* CYCA1, CYCB0, CYCB1: no access */
    REG(0x028) = 0x0000;                                        /* CHCTLA: NBG0 16 colours, cell, 1x1 */
    REG(0x030) = 0xC000;                                        /* PNCN0: 1-word, 12-bit character number */
    REG(0x078) = 1;                                             /* ZMXIN0 = 1.0 */
    REG(0x07C) = 1;                                             /* ZMYIN0 = 1.0 */
    REG(0x0AC) = (uint16_t)((OM_TXT_BACK_OFF / 2u) >> 16);      /* BKTAU */
    REG(0x0AE) = (uint16_t)((OM_TXT_BACK_OFF / 2u) & 0xFFFFu);  /* BKTAL */
    VRAM16[OM_TXT_BACK_OFF / 2] = 0x0000;                       /* back colour: black */
    REG(0x0F8) = 0x0007;                                        /* PRINA: NBG0 priority 7; PRISA-D stay 0 (sprites off) */
    CRAM16[0] = 0x0000; CRAM16[1] = 0x7FFF;                     /* palette 0: white */
    CRAM16[16] = 0x0000; CRAM16[17] = 0x03E0;                   /* palette 1: green */
    CRAM16[32] = 0x0000; CRAM16[33] = 0x001F;                   /* palette 2: red */
    for (g = 0; g < 128; g++)
        for (y = 0; y < 8; y++) {
            uint8_t row = (uint8_t)font8x8_basic[g][y];
            uint16_t w0 = 0, w1 = 0;
            for (x = 0; x < 4; x++) {                           /* 4 bpp: high nibble = left pixel (HWF-05) */
                w0 |= (uint16_t)(((row >> x) & 1u) << (12 - 4 * x));
                w1 |= (uint16_t)(((row >> (x + 4)) & 1u) << (12 - 4 * x));
            }
            VRAM16[(CELL_OFF + g * 32u + y * 4u) / 2u] = w0;
            VRAM16[(CELL_OFF + g * 32u + y * 4u) / 2u + 1u] = w1;
        }
    om_txt_clear();
    REG(0x020) = 0x0001;                                        /* BGON: NBG0 on */
    om_txt_wait_vblank();
    om_txt_display(1);
}

void om_txt_puts(int x, int y, const char *s, int pal)
{
    if (!om_txt_ready) return;
    for (; *s && x < 40; s++, x++)
        if (x >= 0 && y >= 0 && y < 28)
            VRAM16[MAP_OFF / 2 + (uint32_t)y * 64u + (uint32_t)x] = (uint16_t)(((uint32_t)pal << 12) | (CHAR_BASE + ((uint8_t)*s & 0x7Fu)));
}

void om_txt_putn(int x, int y, int v, int pal)
{
    char t[12], o[13];
    int n = 0, i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) o[i++] = '-';
    while (n) o[i++] = t[--n];
    o[i] = 0;
    om_txt_puts(x, y, o, pal);
}
