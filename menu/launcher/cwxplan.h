/* menu/launcher/cwxplan.h -- the pure half of the CWX boot (D-CWX; menu/launcher/cwxboot.h): which IP
 * header is bootable, which root-directory record is the 1st Read File, and where the program goes. Host-tested by
 * menu/host-tests/test_cwx.c. Rules, with their sources:
 *  - header: "SEGA SEGASATURN " (ST-040-R4 §4.3) or "PSEUDO SATURN   " (images patched with the Pseudo Saturn patcher;
 *    Kai main.c stage 4 accepts both, docs/research/m6-research.md §2.2 row 11);
 *  - IP size (IP + 0xE0): 1000H..8000H, long-word aligned (ST-040-R4 §4.3 "IP SIZE"); 0 or over 8000H is refused,
 *    a smaller one is accepted (Kai checks nothing);
 *  - the 1st Read File is "the file of file identifier [2]" (ST-040-R4 §4.3 "1st READ ADDRESS", §4.7): the third
 *    record of the root directory, after "." and ".." -- Kai's cdfs_open(NULL) (m6-research.md row 17). Its own
 *    directory-record size is read, not IP + 0xF4 ("Ignored for CDs", ST-040-R4 §4.3);
 *  - 1st Read Address (IP + 0xF0): "Transfer is not performed when 0H"; long-word aligned; range "larger than
 *    (6002000H + IP SIZE), smaller than (6100000H - 4)" (ST-040-R4 §4.3). Kai also boots a Low Work RAM address
 *    (0x002xxxxx) by loading to 0x06004000 and copying in its boot stub (bootstub.c "CWX LRAM loader"); so do we, up
 *    to the CWX tail (below). Either cache alias of an address is accepted (Kai masks with 0x0F000000). The
 *    spec's lower bound is not enforced by the BIOS and is not ours (next rule);
 *  - the IP the program may not overwrite: its first 1000H bytes (the smallest IP size: System ID, security code,
 *    area codes, and every resident block of ours, igr.h / chkai.h, all below 0x06002D48) and every byte up to its
 *    last non-zero long -- NOT the declared IP size. Retail discs declare 8000H and load the program over the empty
 *    rest: Daytona USA (USA) has IP size 8000H, its IP's last non-zero byte at +0xE8A and its 1st Read File at
 *    0x06003000 (44 more of the 843 images on the user's card are like it, 2026-11-25). The BIOS reads that file from
 *    inside the security code (igr.h: the 0x06000284 call), before the area codes and the IP's own code run, so it
 *    writes over the same bytes, and Kai's start_game stage 5 reads it to its address with no check at all (Kai's
 *    cheat entry for Daytona USA - USA has location 0x06002800). We read the program before the IP runs too,
 *    so the IP's non-zero bytes must stay clear of it. An LWRAM program's HWRAM copy at 0x06004000 is zeroed by the
 *    tail before the IP runs, so there the IP's non-zero bytes must end by 0x06004000.
 * BSD-3-Clause. */
#ifndef OM_CWXPLAN_H
#define OM_CWXPLAN_H
#include <stdint.h>

#define OM_CWX_IP        0x06002000u   /* the IP's home (ST-040-R4 §4.3 1st Read range base) */
#define OM_CWX_IP_MIN    0x1000u       /* the smallest IP size (ST-040-R4 §4.3): never under the program */
#define OM_CWX_IP_ENTRY  0x06002100u   /* the IP code after the 256 B System ID (Kai bootstub_ipprog.S ip_ptr) */
#define OM_CWX_HW_LO     0x06004000u   /* Kai hram_reset() start (soft_reset.c, m6-research.md row 5); LWRAM temp */
#define OM_CWX_HW_END    0x06100000u
#define OM_CWX_LW_LO     0x00200000u
#define OM_CWX_TAIL      0x002FF000u   /* .cwxtail (launcher.ld): the hand-off code, the hook, the staged image */
#define OM_CWX_TAIL_END  0x00300000u   /* LWRAM end */

#define OM_CWX_E_HEADER  -90           /* neither header */
#define OM_CWX_E_IPSIZE  -91           /* IP + 0xE0 is 0, over 8000H or not long-word aligned */
#define OM_CWX_E_FIRST   -92           /* the root directory has no third record, or it is a directory */
#define OM_CWX_E_ADDR    -93           /* the 1st Read Address is not long-word aligned or not in HWRAM / LWRAM */
#define OM_CWX_E_SIZE    -94           /* the 1st Read File would overwrite the IP's used bytes, pass HWRAM or reach the tail */

typedef struct {
    uint32_t exec;      /* the 1st Read Address as the IP gives it (0: no 1st Read File) */
    uint32_t data;      /* where the launcher reads it: exec, or OM_CWX_HW_LO for an LWRAM program (tail copies) */
    uint32_t size;      /* bytes (0 when exec is 0) */
    uint32_t fad;       /* its first sector (FAD = LBA + 150) */
    uint32_t ip_size;
} om_cwx_plan;

int om_cwx_header(const uint8_t *ip);                                                 /* 0 or OM_CWX_E_HEADER */
int om_cwx_first(const uint8_t *dir, uint32_t len, uint32_t *lba, uint32_t *size);    /* 0 or OM_CWX_E_FIRST */
/* ip: the disc's first ip_len bytes (the launcher reads 16 sectors, 8000H); the IP's used bytes are looked for in the
 * first min(ip_len, IP size) of them */
int om_cwx_plan_make(const uint8_t *ip, uint32_t ip_len, uint32_t lba, uint32_t size, om_cwx_plan *p);
const char *om_cwx_strerror(int rc);   /* "CWX: ..." for the codes above, NULL for any other */
#endif
