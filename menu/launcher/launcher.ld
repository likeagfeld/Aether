OUTPUT_FORMAT("elf32-sh")
/* LAUNCHER.BIN body: runs at 0x00200000 in LWRAM (spec §7.1 launch step 2; docs/ledger.md "M2 memory plan").
 * The 4 KB OMSATLNC header is prepended by tools/sbuild.py and is not loaded. HWF-12, HWF-19. */
ENTRY(_lstart)
/* Two caps, deliberately different (M2 deferred (c)):
 *  - LENGTH 0x7B000 here: the linked image (.text..bss) ends by 0x0027B000, where the 16 KB stack
 *    (0x0027B000-0x0027EFFF; __stack_top 0x0027F000 = OM_LW_HANDOFF) begins, so nothing static overlaps it.
 *  - OM_LAUNCHER_MAX = lnchhdr.MAX_BODY = 0x7F000 (menu/src/omlayout.h, tools/omsat/lnchhdr.py): the FORMAT's and the
 *    menu's load-buffer cap, load address up to the hand-off block; loading that much never touches OM_LW_HANDOFF.
 * Every body this script links is <= 0x7B000 <= 0x7F000, so the header cap never binds first; the linker cap is the
 * one that protects the stack. tools/tests/test_ip_gate.py pins both values. */
MEMORY { lwram (rwx) : ORIGIN = 0x00200000, LENGTH = 0x0007B000 }
SECTIONS
{
    /* the files listed here, and in .lui below, are the screens' (.lui); uifont.c stays here: the step-9 page draws text
     * with it after .lui is gone (lorb.h) */
    .text 0x00200000 : { KEEP(*(.lstart)) *(EXCLUDE_FILE(*launcher_lgfx.o *launcher_lscreen.o *launcher_lcheat.o *launcher_chedit.o *launcher_lsaves.o *launcher_svmgr.o *launcher_lorbpage.o *src_biglogo.o *src_logoblend.o *src_uifont_data.o *src_loadbg.o *src_bootanim.o *src_marquee.o *src_cheatpak.o) .text EXCLUDE_FILE(*launcher_lgfx.o *launcher_lscreen.o *launcher_lcheat.o *launcher_chedit.o *launcher_lsaves.o *launcher_svmgr.o *launcher_lorbpage.o *src_biglogo.o *src_logoblend.o *src_uifont_data.o *src_loadbg.o *src_bootanim.o *src_marquee.o *src_cheatpak.o) .text.*) } > lwram
    .rodata ALIGN(4) : { *(EXCLUDE_FILE(*launcher_lgfx.o *launcher_lscreen.o *launcher_lcheat.o *launcher_chedit.o *launcher_lsaves.o *launcher_svmgr.o *launcher_lorbpage.o *src_biglogo.o *src_logoblend.o *src_uifont_data.o *src_loadbg.o *src_bootanim.o *src_marquee.o *src_cheatpak.o) .rodata EXCLUDE_FILE(*launcher_lgfx.o *launcher_lscreen.o *launcher_lcheat.o *launcher_chedit.o *launcher_lsaves.o *launcher_svmgr.o *launcher_lorbpage.o *src_biglogo.o *src_logoblend.o *src_uifont_data.o *src_loadbg.o *src_bootanim.o *src_marquee.o *src_cheatpak.o) .rodata.*) } > lwram
    .data ALIGN(4) : { *(.data .data.*) } > lwram
    .hop ALIGN(16) : { __hop_start = .; KEEP(*(.hop)) __hop_end = .; } > lwram
    /* menu/launcher/igr.h: the in-game reset hook and stub, stored in this image (LMA) right after
     * .hop and copied out at boot. .igrhook runs at OM_IGR_STAGE, .igrstub at OM_IGR_STUB; the stub image is staged
     * at ___igr_hook_end, so the two load images must be contiguous (asserted below). */
    .igrhook 0x06009800 : ALIGN(4) { ___igr_hook_start = .; KEEP(*(.igrhook)) . = ALIGN(4); ___igr_hook_end = .; } AT> lwram
    /* Astal (2026-11-23, igrguard.c): the stub image fills its whole home to 0x06002D48; its last long is the seal
     * word (0 here, set by om_igr_seal on the staged copy) that SYS_EXECDMP's guard checks. */
    .igrstub 0x06002800 : ALIGN(4) { ___igr_stub_start = .; KEEP(*(.igrstub)) . = ALIGN(4); ___igr_code_end = .;
                                     . = 0x544; LONG(0) ___igr_stub_end = .; } AT> lwram
    /* the guard, stored right after the stub so the JHL hook stages hook + stub + guard as one image; copied to
     * OM_IGR_GUARD (0x060003C0, the BIOS copyright text, HWF-80) by the JHL hook or by cwxboot.c */
    .igrguard 0x060003C0 : { ___igr_guard_start = .; KEEP(*(.igrguard)) . = ALIGN(4); ___igr_guard_end = .; } AT> lwram
    ___igr_lma = LOADADDR(.igrhook);
    ___igr_stub_lma = LOADADDR(.igrstub);
    ___igr_guard_lma = LOADADDR(.igrguard);
    ___igr_stub_longs = (___igr_stub_end - ___igr_stub_start) / 4;
    ___igr_guard_longs = (___igr_guard_end - ___igr_guard_start) / 4;
    /* menu/launcher/cwxtail.c, cwxplan.h OM_CWX_TAIL: the CWX boot's last code and its in-game-reset hook,
     * stored after .igrguard and copied to the top 4 KB of LWRAM by cwxboot.c; the IGR stub image is staged right after
     * it at ___cwx_img, so tail + image must end by 0x00300000 (asserted below). */
    .cwxtail 0x002FF000 : ALIGN(4) { ___cwx_tail_start = .; KEEP(*(.cwxtail)) . = ALIGN(4); ___cwx_tail_end = .; } AT> lwram
    ___cwx_tail_lma = LOADADDR(.cwxtail);
    ___cwx_img = ___cwx_tail_end;
    /* Kai's cheat block (chkai.h, at most OM_CHK_MAX = 400 B) is staged right after the IGR stub image */
    ___cwx_chimg = ___cwx_img + (___igr_stub_end - ___igr_stub_start);
    /* The screens (lgfx.h): code and fonts that run only while High Work RAM is the launcher's -- the editors, and a
     * launch until step 9 zeroes or loads over it. Linked at OM_LUI (0x06010000: clear of the BIOS work area, the IGR
     * guard, the cheat block, the stub and the hook's staging, all below 0x0600A000), stored after .cwxtail, copied
     * out by lmain before anything else; its load image then becomes the first bytes of the heap. */
    .lui 0x06010000 : ALIGN(4) { ___lui_start = .;
        KEEP(*(.lui .lui.*)) *(.text .text.* .rodata .rodata.*)
        . = ALIGN(4); ___lui_end = .; } AT> lwram
    ___lui_lma = LOADADDR(.lui);
    ___lui_longs = SIZEOF(.lui) / 4;
    /* .bss sits at the top, under the stack (__bss_base: tools/sbuild.py links twice, first to measure .bss), so the
     * heap runs from .lui's load image up to it */
    .bss __bss_base (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(COMMON) __bss_end = .; }
    __heap_start = ALIGN(LOADADDR(.lui), 16);
    __stack_top = 0x0027F000;
    __heap_end = __bss_base;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(_lstart == 0x00200000, "launcher entry must be the first byte of the LAUNCHER.BIN body")
ASSERT(__hop_start >= 0x00201000, ".hop must sit above the 4 KB that get_mpeg_rom(2,2,0x200000) overwrites (HWF-19)")
/* plus the BRAM buffers (bramtest.c: the 16 KB library, the 8 KB work area, 96 directory entries and
 * guards, 28,044 B, rounded up to 0x7000), malloc'd at step 1. The save swap's arena (pgsrun.h
 * OM_PGS_ARENA: those buffers, 512 directory entries, 256 folder entries and two 32 KB record buffers, 162,516 B on the
 * SH-2, rounded up to 0x28000), malloc'd after step 3; counted on top of cue2desc's 64 KB */
ASSERT(__ld_probe || __heap_end - __heap_start >= 0x10000 + 0x28000, "launcher heap below 64 KB + the save swap's arena")
ASSERT(__ld_probe || __bss_end <= __stack_top - 0x4000, "launcher .bss runs into the stack")
ASSERT(__ld_probe || LOADADDR(.lui) + SIZEOF(.lui) <= __bss_base, ".lui's load image overlaps .bss (zeroed before lmain copies it)")
ASSERT(___lui_end <= 0x06040000, ".lui past its home (lgfx.h OM_LUI_END)")
ASSERT(LOADADDR(.igrstub) == LOADADDR(.igrhook) + SIZEOF(.igrhook), "igr: hook and stub load images must be contiguous")
ASSERT(___igr_stub_end == 0x06002D48, "igr: the stub image must fill its home exactly (OM_IGR_STUB_END, the seal)")
ASSERT(___igr_code_end <= 0x06002D44, "igr: stub code runs into the seal word")
ASSERT(LOADADDR(.igrguard) == LOADADDR(.igrstub) + SIZEOF(.igrstub), "igr: stub and guard load images must be contiguous")
ASSERT(___igr_guard_start == 0x060003C0 && ___igr_guard_end <= 0x06000400, "igr: guard past the BIOS copyright text (0x060003C0-0x060003FF)")
ASSERT(___igr_hook_end + SIZEOF(.igrstub) + SIZEOF(.igrguard) <= 0x0600A000, "igr: staging past 0x0600A000 (cleared by the BIOS re-init)")
ASSERT(___igr_stub_longs > 0, "igr: empty stub")
ASSERT(___cwx_tail_start == 0x002FF000, "cwx: tail must start at OM_CWX_TAIL (cwxplan.h)")
ASSERT(___cwx_img + (___igr_stub_end - ___igr_stub_start) <= 0x00300000, "cwx: tail + staged IGR stub past LWRAM end")
ASSERT(___cwx_chimg + 400 <= 0x00300000, "cwx: tail + staged IGR stub + cheat block (OM_CHK_MAX) past LWRAM end")
ASSERT(LOADADDR(.cwxtail) + SIZEOF(.cwxtail) <= 0x0027B000, "cwx: tail load image overlaps the stack")
