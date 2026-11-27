/* menu/launcher/cwxboot.c -- see cwxboot.h. The order is Kai's (pseudosat_kai main.c start_game stages 1-5 and the
 * Satiator descload path, cdparse.c; docs/research/m6-research.md §2.2), on our CD-block calls:
 *   1. CD reset (AbortFile, DataEnd, ResetSelector, SetSctLen 2048 -- row 6; CWX does not zero the BIOS words
 *      0x06000278/27C that jhboot's om_cd_init_for_boot does) and authentication with DCHG and EFLS cleared (row 8,
 *      HWF-55);
 *   2. High Work RAM 0x06004000-0x060FFFFF zeroed (row 5, Kai hram_reset) -- before any read;
 *   3. the 16 IP sectors (FAD 150) to 0x06002000 (row 10); the header, IP size and 1st Read Address checked
 *      (cwxplan.c, ST-040-R4 §4.3);
 *   4. the Primary Volume Descriptor (LBA 16, ECMA-119 §8.4; isodir.c), the root directory's first sector, its
 *      file identifier [2] (cwxplan.c), and that file read with ONE CD play (a play restarted mid-file can hand over
 *      in-flight sectors of the old one: field gotcha #25, HWF-56) to its address or, for a Low Work RAM program,
 *      to 0x06004000 (row 17); the last partial sector goes through a bounce buffer so nothing past the file's
 *      end is written;
 *   5. the resident chain staged (om_cwx_resident: the in-game reset, Task 20a's stub, unchanged) -- rows 16, 18;
 *   -- every failure up to here returns, with the launcher's error screen still up --
 *   6. SMPC idle, then the video reset of Kai's soft_reset(SC_SOFTRES_EACH_VDPS) (row 19): VDP2 VRAM and CRAM
 *      zeroed, TVMD/RAMCTL/BGON/CHCTLA/MPOFN/SCXIN0/SCXDN0/SCYIN0/PRISA-D set as Kai sets them, VDP1
 *      TVMR/FBCR/PTMR/EWDR/EWLR/EWRR zeroed, drawing ended (ENDR) and the frame buffer zeroed. VDP1 VRAM itself
 *      was restored from the start-up snapshot at launch step 5 (HWF-12), which is Kai's restore_vdp1_ram;
 *   7. the machine handed to the BIOS state (om_boot_handover: VBR as at entry, SR I3-I0 = 7 -- the JHL hand-off
 *      state, proven under the BIOS boot; for CWX it is unmeasured), .cwxtail copied to the top of LWRAM, the cache
 *      purged, and a jump to it (cwxtail.c: LWRAM clear, LWRAM-program copy, ipprog).
 * Register addresses: VDP2 at 0x25F80000 (relative 180000H, VDP2_Manual.txt:145) -- TVMD 180000H (:1269), RAMCTL
 * 18000EH (:1744), BGON 180020H (:2511), CHCTLA 180028H (:2986), MPOFN 18003CH (:4034), SCXIN0/SCXDN0/SCYIN0
 * 180070H-180074H (:5367), PRISA-PRISD 1800F0H-1800F6H (:8733); VRAM 0x25E00000 (512 KB), CRAM 0x25F00000
 * (100000H, :2385; 4 KB). VDP1 registers at 0x25D00000 (100000H, VDP1_Manual.txt:1230-1236: TVMR, FBCR, PTMR,
 * EWDR, EWLR, EWRR, ENDR), frame buffer 0x25C80000 (256 KB). SMPC SF 0x20100063 bit 0 (SMPC_Manual.txt; HWF-53).
 * BSD-3-Clause. */
#include <string.h>
#include "cdblk.h"
#include "cwxboot.h"
#include "cwxplan.h"
#include "igr.h"
#include "isodir.h"
#include "om_fmt.h"
#include "sega_sys.h"
#include "vdp2txt.h"

void om_boot_handover(void);                       /* lseq.c */

extern char __cwx_tail_start[], __cwx_tail_end[], __cwx_tail_lma[], __cwx_img[];
extern char __igr_stub_start[], __igr_stub_end[], __igr_stub_lma[];
extern char __igr_guard_start[], __igr_guard_end[], __igr_guard_lma[];
extern uint32_t __cwx_par[3];
extern om_cwx_res __cwx_res;
extern uint32_t __cwx_cell;
extern uint32_t __cwx_chk[5];                     /* cwxtail.c: {dst, len, sum, master, trapa} */
extern char __cwx_chimg[];
extern const uint8_t asm_cheat_bootstrap_ihr[], asm_cheatcodes_hdlr_ram[];   /* kai/*.s, verbatim */
void om_cwx_go(void) __attribute__((noreturn));

#define SECT        2048u
#define STALL_VBL   600                            /* no new sector for 10 s: the read failed (HWF-11) */
#define SMPC_SF     (*(volatile uint8_t *)0x20100063u)
#define V2(o)       (*(volatile uint16_t *)(0x25F80000u + (o)))
#define V1(o)       (*(volatile uint16_t *)(0x25D00000u + (o)))

static uint32_t s_sec[SECT / 4];                   /* PVD, directory and the bounce sector (4-aligned for om_cd_take) */
static om_boot_trace s_last;
static char s_msg[64];
const om_boot_trace *om_cwx_last(void) { return &s_last; }

/* The 1st Read File, `total` sectors from p->fad in one play; full sectors straight to p->data, the last partial one
 * through s_sec. Bounded by STALL_VBL V-blanks without a new sector. The partition must be empty after it. */
static int read_file(const om_cwx_plan *p)
{
    uint32_t full = p->size / SECT, rest = p->size % SECT, total = full + (rest != 0), got = 0;
    uint8_t *dst = (uint8_t *)p->data;
    int32_t c;
    int rc, stall = 0;
    if (!total)
        return 0;
    if ((rc = om_cd_stream_start((int32_t)p->fad, (int32_t)total)) != 0)
        return rc;
    while (got < total) {
        if ((c = om_cd_stream_count()) < 0)
            return (int)c;
        if (c == 0) {
            if (++stall > STALL_VBL)
                return OM_CD_E_SECTORS;
            om_txt_wait_vblank();
            continue;
        }
        stall = 0;
        if (got < full) {
            uint32_t n = (uint32_t)c < full - got ? (uint32_t)c : full - got;
            if ((rc = om_cd_take(dst + got * SECT, (int32_t)n)) != 0)
                return rc;
            got += n;
        } else {
            if ((rc = om_cd_take((uint8_t *)s_sec, 1)) != 0)
                return rc;
            memcpy(dst + full * SECT, s_sec, rest);
            got++;
        }
    }
    return om_cd_stream_count() == 0 ? 0 : OM_CD_E_SECTORS;   /* a stray sector: the play was not ours alone */
}

/* What the hook copies into the IP once the logo is unpacked (cwxboot.h): the one place that decides what is armed.
 * Check-only here (OM_IGR_* for the trace); the staging itself is om_cwx_stage's, inside the no-return hand-off,
 * because the tail's home (the top of the VDP1 snapshot at OM_LW_SNAPSHOT, consumed at launch step 5) must stay
 * intact while a failure can still return to the error screen, whose exit A restores the snapshot again. */
static int om_cwx_resident(const uint8_t *ip, int igr)
{
    if (!igr)
        return OM_IGR_E_OFF;
    return om_igr_ip_check(ip, 0);                              /* the SEGA header, the security cell, the IP size */
}

static uint32_t s_chlen;
static uint8_t s_chimg[OM_CHK_MAX] __attribute__((aligned(4)));

static void om_cwx_stage(int igr, const om_chk_plan *chk)
{
    uint32_t len = (uint32_t)(__igr_stub_end - __igr_stub_start), i, s;
    const uint32_t *src = (const uint32_t *)__igr_stub_lma;
    if (igr) {
        for (i = 0; i < len / 4u; i++)                          /* Task 20a's stub image, byte for byte */
            ((uint32_t *)__cwx_img)[i] = src[i];
        __cwx_res.dst = (uint32_t)__igr_stub_start;
        __cwx_res.len = len;
        __cwx_res.sum = om_igr_seal((uint32_t *)__cwx_img, len / 4u);   /* the guard's seal; the hook's sum */
        __cwx_res.execdmp = (uint32_t)om_igr_guard;
    }
    if (chk && s_chlen) {                                       /* Kai's block after the stub image */
        for (i = 0, s = 0; i < s_chlen / 4u; i++)
            s += ((uint32_t *)__cwx_chimg)[i] = ((const uint32_t *)s_chimg)[i];
        __cwx_chk[0] = chk->place;
        __cwx_chk[1] = s_chlen;
        __cwx_chk[2] = s;
        __cwx_chk[3] = chk->master;
        __cwx_chk[4] = chk->opc;
    }
    *(volatile uint32_t *)OM_IGR_SEC_CELL = (uint32_t)&__cwx_cell;   /* the security code's read cell -> the hook */
}

static void zero16(uint32_t at, uint32_t bytes)
{
    volatile uint32_t *p = (volatile uint32_t *)at;
    uint32_t i;
    for (i = 0; i < bytes / 4u; i++)
        p[i] = 0;
}

static void __attribute__((noreturn)) handoff(const om_cwx_plan *p, int arm, const om_chk_plan *chk)
{
    uint32_t t;
    for (t = 0; (SMPC_SF & 1u) && t < 2000000u; t++) ;         /* no SMPC command in flight (our pad INTBACK) */
    zero16(0x25E00000u, 0x80000u);                              /* VDP2 VRAM */
    zero16(0x25F00000u, 0x1000u);                               /* VDP2 CRAM */
    V2(0x00) = 0x8000;                                          /* TVMD (Kai: DISP on) */
    V2(0x0E) = 0x0000;                                          /* RAMCTL */
    V2(0x20) = 0x0001;                                          /* BGON (Kai: NBG0 on) */
    V2(0x28) = 0x0000;                                          /* CHCTLA */
    V2(0x3C) = 0x0000;                                          /* MPOFN */
    V2(0x70) = 0x0000; V2(0x72) = 0x0000; V2(0x74) = 0x0000;    /* SCXIN0, SCXDN0, SCYIN0 */
    V2(0xF0) = 0x0000; V2(0xF2) = 0x0000; V2(0xF4) = 0x0000; V2(0xF6) = 0x0000;   /* PRISA-PRISD */
    V1(0x00) = 0; V1(0x02) = 0; V1(0x04) = 0;                   /* TVMR, FBCR, PTMR */
    V1(0x06) = 0; V1(0x08) = 0; V1(0x0A) = 0;                   /* EWDR, EWLR, EWRR */
    V1(0x0C) = 0;                                               /* ENDR: end any drawing */
    zero16(0x25C80000u, 0x40000u);                              /* VDP1 frame buffer */
    memcpy(__cwx_tail_start, __cwx_tail_lma, (size_t)(__cwx_tail_end - __cwx_tail_start));
    __cwx_par[0] = p->exec;
    __cwx_par[1] = p->data;
    __cwx_par[2] = (p->size + 3u) & ~3u;
    /* SYS_EXECDMP -> the guard, armed or not (Astal, igrguard.c): the stub while it is intact, otherwise a console
     * reset (B1, HWF-80: the BIOS CD player cannot run after a CWX boot). Nothing re-inits 0x06000220-0x06000AFF
     * after this under CWX, so the cell and the guard stay. */
    memcpy(__igr_guard_start, __igr_guard_lma, (size_t)(__igr_guard_end - __igr_guard_start));
    *(volatile uint32_t *)OM_IGR_EXECDMP = (uint32_t)om_igr_guard;
    if (arm || (chk && s_chlen))
        om_cwx_stage(arm, chk);
    om_boot_handover();
    /* vector (trapa & 15) -> the block's bootstrap, with VBR the BIOS's again (SYS_SETSINT writes VBR + 4n,
     * HWF-26; Kai's bios_set_sh2_interrupt is this pointer, 0x06000310). Harmless until the hook writes the trapa. */
    if (chk && s_chlen)
        SYS_SETSINT(chk->vec, (void *)chk->place);
    __asm__ volatile ("mov.l   1f,r1\n\t"                       /* CCR <- 0x11 (CP | CE), HWF-13, as hop.c */
                      "mov     #17,r0\n\t"
                      "mov.b   r0,@r1\n\t"
                      "bra     2f\n\t"
                      "nop\n\t"
                      ".align  2\n"
                      "1:  .long   0xFFFFFE92\n"
                      "2:\n" : : : "r0", "r1", "memory");
    om_cwx_go();
}

static uint16_t mem_rd16(uint32_t a) { return *(volatile uint16_t *)a; }
static void mem_wr16(uint32_t a, uint16_t v) { *(volatile uint16_t *)a = v; }

int om_cwx_boot(om_boot_trace *tr, int igr, om_chk_plan *chk)
{
    uint8_t *ip = (uint8_t *)OM_CWX_IP;
    uint32_t rlba, rsize, flba, fsize;
    om_cwx_plan p;
    int rc;
    memset(tr, 0, sizeof *tr);
    tr->igr = OM_IGR_E_OFF;
    om_cd_reset(tr->init_rc);
    tr->auth_rc = om_cd_auth(&tr->auth_status);
    zero16(OM_CWX_HW_LO, OM_CWX_HW_END - OM_CWX_HW_LO);
    if ((rc = tr->read_rc = om_cd_read(ip, 150, 16)) < 0)
        goto out;
    memcpy(tr->head, ip, sizeof tr->head);
    if ((rc = om_cwx_header(ip)) != 0)
        goto out;
    if ((rc = om_cd_read((uint8_t *)s_sec, 150 + (int32_t)OM_ISO_PVD_LBA, 1)) < 0
        || (rc = om_iso_root((const uint8_t *)s_sec, &rlba, &rsize)) < 0
        || (rc = om_cd_read((uint8_t *)s_sec, 150 + (int32_t)rlba, 1)) < 0
        || (rc = om_cwx_first((const uint8_t *)s_sec, rsize < SECT ? rsize : SECT, &flba, &fsize)) < 0
        || (rc = om_cwx_plan_make(ip, 16u * SECT, flba, fsize, &p)) < 0)
        goto out;
    if ((rc = tr->put_rc = read_file(&p)) < 0)
        goto out;
    tr->igr = om_cwx_resident(ip, igr);
    s_chlen = 0;
    if (chk && chk->status == OM_CHS_ARMED) {                   /* the hook is the only way in */
        static const om_chk_mem mem = { mem_rd16, mem_wr16 };
        if (om_igr_ip_check(ip, 0) != OM_IGR_OK)
            chk->status = OM_CHS_NOHOOK;
        else if (chk->place == OM_RES_BASE && om_crc32(0, ip + 0x100, 0xD00) != OM_CHK_SEC_CRC)   /* our place: dead security-code bytes */
            chk->status = OM_CHS_NOHOOK;
        else
            s_chlen = om_chk_arm(chk, &mem, p.exec, p.data, p.size, s_chimg, sizeof s_chimg, asm_cheat_bootstrap_ihr,
                                 OM_CHK_BOOT, asm_cheatcodes_hdlr_ram, OM_CHK_HDLR);
    }
    tr->hirq_in = om_cd_hirq();
    s_last = *tr;
    handoff(&p, tr->igr == OM_IGR_OK, chk);
out:
    tr->boot_rc = rc;
    tr->hirq = om_cd_hirq();
    tr->cdc = om_cd_last_error();                 /* before om_cd_peri: GetPeriStat is a LIBCD call too */
    memcpy(tr->cd_pre, om_cd_last_diag()->pre, sizeof tr->cd_pre);
    memcpy(tr->cd_post, om_cd_last_diag()->post, sizeof tr->cd_post);
    tr->ehst_max = om_cd_last_diag()->ehst_max;
    if (rc == OM_CD_E_SECTORS) {                  /* the IP/PVD/root read timed out: what the drive was doing */
        tr->rd_max = om_cd_last_diag()->cnt_max;
        tr->rd_cur_rc = om_cd_last_diag()->cur_rc;
        tr->rd_status = om_cd_last_diag()->cur_status;
        tr->rd_fad = om_cd_last_diag()->cur_fad;
    }
    tr->peri = om_cd_peri();
    s_last = *tr;
    return rc;
}

const char *om_cwx_boot_strerror(int rc)
{
    const char *s = om_cwx_strerror(rc);
    if (s)
        return s;
    switch (rc) {
    case OM_ISO_E_PVD: return "CWX: no ISO 9660 volume on the disc";
    case OM_ISO_E_ROOT: return "CWX: bad root directory on the disc";
    default: break;
    }
    strcpy(s_msg, "CWX: ");
    strncat(s_msg, om_boot_strerror(rc), sizeof s_msg - 6);
    return s_msg;
}
