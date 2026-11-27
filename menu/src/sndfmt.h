/* menu/src/sndfmt.h -- the sound files (contract/SPEC.md §5.8 SOUND.BIN OMSATSND, §5.12 MUSIC OMSATMUS; tools/omsat/
 * sound.py writes them). Portable C99: compiled for the Saturn and for host tests. BSD-3-Clause. */
#ifndef OM_SNDFMT_H
#define OM_SNDFMT_H
#include <stdint.h>
#define OM_SND_F16     1u
#define OM_SND_LOOP    2u
#define OM_SND_MAXSAMP 65535u        /* SCSP LSA/LEA are 16-bit sample counts (HWF-37) */
#define OM_SND_MAXCLIP 8u
enum { OM_SND_SCROLL, OM_SND_SELECT, OM_SND_RING, OM_SND_SCREAM, OM_SND_CHANT };   /* SOUND.BIN clip ids */
typedef struct { uint32_t off, len, samples; uint16_t rate, fns; int8_t oct; uint8_t flags, id; } om_snd_clip;
typedef struct { const uint8_t *buf; uint32_t count, data_off, data_size; } om_sndfile;
int om_sndfile_open(om_sndfile *f, const uint8_t *buf, uint32_t buf_len, uint32_t file_size);
int om_sndfile_clip(const om_sndfile *f, uint32_t i, om_snd_clip *c);

#define OM_MUS_HEAD   2048u          /* header + info row: the first 2 KB read */
#define OM_MUS_BLOCK  4096u          /* frames per block: one SCSP CA unit (HWF-51) */
#define OM_MUS_BLOCK_BYTES (OM_MUS_BLOCK * 4u)   /* [L 4096 x s16][R 4096 x s16] = 16 KB = 8 reads */
typedef struct {
    uint32_t blocks, data_off, rate, frames, start_frame;
    int16_t gain_cdb;
    int8_t oct;
    uint16_t fns;
} om_mus;
/* buf holds at least the first 64 + 32 bytes; file_size is the file's size on the card. */
int om_mus_open(om_mus *m, const uint8_t *buf, uint32_t buf_len, uint32_t file_size);
/* A music open/seek/read failure (NiGHTS IGR report: "BGM.MUS READ rc -120"): an SD timeout (sd.h OM_SD_TIMEOUT) is
 * retried OM_MUS_RETRIES times (*tries counts them; the caller zeroes it once a block is read), then the music goes
 * silent with no message. Once a timeout has started retries, any failure (the Satiator may be out of step) is
 * handled the same way; otherwise a code (no file, a bad header) is named at once. One retry: each timeout can hold
 * the menu for SAT_HIRQ_WAIT_CAP polls (~28 s, HWF-54's calibration). */
#define OM_MUS_RETRIES 1u
enum { OM_MUS_RETRY, OM_MUS_QUIET, OM_MUS_SHOW };
int om_mus_retry(uint8_t *tries, int rc);
/* hardware 2026-11-27 (SAROO card: two Satiator stalls, then the music never came back): a quiet failure (OM_MUS_QUIET)
 * is not for good. 1 = reopen now: another Satiator command has completed since the failure (ok_now != ok_at_fail,
 * satiator.h s_diag.ok: it answers again, so the reopen is not one more SAT_HIRQ_WAIT_CAP hold) and min_vb V-blanks
 * have passed. A named failure (OM_MUS_SHOW: no file, a bad header) stays latched. */
int om_mus_recover(uint32_t ok_at_fail, uint32_t ok_now, uint32_t vb_since_fail, uint32_t min_vb);
#endif
