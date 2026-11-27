/* L1 save manager capture (emulator tier): the launcher with OM_HO_SAVEMGR and no Satiator -- its demo saves
 * (Launch Options > Manage saves for one game), made up in memory, and a key script through its screens. */
#include <string.h>
#include "spk.h"
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"
#define ROW 4u                                       /* overlay.h OM_OGR_SAVEMGR */

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

/* 2026-11-25, the tool's way back (lmain.c om_go_home): B ends the launcher's demo, which arms the return block this
 * harness wrote as the menu does (unarmed, the panel on its row) and boots the disc in the drive -- this harness again.
 * Found armed: the round trip is shown, and the harness stops there (no second hand-off). */
static om_retblk s_raw;                                  /* the block as found, before SGL starts */
static int returned(void)
{
    static om_retblk r;
    char line[40];
    strcpy(line, "RETURN BLOCK: ");                      /* none (the first start), unarmed or bad: say which */
    strcat(line, !memcmp(s_raw.magic, "OMSATRET", 8) ? (om_ret_ok(&s_raw) ? (s_raw.armed ? "ARMED" : "UNARMED")
                                                                          : "BAD CRC") : "NONE");
    spk_line(3, line);
    if (!om_ret_take(&s_raw, &r)) return 0;
    spk_line(5, "BACK FROM THE TOOL (NO CONSOLE RESET)");
    spk_line(7, "RETURN BLOCK: ARMED, CRC OK");
    strcpy(line, "PANEL ROW ");
    line[10] = (char)('0' + r.ov_row % 10u);
    line[11] = 0;
    strcat(line, r.ov_kind == 3 ? " (LAUNCH OPTIONS)" : "");
    spk_line(8, line);
    strcpy(line, "GAME KEY ");
    strcat(line, r.key);
    spk_line(9, line);
    return 1;
}
static void ret_write(void)
{   /* as the menu's folders.c ret_write: Favorites, its first game, Launch Options on the row */
    om_retblk *r = (om_retblk *)OM_CT(OM_LW_RETURN);
    memset(r, 0, sizeof *r);
    r->ov_kind = 3;                                      /* overlay.h OM_OV_GAME */
    r->ov_row = ROW;
    r->depth = 1;
    r->loc[1].kind = 1;                                  /* browse.h OM_LOC_VROOT, Favorites */
    r->entry = 0;
    strcpy(r->key, "MK-81045");
    om_ret_seal(r);
}

void ss_main(void)
{
    om_lnch_info li;
    om_launch_opts o;
    Uint32 n = (Uint32)(om_launcher_bin_end - om_launcher_bin), i;
    volatile Uint8 *dst = (volatile Uint8 *)OM_CT(OM_LAUNCHER_LOAD);
    om_handoff *h = (om_handoff *)OM_CT(OM_LW_HANDOFF);
    om_ret_copy16(&s_raw, (const volatile void *)OM_SND_RETURN);   /* as the menu's main.c: copied out, cleared */
    om_ret_copy16((volatile void *)OM_SND_RETURN, 0);
    spk_init("L1 SAVE MANAGER (emulator-tier)");
    slGetStatus();
    if (returned())
        for (;;) slSynch();
    ret_write();
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
                     OM_HO_TEST_NO_SATIATOR | OM_HO_SAVEMGR | OM_HO_NO_IGR, &o);
    spk_line(5, "JUMPING (om_launch_jump)");
    om_launch_jump();
}
