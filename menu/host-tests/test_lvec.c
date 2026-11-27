/* menu/host-tests/test_lvec.c -- the launcher's exception vector names and table geometry.
 * Expected values are transcribed independently from SH7604_Hardware.txt Table 4.3 (lines 3841-3925): vector n
 * sits at offset 4n; 0/1 power-on reset PC/SP, 2/3 manual reset PC/SP, 4 general illegal instruction, 5 reserved,
 * 6 slot illegal instruction, 7-8 reserved, 9 CPU address error, 10 DMA address error, 11 NMI, 12 user break,
 * 13-31 reserved, 32-63 trap (user vector), 64-127 interrupts (IRL autovectors 64-71; on-chip modules by VCRx).
 * The linked table itself is checked by tools/sbuild.py check_vectab (build-verified). */
#include "th.h"
#include "lvec.h"

static const char *expect(unsigned v)
{
    if (v < 2) return "power-on reset";
    if (v < 4) return "manual reset";
    if (v == 4) return "general illegal instr";
    if (v == 6) return "slot illegal instr";
    if (v == 9) return "CPU address error";
    if (v == 10) return "DMA address error";
    if (v == 11) return "NMI";
    if (v == 12) return "user break";
    if (v >= 32 && v < 64) return "TRAPA";
    if (v >= 64 && v < 128) return "interrupt";
    return "reserved";
}

int main(void)
{
    unsigned v, reserved = 0;
    char h[9];
    for (v = 0; v < 130; v++) {
        CHECK_STR(om_exc_name(v), expect(v));
        /* the EXC line "EXC: <name> PC=XXXXXXXX" must fit the 40-column text screen (vdp2txt) */
        CHECK(5 + strlen(om_exc_name(v)) + 4 + 8 <= 40);
        if (v < 128 && !strcmp(om_exc_name(v), "reserved")) reserved++;
    }
    CHECK_INT(reserved, 3 + 19);                              /* 5, 7, 8 and 13..31 */
    CHECK_INT(OM_VEC_N, 128);                                 /* vectors 0..127 */
    CHECK_INT(4u * (OM_VEC_N - 1), 0x1FC);                    /* last vector's table offset, Table 4.3 */
    CHECK_INT(OM_VEC_STUB_BYTES, 8);                          /* four 16-bit instructions per stub */
    CHECK(OM_VEC_STUB_BYTES * OM_VEC_N <= 4094);              /* a bra (12-bit disp x 2) reaches the common code */
    om_hex8(h, 0x0600ABCDu);
    CHECK_STR(h, "0600ABCD");
    om_hex8(h, 0);
    CHECK_STR(h, "00000000");
    om_hex8(h, 0xFFFFFFFFu);
    CHECK_STR(h, "FFFFFFFF");
    return th_done("test_lvec");
}
