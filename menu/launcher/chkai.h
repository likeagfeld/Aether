/* menu/launcher/chkai.h -- Pseudo Saturn Kai's cheat method under the CWX boot (M6 Task 27b; user decision 2026-11-24:
 * Kai's own method only, the universal engine 27a deferred). The game's CHEATS.PAK record (SPEC §5.15) gives Kai's
 * master code (address + C30x opcode) and location; the enabled cheats give the codes. As cafe-alpha's
 * cheat_codes.c:372-440 / :594-668 and cheat.c:2579-2814 do (GPL-2.0; re-implemented, the two handler sources are
 * kept verbatim in menu/launcher/kai/):
 *  - 0x00 codes are written once, into the loaded 1st Read File, before the game runs (an LWRAM program's address is
 *    translated into its HWRAM temporary copy, as Kai's bs_params translation does);
 *  - the last 0x0D code becomes the enable condition (config +48/+52); 0x01/0x03 codes go to the code cache (+64,
 *    8 B each), at most the cap; the in-RAM handler follows the cache;
 *  - the config's first 40 B are Kai's interrupt bootstrap with the game's displaced instruction at +32, the handler
 *    pointer at +40 and the config pointer at +44 (measured in the assembled object: sbuild check_chkai pins them);
 *  - vector (opcode & 0xF) -> the bootstrap through SYS_SETSINT (*0x06000310, HWF-26, HWF-81: VBR + 4n; Kai's
 *    bios_set_sh2_interrupt is the same pointer -- the Task 27b brief's "SYS_SETUINT" is SCU-only, 0x06000300);
 *  - the master address gets the trapa opcode (a value not C3xx is C305, cheat_codes.c:594-612).
 * The config lands at its place only inside the CWX hook (cwxtail.c), after the IP has unpacked its logo, and the
 * master is written last, there, so no trapa ever reaches a half-built block. Pure C (host-tested: test_chkai.c);
 * the Saturn-only part is om_chk_arm's caller in cwxboot.c. BSD-3-Clause. */
#ifndef OM_CHKAI_H
#define OM_CHKAI_H
#include <stdint.h>
#include "cheatpak.h"

/* Our cheat block when Kai's location is absent or inside Kai's IP-logo default 0x06002800-0x06002D48, which the
 * in-game-reset stub owns (igr.h): the dead pre-read security-code bytes just below it (plan Task 27a M3). */
#define OM_RES_BASE      0x06002580u
#define OM_RES_END       0x06002800u
/* the block sits in the security code's dead bytes, so a launch arms it only over that very code: zlib CRC-32 of
 * IP + 0x100..+0xDFF, identical in all 11 BIOS images measured (plan Task 27a M2) */
#define OM_CHK_SEC_CRC   0xA0DBEDFFu
#define OM_CHK_CFG       64u           /* Kai's ct_config_t */
#define OM_CHK_BOOT      40u           /* the bootstrap: CHEATS_BOOTSTRAP_IHR_SIZE */
#define OM_CHK_HDLR      96u           /* the in-RAM handler: CHEATS_INRAM_HANDLER_SIZE */
#define OM_CHK_SLOT      32u           /* ct_config_t.master_patch_opc: the displaced instruction */
#define OM_CHK_P_HDLR    40u           /* ct_hdlr_location (the bootstrap's mov.l @(..,pc),r0) */
#define OM_CHK_P_CFG     44u           /* cfg_location (the bootstrap's mov.l @(..,pc),r4) */
#define OM_CHK_EN_ADDR   48u
#define OM_CHK_EN_VAL    52u
#define OM_CHK_COUNT     58u           /* user_codes_cnt */
#define OM_CHK_MAX       (OM_CHK_CFG + OM_CH_MAX_ACTIVE * 8u + OM_CHK_HDLR)   /* 400 B */
/* the default master for a record that has none (a game Kai's 2016 table lacks, cheats added by the user): the BIOS
 * interrupt dispatcher's 0x06000914, Kai's most common master (identical in every retail BIOS, plan Task 27a M1) */
#define OM_CHK_DEF_MASTER 0x06000914u
#define OM_CHK_DEF_OPC    0xC305u

/* where a launch's codes may write (om_chk_code_ok) -- the editor refuses the rest, with the reason */
enum { OM_CHV_OK = 0, OM_CHV_CMD, OM_CHV_RANGE, OM_CHV_ODD, OM_CHV_GUARD, OM_CHV_BLOB };
int om_chk_code_ok(uint32_t addr, uint8_t cmd);
const char *om_chk_why(int v);                                  /* one line for the editor */

/* the cheats line on the launch screen / the arming result */
enum { OM_CHS_OFF = 0, OM_CHS_ARMED, OM_CHS_NONE, OM_CHS_NEED_CWX, OM_CHS_JHL_ONLY, OM_CHS_NOTSUP, OM_CHS_NOREC,
       OM_CHS_ERR, OM_CHS_NOHOOK, OM_CHS_BADCODE };

uint32_t om_chk_place(uint32_t location);                       /* 0 = refuse */
int om_chk_op_safe(uint16_t op, uint16_t prev);                 /* the master's instruction may run moved (1) */
uint16_t om_chk_master_opc(uint16_t value);                     /* (value & 0xFFF0) == 0xC300 ? value : 0xC305 */
/* the most codes of enabled cheats a launch takes: OM_CH_MAX_ACTIVE, or Kai's cache (4 x cache4) at Kai's own place */
uint32_t om_chk_cap(const om_ch_file *f);
uint32_t om_chk_active(const om_ch_file *f);                    /* codes of the enabled cheats (what the cap counts) */
/* a record that has no master gets Kai's default (OM_CHK_DEF_*), location 0 (-> OM_RES_BASE), cache 8 x 4 */
void om_chk_default_master(om_ch_file *f);

typedef struct {
    uint32_t place, len, master;   /* the block's home and size; the master address (real, after the tail's copy) */
    uint16_t opc, saved;           /* the trapa opcode; the displaced instruction (filled by om_chk_arm) */
    uint8_t vec, ncache, nboot, status;
    uint32_t en_addr; uint16_t en_val;
    om_ch_code boot[OM_CH_MAX_ACTIVE], cache[OM_CH_MAX_ACTIVE];
} om_chk_plan;
/* The plan of a launch from the game's record: OM_CHS_ARMED (codes to run), OM_CHS_NONE (no cheat on), _JHL_ONLY,
 * _NOTSUP (no usable place), _BADCODE (an enabled code om_chk_code_ok refuses, or the master is not a code address).
 * Whole cheats past the cap are left out (Kai's cheat.c:2680-2690 drops a cheat that does not fit). */
int om_chk_plan_make(const om_ch_file *f, om_chk_plan *p);
/* Lay the config out at p->place in img (img[0] = place): Kai's bootstrap with p->saved at +32, the two pointers,
 * the enable code, the count, the cache and the handler right after it. Returns bytes used, 0 if over cap. */
uint32_t om_chk_build(uint8_t *img, uint32_t cap, const om_chk_plan *p, const uint8_t *boot, uint32_t boot_len,
                      const uint8_t *hdlr, uint32_t hdlr_len);

/* om_chk_arm's memory (host tests pass a simulated one): 16-bit reads/writes at an address */
typedef struct {
    uint16_t (*rd16)(uint32_t a);
    void (*wr16)(uint32_t a, uint16_t v);
} om_chk_mem;
/* At the CWX hand-off, the 1st Read File in RAM (exec as the IP gives it, data where it was read, size bytes): apply
 * the 0x00 codes, read the displaced instruction, build the block into img. Returns the block's length, or 0. */
uint32_t om_chk_arm(om_chk_plan *p, const om_chk_mem *m, uint32_t exec, uint32_t data, uint32_t size, uint8_t *img,
                    uint32_t cap, const uint8_t *boot, uint32_t boot_len, const uint8_t *hdlr, uint32_t hdlr_len);
/* where address a is while the program still sits at its read address (an LWRAM program's HWRAM copy) */
uint32_t om_chk_xlat(uint32_t a, uint32_t exec, uint32_t data, uint32_t size);
#endif
