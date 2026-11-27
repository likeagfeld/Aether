OUTPUT_FORMAT("elf32-sh")
/* POPUP.BIN (menu/app/popabi.h): the in-menu popups' code, run in LWRAM at OM_LW_LBG (omlayout.h: the stage's top
 * 92,160 B, the LOADBG preload's while no popup is open). Linked by tools/sbuild.py after menu.elf, against its symbols
 * (--just-symbols); the file is .pophdr + .text + .rodata + .data, the header first; .bss follows and is zeroed by the
 * menu (popup.c) before each open. */
ENTRY(_om_pop_header)
SECTIONS
{
    .text 0x00268800 : { KEEP(*(.pophdr)) *(.text .text.*) }
    .rodata ALIGN(4) : { *(.rodata .rodata.*) }
    .data ALIGN(4) : { *(.data .data.*) . = ALIGN(4); ___pop_img_end = .; }
    .bss ALIGN(4) (NOLOAD) : { ___pop_bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); ___pop_bss_end = .; }
    ___pop_img_len = ___pop_img_end - 0x00268800;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(___pop_bss_end <= 0x0027F000, "POPUP.BIN (with .bss) runs past OM_LW_LBG + OM_LW_LBG_MAX (omlayout.h)")
