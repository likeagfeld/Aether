/* L1 orbiting-logo capture (emulator tier): the launcher with OM_HO_IMPORT and no Satiator -- the import's pages with
 * made-up progress and counts, then a CWX launch that fails at step 9 after wiping High Work RAM as CWX does, for the
 * failure page drawn without .lui (lseq.c l1_import_demo, l1_fail9). */
#include <string.h>
#include "spk.h"
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"

extern Uint8 om_launcher_bin[], om_launcher_bin_end[];
extern Uint8 om_loadbg_bin[], om_loadbg_bin_end[];
extern Uint8 om_cover_bin[], om_cover_bin_end[];          /* build/<spike>_cover.raw when made, else empty */       /* EMBED_LOADBG: the eye background (no card here) */

#define LG_FILE 0x06040000u                              /* lgfx.h OM_LG_FILE */

/* a stand-in cover in VDP1 VRAM where the menu's would be (128 x 200 RGB555): a two-tone gradient with a frame */
static void cover(om_launch_opts *o)
{
    volatile Uint16 *v = (volatile Uint16 *)OM_CT(OM_VDP1_VRAM + OM_VRAM_HERO_A);
    Uint32 x, y;
    if (om_cover_bin_end - om_cover_bin == 128 * 200 * 2) {
        for (x = 0; x < 128u * 200u; x++) v[x] = (Uint16)((om_cover_bin[2 * x] << 8) | om_cover_bin[2 * x + 1]);
        o->art = OM_VRAM_HERO_A;
        o->art_w = 128;
        o->art_h = 200;
        return;
    }
    for (y = 0; y < 200; y++)
        for (x = 0; x < 128; x++) {
            Uint32 r = 6 + y / 12, g = 4 + y / 20, b = 10 + x / 10;
            if (x < 3 || y < 3 || x > 124 || y > 196) r = g = b = 28;
            v[y * 128 + x] = (Uint16)(0x8000u | (b << 10) | (g << 5) | r);
        }
    o->art = OM_VRAM_HERO_A;
    o->art_w = 128;
    o->art_h = 200;
}
void om_launch_jump(void);

void ss_main(void)
{
    om_lnch_info li;
    om_launch_opts o;
    Uint32 n = (Uint32)(om_launcher_bin_end - om_launcher_bin), i;
    volatile Uint8 *dst = (volatile Uint8 *)OM_CT(OM_LAUNCHER_LOAD);
    om_handoff *h = (om_handoff *)OM_CT(OM_LW_HANDOFF);
    spk_init("L1 ORBITING LOGO (emulator-tier)");
    slGetStatus();
    if (om_lnch_parse(om_launcher_bin, n, &li)) {
        spk_line(5, "LAUNCHER.BIN HEADER REJECTED");
        for (;;) slSynch();
    }
    for (i = OM_LNCH_HDR; i < n; i++)
        dst[i - OM_LNCH_HDR] = om_launcher_bin[i];
    memset(&o, 0, sizeof o);
    cover(&o);
    memmove((void *)LG_FILE, om_loadbg_bin, (Uint32)(om_loadbg_bin_end - om_loadbg_bin));   /* the harness's own copy */
    o.loader = OM_LDR_CWX;
    strcpy(o.gid, "MK-81009@U");
    om_handoff_fill2(h, "/x", "x/Panzer Dragoon (USA)", 4,
                     OM_HO_TEST_NO_SATIATOR | OM_HO_IMPORT | OM_HO_NO_IGR, &o);
    spk_line(5, "JUMPING (om_launch_jump)");
    om_launch_jump();
}
