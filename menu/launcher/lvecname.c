/* menu/launcher/lvecname.c -- names for the SH-2 exception vectors, per SH7604_Hardware.txt Table 4.3
 * (lines 3841-3925). Pure C so the host suite can check it (menu/host-tests/test_lvec.c). BSD-3-Clause. */
#include "lvec.h"

const char *om_exc_name(uint32_t v)
{
    if (v <= 1) return "power-on reset";          /* 0 PC, 1 SP */
    if (v <= 3) return "manual reset";            /* 2 PC, 3 SP */
    switch (v) {
    case 4:  return "general illegal instr";
    case 6:  return "slot illegal instr";
    case 9:  return "CPU address error";
    case 10: return "DMA address error";
    case 11: return "NMI";
    case 12: return "user break";
    default: break;
    }
    if (v >= 32 && v <= 63) return "TRAPA";       /* trap instruction (user vector) */
    if (v >= 64 && v <= 127) return "interrupt";  /* IRL1-15 autovectors 64-71; on-chip modules set theirs (VCRx) */
    return "reserved";                            /* 5, 7, 8, 13-31, and anything above 127 */
}

void om_hex8(char out[9], uint32_t v)
{
    int i;
    for (i = 7; i >= 0; i--, v >>= 4)
        out[i] = "0123456789ABCDEF"[v & 0xFu];
    out[8] = 0;
}
