OUTPUT_FORMAT("elf32-sh")
/* FIX ROUND 1 (Ruling R10): entry is SGL's own SGL_Start (LIBSGL.A sglI00.o), which is the sole
 * contributor to SLSTART now that crt0.c's main() carries no section attribute -- matches SGL302
 * SAMPLE/COMMON/SL.LNK + S_2_2/MAKEFILE's "-e ___Start" exactly. */
ENTRY(___Start)
SECTIONS
{
    SLSTART 0x06004000 : { ___Start = .; KEEP(*(SLSTART)) }
    .text ALIGN(0x20) : { *(.text .text.*) *(.strings) __etext = .; }
    SLPROG ALIGN(0x20) : { __slprog_start = .; *(SLPROG) __slprog_end = .; }
    .tors ALIGN(0x10) : { ___ctors = .; *(.ctors) ___ctors_end = .; ___dtors = .; *(.dtors) ___dtors_end = .; }
    .rodata ALIGN(0x20) : { *(.rodata .rodata.*) }
    .data ALIGN(0x10) : { *(.data .data.*) __edata = .; }
    .bss ALIGN(0x10) (NOLOAD) : { __bstart = .; *(.bss .bss.*) *(COMMON) __bend = .; _end = .; }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
