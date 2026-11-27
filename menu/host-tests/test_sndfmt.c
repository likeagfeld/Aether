/* menu/host-tests/test_sndfmt.c -- SOUND.BIN / MUSIC readers vs the golden fixtures (contract/SPEC.md §5.8, §5.12).
 * BSD-3-Clause. */
#include "th.h"
#include "om_fmt.h"
#include "sndfmt.h"
#include "sd.h"

static void recrc(uint8_t *m, uint32_t size) { om_put32(m + 0x24, om_crc32(0, m + 64, size)); }

int main(void)
{
    uint32_t n, ns;
    uint8_t *mu = th_load(FIX("tiny.music.mus"), &n), *sf = th_load(FIX("tiny.sound.bin"), &ns), *m;
    om_sndfile f;
    om_snd_clip c;
    om_mus u;
    /* SOUND.BIN */
    CHECK_INT(om_sndfile_open(&f, sf, ns, ns), OM_OK);
    CHECK_INT(f.count, 2);
    CHECK_INT(f.data_off % 2048u, 0);
    CHECK_INT(om_sndfile_clip(&f, 0, &c), OM_OK);
    CHECK_INT(c.flags, 0); CHECK_INT(c.id, OM_SND_SCROLL); CHECK_INT(c.rate, 22050); CHECK_INT(c.oct, -1);
    CHECK_INT(c.fns, 0); CHECK_INT(c.samples, 100); CHECK_INT(c.len, 100);
    CHECK_INT((int8_t)sf[f.data_off + c.off + 1], -10);                /* 8-bit two's complement (HWF-34) */
    CHECK_INT(om_sndfile_clip(&f, 1, &c), OM_OK);
    CHECK_INT(c.id, OM_SND_CHANT);
    CHECK_INT(om_sndfile_clip(&f, 2, &c), OM_E_RANGE);
    m = th_dup(sf, ns);
    om_put32(m + 64 + 8, 70000);                                      /* over the SCSP loop ceiling (HWF-37) */
    recrc(m, 48);
    CHECK_INT(om_sndfile_open(&f, m, ns, ns), OM_OK);
    CHECK_INT(om_sndfile_clip(&f, 0, &c), OM_E_FIELD);
    free(m);
    m = th_dup(sf, ns);
    om_put16(m + 64 + 16, 1024);                                      /* FNS is 10 bits (HWF-36) */
    recrc(m, 48);
    om_sndfile_open(&f, m, ns, ns);
    CHECK_INT(om_sndfile_clip(&f, 0, &c), OM_E_FIELD);
    free(m);
    m = th_dup(sf, ns);
    m[70] ^= 1;
    CHECK_INT(om_sndfile_open(&f, m, ns, ns), OM_E_CRC);
    free(m);
    /* MUSIC */
    CHECK_INT(om_mus_open(&u, mu, OM_MUS_HEAD, n), OM_OK);            /* the menu reads the first 2 KB only */
    CHECK_INT(u.blocks, 2); CHECK_INT(u.frames, 5000); CHECK_INT(u.start_frame, 4500); CHECK_INT(u.rate, 22050);
    CHECK_INT(u.oct, -1); CHECK_INT(u.fns, 0); CHECK_INT(u.gain_cdb, -123); CHECK_INT(u.data_off, 2048);
    CHECK_INT(n, 2048u + 2u * OM_MUS_BLOCK_BYTES);
    CHECK_INT((int16_t)om_be16(mu + 2048), -1000);                    /* block 0, L, frame 0 */
    CHECK_INT((int16_t)om_be16(mu + 2048 + 8192), 1000);              /* block 0, R, frame 0: planar */
    CHECK_INT((int16_t)om_be16(mu + 2048 + 16384 + 2), (4097 * 7) % 2000 - 1000);   /* block 1, L, frame 4097 */
    CHECK_INT(om_mus_open(&u, mu, OM_MUS_HEAD, n - 1), OM_E_RANGE);   /* a truncated file */
    m = th_dup(mu, n);
    om_put16(m + 64 + 4, 1);                                          /* mono: refused */
    recrc(m, 32);
    CHECK_INT(om_mus_open(&u, m, OM_MUS_HEAD, n), OM_E_FIELD);
    free(m);
    m = th_dup(mu, n);
    om_put32(m + 64 + 12, 3u * 4096u);                                /* more frames than blocks hold */
    recrc(m, 32);
    CHECK_INT(om_mus_open(&u, m, OM_MUS_HEAD, n), OM_E_FIELD);
    free(m);
    m = th_dup(mu, n);                                                /* 2 GB of blocks: s_seek's int would wrap */
    om_put32(m + 0x0C, 0x20000u); om_put32(m + 0x1C, 0x80000000u);
    CHECK_INT(om_mus_open(&u, m, OM_MUS_HEAD, 0x80000800u), OM_E_FIELD);
    om_put32(m + 0x0C, 0x1FFFFu); om_put32(m + 0x1C, 0x7FFFC000u);    /* just under: accepted */
    om_put32(m + 64 + 12, 0x1FFFFu * 4096u);
    recrc(m, 32);
    CHECK_INT(om_mus_open(&u, m, OM_MUS_HEAD, 0x7FFFC800u), OM_OK);
    free(m);
    m = th_dup(mu, n);
    m[64 + 3] ^= 1;
    CHECK_INT(om_mus_open(&u, m, OM_MUS_HEAD, n), OM_E_CRC);
    free(m);
    free(mu); free(sf);
    {   /* NiGHTS IGR report (BGM.MUS READ rc -120): a timed-out music read is retried once, then silence, no message */
        uint8_t t = 0;
        CHECK_INT(OM_MUS_RETRIES, 1);
        CHECK_INT(om_mus_retry(&t, OM_SD_TIMEOUT), OM_MUS_RETRY);
        CHECK_INT(om_mus_retry(&t, OM_SD_TIMEOUT), OM_MUS_QUIET);
        CHECK_INT(om_mus_retry(&t, OM_SD_TIMEOUT), OM_MUS_QUIET);
        t = 0;                                       /* a missing or bad file is not transient: named at once */
        CHECK_INT(om_mus_retry(&t, OM_SD_NOFILE), OM_MUS_SHOW);
        CHECK_INT(om_mus_retry(&t, OM_E_FIELD), OM_MUS_SHOW);
        CHECK_INT(t, 0);
        CHECK_INT(om_mus_retry(&t, OM_SD_TIMEOUT), OM_MUS_RETRY);   /* after a timeout the Satiator may be out of */
        CHECK_INT(om_mus_retry(&t, OM_E_SHORT), OM_MUS_QUIET);      /* step: a short read or a status byte is quiet too */
    }
    {   /* hardware 2026-11-27: after the quiet failure the music comes back once the Satiator answers again */
        CHECK_INT(om_mus_recover(7, 7, 100000, 120), 0);           /* no command completed since: no reopen (another hold) */
        CHECK_INT(om_mus_recover(7, 8, 119, 120), 0);              /* it answers, but not 2 s yet */
        CHECK_INT(om_mus_recover(7, 8, 120, 120), 1);              /* it answers and 2 s have passed: reopen */
        CHECK_INT(om_mus_recover(0xFFFFFFFFu, 0, 120, 120), 1);    /* the counter's wrap */
    }
    return th_done("test_sndfmt");
}
