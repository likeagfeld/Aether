/* menu/app/audio.c -- see audio.h. Direct SCSP programming (decision D2): no SGL/SBL sound call is made, and the
 * 68000 is held by SMPC SNDOFF (HWF-38) before the first sound-RAM write. Register facts: slot words HWF-34, common
 * word HWF-35, pitch HWF-36, loops HWF-37, DSP HWF-39, the CA monitor HWF-51, TL/DISDL/DIPAN levels HWF-52.
 * Sound RAM is written with 16-bit stores only (HWF-33). Hand-offs between the two contexts:
 *  - the main loop (om_audio_tick, om_sfx, events) programs slots and writes sound RAM;
 *  - the V-blank hook (om_audio_vblank) reads CA, advances the ring's `played`, steps the fade and the ducking and
 *    writes the two music slots' TL word; its only key-on is the error's second tone on its own slot 12. KYONEX keys
 *    every slot by its KB bit (HWF-34), so every slot set-up below keeps KB = 0 until its final write.
 * BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "satiator.h"
#include "audio.h"
#include "boottime.h"
#include "cdblk.h"
#include "folders.h"
#include "isodir.h"
#include "msgfmt.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "sd.h"
#include "sndfmt.h"
#include "sndmix.h"
#include "text.h"
#include "uitext.h"
#include "vdp2theme.h"

#define SCSP_SLOT(n)  ((volatile uint16_t *)(0x25B00000u + 0x20u * (uint32_t)(n)))   /* HWF-34 */
#define SCSP_COMMON   ((volatile uint16_t *)0x25B00400u)          /* HWF-35: MEM4MB 9, DAC18B 8, VER 7:4, MVOL 3:0 */
#define SCSP_RING     ((volatile uint16_t *)0x25B00402u)          /* HWF-39: RBL 8:7, RBP 6:0 */
#define SCSP_MON      ((volatile uint16_t *)0x25B00408u)          /* HWF-51: MSLC 15:11, CA 10:7 */
#define SCSP_MPRO     ((volatile uint16_t *)0x25B00800u)          /* HWF-39: 128 steps x 64 bits = 512 words */
#define SNDRAM16(o)   ((volatile uint16_t *)(OM_SNDRAM + (o)))   /* HWF-33 */
enum { W_CTL, W_SA, W_LSA, W_LEA, W_EG1, W_EG2, W_TL, W_MOD, W_PITCH, W_LFO, W_DSP, W_MIX };   /* HWF-34 word index */
#define CTL_KX        0x1000u
#define CTL_KB        0x0800u
#define CTL_LOOP      0x0020u        /* LPCTL 01 normal loop (HWF-37) */
#define CTL_PCM8      0x0010u
#define MIX(dipan)    ((uint16_t)((7u << 13) | ((uint32_t)(dipan) << 8)))   /* DISDL 7 = 0 dB, EFSDL/EFPAN 0 (HWF-52) */
#define PAN_C         0x00u          /* center: 0 dB both sides (HWF-52, Table 4.28) */
#define PAN_L         0x1Fu          /* left 0 dB, right -inf */
#define PAN_R         0x0Fu          /* left -inf, right 0 dB */
#define MVOL          0xFu
#define RBP_PARK      0x3Cu          /* DSP ring at 0x78000 (PCM_Work, unused), RBL 0 = 16 KB */

#define SL_MUS_L      0
#define SL_MUS_R      1
#define SL_CHANT      2
#define SL_SFX0       8              /* round-robin 8-11 */
#define SL_ERR2       12             /* the error's second tone, keyed from the V-blank hook */
#define RING_L        0x10000u       /* ledger D3: the music region; 7 x 8 KB per channel + a 2 B guard */
#define RING_R        0x20000u
#define RING_CH_BYTES (OM_RING_SLOTS * OM_MUS_BLOCK * 2u)   /* 57,344 */
#define SFX_BASE      0x30000u
#define SFX_END       0x40000u
#define CHANT_BASE    0x10000u
#define CHUNK         2048u          /* S_MAXBUF */
#define PRIME_BLOCKS  3u
#define BOOT_VB       180u           /* SOUND.BIN: 3 s for the whole boot load */

static uint32_t s_count[OM_AEV_COUNT];
static uint32_t s_bounce[CHUNK / 4];                      /* s_read target, 4-aligned (libsatiator) */
static om_snd_clip s_clip[5];
static uint32_t s_clip_sa[5];
static uint8_t s_have[5], s_sndoff, s_inited, s_sfx_on = 1, s_next, s_held, s_music_lost;
static uint32_t s_vps = 60;
static char s_boot_msg[48];

/* V-blank-shared state (written by one side only, as noted) */
static volatile uint32_t s_vbl_last, s_sfx_until, s_scream_end, s_err2_at, s_underruns;
static volatile uint8_t s_err2, s_playing, s_starving, s_duck, s_vstate;
static volatile uint8_t s_vol_tl;
/* review-sound M4: volatile -- both contexts touch them. om_fade_to's three fields are not written atomically; a V-blank
 * between them steps one frame with a stale step, which is harmless (the next frame is right). */
static volatile om_fade s_fade;                           /* stepped by the V-blank hook; retargeted by the main loop */
static volatile om_ring s_ring;                           /* played/last: V-blank hook; filled: main loop */

/* the music stream (main loop) */
enum { M_OFF, M_WAIT_CHANT, M_OPEN, M_PRIME, M_PLAY, M_STOPPING, M_OUT, M_FAILED };
static uint8_t s_mstate, s_want, s_started, s_chunk, s_seek_due, s_fade_in_long, s_paused;
static int s_fd = -1;
static om_mus s_mus;
static uint32_t s_fsize, s_fblock, s_slot_file[OM_RING_SLOTS], s_resume, s_saved = 0xFFFFFFFFu, s_chant_end;
static uint32_t s_bytes, s_vb0, s_boot_vb;                 /* diagnostics: bytes streamed since the music started */

static uint32_t now(void) { return om_v2_vblanks(); }

/* --- SCSP --------------------------------------------------------------------------------------------------------- */

static void key_exec(int n) { SCSP_SLOT(n)[W_CTL] = (uint16_t)(SCSP_SLOT(n)[W_CTL] | CTL_KX); }

static void key_off(int n)
{
    volatile uint16_t *s = SCSP_SLOT(n);
    s[W_CTL] = (uint16_t)((s[W_CTL] & ~CTL_KB) | CTL_KX);       /* KB = 0, execute (HWF-34) */
}

static void sample_gap(void)
{   /* >= 2 output samples (45 us at 44.1 kHz) between a key-off and the next key-on of the same slot. A one-shot that
     * has ended is still KEY_ON (LPCTL off stops the read, not the key), and "KEY_ON on a slot that is ON = Ignore"
     * (HWF-34, Figure 4.8): when both KYONEX writes fell inside one sample, the slot stayed silent while the music
     * ducked -- measured on Mednafen 1.32.1, Task 14/15 rec3 (3 of the tour's presses). Bounded by the FRT and a count */
    uint16_t t0 = om_ut_frc();
    uint32_t i;
    for (i = 0; i < 20000u && om_ut_frc_us((uint16_t)(om_ut_frc() - t0)) < 60u; i++) ;
}

static void slot_setup(int n, uint32_t sa, uint32_t lea, uint16_t ctl, int8_t oct, uint16_t fns, uint8_t tl,
                       uint16_t mix)
{   /* KB stays 0 until the caller keys on: a KYONEX from the V-blank hook meanwhile only "OFF executes" it */
    volatile uint16_t *s = SCSP_SLOT(n);
    key_off(n);
    sample_gap();
    s[W_CTL] = (uint16_t)(ctl | ((sa >> 16) & 0xFu));         /* SA[19:16] */
    s[W_SA] = (uint16_t)(sa & 0xFFFFu);
    s[W_LSA] = 0;
    s[W_LEA] = (uint16_t)lea;
    s[W_EG1] = 0x001F;                                         /* AR 31 (instant), D1R = D2R = 0: no decay */
    s[W_EG2] = 0x001F;                                         /* DL 0, RR 31 */
    s[W_TL] = tl;                                              /* SDIR 0: through EG and TL (HWF-52) */
    s[W_MOD] = 0;
    s[W_PITCH] = (uint16_t)((((uint16_t)oct & 0xFu) << 11) | (fns & 0x3FFu));   /* HWF-36 */
    s[W_LFO] = 0;
    s[W_DSP] = 0;                                              /* ISEL/IMXL 0: nothing to the DSP mix stack */
    s[W_MIX] = mix;
}

static void play_clip(int n, int id, int semis, uint8_t tl)
{
    const om_snd_clip *c = &s_clip[id];
    int8_t o;
    uint16_t f;
    uint32_t until;
    om_snd_shift(c->oct, c->fns, semis, &o, &f);
    slot_setup(n, s_clip_sa[id], c->samples - 1u, (uint16_t)((c->flags & OM_SND_F16) ? 0u : CTL_PCM8), o, f, tl,
               MIX(PAN_C));
    SCSP_SLOT(n)[W_CTL] = (uint16_t)(SCSP_SLOT(n)[W_CTL] | CTL_KB | CTL_KX);   /* key on */
    until = now() + om_snd_vblanks(c->samples, o, f, s_vps);
    if ((int32_t)(until - s_sfx_until) > 0) s_sfx_until = until;   /* the music ducks while it sounds */
}

/* MEM4MB 1, DAC18B 0 (HWF-35), MVOL max. The word is WRITE-ONLY (SCSP manual p.88: "MVOL (W)", "MEM4MB (w)"; HWF-35):
 * it is always written whole, never read-modified-written. Task 14/15 measured why: 13c's borrow read it back and
 * rewrote it, and the -soundrecord WAV went digital-silent (MVOL 0) from the borrow until the next music key-on --
 * the scream was inaudible. SGL never writes this word (HWF-39 recheck, linked-binary literal method), so there is no
 * "SGL default" to restore at the hand-off: the documented value stays, and the game's sound driver sets its own. */
static void common_set(void) { *SCSP_COMMON = (uint16_t)(0x0200u | MVOL); }

static void scsp_init(void)
{
    uint32_t i, w;
    common_set();
    for (i = 0; i < 32u; i++) {
        volatile uint16_t *s = SCSP_SLOT(i);
        s[W_CTL] = 0;                                          /* KB 0 everywhere ... */
        for (w = 1; w < 12u; w++) s[w] = 0;                    /* EFSDL/EFPAN, ISEL/IMXL: no DSP input */
    }
    key_exec(0);                                               /* ... one KYONEX keys all 32 off */
    for (i = 0; i < 512u; i++) SCSP_MPRO[i] = 0;               /* an all-zero microprogram (HWF-39) */
    *SCSP_RING = RBP_PARK;                                     /* RBL 0, ring parked at 0x78000 */
    *SCSP_MON = (uint16_t)(SL_MUS_L << 11);                    /* MSLC: CA follows the left music slot */
    s_inited = 1;
}

static void copy16(uint32_t dst, const uint8_t *src, uint32_t len)
{
    volatile uint16_t *d = SNDRAM16(dst);
    uint32_t i;
    for (i = 0; i + 1u < len; i += 2u)
        *d++ = (uint16_t)((src[i] << 8) | src[i + 1]);
    if (len & 1u)
        *d = (uint16_t)(src[len - 1u] << 8);
}

static void copy_chunk(uint32_t dst)
{   /* one 2 KB read: big-endian samples, as they are in sound RAM; 16-bit stores (HWF-33) */
    volatile uint16_t *d = SNDRAM16(dst);
    uint32_t i;
    for (i = 0; i < CHUNK / 4u; i += 2u) {
        uint32_t a = s_bounce[i], b = s_bounce[i + 1u];
        d[0] = (uint16_t)(a >> 16); d[1] = (uint16_t)a; d[2] = (uint16_t)(b >> 16); d[3] = (uint16_t)b;
        d += 4;
    }
}

/* --- boot: SNDOFF, SCSP, SOUND.BIN from the menu disc --------------------------------------------------------------- */

static void boot_fail(const char *what, int rc)
{
    om_msg_rc(s_boot_msg, sizeof s_boot_msg, "SOUND: ", what, rc);
}

/* M6 Task 19 reclaim: only om_audio_boot (now cold) calls these; -Os them too, else they stop being inlined there
 * and reappear as full-size standalone functions, giving back most of the saving (measured). */
__attribute__((cold, noinline, optimize("Os")))
static int cd_read(uint8_t *dst, uint32_t fad, uint32_t n, uint32_t deadline)
{   /* bounded like bootplay.c's read_now: stream n sectors, a V-blank at a time */
    int rc;
    if ((rc = om_cd_stream_start((int32_t)fad, (int32_t)n)) != 0) return rc;
    for (;;) {
        int32_t c = om_cd_stream_count();
        if (c < 0) return c;
        if ((uint32_t)c >= n) return om_cd_take(dst, (int32_t)n);
        if ((int32_t)(now() - deadline) >= 0) return OM_CD_E_WAIT;
        slSynch();
    }
}

__attribute__((cold, noinline, optimize("Os")))
static int load_bank(const uint8_t *buf, uint32_t size)
{
    om_sndfile f;
    om_snd_clip c;
    uint32_t i, sfx = SFX_BASE;
    int rc;
    if ((rc = om_sndfile_open(&f, buf, size, size)) != 0) return rc;
    for (i = 0; i < f.count; i++) {
        if ((rc = om_sndfile_clip(&f, i, &c)) != 0) return rc;
        if (c.id > OM_SND_CHANT || (c.flags & OM_SND_LOOP)) return OM_E_FIELD;
        if (c.id == OM_SND_CHANT) {
            if (c.len > RING_R + 0x10000u - CHANT_BASE) return OM_E_FULL;
            s_clip_sa[c.id] = CHANT_BASE;
        } else {
            if (sfx + c.len > SFX_END) return OM_E_FULL;
            s_clip_sa[c.id] = sfx;
            sfx += (c.len + 1u) & ~1u;
        }
        copy16(s_clip_sa[c.id], buf + f.data_off + c.off, c.len);
        s_clip[c.id] = c;
        s_have[c.id] = 1;
    }
    return OM_OK;
}

/* M6 Task 19 reclaim: audio.c is HOT (tools/sbuild.py COLD_OS) for om_audio_tick/om_audio_vblank's per-frame cost,
 * but this one function runs exactly once, at boot, before the boot animation (main.c) -- COLD_OS's own stated
 * criterion ("once per boot, launch or diagnostics visit"), just never carved out as its own attribute. The CD
 * read timing it measures (s_boot_vb) is I/O-bound, not sensitive to -Os vs -O2 codegen. */
__attribute__((cold, noinline, optimize("Os")))
int om_audio_boot(uint8_t *work, uint32_t cap, om_disc_file *also)
{
    uint32_t t0, deadline, lba, size, n, i;
    int rc, r4[4];
    /* review-c2-12-14.md Minor: OM_AUDIO_E_NOTTRIED, not OM_E_SHORT -- an early exit below (SNDOFF/CD-busy/PVD/ROOT
     * DIR failure, before the `if (also)` block runs) must never be reported as "file too short" when the file was
     * never attempted at all. */
    if (also) { also->len = 0; also->rc = OM_AUDIO_E_NOTTRIED; }
    s_vps = om_tv_is_pal() ? 50u : 60u;                        /* HWF-10, HWF-11 */
    slIntFunction(om_v2_vblank);                               /* the V-blank count every budget reads */
    t0 = now();
    deadline = t0 + BOOT_VB;
    for (i = 0; i < 30u && slSoundOff() != OK; i++)            /* SMPC SNDOFF through SGL's queue (HWF-38, HWF-40) */
        slSynch();
    if (i == 30u) { boot_fail("SNDOFF NOT ACCEPTED", OM_E_FULL); return OM_E_FULL; }
    slSynch(); slSynch(); slSynch();                           /* issued with SGL's next SMPC transfer */
    s_sndoff = 1;
    scsp_init();
    om_cd_spin_cap(100000u);                                   /* bootplay.c's cap: ms per wedged command, not s */
    while (!om_cd_ready()) {
        if ((int32_t)(now() - deadline) >= 0) { rc = OM_CD_E_CMOK; boot_fail("CD BLOCK BUSY", rc); goto out; }
        slSynch();
    }
    om_cd_reset(r4);
    if ((rc = cd_read(work, 150u + OM_ISO_PVD_LBA, 1, deadline)) != 0) { boot_fail("PVD READ", rc); goto out; }
    if ((rc = om_iso_root(work, &lba, &size)) != 0) { boot_fail("PVD", rc); goto out; }
    n = (size + OM_SECTOR - 1u) / OM_SECTOR;
    if (n > 8u) n = 8u;
    if ((rc = cd_read(work, 150u + lba, n, deadline)) != 0) { boot_fail("ROOT DIR READ", rc); goto out; }
    if (also) {   /* found in the root still in work, read first (it sits before SOUND.BIN on the disc) */
        uint32_t alba, asize;
        int arc = om_iso_find(work, n * OM_SECTOR, also->name, &alba, &asize);
        if (!arc && !asize) arc = OM_E_SHORT;
        if (!arc && (asize + OM_SECTOR - 1u) / OM_SECTOR * OM_SECTOR > also->cap) arc = OM_E_FULL;
        if (!arc) arc = cd_read(also->dst, 150u + alba, (asize + OM_SECTOR - 1u) / OM_SECTOR, deadline);
        also->len = arc ? 0 : asize;
        also->rc = arc;
    }
    if ((rc = om_iso_find(work, n * OM_SECTOR, OM_SOUND_BIN, &lba, &size)) != 0) {
        boot_fail("SOUND.BIN NOT ON THE DISC", rc);
        goto out;
    }
    if (size > cap || size > 64u * OM_SECTOR) { rc = OM_E_FULL; boot_fail("SOUND.BIN TOO BIG", rc); goto out; }
    if ((rc = cd_read(work, 150u + lba, (size + OM_SECTOR - 1u) / OM_SECTOR, deadline)) != 0) {
        boot_fail("SOUND.BIN READ", rc);
        goto out;
    }
    if ((rc = load_bank(work, size)) != 0) boot_fail("SOUND.BIN", rc);
out:
    om_cd_stream_stop();
    om_cd_spin_cap(0);
    memset(work, 0, cap < 64u * OM_SECTOR ? cap : 64u * OM_SECTOR);   /* the animation gets its arena back clean */
    s_boot_vb = now() - t0;                                    /* the boot load's cost in V-blanks (om_audio_boot_vblanks) */
    return rc;
}

const char *om_audio_boot_msg(void) { return s_boot_msg; }

void om_audio_chant(void)
{   /* bootplay.c's OM_BOOTEV_CHANT, right after the slSynch that hands frame 137 to VDP1. The sound starts now; the
     * frame is displayed one frame change later (bootplay.h): no compensation, the chant's own onset is ~10 ms in.
     * review-sound M6: the chant plays whatever Sounds says -- SETTINGS.INI cannot be read before API mode */
    if (!s_inited || !s_have[OM_SND_CHANT]) return;
    play_clip(SL_CHANT, OM_SND_CHANT, 0, 0);
    s_chant_end = now() + om_snd_vblanks(s_clip[OM_SND_CHANT].samples, s_clip[OM_SND_CHANT].oct,
                                         s_clip[OM_SND_CHANT].fns, s_vps) + 2u;
    s_sfx_until = now();                                       /* nothing to duck at boot */
}

/* --- SFX ----------------------------------------------------------------------------------------------------------- */

static void sfx(int id, int semis, uint8_t tl)
{
    if (!s_inited || !s_sfx_on || !s_have[id]) return;
    play_clip(SL_SFX0 + (s_next++ & 3), id, semis, tl);
}

void om_sfx(unsigned id)
{
    switch (id) {
    case OM_SFX_SCROLL: sfx(OM_SND_SCROLL, 0, 0); break;
    case OM_SFX_SCROLL_HELD: if ((s_held ^= 1u) != 0) sfx(OM_SND_SCROLL, 0, OM_TL_HELD); break;
    case OM_SFX_SELECT: sfx(OM_SND_SELECT, 0, 0); break;
    case OM_SFX_BACK: sfx(OM_SND_SELECT, -5, 0); break;
    case OM_SFX_FAV_ADD: sfx(OM_SND_RING, 0, 0); break;
    case OM_SFX_FAV_REMOVE: sfx(OM_SND_RING, -5, 0); break;
    case OM_SFX_SORT: sfx(OM_SND_SCROLL, 4, 0); break;
    case OM_SFX_ERROR:
        if (!s_inited || !s_sfx_on || !s_have[OM_SND_SELECT]) break;
        sfx(OM_SND_SELECT, -7, 0);
        s_err2_at = now() + 7u;                                /* the second, lower tone 117 ms later */
        s_err2 = 1;
        break;
    default: break;
    }
}

/* --- the V-blank hook -------------------------------------------------------------------------------------------- */

#ifdef OMSAT_FAKECARD
static uint8_t s_amin[5] = { 255, 255, 255, 255, 255 }, s_amin_cur = 255, s_amin_i, s_amin_tick;   /* M6 Task 24 */
#endif
void om_audio_vblank(void)
{
    uint32_t t, n, amp;
    om_v2_vblank();
    if (!s_inited) return;
    t = now();
    n = t - s_vbl_last;
    s_vbl_last = t;
    if (n > 8u) n = 8u;
    if (s_err2 && (int32_t)(t - s_err2_at) >= 0) {
        s_err2 = 0;
        play_clip(SL_ERR2, OM_SND_SELECT, -12, 0);
    }
    if (!s_playing) return;
    om_ring_ca(&s_ring, (uint32_t)(*SCSP_MON >> 7) & 0xFu);   /* HWF-51 */
    if (s_vstate == M_PLAY && !s_starving && !om_ring_ahead(&s_ring)) {
        s_starving = 1;                                        /* the last valid block: fade out before it ends */
        s_underruns++;
        om_fade_to(&s_fade, 0, 6);
    }
    if (om_ring_starved(&s_ring)) om_fade_set(&s_fade, 0);     /* never play a block that was not written */
#ifdef OMSAT_FAKECARD
    {   /* the ring's lowest lead, kept per second over the last 5 s, so a slow steady
         * drain that a 95 % rate over 15 s still calls PASS shows (fake diagnostics only, never in --app) */
        uint8_t a = (uint8_t)(om_ring_ahead(&s_ring) > 255u ? 255u : om_ring_ahead(&s_ring));
        if (a < s_amin_cur) s_amin_cur = a;
        if (++s_amin_tick >= 60u) {
            s_amin_tick = 0;
            s_amin[s_amin_i] = s_amin_cur;
            s_amin_i = (uint8_t)((s_amin_i + 1u) % 5u);
            s_amin_cur = 255;
        }
    }
#endif
    amp = om_fade_tick(&s_fade, n);
    s_duck = om_duck_tick(s_duck, (int32_t)(s_sfx_until - t) > 0, n);
    {
        uint16_t tl = om_snd_tl_sum(s_vol_tl, om_snd_amp_tl(amp), s_duck);
        SCSP_SLOT(SL_MUS_L)[W_TL] = tl;
        SCSP_SLOT(SL_MUS_R)[W_TL] = tl;
    }
}

/* --- the music stream (main loop) ------------------------------------------------------------------------------------ */

static void mus_status(const char *what, int rc)
{
    char msg[48];
    om_msg_rc(msg, sizeof msg, "MUSIC: ", what, rc);
    om_folders_status_soft(msg);                               /* never over an error or a launch outcome (I1) */
}

static void mus_close(void)
{
    if (s_fd >= 0) s_close(s_fd);
    s_fd = -1;
}

static void mus_silence(void)
{   /* key the two music slots off (the fade has reached 0 or the stream stopped) */
    s_playing = 0;
    key_off(SL_MUS_L);
    key_off(SL_MUS_R);
}

static uint8_t s_tries;                                       /* om_mus_retry: timed-out tries since the last block */
static uint32_t s_retry_at;                                    /* no SD call before this V-blank (a retry's pause) */
static uint8_t s_soft;                                         /* M_FAILED by OM_MUS_QUIET: om_mus_recover reopens */
static uint32_t s_fail_ok, s_fail_vb;                          /* s_diag.ok and the V-blank at that failure */

static void mus_fail(const char *what, int rc)
{   /* NiGHTS IGR report ("BGM.MUS READ rc -120"): a timeout closes the file and om_audio_tick reopens it 1 s later,
     * where it was (s_seek_due); once, then silence with no message (each timeout can hold the menu ~28 s). Any
     * failure after a timeout is treated the same; other failures are named, as before. */
    int k = om_mus_retry(&s_tries, rc);
    mus_close();
    s_seek_due = 1;
    if (k == OM_MUS_RETRY) { s_retry_at = now() + s_vps; return; }
    mus_silence();
    s_vstate = s_mstate = M_FAILED;
    s_soft = (uint8_t)(k == OM_MUS_QUIET);                     /* hardware 2026-11-27: not silent for good */
    s_fail_ok = s_diag.ok;                                     /* after mus_close's own command */
    s_fail_vb = now();
    if (k == OM_MUS_SHOW) mus_status(what, rc);
}

static uint32_t playing_block(void)
{
    return s_playing ? s_slot_file[om_ring_slot(s_ring.played)] : s_resume;
}

static int mus_open(void)
{
    int rc;
    uint32_t len;
    s_fd = -1;
    if ((rc = om_sd_size(OM_PATH_MUSIC, &s_fsize)) < 0) { mus_fail("BGM.MUS NOT FOUND", rc); return rc; }
    if ((rc = s_open(om_sd_path(OM_PATH_MUSIC), FA_READ | FA_OPEN_EXISTING)) < 0) {
        mus_fail("BGM.MUS OPEN", rc = om_sd_err(rc));
        return rc;
    }
    s_fd = rc;
    if ((rc = s_read(s_fd, s_bounce, CHUNK)) != (int)CHUNK) { mus_fail("BGM.MUS READ", rc < 0 ? om_sd_err(rc) : OM_E_SHORT); return -1; }
    if ((rc = om_mus_open(&s_mus, (const uint8_t *)s_bounce, CHUNK, s_fsize)) != 0) { mus_fail("BGM.MUS", rc); return rc; }
    if (s_resume == 0xFFFFFFFFu) {                             /* the first start: MUSICPOS.TXT, else the file's offset */
        int32_t b = -1;
        mus_close();                                           /* one file at a time for the small read */
        if ((rc = om_sd_read_all(OM_PATH_MUSICPOS, s_bounce, CHUNK - 4u, &len)) == OM_SD_TIMEOUT) {
            mus_fail("MUSICPOS", rc);                          /* a retry, not the saved position overwritten */
            return rc;
        }
        if (!rc)
            b = om_mus_state_parse((const char *)s_bounce, len, s_fsize, s_mus.blocks);
        s_resume = b >= 0 ? (uint32_t)b : s_mus.start_frame >> 12;   /* OM_MUS_BLOCK = 4096 frames */
        s_saved = b >= 0 ? (uint32_t)b : 0xFFFFFFFFu;
        if ((rc = s_open(om_sd_path(OM_PATH_MUSIC), FA_READ | FA_OPEN_EXISTING)) < 0) {
            mus_fail("BGM.MUS OPEN", rc = om_sd_err(rc));
            return rc;
        }
        s_fd = rc;
    }
    if (s_resume >= s_mus.blocks) s_resume = 0;
    s_fblock = s_resume;
    s_seek_due = 1;
    return 0;
}

static int mus_reopen(void)
{   /* after om_audio_sd_pause: the same file again; read_chunk seeks to s_fblock / s_chunk (s_seek_due) */
    int fd = s_open(om_sd_path(OM_PATH_MUSIC), FA_READ | FA_OPEN_EXISTING);
    if (fd < 0) { mus_fail("BGM.MUS REOPEN", om_sd_err(fd)); return fd; }
    s_fd = fd;
    s_paused = 0;
    s_seek_due = 1;
    return 0;
}

static void ring_start(void)
{
    om_ring_reset(&s_ring);
    s_chunk = 0;
    s_seek_due = 1;                                            /* an underrun's re-prime restarts its block at chunk 0 */
    s_starving = 0;
}

static int read_chunk(void)
{
    uint32_t slot = om_ring_slot(s_ring.filled), part = s_chunk & 3u, base = (s_chunk & 4u) ? RING_R : RING_L;
    int r;
    if (s_seek_due) {                                          /* a resume, or the wrap at the end of the file */
        r = s_seek(s_fd, (int)(s_mus.data_off + s_fblock * OM_MUS_BLOCK_BYTES + s_chunk * CHUNK), C_SEEK_SET);
        if (r < 0) { mus_fail("BGM.MUS SEEK", om_sd_err(r)); return -1; }
        s_seek_due = 0;
    }
    r = s_read(s_fd, s_bounce, CHUNK);
    if (r != (int)CHUNK) { mus_fail("BGM.MUS READ", r < 0 ? om_sd_err(r) : OM_E_SHORT); return -1; }
    copy_chunk(base + slot * (OM_MUS_BLOCK * 2u) + part * CHUNK);
    if (!slot && !part)                                        /* data at SA+LEA = data at SA+LSA (SCSP manual, HWF-37) */
        *SNDRAM16(base + RING_CH_BYTES) = (uint16_t)(s_bounce[0] >> 16);
    s_bytes += CHUNK;
    if (++s_chunk == 8u) {
        s_chunk = 0;
        s_tries = 0;
        s_slot_file[slot] = s_fblock;
        s_ring.filled++;
        if (++s_fblock == s_mus.blocks) { s_fblock = 0; s_seek_due = 1; }
    }
    return 0;
}

static void mus_key_on(uint32_t fade_vb)
{
    common_set();                                              /* MEM4MB again after a hand-off rehearsal (fake build) */
    *SCSP_MON = (uint16_t)(SL_MUS_L << 11);
    om_fade_set(&s_fade, 0);
    s_duck = 0;
    slot_setup(SL_MUS_L, RING_L, OM_RING_SLOTS * OM_MUS_BLOCK, CTL_LOOP, s_mus.oct, s_mus.fns, OM_TL_MUTE, MIX(PAN_L));
    slot_setup(SL_MUS_R, RING_R, OM_RING_SLOTS * OM_MUS_BLOCK, CTL_LOOP, s_mus.oct, s_mus.fns, OM_TL_MUTE, MIX(PAN_R));
    SCSP_SLOT(SL_MUS_L)[W_CTL] = (uint16_t)(SCSP_SLOT(SL_MUS_L)[W_CTL] | CTL_KB);
    SCSP_SLOT(SL_MUS_R)[W_CTL] = (uint16_t)(SCSP_SLOT(SL_MUS_R)[W_CTL] | CTL_KB | CTL_KX);   /* both in one KYONEX */
    s_ring.last = 0;
    s_vbl_last = now();
    om_fade_to(&s_fade, 65536u, fade_vb);
    s_vstate = M_PLAY;
    s_playing = 1;
    if (s_music_lost) s_music_lost = 0;                        /* om_audio_music_reloaded: the ring is ours again */
}

void om_audio_tick(void)
{
    uint32_t reads = 0, budget;
    if (!s_inited || (int32_t)(now() - s_retry_at) < 0) return;
    switch (s_mstate) {
    case M_WAIT_CHANT:                                         /* the music region holds the chant until it ends */
        if ((int32_t)(now() - s_chant_end) < 0) break;
        s_mstate = M_OPEN;
        /* fall through */
    case M_OPEN:
        if (!s_want) { s_mstate = M_OFF; break; }
        if (s_fd < 0 && mus_open() != 0) break;
        if (s_fd >= 0 && s_mstate != M_FAILED) {
            mus_silence();
            key_off(SL_CHANT);                                 /* review-sound M1: off before the ring overwrites it */
            ring_start();
            s_mstate = M_PRIME;
            if (!s_vb0) s_vb0 = now();
        }
        break;
    case M_PRIME:
        if (s_fd < 0 && mus_reopen() != 0) return;
        while (reads < 4u && om_ring_can_write(&s_ring) && s_ring.filled < PRIME_BLOCKS) {
            if (read_chunk() != 0) return;
            reads++;
        }
        if (s_ring.filled >= PRIME_BLOCKS) {
            mus_key_on(s_fade_in_long ? s_vps * 3u : s_vps / 4u);   /* 3 s from silence, 250 ms after an underrun */
            s_mstate = M_PLAY;
        }
        break;
    case M_PLAY:
        if (s_starving && om_fade_done(&s_fade) && !s_fade.amp) {   /* ran dry: silent now, re-prime where we are */
            mus_silence();
            ring_start();
            s_fade_in_long = 0;
            s_mstate = M_PRIME;
            break;
        }
        if (s_fd < 0 && om_ring_can_write(&s_ring) && mus_reopen() != 0) return;
        budget = om_ring_ahead(&s_ring) < 2u ? 3u : om_ring_ahead(&s_ring) < 4u ? 2u : 1u;
        while (reads < budget && om_ring_can_write(&s_ring)) {
            if (read_chunk() != 0) return;
            reads++;
        }
        s_resume = playing_block();
        break;
    case M_FAILED:                                             /* a quiet failure: back once the Satiator answers */
        if (s_soft && s_want && om_mus_recover(s_fail_ok, s_diag.ok, now() - s_fail_vb, s_vps * 2u)) {
            s_soft = 0;
            s_tries = 0;
            s_fade_in_long = 1;
            s_mstate = M_OPEN;                                 /* mus_open, ring_start, M_PRIME (s_resume kept) */
        }
        break;
    case M_STOPPING:                                           /* music switched off: fade, then close */
        if (!om_fade_done(&s_fade)) break;
        s_resume = playing_block();
        mus_silence();
        mus_close();
        s_vstate = s_mstate = M_OFF;
        break;
    default:
        break;
    }
#ifdef OMSAT_FAKECARD
    {   /* NBG0 row 23: the stream's state, blocks played / filled, underruns, KB/s read, blocks/s x 100 played */
        static uint32_t tk;
        if (++tk % 30u == 0 && om_boot_diag()) {   /* only with `fakecard --diag` (M3-R40/M3-R45) */
            char line[41], v[12];
            uint32_t dt = now() - s_vb0;
            strcpy(line, "MUS S"); strcat(line, om_itoa(v, s_mstate));
            strcat(line, " P"); strcat(line, om_itoa(v, (int)s_ring.played));
            strcat(line, " F"); strcat(line, om_itoa(v, (int)s_ring.filled));
            strcat(line, " U"); strcat(line, om_itoa(v, (int)s_underruns));
            strcat(line, " K"); strcat(line, om_itoa(v, dt ? (int)(s_bytes * s_vps / 1024u / dt) : 0));
            strcat(line, " B"); strcat(line, om_itoa(v, (int)s_resume));
            om_text_put(0, 23, line, 40);
        }
    }
#endif
}

static void want_music(int music)
{
    if (music && !s_want) {
        s_want = 1;
        if (s_mstate == M_OFF || s_mstate == M_STOPPING || s_mstate == M_FAILED) {   /* M_FAILED: Music on retries */
            if (s_mstate == M_STOPPING) { s_resume = playing_block(); mus_silence(); mus_close(); }
            s_fade_in_long = 1;
            s_tries = 0;
            s_mstate = M_OPEN;
        }
    } else if (!music && s_want) {
        s_want = 0;
        if (s_mstate == M_PRIME || s_mstate == M_OPEN || s_mstate == M_WAIT_CHANT) { mus_close(); s_vstate = s_mstate = M_OFF; }
        else if (s_mstate == M_PLAY) { s_vstate = s_mstate = M_STOPPING; om_fade_to(&s_fade, 0, s_vps / 4u); }
    }
}

void om_audio_settings(int music, uint32_t music_vol, int sfx_on)
{
    s_vol_tl = om_snd_vol_tl(music_vol);
    s_sfx_on = (uint8_t)(sfx_on != 0);
    if (s_started) want_music(music);
}

void om_audio_menu_start(int music, uint32_t music_vol, int sfx_on)
{
    if (s_started) return;
    s_started = 1;
    s_resume = 0xFFFFFFFFu;
    s_fade_in_long = 1;
    s_mstate = M_WAIT_CHANT;
    s_want = (uint8_t)(music != 0);
    om_audio_settings(music, music_vol, sfx_on);
    if (!s_want) s_mstate = M_OFF;
    if (s_boot_msg[0]) om_folders_status(s_boot_msg);          /* a boot failure, named once the menu can show it */
}

void om_audio_save_state(void)
{
    char t[48];
    uint32_t n, b;
    if (!s_fsize || s_resume == 0xFFFFFFFFu) return;
    b = s_mstate == M_PLAY ? playing_block() : s_resume;
    if (b == s_saved) return;
    n = om_mus_state_fmt(t, sizeof t, s_fsize, b);
    if (om_sd_write_buf(OM_PATH_MUSICPOS, t, n) == 0) s_saved = b;
}

void om_audio_handoff(void)
{   /* the game must not inherit a keyed slot or our common word; the 68000 stays held */
    uint32_t i;
    mus_close();
    s_vstate = 0;
    s_playing = 0;
    s_err2 = 0;
    s_mstate = M_OUT;
    if (!s_sndoff) return;                                     /* review-final-m3 I1: also run from the error screens'
                                                                * exits, which can come before om_audio_boot -- no slot
                                                                * of ours sounds then, and no SCSP write is made with the
                                                                * 68000 not yet held (ledger D3) */
    for (i = 0; i < 32u; i++) SCSP_SLOT(i)[W_CTL] = (uint16_t)(SCSP_SLOT(i)[W_CTL] & ~CTL_KB);
    key_exec(0);
    common_set();                                              /* write-only: see common_set */
}

/* --- the loading screen's events (Task 13c hooks) ----------------------------------------------------------------- */

void om_audio_event(unsigned ev)
{
    if (ev >= OM_AEV_COUNT) return;
    s_count[ev]++;
    switch (ev) {
    case OM_AEV_SELECT:
        om_sfx(OM_SFX_SELECT);
        break;
    case OM_AEV_MUSIC_FADE_OUT:                                /* 500 ms, and no more SD reads: the launch owns the card */
        if (s_mstate == M_PLAY || s_mstate == M_STOPPING) s_resume = playing_block();
        mus_close();
        if (s_playing) { s_vstate = M_OUT; om_fade_to(&s_fade, 0, s_vps / 2u); }
        s_mstate = M_OUT;
        break;
    case OM_AEV_MUSIC_RESUME:
        s_vstate = M_OFF;
        mus_silence();
        if (s_mstate == M_FAILED) break;                       /* I1: a failure stays latched until Music off/on (a
                                                                * quiet one: om_audio_tick's M_FAILED case) */
        s_mstate = M_OFF;
        if (s_want) { s_fade_in_long = 1; s_mstate = M_OPEN; }
        break;
    case OM_AEV_SCREAM:
        if (s_inited && s_sfx_on && s_have[OM_SND_SCREAM]) {
            play_clip(SL_SFX0 + (s_next++ & 3), OM_SND_SCREAM, 0, 0);
            s_scream_end = now() + om_snd_vblanks(s_clip[OM_SND_SCREAM].samples, s_clip[OM_SND_SCREAM].oct,
                                                  s_clip[OM_SND_SCREAM].fns, s_vps);
        }
        break;
    default:
        break;
    }
}

uint32_t om_audio_events(unsigned ev) { return ev < OM_AEV_COUNT ? s_count[ev] : 0; }

uint32_t om_audio_scream_len(void)
{   /* final M3 round: the loading screen times the scream to end its minimum time */
    const om_snd_clip *c = &s_clip[OM_SND_SCREAM];
    if (!s_inited || !s_sfx_on || !s_have[OM_SND_SCREAM]) return 0;
    return om_snd_vblanks(c->samples, c->oct, c->fns, s_vps);
}

uint32_t om_audio_scream_remaining(void)
{
    int32_t left = (int32_t)(s_scream_end - now());
    return left > 0 ? (uint32_t)left : 0u;
}

uint32_t om_audio_underruns(void) { return s_underruns; }

void om_audio_tick_low(void)
{   /* between a long frame's other SD reads, a refill only when the ring runs low
     * (under 3 blocks, 0.56 s, ahead), so the music's margin no longer rests on the frame rate, and a full ring does
     * not take the reads' SD time */
    if (s_inited && s_mstate == M_PLAY && om_ring_ahead(&s_ring) >= 3u) return;
    om_audio_tick();
}
#ifdef OMSAT_FAKECARD
uint32_t om_audio_stream_bytes(void) { return s_bytes; }   /* the panel's refill-rate check */
uint32_t om_audio_min_ahead5(void)
{
    uint32_t i, m = s_amin_cur;
    for (i = 0; i < 5u; i++) if (s_amin[i] < m) m = s_amin[i];
    return m;
}
#endif
uint32_t om_audio_boot_vblanks(void) { return s_boot_vb; }

void om_audio_sd_pause(void)
{
    if (s_fd < 0) return;
    mus_close();
    s_paused = 1;                                              /* om_audio_tick reopens and seeks (read_chunk) */
    s_seek_due = 1;
}

int om_audio_music_faded(void)
{
    if (s_playing && (s_fade.amp || !om_fade_done(&s_fade))) return 0;
    if (s_playing) mus_silence();
    return 1;
}

/* --- the loading screen's sound-RAM borrow ----------------------------------------------- */

static int borrowed(uint32_t a, uint32_t len)
{   /* [a, a + len) meets 0x10000-0x2FFFF or 0x40000-0x77FFF */
    return (a < 0x30000u && a + len > 0x10000u) || (a < 0x78000u && a + len > 0x40000u);
}

int om_audio_sndram_borrow(void)
{
    uint32_t n, w, rbase, rlen;
    int mpro = 0;
    if (!s_sndoff) return OM_AUDIO_E_68K;                      /* never write 68000 RAM with the 68000 running */
    if (!s_inited) return OM_AUDIO_E_68K;
    w = RBP_PARK;                                              /* RBL/RBP are write-only (SCSP manual :4549, :4564):
                                                                * the value scsp_init wrote, not a read-back */
    rbase = (w & 0x7Fu) << 13;                                 /* RBP x 8 KB */
    rlen = 0x4000u << ((w >> 7) & 3u);                         /* RBL: 16 KB << code */
    for (n = 0; n < 512u && !mpro; n++)                        /* any non-zero step: a microprogram */
        mpro = SCSP_MPRO[n] != 0;
    if (mpro && borrowed(rbase, rlen))                         /* review-sound M8: dead by construction now (RBP_PARK */
        return OM_AUDIO_E_DSP;                                 /* is never borrowed), kept as a guard for future edits */
    s_playing = 0;
    for (n = 0; n < 32u; n++) {                                /* key off what reads a borrowed byte: [SA, SA + LEA) */
        volatile uint16_t *s = SCSP_SLOT(n);
        uint16_t w0 = s[W_CTL];
        uint32_t sa = ((uint32_t)(w0 & 0xFu) << 16) | s[W_SA];
        uint32_t len = ((uint32_t)s[W_LEA] + 1u) << ((w0 & CTL_PCM8) ? 0 : 1);
        if (borrowed(sa, len))
            s[W_CTL] = (uint16_t)((w0 & ~CTL_KB) | CTL_KX);
    }
    common_set();                                              /* MEM4MB = 1 (the whole 512 KB), written whole */
    s_music_lost = 1;
    return 0;
}

int om_audio_music_lost(void) { return s_music_lost; }
int om_audio_sndram_free_ok(void) { return s_sndoff && s_inited; }
void om_audio_music_reloaded(void) { s_music_lost = 0; }
