/* menu/launcher/lvec.h -- the launcher's own SH-2 exception vector table. The launcher
 * runs after the menu, whose vectors (behind the VBR the BIOS left, which the menu never changes) point into High
 * WRAM that the launch overwrites. So VBR points here while the launcher owns the machine, and every vector lands
 * in a stub that shows "EXC: <name> PC=<hex>" and then the error-screen exits (HWF-25). Layout: per SH7604_Hardware.txt
 * Table 4.3 (lines 3841-3925; vector n at VBR + 4n, 0..127) and Table 4.4 (lines 3939-3947). BSD-3-Clause. */
#ifndef OM_LVEC_H
#define OM_LVEC_H
#include <stdint.h>
#define OM_VEC_N          128u   /* vectors 0..127, SH7604 Table 4.3 */
#define OM_VEC_STUB_BYTES 8u     /* mov.l r0,@-r15 / mov #n,r0 / bra common / nop */

const char *om_exc_name(uint32_t vec);          /* pure; host-tested (test_lvec.c) */
void om_hex8(char out[9], uint32_t v);          /* pure; 8 upper-case hex digits + NUL */

#ifndef OM_HOST_TEST
extern const uint32_t om_vectab[OM_VEC_N];
void om_vec_install(void);                      /* first call saves the entry VBR; every call sets VBR = om_vectab */
void om_vec_to_entry(void);                     /* VBR = the saved entry VBR (the BIOS's; see lseq.c) */
uint32_t om_vec_entry_vbr(void);
void om_exc(uint32_t vec, uint32_t pc, uint32_t sr) __attribute__((noreturn));
#endif
#endif
