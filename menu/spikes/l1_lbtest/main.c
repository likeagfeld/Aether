/* L1: emulator-tier harness for LAUNCHER.BIN (no Satiator needed). Embeds menu/launcher/out/LAUNCHER.BIN, checks its
 * header, writes a hand-off block with a test flag, copies the body to 0x00200000 and jumps -- the same steps as
 * menu/app/launch.c + launchj.c. A: error-screen test. B: self-boot test. A boot counter at
 * OM_LW_HANDOFF + 0x800 (outside the 536-byte hand-off block) survives a successful self-boot.
 * emulator tier: with no button for 5 s the harness runs B by itself (the launcher arms the in-game
 * reset by default). Booted that way, it finds SYS_EXECDMP (0x0600026C) pointing into the IGR stub, shows it, and
 * after 3 s calls SYS_EXECDMP the way a game's A+B+C+Start exit does: the stub's Satiator commands time out (no
 * Satiator on an emulator) and SYSRES restarts the console, where the BIOS CD player would appear without it.
 * the hand-off block is version 2 and names the loader. No button for 5 s now runs the CWX self-boot
 * (C does too; B stays the JHL self-boot): the launcher reads this disc's IP and 1st Read File itself and enters
 * the IP, no bios_loadcd_boot. The witness of which loader booted the harness is 0x06000290: Kai's ipprog (and so
 * our CWX tail) zeroes it, the BIOS boot leaves its own value there (SAROO's emulation sets 3: m6-research.md §2.2
 * row 25) -- shown on line 13.
 * like the menu, the harness turns on SGL's persistent INTBACK at boot (slGetStatus) and jumps through the
 * menu's own om_launch_jump (menu/app/launchj.c: the slave SH-2 stopped first, HWF-78) instead of a copy of it.
 * the first no-button boot since power-on also sets OM_HO_TEST_BRAM: the launcher's step 1 lists the
 * emulator's backup RAM and runs a write / verify / read / delete round trip of one test record (bramtest.c), then
 * the CWX self-boot goes on. Run it only with a scratch Mednafen save directory (filesys.path_sav). */
#include <string.h>
#include "spk.h"
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"
#include "l1gate.h"

extern Uint8 om_launcher_bin[], om_launcher_bin_end[];
extern Uint8 om_loadbg_bin[], om_loadbg_bin_end[];       /* EMBED_LOADBG: the launch's screens (no card here) */
extern Uint8 om_cover_bin[], om_cover_bin_end[];         /* build/l1_lbtest_cover.raw when made, else a placeholder */
void om_launch_jump(void);                           /* menu/app/launchj.c, linked by sbuild for EMBED_LAUNCHER */
#define COUNTER ((volatile Uint32 *)OM_CT(OM_LW_HANDOFF + 0x800))

static void go(Uint32 flags, Uint8 loader)
{
    om_launch_opts o;
    Uint32 n = (Uint32)(om_launcher_bin_end - om_launcher_bin), i;
    volatile Uint8 *dst = (volatile Uint8 *)OM_CT(OM_LAUNCHER_LOAD);
    om_handoff *h = (om_handoff *)OM_CT(OM_LW_HANDOFF);
    for (i = OM_LNCH_HDR; i < n; i++)
        dst[i - OM_LNCH_HDR] = om_launcher_bin[i];
    memset(&o, 0, sizeof o);
    if (om_cover_bin_end - om_cover_bin == 128 * 200 * 2) {   /* the cover the launch's page shows (VDP1 VRAM) */
        volatile Uint16 *v = (volatile Uint16 *)OM_CT(OM_VDP1_VRAM + OM_VRAM_HERO_A);
        for (i = 0; i < 128u * 200u; i++) v[i] = (Uint16)((om_cover_bin[2 * i] << 8) | om_cover_bin[2 * i + 1]);
        o.art = OM_VRAM_HERO_A;
        o.art_w = 128;
        o.art_h = 200;
    }
    memmove((void *)0x06040000u, om_loadbg_bin, (Uint32)(om_loadbg_bin_end - om_loadbg_bin));   /* lgfx.h OM_LG_FILE */
    o.loader = loader;
    o.igr = 1;                                       /* the launcher arms the in-game reset by default */
    om_handoff_fill2(h, "/", "Panzer Dragoon (USA).cue", 4, flags, &o);   /* the test boots the drive's disc */
    COUNTER[1] = COUNTER[1] + 1;                     /* survives only if the self-boot leaves LWRAM alone */
    om_launch_jump();                                /* the menu's own jump (slave SH-2 off), not a copy */
}

#define EXECDMP (*(volatile Uint32 *)0x0600026Cu)
#define IPVAR1  (*(volatile Uint32 *)0x06000290u)   /* zeroed by the CWX tail (cwxtail.c step 4) */

void ss_main(void)
{
    char b[12];
    om_lnch_info li;
    Uint32 n = (Uint32)(om_launcher_bin_end - om_launcher_bin), f;
    Uint32 dmp = EXECDMP, var1 = IPVAR1;
    int rc;
    spk_init("L1 LAUNCHER HARNESS (emulator-tier)");
    slGetStatus();                                   /* the menu's persistent INTBACK (main.c, HWF-40) */
    spk_line(9, "SYS_EXECDMP (0x0600026C):");
    spk_line(10, spk_u32(b, dmp));
    spk_line(13, "0x06000290 AT ENTRY (CWX: 0):");
    spk_line(14, spk_u32(b, var1));
    /* the CWX self-boot armed Kai's cheat path with one code (lseq.c): C0DE = 49374 here means the master's
     * trapa reached the bootstrap and the handler ran under this SGL program's interrupts */
    spk_line(15, "CHEAT WORD 0x060E0000 (C0DE = 49374):");
    spk_line(16, spk_u32(b, *(volatile Uint16 *)OM_L1_CHEAT_WORD));
    if (dmp >= 0x06002800u && dmp < 0x06002D48u) {   /* menu/launcher/igr.h OM_IGR_STUB .. OM_IGR_STUB_END */
        spk_line(11, "IGR ARMED: calling SYS_EXECDMP in 3 s");
        for (f = 0; f < 180; f++) slSynch();
        spk_line(12, "SYS_EXECDMP now (expect a console reset)");   /* line 13-14: the loader witness, above */
        slSynch();
        ((void (*)(void))dmp)();
        spk_line(13, "SYS_EXECDMP RETURNED");
        for (;;) slSynch();
    }
    spk_line(11, "IGR not armed (BIOS value)");
    if (COUNTER[0] != 0x4C425453u) {                 /* "LBTS": first boot since power-on */
        COUNTER[0] = 0x4C425453u;
        COUNTER[1] = 0;
    }
    spk_line(3, "self-boot count:");
    spk_line(4, spk_u32(b, COUNTER[1]));
    rc = om_lnch_parse(om_launcher_bin, n, &li);
    if (rc) {
        spk_line(6, "LAUNCHER.BIN HEADER REJECTED rc (negated):");
        spk_line(7, spk_u32(b, (Uint32)-rc));
        for (;;) slSynch();
    }
    spk_line(6, "A: error screen  B: JHL boot  C: CWX boot");
    spk_line(7, "(no button for 5 s: C, the CWX self-boot)");
    for (f = 0; ; f++) {
        Uint16 p;
        slSynch();
        p = spk_pressed();
        if (p & PER_DGT_TA) go(OM_HO_TEST_ERRSCREEN, OM_LDR_JHL);
        if (p & PER_DGT_TB) go(OM_HO_TEST_NO_SATIATOR, OM_LDR_JHL);
        if (p & PER_DGT_TC) go(OM_HO_TEST_NO_SATIATOR, OM_LDR_CWX);
        if (f == 300)                                /* the first auto boot also runs the BRAM test */
            go(OM_HO_TEST_NO_SATIATOR | (COUNTER[1] ? 0u : OM_HO_TEST_BRAM), OM_LDR_CWX);
    }
}
