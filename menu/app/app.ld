OUTPUT_FORMAT("elf32-sh")
/* M2 menu. Same layout and entry as menu/spikes/common/spike.ld (SGL302 SAMPLE/COMMON/SL.LNK; Ruling R10:
 * SGL_Start is the sole SLSTART occupant at 0x06004000), plus a hard end-of-image check. HWF-12, HWF-20 */
ENTRY(___Start)
/* the link runs with --gc-sections (tools/sbuild.py). KEEP() protects SGL's entry (SLSTART), SLPROG,
 * the ctor/dtor tables and every section of the three SEGA archives (built without per-function sections; parts of
 * them are reached only through SGL's own tables), so only our -ffunction-sections objects and libc are collected. */
SECTIONS
{
    SLSTART 0x06004000 : { ___Start = .; KEEP(*(SLSTART)) }
    .text ALIGN(0x20) : { KEEP(*SEGA_SYS.A:*(.text)) KEEP(*LIBCD.A:*(.text)) KEEP(*LIBSGL.A:*(.text))
                          *(.text .text.*) KEEP(*(.strings)) __etext = .; }
    SLPROG ALIGN(0x20) : { __slprog_start = .; KEEP(*(SLPROG)) __slprog_end = .; }
    .tors ALIGN(0x10) : { ___ctors = .; KEEP(*(.ctors)) ___ctors_end = .; ___dtors = .; KEEP(*(.dtors)) ___dtors_end = .; }
    .rodata ALIGN(0x20) : { KEEP(*SEGA_SYS.A:*(.rodata)) KEEP(*LIBCD.A:*(.rodata)) KEEP(*LIBSGL.A:*(.rodata))
                            *(.rodata .rodata.*) }
    .data ALIGN(0x10) : { KEEP(*SEGA_SYS.A:*(.data)) KEEP(*LIBCD.A:*(.data)) KEEP(*LIBSGL.A:*(.data))
                          *(.data .data.*) __edata = .; }
    .bss ALIGN(0x10) (NOLOAD) : { __bstart = .; KEEP(*SEGA_SYS.A:*(.bss)) KEEP(*LIBCD.A:*(.bss)) KEEP(*LIBSGL.A:*(.bss))
                                  *(.bss .bss.*) *(COMMON) __bend = .; _end = .; }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(_end <= 0x060F0000, "menu image + bss overlaps the SGL work area at 0x060F0000 (docs/ledger.md, HWF-12)")
