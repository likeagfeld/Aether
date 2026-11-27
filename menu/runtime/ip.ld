OUTPUT_FORMAT("elf32-sh")
/* DEVIATION (docs/ledger.md "Build findings"): the brief omits SEGA's AIP (smpsys.obj) and relies on
 * set_ip_size's zero padding to fill the rest of the IP. Measured this session: SYS_INIT.O's START routine
 * ends with "bra ENDOFPG" to the very last byte of its own .text (objdump -dr), i.e. control falls through
 * into whatever is linked immediately after SYS_INIT.O. ST-040-R4 §4.6 says the Application Initial Program
 * "is executed immediately after the area code is executed by placing it after the area code group" -- that
 * fall-through IS the AIP call, not an optional extra. Falling into zero padding would NOT jump to the 1st
 * Read Address (SGL_Start, 0x06004000); it would execute whatever SH-2 opcode 0x0000 decodes to. So this
 * adds our own minimal AIP: a 0x20-byte (32-byte), nop-padded SH-2 stub (sbuild.py build_ip(), emitted the same way as sysid.bin,
 * via objcopy -I binary) that does "mov.l 0x06004000,r0 ; jmp @r0 ; nop", placed immediately after
 * SYS_INIT.O with no gap -- own code, not a Jo or third-party file, per the task's fix rules. */
SECTIONS
{
    IP 0x06002000 :
    {
        KEEP(*sysid.o(*))
        KEEP(*SYS_SEC.O(*))
        KEEP(*SYS_AREJ.O(*))
        KEEP(*SYS_ARET.O(*))
        KEEP(*SYS_AREU.O(*))
        KEEP(*SYS_AREE.O(*))
        KEEP(*SYS_INIT.O(*))
        KEEP(*aip.o(*))
    }
}
