/* FIX ROUND 1 (Controller Ruling R10, docs/ledger.md "Build findings" #5): the entry at 0x06004000 is
 * SGL's OWN SGL_Start (LIBSGL.A, sglI00.o, section SLSTART) -- not a function of ours. Per
 * SGL302 SAMPLE/COMMON/SL.LNK (ENTRY ___Start) and S_2_2/MAKEFILE (`-e ___Start`, links only cinit.o,
 * no user object contributes to SLSTART): SGL_Start sets r15 = MasterStack, SR, the cache control
 * register and GBR, then "jmp @main". main() here is that jump target -- it must NOT carry a section
 * attribute (nothing of ours belongs in SLSTART; SGL_Start must be the sole occupant so it lands at
 * 0x06004000). This mirrors SGL302 SAMPLE/COMMON/CINIT.C exactly: clear .bss, clear the SGL system
 * variables, call ss_main(). */
#include "sgl.h"

#define SYSTEM_WORK 0x060ffc00                 /* per SGL302 SAMPLE/COMMON/CINIT.C */
#define SYSTEM_SIZE (0x06100000 - 0x060ffc00)

extern Uint32 _bstart, _bend;
extern void ss_main(void);

void main(void)
{
    Uint8 *p;
    for (p = (Uint8 *)&_bstart; p < (Uint8 *)&_bend; p++)
        *p = 0;
    for (p = (Uint8 *)SYSTEM_WORK; p < (Uint8 *)(SYSTEM_WORK + SYSTEM_SIZE); p++)
        *p = 0;
    ss_main();
    for (;;)
        ;
}
