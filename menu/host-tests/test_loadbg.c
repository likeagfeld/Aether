/* menu/host-tests/test_loadbg.c -- LOADBG.BIN (contract/SPEC.md §5.11): the reader on the shipped asset, the
 * keyframe and two whole passes of the loop against every index CRC-32 (so the wrap really returns to frame 0), the
 * changed-run walker (the runs rebuild each frame from the one before: nothing the decoder changed is missed, so the
 * uploader writes exactly those pixels), the streaming byte counts, and corrupt or truncated files. BSD-3-Clause. */
#include "th.h"
#include "loadbg.h"
#include "om_fmt.h"

#define ASSET "../app/assets/LOADBG.BIN"

static uint8_t s_pix[OM_LBG_MAX_W * OM_LBG_MAX_H], s_prev[OM_LBG_MAX_W * OM_LBG_MAX_H];
static uint32_t s_w, s_runs_px, s_bad_run;

static void emit(void *ctx, uint32_t row, uint32_t col, uint32_t n)
{
    uint8_t *dst = (uint8_t *)ctx;
    if (col + n > s_w) s_bad_run++;
    memcpy(dst + row * s_w + col, s_pix + row * s_w + col, n);
    s_runs_px += n;
}

int main(void)
{
    uint32_t n, i, bad = 0, runs_bad = 0, total_runs = 0, maxpx = 0;
    uint8_t *f = th_load(ASSET, &n), *m;
    om_lbg g, g2;
    om_lbg_play p;
    CHECK_INT(om_lbg_open(&g, f, om_lbg_head_bytes(f, n), n), OM_OK);
    CHECK_INT(g.w, 320);                                               /* Task 13c round 2: full resolution, 30 fps */
    CHECK_INT(g.h, 224);
    CHECK_INT(g.scale, 1);
    CHECK_INT(g.fps, 30);
    CHECK_INT(g.n, 63);
    CHECK(om_lbg_key_need(&g) < om_lbg_need(&g, 0));                 /* the keyframe comes first */
    CHECK(om_lbg_need(&g, g.n - 1) == n);                             /* the last loop frame ends the file */
    CHECK_INT(om_lbg_start(&p, &g, s_pix), OM_OK);
    CHECK_INT(p.frame, 0);
    s_w = g.w;
    for (i = 0; i < 2u * g.n; i++) {                                   /* two passes: the wrap lands on frame 0 */
        memcpy(s_prev, s_pix, g.w * g.h);
        if (om_lbg_step(&p, &g, 1) != OM_OK) bad++;
        s_runs_px = 0;
        total_runs += om_lbg_runs(&p, &g, emit, s_prev);
        if (memcmp(s_prev, s_pix, g.w * g.h)) runs_bad++;              /* the runs cover every changed pixel */
        if (s_runs_px > maxpx) maxpx = s_runs_px;
    }
    CHECK_INT(bad, 0);
    CHECK_INT(runs_bad, 0);
    {   /* the decode in pieces (7 ops a call): a whole pass, every CRC checked, lands on the same pixels */
        static uint8_t ref[OM_LBG_MAX_W * OM_LBG_MAX_H];
        om_lbg_play q;
        om_lbg_dec d;
        uint32_t fr, okp = 0;
        int r;
        memcpy(ref, s_pix, g.w * g.h);
        CHECK_INT(om_lbg_start(&q, &g, s_prev), OM_OK);
        for (fr = 0; fr < g.n; fr++) {
            uint32_t off, len;
            om_lbg_entry_of(&g, om_lbg_next(&q, &g), &off, &len);
            CHECK_INT(om_lbg_dec_begin(&d, &q, &g, f + off, len, 1), OM_OK);
            while ((r = om_lbg_dec_run(&d, &q, &g, 7)) == 1) ;
            if (r == 0) okp++;
        }
        CHECK_INT(okp, g.n);
        CHECK_INT(q.frame, 0);
        CHECK(memcmp(s_prev, ref, g.w * g.h) == 0);                     /* both at frame 0 again */
    }
    CHECK_INT(s_bad_run, 0);
    CHECK_INT(p.frame, 0);
    printf("test_loadbg: %u runs over two passes, at most %u px written in one frame\n", total_runs, maxpx);
    CHECK(maxpx < g.w * g.h);
    /* corrupt data: the CRC check names it */
    m = th_dup(f, n);
    {
        uint32_t off = om_lbg_need(&g, 4) - 1;                         /* a byte inside frame 4's entry */
        m[off] ^= 0x01;
        CHECK_INT(om_lbg_open(&g2, m, n, n), OM_OK);                   /* the index is intact */
        CHECK_INT(om_lbg_start(&p, &g2, s_pix), OM_OK);
        for (i = 1; i < 4; i++) CHECK_INT(om_lbg_step(&p, &g2, 1), OM_OK);
        CHECK(om_lbg_step(&p, &g2, 1) < 0);
    }
    free(m);
    m = th_dup(f, n);
    m[600] ^= 0xFF;                                                    /* inside the index: its CRC fails */
    CHECK_INT(om_lbg_open(&g2, m, n, n), OM_E_CRC);
    free(m);
    m = th_dup(f, n);
    m[64 + 6] = 0; m[64 + 7] = 2;                                      /* codec 2 is BOOTANIM's, not a loop */
    CHECK_INT(om_lbg_open(&g2, m, n, n), OM_LBG_E_INFO);
    memcpy(m, "OMSATBAN", 8);
    CHECK_INT(om_lbg_open(&g2, m, n, n), OM_E_MAGIC);
    free(m);
    CHECK_INT(om_lbg_open(&g2, f, 100, n), OM_E_SHORT);                /* the head has not arrived */
    CHECK(om_lbg_open(&g2, f, om_lbg_head_bytes(f, n), n - 1) < 0);    /* the file is shorter than its entries */
    CHECK_INT(om_lbg_head_bytes(f, 10), 0);
    free(f);
    {   /* eye-animation ruling: the file's part past HWRAM fills the free sound-RAM region
         * 0x40000-0x77FFF FIRST (ledger D3: no slot, clip, ring or DSP ring reads it while the music plays, so a panel
         * can fill it); then 92,160 B of LWRAM (OM_LW_STAGE + 0x8800 .. + 0x1F000, writable while the music
         * plays), and only then the music region 0x10000-0x2FFFF, borrowed after the music has faded */
        const uint32_t lw0 = OM_LBG_SR1_LEN, sr2 = OM_LBG_SR1_LEN + OM_LBG_LW_LEN;
        CHECK_INT(OM_LBG_LW_LEN, 0x16800);                              /* 0x1F000 - 0x8800 */
        CHECK_INT(OM_LBG_LW_V0, lw0);
        CHECK_INT(OM_LBG_SR2_V0, sr2);
        CHECK_INT(om_lbg_sr_off(0), 0x40000);
        CHECK_INT(om_lbg_sr_off(OM_LBG_SR1_LEN - 2u), 0x77FFE);
        CHECK_INT(om_lbg_sr_off(sr2), 0x10000);                         /* the music region starts after LWRAM's */
        CHECK_INT(om_lbg_sr_off(sr2 + OM_LBG_SR2_LEN - 2u), 0x2FFFE);
        CHECK_INT(OM_LBG_SR1_OFF + OM_LBG_SR1_LEN, 0x78000);           /* ends where the parked DSP ring starts */
        CHECK_INT(OM_LBG_SR2_OFF + OM_LBG_SR2_LEN, 0x30000);           /* ends where the SFX clips start */
        CHECK_INT(om_lbg_sr_music(OM_LBG_SR1_LEN - 1u), 0);
        CHECK_INT(om_lbg_sr_music(lw0), 0);                             /* LWRAM never waits for the music */
        CHECK_INT(om_lbg_sr_music(sr2 - 1u), 0);
        CHECK_INT(om_lbg_sr_music(sr2), 1);
        CHECK_INT(om_lbg_lw(lw0 - 1u), 0);
        CHECK_INT(om_lbg_lw(lw0), 1);
        CHECK_INT(om_lbg_lw(sr2 - 1u), 1);
        CHECK_INT(om_lbg_lw(sr2), 0);
        CHECK_INT(om_lbg_region_end(0x100u), lw0);                      /* a read or a staging copy stops at it */
        CHECK_INT(om_lbg_region_end(lw0 - 4u), lw0);
        CHECK_INT(om_lbg_region_end(lw0), sr2);
        CHECK_INT(om_lbg_region_end(sr2 - 4u), sr2);
        CHECK_INT(om_lbg_region_end(sr2), 0xFFFFFFFFu);
        CHECK(OM_LBG_SR1_LEN + OM_LBG_LW_LEN + OM_LBG_SR2_LEN >= 512114u - 167180u);   /* the shipped file fits */
    }
    return th_done("test_loadbg");
}
