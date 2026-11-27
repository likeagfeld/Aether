/* menu/ui/vertag.c -- see vertag.h. BSD-3-Clause. */
#include <string.h>
#include "anim.h"
#include "omlayout.h"
#include "safearea.h"
#include "uifont.h"
#include "v1.h"
#include "version.h"
#include "vertag.h"

#define VT_SAFE OM_SAFE_BOOT               /* inside the 24 px safe area at every level (layout.h VERTAG) */
#define VT_LEVEL 102u                      /* about 40 % of the way from the background to white */
#define VT_CAP 48                          /* texture width cap: 48 x 13 4 bpp = 312 B (omlayout.h budget 512 B) */
static const uint16_t k_ramp[16] = { 0, 17, 34, 51, 68, 85, 102, 119, 137, 154, 171, 188, 205, 222, 239, 256 };
static uint16_t s_w8, s_w, s_h, s_asc;
static uint8_t s_ok, s_tex_sent;
static uint16_t s_lut_bg;
static uint32_t s_lut_a = 0xFFFFFFFFu;     /* no LUT sent yet */
static uint8_t s_ras[VT_CAP / 2 * 16] __attribute__((aligned(4)));
typedef char vt_fits[OM_VRAM_VERTAG_T + VT_CAP / 2 * 16 <= OM_VRAM_VERTAG_END ? 1 : -1];

void om_vertag_init(void)
{
    const om_face *f = &om_face_label8;
    int ink, h;
    if (s_ok) return;
    ink = om_uf_width_tracked(f, OM_VERSION_TAG, 0);
    h = om_uf_height(f);
    if (h > 16) h = 16;
    s_w8 = (uint16_t)((ink + 7) & ~7);
    if (s_w8 > VT_CAP) s_w8 = VT_CAP;
    memset(s_ras, 0, sizeof s_ras);
    s_w = (uint16_t)om_uf_draw_tracked(f, OM_VERSION_TAG, s_ras, s_w8, s_w8, h, 0, f->asc, 0);
    s_h = (uint16_t)h;
    s_asc = f->asc;
    s_ok = 1;
}

void om_vertag_draw(uint16_t bg, uint32_t a256, int z)
{
    int i, x, base;
    if (!s_ok || !a256) return;
    if (a256 > 256u) a256 = 256u;
    /* review-final-m3 M10: each is sent only when it changes (the texture once, the LUT when the backdrop or the fade
     * level moves), so VDP1 never reads a slot that is being rewritten under a list it is still drawing. Nothing else
     * writes 0x35420-0x3563F (review-final-m3 "VDP1": between the boot-only range and the header LOGO, which starts there), so neither goes stale. */
    if (!s_tex_sent) {
        om_v1_write_bytes(OM_VRAM_VERTAG_T, s_ras, (uint32_t)(s_w8 / 2 * s_h));
        s_tex_sent = 1;
    }
    if (bg != s_lut_bg || a256 != s_lut_a) {
        uint16_t lut[16];
        lut[0] = 0;
        for (i = 1; i < 16; i++)
            lut[i] = (uint16_t)(om_c555_mix(bg, 0x7FFF, (((k_ramp[i] * VT_LEVEL) >> 8) * a256) >> 8) | 0x8000u);
        om_v1_write(OM_VRAM_VERTAG_L, lut, 16);
        s_lut_bg = bg;
        s_lut_a = a256;
    }
    x = 320 - VT_SAFE - (int)s_w;                         /* right-aligned on the pen's end */
    base = 224 - VT_SAFE - (int)om_face_label8.desc;      /* descenders stay inside the safe area */
    om_v1_sprite(x, base - (int)s_asc, OM_VRAM_VERTAG_T, s_w8, s_h, OM_VRAM_VERTAG_L, 0, z);
}
