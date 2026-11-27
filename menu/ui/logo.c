/* menu/ui/logo.c -- see logo.h. 40x25 RGB555 VDP1 sprite (menu/app/assets/logo.bin, tools/omsat/gen_logo.py; SEGA
 * artwork, credited in CREDITS.md) at OM_VRAM_LOGO. Drawn as a raw normal-sprite command (menu/ui/v1.c) at the
 * mockup's header position (10,8), RGB mode with code 0x0000 transparent (SPdis clear), ECdis (HWF-01, HWF-03).
 * logo.bin v2 carries 4-bit alpha after the words; the upload blends edge pixels with the NBG1 backdrop
 * behind the header logo (menu/src/logoblend.c), so it is redone when the backdrop changes (om_logo_rebind).
 * the loading screen's footer has its own 22x15 logo (footlogo.bin, 24 wide), blended by the generator for
 * that screen's flat dark scrim, at OM_VRAM_FOOTLOGO; uploaded once and drawn 1:1 (no scaled header texture).
 * drawn at the LOGO box through the safe-area transform the menu sets (a scaled sprite away from Standard),
 * and blended with the backdrop where each pixel lands on screen (g_om_safe), so the two always agree; a re-blend
 * waits for VDP1 to finish its list first (EDSR CEF). BSD-3-Clause. */
#include "sgl.h"
#undef pal                     /* SGL.H's old-texture macro: om_logo_img has a pal field (logoblend.h) */
#include "om_fmt.h"
#include "omlayout.h"
#include "layout.h"
#include "logo.h"
#include "logoblend.h"
#include "safearea.h"
#include "v1.h"
#include "vdp2theme.h"

extern Uint8 om_logo[], om_logo_end[];                         /* sbuild embed_blob */
extern Uint8 om_footlogo[], om_footlogo_end[];                 /* already blended for the loading screen */
#define LOGO_WORDS (OM_LOGO_W * OM_LOGO_H * 2u)                /* 2,000 B of RGB555, then 500 B of alpha nibbles */
#define LOGO_V2    (LOGO_WORDS + OM_LOGO_W * OM_LOGO_H / 2u)

static int s_ok;

static uint16_t bg_at(void *ctx, int x, int y)
{   /* (x, y) is a design pixel of the LOGO box: the backdrop where the menu's transform puts it (vdp2theme's own
     * transform is not set outside a view's draw group, so the point is mapped here) */
    (void)ctx;
    return om_v2_bg_at(om_safe_xu(&g_om_safe, x), om_safe_yu(&g_om_safe, y));
}

/* EDSR (VDP1 0x100010) bit 1 = CEF: "the end bit in current frame has been fetched and plotting is ended"; while it is
 * set, "VRAM can be accessed without the overhead for stopping drawing" (VDP1_Manual.txt:2323-2352). It is cleared at
 * the next frame change, so a texture written now is not read by the list VDP1 is drawing. Whether SGL starts a new
 * draw before the next slSynch is not verified here (hardware check line, docs/m3-hardware-check.md M6 section). */
#define EDSR (*(volatile Uint16 *)0x25D00010)
#define CEF_WAIT_POLLS 2000000u    /* a backstop only, should V-blanks not be counted: the bound is two V-blanks */

static void wait_vdp1_idle(void)
{
    uint32_t n, v0 = om_v2_vblanks();
    for (n = 0; n < CEF_WAIT_POLLS && !(EDSR & 2u) && om_v2_vblanks() - v0 < 2u; n++)
        ;
}

static void upload(om_backdrop_fn bg)
{   /* byte-wise reads (om_be16): objcopy -I binary gives no alignment; VDP1 VRAM writes are 16-bit (HWF-23) */
    om_logo_blend_direct(om_logo, om_logo + LOGO_WORDS, (int)OM_LOGO_W, (int)OM_LOGO_H, OM_BX(LOGO)->x0,
                         OM_BX(LOGO)->y0, bg, 0, (volatile uint16_t *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_LOGO));
}

int om_logo_init(void)
{
    volatile Uint16 *f = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_FOOTLOGO);
    uint32_t i;
    if ((uint32_t)(om_logo_end - om_logo) != LOGO_V2 ||
        (uint32_t)(om_footlogo_end - om_footlogo) != OM_FOOTLOGO_W * OM_FOOTLOGO_H * 2u)
        return OM_E_FIELD;
    for (i = 0; i < OM_FOOTLOGO_W * OM_FOOTLOGO_H; i++)       /* byte-wise reads, 16-bit writes (HWF-23) */
        f[i] = om_be16(om_footlogo + 2u * i);
    s_ok = 1;
    upload(0);                                                 /* before om_v2_init: no backdrop yet (black) */
    return OM_OK;
}

void om_logo_rebind(void)
{   /* a theme or Safe area change, with the logo on screen (no write while VDP1 may be drawing it) */
    if (s_ok) {
        wait_vdp1_idle();
        upload(bg_at);
    }
}

void om_logo_draw(void)
{   /* at the LOGO box (the upload's blend position), not a fixed (10,8) */
    om_v1_sprite(OM_BX(LOGO)->x0, OM_BX(LOGO)->y0, OM_VRAM_LOGO, OM_LOGO_W, OM_LOGO_H, 0, 0, OM_Z_HEADER);
}

void om_logo_draw_footer(int x, int y, int z)
{
    om_v1_sprite(x, y, OM_VRAM_FOOTLOGO, OM_FOOTLOGO_W, OM_FOOTLOGO_H, 0, 0, z);
}
