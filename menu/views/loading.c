/* menu/views/loading.c -- see loading.h. M3 Task 13c. BSD-3-Clause.
 *
 * Background: LOADBG.BIN (SPEC §5.11), since Task 13c round 2 (user decision) 320x224 at 30 fps, 507,509 B, shown 1:1
 * on NBG1, the menu's 256-color bitmap (B0, palette 1 = CRAM 256-511, HWF-28/30; a 2x file would use NBG1's 0.5
 * coordinate increment, HWF-46). Why NBG1 and not a VDP1 sprite: no VDP1 fill time and no VDP1 VRAM, and it is the
 * mockup's own design. The file is read from the moment A is pressed (the menu's fade to black, then the loading screen),
 * into the card-scan work area and the sound RAM idle during loading (see "preload"). The loop plays as the file comes
 * in (2026-11-20, user: "playing immediately"): a frame is shown once its entry has been read, so on a card slower than
 * the loop's ~240 KB/s the first pass runs at the read rate and the loop reaches its 30 fps once the file is all in.
 * NBG1 is double-buffered and the bulk moves are SCU DMA (see bg_flip_step / bg_work); the CPU only decodes, under
 * per-V-blank deadlines, so the screen never misses a V-blank for it and never shows a partial frame. The darkening scrim is baked into the frames (tools/omsat/loadbg.py).
 * The text's anti-aliasing is pre-blended against the average background under each box, computed from the keyframe
 * (VDP1 cannot blend with VDP2, HWF-32, decision D1).
 *
 * The cover is a VDP1 distorted sprite with the mockup's tilt (HWF-47); its drop shadow is an NBG2 soft rectangle (color
 * calculation over the animated NBG1, a real translucent shadow, HWF-31); its reflection is translucent too: palette-
 * format bands blended by VDP2 sprite color calculation (HWF-48).
 *
 * Launch steps (launch.h): the dots fill when the work they name has really completed, never ahead of it. The
 * LAUNCHER.BIN body is read 2 KB at a time within a per-frame time budget, so the screen keeps every V-blank. */
#include <string.h>
#include "sgl.h"
#include "anim.h"
#include "art.h"
#include "audio.h"
#include "fade.h"
#include "layout.h"
#include "loadbg.h"
#include "loading.h"
#include "logo.h"
#include "marquee.h"
#include "msgfmt.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "pad.h"
#include "safearea.h"
#include "satiator.h"
#include "sd.h"
#include "text.h"
#include "uiline.h"
#include "vdp2theme.h"

#include "boottime.h"

#ifdef OMSAT_FAKECARD
#include "fakesat.h"                 /* the --pglong-script marker */
#endif
#ifdef OMSAT_FAKECARD                /* integration (M3-R40/M3-R45): only with `fakecard --diag`, like 13d's lines */
#define LD_VERBOSE om_boot_diag()    /* the fake build prints the frame counter and the step events on NBG0 */
#else
#define LD_VERBOSE 0
#endif

#define FADE_MASK   (NBG1ON | NBG2ON | SPRON | BACKON)   /* NBG0 is left alone: the fake build's counters stay up */
#define FADE_MS     250u
#define CHUNK       2048u            /* S_MAXBUF: one s_read */
#define SD_END_US   9000             /* SD work runs after the upload and starts no read past this much of the frame
                                      * (16,683 us): one 2 KB read may run past it; staging, decode, drawing follow */
#define PRELOAD_US  12500            /* the preload's end while the loop stands still (a step starts only if it fits) */
#define SD_OP_US    3000             /* a step that is not chunked (LAUNCHER.BIN's stat + open, the RECENT.TXT write)
                                      * starts only before SD_END_US - SD_OP_US, else it waits for the next frame ... */
#define OP_WAIT     8u               /* ... for at most 8 frames: then it runs anyway (a missed frame, never a stall) */
#define SCREAM_CAP  120u             /* V-blanks: the scream wait is bounded whatever the audio side reports */
#define NOTICE_VBL  60u              /* a non-fatal notice (RECENT.TXT) holds the launch for one second, as before */
#define BG_NOTICE   180u             /* a background notice replaces the cancel hint for three seconds */
#define REFL_ROWS_T 69u              /* the reflection: the cover's last 69 rows (tall 128x200; mockup 60 of 174) */
#define REFL_ROWS_S 55u              /* square 160x160 */
#define REFL_BANDS  4                /* the reflection: 4 strips of 8 rows below the cover (mockup 38 rows, 198-236) */
#define REFL_BAND   8
#define REFL_H      (REFL_BANDS * REFL_BAND)
#define OM_VRAM_REFL OM_VRAM_BOOTANIM    /* its 8 bpp texture, at most 160 x 32 = 5,120 B (boot-only range, free here) */
#define REFL_CRAM   768u             /* its 3-3-2 palette: CRAM 768-1023 (the boot animation's, free here) */
#define REFL_DC_HI  0x0300u          /* color bank DC10-8 = 3 -> CRAM 768 + dot (HWF-44) */
#define REFL_PR     0x2000u          /* sprite type 3 PR1-0 = 01 -> priority register 1 (HWF-48) */
#define REFL_PRIO   5                /* its priority: above NBG1 (4); equal to NBG2, where sprites win (Table 11.1) */

enum {   /* Z bands (menu/ui/v1.h): bigger is further back */
    ZL_REFLCLIP = 200, ZL_REFL = 199, ZL_COVER = 197, ZL_EDGE = 196, ZL_NOART = 195, ZL_DECO = 190,
    ZL_T1 = 180, ZL_T2 = 178, ZL_M1 = 176, ZL_M2 = 174, ZL_TXT = 170, ZL_PILL = 160, ZL_STATUS = 158
};
enum { ST_WAIT_BG, ST_BEGIN, ST_OPEN, ST_READ, ST_LOADED, ST_SCREAM, ST_SCREAMING, ST_COMMIT, ST_NOTICE, ST_DONE,
       ST_FAILED };   /* from ST_COMMIT on the launch is committed: no cancel */
static const char *const k_step[3] = { "Reading disc image", "Preparing launcher", "Starting game" };

/* --- preload ----------------------------------------------------------------------------------------------------- */
/* Task 13c rounds 2-3: the background is 320x224 at 30 fps, 507,509 B, kept in two places (SPEC §5.11): the card-scan
 * work area holds the frame on screen, a 16 KB staging buffer, the SCU DMA tables and the head of the file (the
 * keyframe is decoded there in place); the rest goes to sound RAM idle during loading (menu/app/audio.c
 * om_audio_sndram_borrow: the music region 0x10000-0x2FFFF, then the unclaimed 0x40000-0x77FFF; ledger D3), borrowed
 * only once the head is in and the music has faded. The preload writes sound RAM with 16-bit accesses; a loop entry
 * kept there is brought into the staging buffer by SCU DMA before it is decoded. */
#define STAGE       16384u           /* tools/omsat/loadbg.py STAGE: no loop entry kept in sound RAM is larger (+8: an
                                      * aligned DMA's slack); entries in HWRAM are decoded in place, any size */
#define DMA_TBL     (12u * OM_LBG_MAX_H + 24u)   /* 2,712 B: the row table (its write addresses retargeted per
                                                * transfer), the 2-entry staging table */
#define TBL_ALIGN   4096u            /* TechBulletin SCU No. 25: an indirect table starts on a power-of-two boundary
                                      * >= its size -- 2,688 B -> 4,096; the staging table (24 B) then lands on
                                      * tbl + 2,688 = 84 x 32, a 32-byte boundary (HWF-50) */
/* the sound-RAM part's two regions: loadbg.h OM_LBG_SR1 (0x40000-0x77FFF, free: filled first, even by a panel's early
 * read while the music plays) and OM_LBG_SR2 (the music region 0x10000-0x2FFFF, after the borrow) -- M6 Task 18 */
#define FADE_VBL    36u              /* at most 600 ms after the launch for the music's fade */
#define MIN_UP_NTSC 240u             /* user, 2026-11-20 (was 3.0 s): the screen stays up at least 4.0 s, from fully faded up to */
#define MIN_UP_PAL  200u             /* the final fade; the dots pace to it, the scream ends it */

static struct {
    uint8_t *arena, *file, *pix, *stage;
    uint32_t *tbl, *stbl;            /* SCU DMA indirect tables: frame rows -> a bitmap; the staging copy */
    uint32_t cap, size, got, hw, w, h, t_vbl0, t_vbl;
    uint8_t *bounce;                 /* 2 KB on the way to sound RAM: never the staging buffer, which playback uses */
    int32_t step_us;                 /* the last read step's measured time (an SD read + its sound-RAM copy) */
    int fd, rc;
    uint8_t phase;                   /* 0 idle, 1 reading, 2 done, 3 failed */
    uint8_t borrowed;                /* the sound-RAM regions have been borrowed (om_audio_sndram_borrow) */
    uint8_t early;                   /* a panel's read ahead (the HWRAM part only), not yet a launch */
    uint32_t early_got;              /* the bytes an adopted early read had brought in by the launch (diagnostics) */
    uint32_t black_vbl;              /* V-blanks the rest of an adopted read took while black (diagnostics) */
    uint32_t lw_sum;                 /* the LW region's bytes so far, hashed as read (lw_hash) */
    uint32_t lw_lost;                /* times a check found them overwritten (diagnostics) */
} s_pre;

/* the LW region (loadbg.h): OM_LW_LBG, written and checked through the cache-through alias (HWF-12),
 * decoded through the cached one after its lines are purged */
#define LW_CT      ((uint8_t *)(uintptr_t)OM_CT(OM_LW_LBG))
#define LW_C       ((const uint8_t *)(uintptr_t)OM_LW_LBG)
#define LW_BASIS   2166136261u       /* FNV-1a offset basis; the prime is 16777619 */
#define BLACK_CAP  18u               /* an adopted read's catch-up while black: 0.3 s at most, then the
                                      * rest arrives during the loading screen, as with no panel (a slow card) */
typedef char lw_lbg_fits[(OM_LW_LBG_MAX == OM_LBG_LW_LEN && OM_LW_LBG + OM_LW_LBG_MAX == OM_LW_HANDOFF &&
                          OM_LW_LBG >= OM_LW_STAGE && OM_LW_LBG + OM_LW_LBG_MAX <= OM_LW_STAGE + OM_LW_STAGE_MAX) ? 1 : -1];

static int32_t us_since(uint16_t t0) { return (int32_t)om_ut_frc_us((uint16_t)(om_ut_frc() - t0)); }

static uint16_t to_ticks(uint32_t us)
{   /* a deadline in FRT ticks, so a hot loop compares counters instead of dividing (SH-2: no hardware divide) */
    uint32_t per1000 = om_ut_frc_us(1000u), t = us * 1000u / (per1000 ? per1000 : 1u);
    return (uint16_t)(t > 0xFFFFu ? 0xFFFFu : t);
}

static uint32_t lw_hash(uint32_t h, const uint8_t *p, uint32_t n)
{   /* FNV-1a over 32-bit words (p 4-aligned), then the tail's bytes: only the file's last read has a tail */
    const uint32_t *w = (const uint32_t *)(const void *)p;
    uint32_t i;
    for (i = 0; i < n / 4u; i++) h = (h ^ w[i]) * 16777619u;
    for (i = n & ~3u; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t lw_have(void)
{   /* bytes of the LW region read so far */
    uint32_t v0 = s_pre.hw + OM_LBG_LW_V0;
    if (!s_pre.size || s_pre.got <= v0) return 0;
    return s_pre.got - v0 > OM_LBG_LW_LEN ? OM_LBG_LW_LEN : s_pre.got - v0;
}

static void lw_rewind(void)
{   /* the LW bytes are gone: the read goes on from LW's start (the file reopens at the next step) */
    s_pre.got = s_pre.hw + OM_LBG_LW_V0;
    s_pre.lw_sum = LW_BASIS;
    if (s_pre.phase == 2) s_pre.phase = 1;
}

static __attribute__((cold, noinline, optimize("Os"))) void lw_verify(void)
{   /* OM_LW_STAGE's other tenants (the art job, the list, Settings' save, the saver) may have written over
     * the LW region since it was read: hash it again (~92 KB of LWRAM) and rewind on a mismatch */
    uint32_t n = lw_have();
    if (n && lw_hash(LW_BASIS, LW_CT, n) != s_pre.lw_sum) {
        lw_rewind();
        s_pre.lw_lost++;
    }
}

static void pre_fail(int rc)
{
    if (s_pre.fd >= 0) s_close(s_pre.fd);
    s_pre.fd = -1;
    s_pre.rc = rc;
    s_pre.phase = 3;
}

static int early_held(uint8_t *arena, uint32_t cap)
{
    return s_pre.early && (s_pre.phase == 1 || s_pre.phase == 2) && s_pre.arena == arena && s_pre.cap == cap;
}

void om_loading_preload_begin(uint8_t *arena, uint32_t cap)
{
    if (early_held(arena, cap)) {
        lw_verify();                 /* the LW bytes, unless overwritten since */
        s_pre.early = 0;             /* adopt the panel's read; it goes on from where it stopped */
        s_pre.early_got = s_pre.got;
        s_pre.t_vbl0 = om_v2_vblanks();
        return;
    }
    memset(&s_pre, 0, sizeof s_pre);
    s_pre.arena = arena;
    s_pre.cap = cap;
    s_pre.fd = -1;
    s_pre.phase = 1;
    s_pre.lw_sum = LW_BASIS;
    s_pre.t_vbl0 = om_v2_vblanks();
}

void om_loading_preload_early(uint8_t *arena, uint32_t cap)
{
    if (early_held(arena, cap)) {    /* a paused early read: preload_step reopens the file */
        lw_verify();
        return;
    }
    om_loading_preload_begin(arena, cap);
    s_pre.early = 1;
}

void om_loading_preload_lw_drop(void)
{   /* the stage is needed again (the selection moved): the LW bytes are given up */
    if (s_pre.early && lw_have()) lw_rewind();
}

int om_loading_preload_lw_held(void) { return s_pre.early && (s_pre.phase == 1 || s_pre.phase == 2) && lw_have(); }

void om_loading_preload_pause(void)
{
    if (s_pre.fd >= 0) s_close(s_pre.fd);
    s_pre.fd = -1;
}

void om_loading_preload_drop(void)
{
    om_loading_preload_pause();
    s_pre.phase = 0;
    s_pre.early = 0;
}

uint32_t om_loading_preload_have(void) { return s_pre.got; }

static volatile uint16_t *sr_word(uint32_t v)
{   /* byte v (even) of the sound-RAM part of the file -> its 16-bit word */
    uint32_t o = om_lbg_sr_off(v);
    return (volatile uint16_t *)(OM_SNDRAM + o);
}

static void sr_put(uint32_t v, const uint8_t *src, uint32_t n)
{   /* n bytes (v, n even) into sound RAM, 16-bit writes (HWF-33) */
    uint32_t k;
    for (k = 0; k < n; k += 2)
        *sr_word(v + k) = (uint16_t)((src[k] << 8) | src[k + 1]);
}

static uint32_t early_end(void)
{   /* eye-animation ruling: a panel's early read stops before the music region -- the HWRAM part
     * and the free sound-RAM region (when the sound side is set up), never the music's own bytes */
    return s_pre.hw + (om_audio_sndram_free_ok() ? OM_LBG_SR2_V0 : 0u);   /* SR1, then LWRAM */
}

static void preload_step(int32_t budget_us, uint16_t t0)
{
    int steps = 0;
    if (s_pre.phase != 1) return;
    if (s_pre.early && s_pre.size && s_pre.got >= early_end()) return;   /* the early part is in */
    if (s_pre.fd < 0 && s_pre.size) {                          /* a paused read goes on */
        if ((s_pre.fd = om_sd_open(OM_PATH_LOADBG)) < 0) { int rc = s_pre.fd; s_pre.fd = -1; pre_fail(rc); return; }
    } else if (s_pre.fd < 0) {
        static uint32_t head[20];                               /* 80 B, 4-aligned (om_sd_read_at) */
        const uint8_t *hb = (const uint8_t *)head;
        uint32_t size, pixb;
        int rc;
        if ((rc = om_sd_size(OM_PATH_LOADBG, &size)) < 0) { pre_fail(rc); return; }
        if (size < 592u) { pre_fail(OM_E_SHORT); return; }
        if ((s_pre.fd = om_sd_open(OM_PATH_LOADBG)) < 0) { rc = s_pre.fd; s_pre.fd = -1; pre_fail(rc); return; }
        if ((rc = om_sd_read_at(s_pre.fd, 0, head, sizeof head)) != 0) { pre_fail(rc); return; }
        s_pre.w = om_be16(hb + 64);
        s_pre.h = om_be16(hb + 66);
        if (!s_pre.w || !s_pre.h || s_pre.w > OM_LBG_MAX_W || s_pre.h > OM_LBG_MAX_H) { pre_fail(OM_LBG_E_INFO); return; }
        pixb = (s_pre.w * s_pre.h + 3u) & ~3u;
        if (pixb + STAGE + 8u + TBL_ALIGN + DMA_TBL + 8u >= s_pre.cap) { pre_fail(OM_E_RANGE); return; }
        s_pre.pix = s_pre.arena;
        s_pre.stage = s_pre.arena + pixb;
        {   /* the tables on a 4,096-byte boundary inside the arena (No. 25); the file's HWRAM part is the worst case
             * (a full 4,096 B of alignment slack), so it is the same for every build: tools/omsat/loadbg.py capacity() */
            uintptr_t t = ((uintptr_t)(s_pre.stage + STAGE + 8u) + TBL_ALIGN - 1u) & ~(uintptr_t)(TBL_ALIGN - 1u);
            s_pre.tbl = (uint32_t *)t;
            s_pre.stbl = s_pre.tbl + 3u * OM_LBG_MAX_H;
            if (((uintptr_t)s_pre.tbl & (TBL_ALIGN - 1u)) || ((uintptr_t)s_pre.stbl & 31u)) {
                pre_fail(OM_LBG_E_INFO);                           /* never: the asserts of the rule */
                return;
            }
        }
        s_pre.file = (uint8_t *)(s_pre.stbl + 6);
        s_pre.hw = (s_pre.cap - pixb - STAGE - 8u - TBL_ALIGN - DMA_TBL - 8u) & ~3u;   /* the file's HWRAM part */
        {   /* the bounce buffer, at no HWRAM cost: the tables' alignment gap (stage end .. tbl) and the arena's unused
             * tail (file + hw .. arena + cap) add up to TBL_ALIGN + 8 or more bytes, so one of them holds CHUNK */
            uint8_t *gap = s_pre.stage + STAGE + 8u, *tail = s_pre.file + s_pre.hw;
            s_pre.bounce = (uint32_t)((uint8_t *)s_pre.tbl - gap) >= CHUNK ? gap
                         : (uint32_t)(s_pre.arena + s_pre.cap - tail) >= CHUNK ? tail : 0;
            if (!s_pre.bounce || ((uintptr_t)s_pre.bounce & 3u)) { pre_fail(OM_LBG_E_INFO); return; }   /* never */
        }
        if (size > s_pre.hw && size - s_pre.hw > OM_LBG_SR2_V0 + OM_LBG_SR2_LEN) { pre_fail(OM_E_RANGE); return; }
        s_pre.size = size;
    }
    for (;;) {   /* a step starts only if it ends by budget_us, on its measured time (one 2 KB s_read: ~8 ms on the
                  * user's card, 245-296 KB/s; floors 2.5 ms, 5 ms with the sound-RAM copy); the first step of a call
                  * also starts when it still ends inside the frame (15.5 ms: the menu's fade-out, after its drawing),
                  * or always when it is early (< 2 ms), so a card slower than the budget still makes progress */
        uint32_t n = s_pre.size - s_pre.got;
        int32_t est = s_pre.got < s_pre.hw ? 2500 : 5000, now = us_since(t0);
        uint16_t ts;
        int rc;
        if (s_pre.got >= s_pre.size) break;
        if (s_pre.early && s_pre.got >= early_end()) break;    /* not the music's region before the launch */
        if (s_pre.step_us > est) est = s_pre.step_us;
        if (now + est >= budget_us && (steps || (now >= 2000 && now + est >= 15500))) break;
        ts = om_ut_frc();
        if (n > CHUNK) n = CHUNK;
        if (s_pre.got < s_pre.hw) {                               /* straight into the HWRAM part */
            if (n > s_pre.hw - s_pre.got) n = s_pre.hw - s_pre.got;
            if ((rc = om_sd_read_at(s_pre.fd, s_pre.got, s_pre.file + s_pre.got, n)) != 0) { pre_fail(rc); return; }
        } else if (om_lbg_lw(s_pre.got - s_pre.hw)) {             /* into LWRAM, any time; hashed */
            uint32_t v = s_pre.got - s_pre.hw;
            uint8_t *d = LW_CT + (v - OM_LBG_LW_V0);
            if (n > OM_LBG_SR2_V0 - v) n = OM_LBG_SR2_V0 - v;   /* never across the regions */
            if ((rc = om_sd_read_at(s_pre.fd, s_pre.got, d, n)) != 0) { pre_fail(rc); return; }
            s_pre.lw_sum = lw_hash(s_pre.lw_sum, d, n);
        } else {                                                  /* via the staging buffer into sound RAM */
            uint32_t v = s_pre.got - s_pre.hw;
            if (!s_pre.borrowed && !om_audio_sndram_free_ok()) {   /* never write sound RAM with the 68000 running */
                if ((rc = om_audio_sndram_borrow()) != 0) { pre_fail(rc); return; }
            }
            if (!s_pre.borrowed && om_lbg_sr_music(v)) {
                                     /* the music region only once the music has faded
                                      * (om_audio_music_faded) or FADE_VBL V-blanks have passed, whichever is first */
                if (!om_audio_music_faded() && om_v2_vblanks() - s_pre.t_vbl0 < FADE_VBL) return;
                if ((rc = om_audio_sndram_borrow()) != 0) { pre_fail(rc); return; }
                s_pre.borrowed = 1;
            }
            if (n > om_lbg_region_end(v) - v) n = om_lbg_region_end(v) - v;   /* never across the regions */
            if ((rc = om_sd_read_at(s_pre.fd, s_pre.got, s_pre.bounce, n)) != 0) { pre_fail(rc); return; }
            if (n & 1u) s_pre.bounce[n] = 0;
            sr_put(v, s_pre.bounce, (n + 1u) & ~1u);
        }
        s_pre.got += n;
        s_pre.step_us = us_since(ts);
        steps++;
    }
    if (s_pre.got == s_pre.size) {
        s_close(s_pre.fd);
        s_pre.fd = -1;
        s_pre.phase = 2;
        s_pre.t_vbl = om_v2_vblanks() - s_pre.t_vbl0;
    }
}

void om_loading_preload_tick(uint16_t t0, int32_t until_us) { preload_step(until_us, t0); }

void om_loading_preload_tick_vb(uint16_t t0, uint32_t vb0, uint32_t nvbl)   /* every static-panel frame: -O2 */
{   /* a static panel reads whole 2 KB steps until nvbl V-blanks after its iteration began (t0, vb0);
     * frames drop, input is polled between calls. The time is summed step by step (each < 20 ms: the 16-bit FRT may
     * wrap past that) and never taken below the V-blanks seen; a step starts only if it ends 0.7 ms before the last */
    uint32_t end = nvbl * 16683u - 700u, el = (uint32_t)us_since(t0);
    uint16_t prev = om_ut_frc();
    while (s_pre.phase == 1) {
        uint32_t g = s_pre.got, vbt = (om_v2_vblanks() - vb0) * 16683u;
        uint32_t est = (uint32_t)(s_pre.step_us > 2500 ? s_pre.step_us : 2500);   /* as preload_step's own floor */
        uint16_t now = om_ut_frc();
        el += (uint32_t)om_ut_frc_us((uint16_t)(now - prev));
        prev = now;
        if (el < vbt) el = vbt;
        if (el + est > end) break;
        preload_step(15000, om_ut_frc());
        if (s_pre.got == g) break;                        /* paused, failed, or the early part is in */
        om_audio_tick_low();     /* the music's refill between steps, not once a long frame */
        if (om_pad_peek()) break;   /* a press is latched for the next poll; go and handle it */
#ifdef OMSAT_FAKECARD
        {   /* fakecard --pglong-script: an SD stall of 9 V-blanks every 50th step (the music must ride it out) */
            static int16_t slow = -1;
            static uint32_t k;
            if (slow < 0) slow = (int16_t)om_fakesat_has("aether/FAKESCRIPT.PGLONG");
            if (slow && ++k % 50u == 0u) {
                uint32_t v = om_v2_vblanks();
                while (om_v2_vblanks() - v < 9u)
                    ;
            }
        }
#endif
    }
}

/* --- the background ---------------------------------------------------------------------------------------------- */
/* 30 fps: a frame is shown for two V-blanks; the pipeline (stage, decode, frame DMA, flip) is below, at bg_flip_step.
 * A frame is staged once its entry has been read (bg_entry_in): while the file is still coming in, the frame on screen
 * stays until the next one's bytes are there. */
static struct {
    om_lbg g;
    om_lbg_play p;
    om_lbg_dec dec;                  /* the decode in progress */
    uint8_t opened, started, dead, decoding, dbuf, disp, ready, ph;
    const uint8_t *ops;
    uint32_t len, stage_len, stage_got, vbl, due, flip_vbl, dma_vbl;
    uint16_t dma_t;
    int rc;
    uint32_t shown, wraps, held, late, dec_split, dec_us, st_us, xf_us, dec_max, stage_max, xfer_max, xfer_block_us;
    uint32_t first_vbl;              /* the V-blank of the first animated flip (0: none yet) */
} s_bg;
static uint16_t s_bgc[6];            /* the average background under: label, title, meta, steps, hint, brand */

static volatile Uint16 *cram(uint32_t n) { return (volatile Uint16 *)(OM_CRAM + 2u * n); }   /* HWF-30 */

static uint16_t avg_under(int x0, int y0, int x1, int y1)
{   /* the keyframe's mean color inside a screen box (every 4th pixel), as a VDP1 RGB code */
    uint32_t r = 0, g = 0, b = 0, n = 0;
    int x, y, sh = s_bg.g.scale == 2u ? 1 : 0;
    for (y = y0; y <= y1; y += 4)
        for (x = x0; x <= x1; x += 4) {
            uint16_t c = om_lbg_color(&s_bg.g, s_pre.pix[(uint32_t)(y >> sh) * s_bg.g.w + (uint32_t)(x >> sh)]);
            r += c & 31u; g += (c >> 5) & 31u; b += (c >> 10) & 31u; n++;
        }
    if (!n) return 0x8000u;
    return (uint16_t)(0x8000u | ((b / n) << 10) | ((g / n) << 5) | (r / n));   /* once, at start: not a hot loop */
}

static void text_bgs(void)
{
    static const uint8_t box[6] = { OM_B_LD_LABEL, OM_B_LD_TITLE, OM_B_LD_META1, OM_B_LD_STEPS, OM_B_LD_HINT,
                                    OM_B_LD_BRAND };
    int i;
    for (i = 0; i < 6; i++) {
        const om_box *b = &om_boxes[box[i]];
        int r[4];
        om_safe_rect(&g_om_safe, b->x0, b->y0, b->x1, b->y1, r);   /* where the box is on screen */
        s_bgc[i] = s_bg.started ? avg_under(r[0], r[1], r[2], r[3]) : om_c555_rgb(0x04081A);
    }
}

/* --- the translucent reflection (HWF-48) --------------------------------------------------------------------------- */
static int s_refl_w;                 /* the width the reflection texture was built for (0: none) */

static void refl_build(int rows, int tall)
{   /* the cover's last `rows` rows, flipped and squeezed into REFL_H rows, darkened toward the floor, 3-3-2 indexed */
    uint32_t tex;
    int w, h, t, y, xx;
    volatile Uint16 *src;
    volatile Uint16 *dst = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_REFL);
    s_refl_w = 0;
    if (!om_art_hero(&tex, &w, &h, &t) || t != tall || w > 160)
        return;
    src = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + tex);
    for (y = 0; y < REFL_H; y++) {
        int sy = h - 1 - y * rows / REFL_H, keep = 256 - y * 128 / REFL_H;   /* once, while black: not a hot loop */
        for (xx = 0; xx < w; xx += 2) {
            uint8_t px[2];
            int k;
            for (k = 0; k < 2; k++) {
                uint16_t c = src[sy * w + xx + k];
                uint32_t r = (c & 31u) * (uint32_t)keep >> 8, g = (c >> 5 & 31u) * (uint32_t)keep >> 8,
                         b = (c >> 10 & 31u) * (uint32_t)keep >> 8;
                uint8_t q8 = (uint8_t)((r >> 2) << 5 | (g >> 2) << 2 | (b >> 3));
                px[k] = !(c & 0x8000u) ? 0 : q8 ? q8 : 1;               /* 0 = transparent (an empty cover pixel) */
            }
            dst[(y * w + xx) >> 1] = (Uint16)(px[0] << 8 | px[1]);   /* 16-bit writes (HWF-23) */
        }
    }
    for (xx = 1; xx < 256; xx++) {                                   /* index -> RGB555 (index 1 = also black) */
        uint32_t r = (uint32_t)(xx >> 5) * 31u / 7u, g = (uint32_t)(xx >> 2 & 7) * 31u / 7u, b = (uint32_t)(xx & 3) * 31u / 3u;
        *cram(REFL_CRAM + (uint32_t)xx) = (Uint16)(xx == 1 ? 0 : (b << 10 | g << 5 | r));
    }
    s_refl_w = w;
}

static void refl_vdp2(int on)
{   /* VDP2 sprite color calculation (HWF-48): palette dots whose priority number == REFL_PRIO blend with what is under
     * them at ratio register CC1-0; RGB dots (the cover, the text, every 13a element) take priority register 0 (6) and
     * stay opaque. Off again (the menu's own state) when the screen ends. */
    const om_theme *t = om_v2_theme();
    uint16_t n2 = t->panel_alpha >= 100 ? 0 : NBG2ON;
    if (on) {
        slPrioritySpr1(REFL_PRIO);
        slSpriteCCalcCond(CC_PR_CN);
        slSpriteCCalcNum(REFL_PRIO);
        slColRateSpr0(18);                                       /* top : second = 13 : 19 -> 40 % reflection */
        slColRateSpr1(22);                                       /* 9 : 23 -> 28 % */
        slColRateSpr2(26);                                       /* 5 : 27 -> 16 % */
        slColRateSpr3(29);                                       /* 2 : 30 -> 6 % */
        slColorCalcOn((Uint16)(n2 | SPRON));
    } else
        slColorCalcOn(n2);
}

static int bg_dbuf_start(void);      /* below: the double-buffered NBG1 (round 3) */

static void bg_fail(int rc)
{
    if (!s_bg.rc) s_bg.rc = rc;
    s_bg.dead = 1;
}

static void bg_try_start(void)
{
    uint32_t i, y;
    int rc;
    if (s_bg.dead || s_pre.phase == 3 || !s_pre.file) {
        if (s_pre.phase == 3) bg_fail(s_pre.rc);
        return;
    }
    if (!s_bg.opened) {
        uint32_t need = om_lbg_head_bytes(s_pre.file, s_pre.got);
        if (!need || s_pre.got < need || s_pre.got < 592u) return;
        if ((rc = om_lbg_open(&s_bg.g, s_pre.file, s_pre.got, s_pre.size)) != OM_OK) { bg_fail(rc); return; }
        if (s_bg.g.w != s_pre.w || s_bg.g.h != s_pre.h || om_lbg_key_need(&s_bg.g) > s_pre.hw) {
            bg_fail(OM_LBG_E_INFO);                              /* the keyframe must lie in HWRAM */
            return;
        }
        s_bg.opened = 1;
    }
    if (!s_bg.started && s_pre.got >= om_lbg_key_need(&s_bg.g)) {
        if ((rc = om_lbg_start(&s_bg.p, &s_bg.g, s_pre.pix)) != OM_OK) { bg_fail(rc); return; }
        for (i = 0; i < 256; i++)
            *cram(256u * OM_V2_BMP_PAL + i) = om_lbg_color(&s_bg.g, i);
        (void)y;
        if ((rc = bg_dbuf_start()) != OM_OK) { bg_fail(rc); return; }   /* the still keyframe in both bitmaps */
        slZoomNbg1(s_bg.g.scale == 2u ? toFIXED(0.5) : toFIXED(1.0), s_bg.g.scale == 2u ? toFIXED(0.5) : toFIXED(1.0));
        s_bg.started = 1;
        text_bgs();
    }
}

static void bg_fallback(void)
{   /* no LOADBG.BIN (yet): a plain dark backdrop, the scrim's color, over the whole 320x224 */
    uint32_t y;
    *cram(256u * OM_V2_BMP_PAL) = (Uint16)(om_c555_rgb(0x04081A) & 0x7FFFu);
    for (y = 0; y < 224u; y++) {
        volatile Uint16 *d = (volatile Uint16 *)OM_V2_BMP + y * 256u;
        uint32_t x;
        for (x = 0; x < 160u; x++) d[x] = 0;
    }
    slZoomNbg1(toFIXED(1.0), toFIXED(1.0));
}

/* The background's work, every V-blank (Task 13c round 3): NBG1 is double-buffered -- two 512x256
 * bitmaps, B0 (the menu's) and A1 (free during loading; its cycle pattern gets NBG1's two character reads, HWF-49) --
 * and the bulk moves are SCU DMA level 0 (SGL keeps levels 1 and 2 for its own V-blank transfers, HWF-50):
 *   1. stage the next entry: in place when it lies in HWRAM; from sound RAM by DMA into the staging buffer (4-byte
 *      aligned ranges, its cache lines purged before the CPU reads it, SH7604 8.4.7); the packer pads the file so
 *      no loop entry straddles the HWRAM / sound-RAM boundary;
 *   2. decode it into the frame buffer, in pieces of 32 ops, until DEC_END_US;
 *   3. DMA the whole decoded frame, row by row (an indirect table of h rows: the bitmap's rows are 512 B apart), into
 *      the bitmap that is NOT on screen;
 *   4. flip NBG1's bitmap base to it at the frame's V-blank (every 2 V-blanks at 30 fps; SGL writes the register at
 *      V-blank): no partial frame is ever shown, and the next frame is only written into the other bitmap once the
 *      flip has happened.
 * Every DMA wait is a poll of the status register, bounded in V-blanks (DMA_VBL): a stuck DMA is a named failure. */
#define DEC_END_US   10500            /* the drawing follows (up to ~6 ms when a status pill is rasterized) */
#define STAGE_END_US 10500            /* a staging DMA is waited for until then, else polled next V-blank */
#define DMA_VBL      30u              /* V-blanks a DMA may take before it is a failure */
#define XFER_LAST_US 13000            /* a frame DMA starts no later than this into the frame (else next V-blank) */
#define SCU_REG(o)   ((volatile uint32_t *)(0x25FE0000u + (o)))   /* SCU_Manual.txt 3.2 (HWF-50) */
#define SCU_D0W      SCU_REG(0x04)
#define SCU_D0AD     SCU_REG(0x0C)
#define SCU_D0EN     SCU_REG(0x10)
#define SCU_D0MD     SCU_REG(0x14)
#define SCU_DSTA     SCU_REG(0x7C)
#define DSTA_D0      0x00010030u      /* D0BK (16), D0WT (5), D0MV (4): level 0 not stopped */
#define BMP_A1       0x25E20000u      /* VDP2 VRAM bank A1: the second NBG1 bitmap (HWF-49) */
#define CYC_A1_NBG1  0x55EEEEEEu      /* A1 while it holds a bitmap: NBG1 character reads x2, CPU (as B0, HWF-29) */
enum { P_STAGE, P_STAGE_DMA, P_DECODE, P_XFER, P_XFER_DMA };

static int bg_entry_in(void)
{   /* the next loop entry has been read in whole (into HWRAM, or into sound RAM) */
    uint32_t off, len;
    if (s_pre.phase == 2) return 1;
    om_lbg_entry_of(&s_bg.g, om_lbg_next(&s_bg.p, &s_bg.g), &off, &len);
    return s_pre.phase == 1 && off + len <= s_pre.got;
}

static int bg_busy(void)
{   /* the pipeline has CPU work this V-blank (a staging DMA to wait for, a decode), so the preload leaves it time */
    if (!s_bg.started || s_bg.dead || !s_bg.dbuf) return 0;
    if (s_bg.ph == P_STAGE) return !s_bg.ready && bg_entry_in();
    return s_bg.ph == P_STAGE_DMA || s_bg.ph == P_DECODE;
}

static uint32_t phys(const void *p) { return (uint32_t)(uintptr_t)p & 0x07FFFFFFu; }   /* SCU addresses: bits 26-0 */
static int dma0_busy(void) { return (*SCU_DSTA & DSTA_D0) != 0u; }

static void dma0_indirect(const uint32_t *tbl, uint32_t add)
{   /* level 0, indirect mode (table of {byte count, write address, read address | bit 31 on the last}), started now */
    *SCU_D0EN = 0;
    *SCU_D0W = phys(tbl);
    *SCU_D0AD = add;                 /* 0x101: read +4, write +2 (a B-bus destination); 0x102: write +4 (HWRAM) */
    *SCU_D0MD = 0x01000007u;         /* indirect, addresses kept, start factor 111 = the GO bit */
    *SCU_D0EN = 0x101u;              /* enable + GO */
}

static int dma0_wait_bounded(void)
{   /* a blocking wait (setup and exit only, while the screen is black or fading), at most DMA_VBL V-blanks */
    uint32_t v0 = om_v2_vblanks();
    while (dma0_busy())
        if (om_v2_vblanks() - v0 > DMA_VBL) return OM_LBG_E_DMA;
    return OM_OK;
}

static void bmp_table(uint32_t b)
{   /* the row table: frame row r (w bytes at pix + r w) -> bitmap row r (512 B stride) of B0 (b 0) or A1 (b 1); the
     * CPU's stores reach memory before the DMA reads them (write-through cache, HWF-50) */
    uint32_t r, *t = s_pre.tbl, base = phys((const void *)(uintptr_t)(b ? BMP_A1 : OM_V2_BMP));
    for (r = 0; r < s_bg.g.h; r++, t += 3) {
        t[0] = s_bg.g.w;
        t[1] = base + r * 512u;
        t[2] = phys(s_pre.pix + r * s_bg.g.w);
    }
    t[-1] |= 0x80000000u;            /* the end code: on the last entry's read address */
}

static void bmp_xfer(uint32_t b)
{
    bmp_table(b);
    dma0_indirect(s_pre.tbl, 0x101u);
}

static void bg_flip(uint32_t b)
{
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void *)(uintptr_t)(b ? BMP_A1 : OM_V2_BMP));   /* at the next V-blank */
    s_bg.disp = (uint8_t)b;
}

static int bg_dbuf_start(void)
{   /* the keyframe into both bitmaps (black: blocking, bounded), A1's cycle pattern, B0 on screen */
    int rc;
    slScrCycleSet(OM_V2_CYC_A0, CYC_A1_NBG1, OM_V2_CYC_B0, OM_V2_CYC_B1);   /* HWF-49 */
    s_bg.dbuf = 1;                   /* from here bg_dbuf_end restores A1, even after a failure below */
    if ((rc = dma0_wait_bounded()) != OM_OK) return rc;
    {
        uint16_t t = om_ut_frc();                             /* a whole frame's DMA, measured once (blocking) */
        bmp_xfer(0);
        if ((rc = dma0_wait_bounded()) != OM_OK) return rc;
        s_bg.xfer_block_us = (uint32_t)us_since(t);
    }
    bmp_xfer(1);
    if ((rc = dma0_wait_bounded()) != OM_OK) return rc;
    bg_flip(0);
    return OM_OK;
}

static int bg_dbuf_end(void)
{   /* the menu's VDP2 state back: no DMA left running, B0 the bitmap, A1 all CPU again; rc: a DMA that never stopped */
    int rc = dma0_wait_bounded();
    if (s_bg.dbuf) {
        bg_flip(0);
        slScrCycleSet(OM_V2_CYC_A0, OM_V2_CYC_A1, OM_V2_CYC_B0, OM_V2_CYC_B1);
        s_bg.dbuf = 0;
    }
    return rc;
}

static void purge_lines(const uint8_t *p, uint32_t n)
{   /* SH7604 8.4.7 associative purge: a longword write to address + 0x40000000 invalidates that 16-byte line */
    uint32_t a = (uint32_t)(uintptr_t)p & ~15u, e = (uint32_t)(uintptr_t)p + n;
    for (; a < e; a += 16u)
        *(volatile uint32_t *)(uintptr_t)((a & 0x1FFFFFFFu) | 0x40000000u) = 0;
}

static void stage_begin(uint16_t t0)
{   /* the next entry: where its ops are in HWRAM, or the staging DMA that brings them from sound RAM */
    uint32_t off, len;
    (void)t0;
    om_lbg_entry_of(&s_bg.g, om_lbg_next(&s_bg.p, &s_bg.g), &off, &len);
    s_bg.len = len;
    if (off + len <= s_pre.hw) {
        s_bg.ops = s_pre.file + off;
        s_bg.ph = P_DECODE;
        return;
    }
    if (len > STAGE) { bg_fail(OM_LBG_E_INFO); return; }
    if (off < s_pre.hw) { bg_fail(OM_LBG_E_INFO); return; }   /* straddles the boundary: the packer never does that */
    {
        uint32_t v0 = off - s_pre.hw, a0 = v0 & ~3u, a1 = (v0 + len + 3u) & ~3u, *t = s_pre.stbl, n = 0, v, e;
        if (om_lbg_lw(v0) && v0 + len <= OM_LBG_SR2_V0) {     /* all in LWRAM: decoded in place (SCU DMA
                                                               * cannot read Work RAM-L, SCU_Manual.txt Figure 2.2) */
            const uint8_t *p = LW_C + (v0 - OM_LBG_LW_V0);
            purge_lines(p, len);                              /* written cache-through: no stale line is read */
            s_bg.ops = p;
            s_bg.ph = P_DECODE;
            return;
        }
        if (dma0_busy()) return;                              /* retried next V-blank */
        for (v = a0; v < a1; v = e) {   /* by DMA from sound RAM, piece by region; an LWRAM piece by the CPU */
            e = om_lbg_region_end(v);
            if (e > a1) e = a1;
            if (om_lbg_lw(v))
                memcpy(s_pre.stage + (v - a0), LW_CT + (v - OM_LBG_LW_V0), e - v);
            else {
                if (n == 2u) { bg_fail(OM_LBG_E_INFO); return; }   /* never: an entry (<= STAGE) meets two regions */
                t[0] = e - v;
                t[1] = phys(s_pre.stage + (v - a0));
                t[2] = (0x05A00000u + om_lbg_sr_off(v)) & 0x07FFFFFFu;
                t += 3;
                n++;
            }
        }
        s_bg.ops = s_pre.stage + (v0 - a0);
        s_bg.stage_len = a1 - a0;
        if (!n) { s_bg.ph = P_DECODE; return; }              /* never: an all-LWRAM entry is decoded in place */
        t[-1] |= 0x80000000u;
        dma0_indirect(s_pre.stbl, 0x102u);
        s_bg.dma_vbl = s_bg.vbl;
        s_bg.dma_t = om_ut_frc();
        s_bg.ph = P_STAGE_DMA;
    }
}

static void bg_flip_step(void)
{   /* first in the frame: a finished transfer is ready; at its V-blank the frame is flipped to */
    if (!s_bg.started || s_bg.dead || !s_bg.dbuf) return;
    s_bg.vbl++;
    if (s_bg.ph == P_XFER_DMA && !dma0_busy()) {
        s_bg.xf_us = (uint32_t)us_since(s_bg.dma_t);          /* start to seen-done: an upper bound */
        if (s_bg.xf_us > s_bg.xfer_max) s_bg.xfer_max = s_bg.xf_us;
        s_bg.ready = 1;
        s_bg.ph = P_STAGE;
    }
    if (s_bg.ready && s_bg.vbl >= s_bg.due) {                 /* 4: show the frame transferred last */
        bg_flip(s_bg.disp ^ 1u);
        if (s_bg.due && s_bg.vbl > s_bg.due) s_bg.late += s_bg.vbl - s_bg.due;
        s_bg.due = s_bg.vbl + (s_bg.g.fps == 30u ? 2u : 1u);
        s_bg.flip_vbl = s_bg.vbl;
        s_bg.ready = 0;
        if (!s_bg.shown) s_bg.first_vbl = om_v2_vblanks();
        s_bg.shown++;
        if (!s_bg.p.frame) s_bg.wraps++;
    }
}

static void bg_work(int check, uint16_t t0)
{
    int rc, guard;
    if (!s_bg.started || s_bg.dead || !s_bg.dbuf) return;
    for (guard = 0; guard < 6; guard++) {
        switch (s_bg.ph) {
        case P_STAGE:
            if (s_bg.ready) return;                           /* one frame ahead at most */
            if (!bg_entry_in()) { s_bg.held++; return; }      /* its bytes are still on the way: the frame stays */
            stage_begin(t0);
            if (s_bg.dead || s_bg.ph == P_STAGE) return;
            break;
        case P_STAGE_DMA: {
            uint16_t lim = to_ticks(STAGE_END_US);            /* a short entry: wait for it in this V-blank */
            while (dma0_busy() && (uint16_t)(om_ut_frc() - t0) < lim)
                ;
            if (dma0_busy()) {
                if (s_bg.vbl - s_bg.dma_vbl > DMA_VBL) { bg_fail(OM_LBG_E_DMA); return; }
                return;
            }
            purge_lines(s_pre.stage, s_bg.stage_len);
            if (s_bg.vbl == s_bg.dma_vbl) {
                s_bg.st_us = (uint32_t)us_since(s_bg.dma_t);
                if (s_bg.st_us > s_bg.stage_max) s_bg.stage_max = s_bg.st_us;
            }
            s_bg.ph = P_DECODE;
            break;
        }
        case P_DECODE: {
            uint16_t t = om_ut_frc(), lim = to_ticks(DEC_END_US);
            if (!s_bg.decoding) {
                if ((rc = om_lbg_dec_begin(&s_bg.dec, &s_bg.p, &s_bg.g, s_bg.ops, s_bg.len,
                                           check && om_lbg_next(&s_bg.p, &s_bg.g) == 0u)) != OM_OK) {
                    bg_fail(rc);
                    return;
                }
                s_bg.decoding = 1;
                s_bg.dec_us = 0;
            }
            rc = 1;
            while (rc == 1 && (uint16_t)(om_ut_frc() - t0) < lim)
                rc = om_lbg_dec_run(&s_bg.dec, &s_bg.p, &s_bg.g, 32u);
            s_bg.dec_us += (uint32_t)us_since(t);
            if (rc < 0) { bg_fail(rc); return; }
            if (rc == 1) { s_bg.dec_split++; return; }
            if (s_bg.dec_us > s_bg.dec_max) s_bg.dec_max = s_bg.dec_us;
            s_bg.decoding = 0;
            s_bg.ph = P_XFER;
            break;
        }
        case P_XFER:
            if (s_bg.vbl <= s_bg.flip_vbl || dma0_busy()) return;   /* the other bitmap is still on screen */
            if (us_since(t0) > XFER_LAST_US) return;          /* not across V-blank-in (SGL's level 2) */
            bmp_xfer(s_bg.disp ^ 1u);
            s_bg.dma_vbl = s_bg.vbl;
            s_bg.dma_t = om_ut_frc();
            s_bg.ph = P_XFER_DMA;
            break;
        case P_XFER_DMA:                                      /* finished: seen by bg_flip_step */
            if (s_bg.vbl - s_bg.dma_vbl > DMA_VBL) bg_fail(OM_LBG_E_DMA);
            return;
        default:
            return;
        }
    }
}

/* --- the screen --------------------------------------------------------------------------------------------------- */
static om_ut s_lab, s_title[2], s_meta[2], s_step[3], s_stepa[3], s_hint, s_status, s_b1, s_noart;

void om_loading_alloc(void (*alloc)(om_ut *t, int cap, int h))
{
    int hl = om_uf_height(&om_face_label), hh = om_uf_height(&om_face_hero), hs = om_uf_height(&om_face_small), i;
    alloc(&s_lab, 128, hl);
    for (i = 0; i < 2; i++) {
        alloc(&s_title[i], 504, hh);
        alloc(&s_meta[i], 504, hs);
    }
    for (i = 0; i < 3; i++) {
        alloc(&s_step[i], 176, hs);                      /* plain, and in progress (with U+2026) */
        alloc(&s_stepa[i], 176, hs);
    }
    alloc(&s_hint, 168, hs);
    alloc(&s_status, 504, hs);
    alloc(&s_b1, om_icon_wordmark_s.w, om_icon_wordmark_s.h);   /* the Aether wordmark (uifont.py WORDMARKS) */
    om_ut_icon(&s_b1, &om_icon_wordmark_s);
    alloc(&s_noart, 40, hs);
}

typedef struct {
    const om_load_game *g;
    const om_launch_bufs *lb;
    const om_load_env *env;
    om_load_out *out;
    om_launch_ld ld;
    int state, done[3], real[3], failed_step, lines, exiting, marquee, miss_state, prev_state, crc_frame, raster, tool;
    uint32_t deferred;                  /* frames an unchunked step has waited for time (run anyway after OP_WAIT) */
    uint32_t vb_last, wait_vbl, notice_vbl, bg_notice, mq;
    uint32_t up_vbl, min_up;            /* the V-blank the screen was fully up (0: not yet), and the minimum time */
    uint32_t fade_vbl;                  /* the V-blank the fade-up began (the screen's first visible frame) */
    int32_t real_q, bar_q;              /* the bar's target from the work done, and as last set */
    om_tween cover, tfade, prog;
    char notice[64];
    uint16_t txt, dim, faint, acc, acc2, err;
} ld_ctx;

static int base_of(int mid, const om_face *f) { return mid + f->mid; }

static void bar_to(ld_ctx *x, int32_t q12, int ease)
{
    const om_box *b = OM_BX(LD_BAR);
    int32_t px = (int32_t)(((b->x1 - b->x0) * q12) >> 12);
    if (ease) om_tween_to(&x->prog, px, om_anim_frames(250, x->env->anim, 0, x->env->fps));
    else om_tween_jump(&x->prog, px);
}

static void set_target(ld_ctx *x, int32_t q12)
{   /* the work's own target: pace() shows it eased (250 ms), never ahead of the work done nor of the minimum time */
    x->real_q = q12;
}

static uint32_t up_for(const ld_ctx *x) { return x->up_vbl ? om_v2_vblanks() - x->up_vbl : 0u; }

static uint32_t scream_gate(const ld_ctx *x)
{   /* "Starting game..." and the scream close the window: the scream starts so that it ends at the minimum time (never
     * before 2/3 of it, so the three dots stay spread) */
    uint32_t len = om_audio_scream_len(), g = x->min_up > len ? x->min_up - len : 0u;
    return g > x->min_up * 2u / 3u ? g : x->min_up * 2u / 3u;
}

static void pace(ld_ctx *x)
{   /* final M3 round (user): a dot fills on its step's REAL completion, but never before its share of the minimum time;
     * the bar eases across the window (linear in time while time is the limit, eased toward the work otherwise) */
    uint32_t el = up_for(x), gate[3];
    int32_t tq = !x->up_vbl ? 0 : el >= x->min_up ? OM_Q : (int32_t)((el << 12) / x->min_up), q;
    int i;
    gate[0] = x->min_up / 3u;
    gate[1] = scream_gate(x);
    gate[2] = x->min_up;
    for (i = 0; i < 3; i++)
        if (x->real[i] && !x->done[i] && x->up_vbl && el >= gate[i] && (i == 0 || x->done[i - 1]))
            x->done[i] = 1;
    q = x->real_q < tq ? x->real_q : tq;
    if (q != x->bar_q) {
        bar_to(x, q, q == x->real_q);
        x->bar_q = q;
    }
}

static void fail(ld_ctx *x, int step, const char *prefix, int rc)
{
    om_launch_abort(&x->ld);
    x->failed_step = step;
    x->state = ST_FAILED;
    x->out->rc = rc;
    om_msg_rc(x->out->msg, sizeof x->out->msg, prefix, om_strerror(rc), rc);
    x->raster = 1;                                       /* the pill's text is rasterized in this frame's drawing */
}

static void work(ld_ctx *x, uint16_t t0)
{   /* the launch, one state per call, SD work within the frame's time budget */
    int rc;
    if ((x->state == ST_OPEN || x->state == ST_COMMIT) && us_since(t0) >= SD_END_US - SD_OP_US &&
        x->deferred < OP_WAIT) {
        x->deferred++;                                   /* this frame has no time left for an unchunked step */
        return;
    }
    x->deferred = 0;
    switch (x->state) {
    case ST_WAIT_BG:                                     /* one SD file at a time: the background first */
        if (s_pre.phase == 1) return;
        x->state = ST_BEGIN;
        /* fall through */
    case ST_BEGIN:
        if ((rc = om_launch_begin(x->g->path, x->g->area, x->g->flags, &x->g->opts, x->lb, &x->out->li)) < 0) {
            fail(x, 0, "LAUNCH FAILED: DISC IMAGE PATH ", rc);
            return;
        }
        x->real[0] = 1;
        set_target(x, OM_Q / 3);
        x->state = ST_OPEN;
        return;
    case ST_OPEN:
        if ((rc = om_launch_open(&x->ld, x->lb, &x->out->li)) < 0) {
            fail(x, 1, "LAUNCH FAILED: LAUNCHER.BIN ", rc);
            return;
        }
        x->state = ST_READ;
        /* fall through */
    case ST_READ:
        do {
            rc = om_launch_read(&x->ld, x->lb, &x->out->li, CHUNK);
        } while (rc > 0 && us_since(t0) + 2500 < SD_END_US);   /* a 2 KB read starts only if it ends in time */
        if (rc < 0) {
            fail(x, 1, "LAUNCH FAILED: LAUNCHER.BIN ", rc);
            return;
        }
        if (x->ld.inf.body_size)                         /* the bar follows the bytes read: 1/3 .. 72 % */
            set_target(x, OM_Q / 3 + (int32_t)((uint32_t)(OM_Q * 39 / 100) * (x->ld.done >> 8) /
                                               ((x->ld.inf.body_size >> 8) + 1u)));
        if (rc > 0) return;
        x->state = ST_LOADED;
        return;
    case ST_LOADED:                                      /* LAUNCHER.BIN in and its CRC passed */
        x->real[1] = 1;
        set_target(x, OM_Q * 72 / 100);
        x->state = x->tool ? ST_COMMIT : ST_SCREAM;      /* a tool: no scream, no minimum time */
        return;
    case ST_SCREAM:                                      /* "Starting game...": the scream, on silence (R34), timed to
                                                          * end the minimum time (scream_gate); cancel stays open */
        if (!x->done[1] || up_for(x) < scream_gate(x)) return;
        om_audio_event(OM_AEV_SCREAM);
        x->wait_vbl = om_v2_vblanks();
        set_target(x, OM_Q);
        x->state = ST_SCREAMING;
        if (LD_VERBOSE) {
            char line[41], n[12];
            strcpy(line, "SCREAM HOOK AT V");
            strcat(line, om_itoa(n, (int)x->wait_vbl));
            om_text_put(0, 2, line, 40);
        }
        return;
    case ST_SCREAMING:
        if (om_audio_scream_remaining() && om_v2_vblanks() - x->wait_vbl < SCREAM_CAP) return;
        x->real[2] = 1;
        if (!x->done[2]) return;                         /* pace(): not before the minimum time */
        x->state = ST_COMMIT;
        return;
    case ST_COMMIT:      /* the commit point. RECENT.TXT is written now, just before the final fade;
                          * a canceled launch never enters Recently Played, and from here there is no cancel */
        x->notice[0] = 0;
        if (x->env->loaded) x->env->loaded(x->env->ctx, x->notice, sizeof x->notice);   /* RECENT.TXT */
        x->state = x->notice[0] ? ST_NOTICE : ST_DONE;
        x->raster = x->notice[0] != 0;
        x->notice_vbl = om_v2_vblanks();
        return;
    case ST_NOTICE:                                      /* a RECENT.TXT error shows for a second, then the launch */
        if (om_v2_vblanks() - x->notice_vbl < NOTICE_VBL) return;
        x->notice[0] = 0;
        x->state = ST_DONE;
        return;
    default:
        return;
    }
}

static void pill(int x0, int y0, int x1, int y1, uint16_t col, int z)
{   /* the menu's toast pill: an opaque rounded bar of three quads */
    om_v1_quad(x0 + 2, y0, x1 - 2, y1, col, 0, 0, z);
    om_v1_quad(x0 + 1, y0 + 1, x1 - 1, y1 - 1, col, 0, 0, z);
    om_v1_quad(x0, y0 + 2, x1, y1 - 2, col, 0, 0, z);
}

static void dot(int x, int y, uint16_t col)
{   /* a 6 x 6 rounded dot (mockup: rounded_rectangle radius 2) */
    om_v1_quad(x + 1, y, x + 4, y + 5, col, 0, 0, ZL_DECO);
    om_v1_quad(x, y + 1, x + 5, y + 4, col, 0, 0, ZL_DECO);
}

static void cover_quad(const ld_ctx *x, int16_t q[8])
{   /* the mockup's tilt (the right edge 8 px taller), sliding in from OM_LD_SLIDE px with more tilt, ease-out */
    static const int16_t tall[8] = { 190, 26, 298, 22, 298, 200, 190, 196 };
    static const int16_t sq[8] = { 190, 59, 298, 55, 298, 165, 190, 161 };
    static const int8_t extra[8] = { 0, 3, 0, -6, 0, 6, 0, -3 };   /* the tilt still settling at the start */
    const int16_t *f = x->g->square ? sq : tall;
    int32_t r = OM_Q - om_tween_value(&x->cover), dx = (om_anim_dist(OM_LD_SLIDE, x->env->anim) * r) >> 12;
    int i;
    for (i = 0; i < 8; i++)
        q[i] = (int16_t)(f[i] + ((i & 1) ? (extra[i] * r) >> 12 : dx));
}

static void draw_cover(ld_ctx *x)
{
    uint32_t tex;
    int w, h, tall, have = x->g->art && om_art_hero(&tex, &w, &h, &tall), i;
    int16_t q[8], r[8];
    (void)i;
    if (x->tool && !x->g->art) return;                   /* a tool with no game (Settings): no cover, no slab */
    cover_quad(x, q);
    /* the reflection (HWF-48): REFL_BANDS strips of the pre-flipped, pre-darkened 8 bpp copy below the cover's lower
     * edge, each a color-bank sprite whose CC bits pick its own VDP2 sprite color-calculation ratio (40 % .. 6 %), so it
     * blends with the moving NBG1 background under it; clipped at the safe area */
    om_v1_clip(0, 0, 319, OM_BX(LD_REFL)->y1, ZL_REFLCLIP);
    if (have) {
        if (s_refl_w == w) {
            int b;
            for (b = 0; b < REFL_BANDS; b++) {
                int y0 = b * REFL_BAND, y1 = y0 + REFL_BAND;
                r[0] = q[6]; r[1] = (int16_t)(q[7] + 1 + y0);          /* A, B: the band's top (texture row 0) */
                r[2] = q[4]; r[3] = (int16_t)(q[5] + 1 + y0);
                r[4] = q[4]; r[5] = (int16_t)(q[5] + y1);              /* C, D: its bottom */
                r[6] = q[6]; r[7] = (int16_t)(q[7] + y1);
                om_v1_distorted_bank(r, OM_VRAM_REFL + (uint32_t)(y0 * w), w, REFL_BAND,
                                     (uint16_t)(REFL_PR | ((uint32_t)b << 11) | REFL_DC_HI), OM_V1_CLIP, ZL_REFL);
            }
        }
        om_v1_distorted(q, tex, w, h, 0, 0, 0, ZL_COVER);
    } else {                                                         /* NO ART: the menu's placeholder card, tilted */
        uint16_t fill = om_c555_rgb(0x282830), edge = om_c555_rgb(0x78788C);
        int16_t e[8], f[8];
        static const int8_t in3[8] = { 3, 3, -3, 3, -3, -3, 3, -3 };
        for (i = 0; i < 8; i++) { e[i] = (int16_t)(q[i] + in3[i]); f[i] = (int16_t)(q[i] + in3[i] * 5 / 3); }
        om_v1_poly4(q, fill, 0, 0, ZL_COVER);                       /* no reflection: an opaque slab, not a card */
        om_v1_poly4(e, edge, 0, 0, ZL_COVER);
        om_v1_poly4(f, fill, 0, 0, ZL_COVER);
        om_ut_text(&s_noart, OM_FACE_SMALL, "NO ART", 0);
        om_ut_colors(&s_noart, om_c555_rgb(0xC8C8D2), fill, 256);
        om_ut_draw_base(&s_noart, (q[0] + q[2] + 1 - s_noart.w) / 2, (q[1] + q[7]) / 2 + 4, 0, ZL_NOART);
    }
    om_v1_line(q[0], q[1], q[2], q[3], om_c555_rgb(0x9AA6B8), 0, ZL_EDGE);   /* the top edge's highlight (alpha 90) */
}

static void draw_text(ld_ctx *x)
{
    const om_box *t = OM_BX(LD_TITLE), *m1 = OM_BX(LD_META1), *m2 = OM_BX(LD_META2);
    uint16_t fade = (uint16_t)om_tween_value(&x->tfade), bgl = s_bgc[0], bgt = s_bgc[1], bgm = s_bgc[2],
             bgs = s_bgc[3], bgh = s_bgc[4], bgb = s_bgc[5];
    int i, base;
    /* NOW LOADING + the accent underline */
    om_ut_colors(&s_lab, x->acc2, bgl, 256);
    om_ut_draw_base(&s_lab, OM_BX(LD_LABEL)->x0, OM_LD_LABEL_BASE, 0, ZL_TXT);
    om_v1_quad(OM_BX(LD_ULINE)->x0, OM_BX(LD_ULINE)->y0, OM_BX(LD_ULINE)->x1, OM_BX(LD_ULINE)->y1, x->acc, 0, 0, ZL_DECO);
    /* the title (fades in, 200 ms), at most two lines; a line wider than the box marquees */
    for (i = 0; i < x->lines; i++) {
        om_ut *s = &s_title[i];
        int32_t per = om_marquee_period(s->full_w, om_box_w(OM_B_LD_TITLE));
        int32_t off = x->marquee && per && s->w == s->full_w ? om_marquee_off(x->mq, s->full_w, om_box_w(OM_B_LD_TITLE),
                                                                               x->env->fps) : 0;
        om_ut_colors(s, x->txt, bgt, fade);
        om_ut_draw_box(s, t->x0, t->x1, OM_LD_TITLE_BASE + i * OM_LD_TITLE_LH, 0, off, x->marquee ? per : 0, 0,
                       i ? ZL_T2 + 1 : ZL_T1 + 1, i ? ZL_T2 : ZL_T1);
    }
    for (i = 0; i < 2; i++) {
        om_ut *s = &s_meta[i];
        const om_box *b = i ? m2 : m1;
        int32_t per = om_marquee_period(s->full_w, om_box_w(OM_B_LD_META1));
        int32_t off = x->marquee && per && s->w == s->full_w ? om_marquee_off(x->mq, s->full_w, om_box_w(OM_B_LD_META1),
                                                                               x->env->fps) : 0;
        om_ut_colors(s, x->dim, bgm, fade);
        om_ut_draw_box(s, b->x0, b->x1, base_of(OM_LD_META_MID + i * OM_LD_META_LH, &om_face_small), 0, off,
                       x->marquee ? per : 0, 0, i ? ZL_M2 + 1 : ZL_M1 + 1, i ? ZL_M2 : ZL_M1);
    }
    /* the checklist: a dot fills when its step has really completed (a tool's hand-off has none) */
    for (i = 0; i < (x->tool ? 0 : 3); i++) {
        int mid = OM_LD_STEP_MID + i * OM_LD_STEP_H, active = !x->done[i] && (i == 0 || x->done[i - 1]);
        om_ut *st = active && x->state != ST_FAILED ? &s_stepa[i] : &s_step[i];   /* in progress: with U+2026 */
        uint16_t dc = x->done[i] ? x->acc : x->failed_step == i && x->state == ST_FAILED ? x->err
                    : active ? om_ui_mix(bgs, x->acc2, 120) : om_ui_mix(bgs, x->acc2, 60);
        dot(OM_BX(LD_DOTS)->x0, mid - 3, dc);
        om_ut_colors(st, x->done[i] ? x->dim : active ? x->txt : x->faint, bgs, 256);
        om_ut_draw_base(st, OM_BX(LD_STEPS)->x0, base_of(mid, &om_face_small), 0, ZL_TXT);
    }
    {   /* the progress bar: track (white at 40/255 over the background), the eased fill and its knob */
        const om_box *b = OM_BX(LD_BAR);
        int xf = b->x0 + (int)om_tween_value(&x->prog);
        om_v1_quad(b->x0, b->y0 + 1, b->x1, b->y1 - 1, om_ui_mix(bgs, 0xFFFF, 40), 0, 0, ZL_DECO + 1);
        if (xf > b->x0) om_v1_quad(b->x0, b->y0 + 1, xf, b->y1 - 1, x->acc, 0, 0, ZL_DECO);
        om_v1_quad(xf - 5, b->y0 + 1, xf, b->y1 - 1, x->acc2, 0, 0, ZL_DECO - 1);
        om_v1_quad(xf - 4, b->y0, xf - 1, b->y1, x->acc2, 0, 0, ZL_DECO - 1);
    }
    /* the hint row: a failure (red pill, rc, until a press), a notice, or the cancel hint */
    base = base_of(OM_LD_HINT_MID, &om_face_small);
    {
        const om_box *h = OM_BX(LD_STATUS);
        const char *msg = 0;
        char bgmsg[64];
        if (x->state == ST_FAILED)
            msg = x->out->msg;
        else if (x->notice[0])
            msg = x->notice;
        else if (x->bg_notice > 1u && s_bg.rc) {                     /* non-fatal: the screen runs on without it */
            om_msg_rc(bgmsg, sizeof bgmsg, "BACKGROUND: LOADBG.BIN ", om_strerror(s_bg.rc), s_bg.rc);
            msg = bgmsg;
        }
        if (msg && msg[0]) {
            uint16_t pc = x->state == ST_FAILED ? om_c555_rgb(0x5A1E1E) : om_ui_mix(bgh, x->txt, 30);
            int32_t per, off;
            om_ut_text(&s_status, OM_FACE_SMALL, msg, -(om_box_w(OM_B_LD_STATUS) - 12));
            per = om_marquee_period(s_status.full_w, om_box_w(OM_B_LD_STATUS) - 12);
            off = per && s_status.w == s_status.full_w ? om_marquee_off(x->mq, s_status.full_w,
                                                                       om_box_w(OM_B_LD_STATUS) - 12, x->env->fps) : 0;
            pill(h->x0, h->y0 + 1, h->x1, h->y1 - 1, pc, ZL_PILL);
            om_ut_colors(&s_status, x->txt, pc, 256);
            om_ut_draw_box(&s_status, h->x0 + 6, h->x1 - 6, base, 0, off, per, 0, ZL_STATUS + 1, ZL_STATUS);
        } else {
            om_ut_colors(&s_hint, om_ui_mix(bgh, x->txt, 110), bgh, 256);
            om_ut_draw_base(&s_hint, OM_BX(LD_HINT)->x0, base, 0, ZL_TXT);
        }
    }
    /* the footer: the logo and the Aether wordmark (om_icon_wordmark_s: cap rows 1..7, centred on the brand line) */
    om_logo_draw_footer(OM_BX(LD_LOGO)->x0, OM_BX(LD_LOGO)->y0, ZL_TXT);   /* its own 22x15 logo */
    om_ut_colors(&s_b1, x->txt, bgb, 256);
    om_ut_draw(&s_b1, OM_BX(LD_BRAND)->x0, OM_LD_BRAND_MID - 4, 0, ZL_TXT);
}

static uint32_t s_seg[4];            /* fake build: the worst time to the end of upload / decode / cover / text, us */
static void seg(const ld_ctx *x, int i, uint16_t t0)
{
    uint32_t us;
    if (!LD_VERBOSE || x->crc_frame) return;
    us = (uint32_t)us_since(t0);
    if (us > s_seg[i]) s_seg[i] = us;
}

static void diag(const ld_ctx *x)
{   /* fake build, above the screen's content: row 0 = background frame / V-blank / misses (and the step state of the
     * first) / loops / step state; row 1 = the preload (bytes, V-blanks, rc, frames held waiting for data) */
    char line[41], n[12];
    if (!LD_VERBOSE) return;
    strcpy(line, "LD F");
    strcat(line, om_itoa(n, (int)s_bg.p.frame));
    strcat(line, " V");
    strcat(line, om_itoa(n, (int)om_v2_vblanks()));
    strcat(line, " M");
    strcat(line, om_itoa(n, (int)x->out->misses));
    if (x->out->misses) {
        strcat(line, "@S");
        strcat(line, om_itoa(n, x->miss_state));
    }
    strcat(line, " L");
    strcat(line, om_itoa(n, (int)s_bg.wraps));
    strcat(line, " C");
    strcat(line, om_itoa(n, (int)(x->out->draw_us / 100u)));
    strcat(line, "/");
    strcat(line, om_itoa(n, (int)(x->out->frame_us / 100u)));
    strcat(line, " S");
    strcat(line, om_itoa(n, x->state));
    if (s_bg.wraps && !s_bg.rc)                          /* frame 0's CRC-32 checked on the first wrap */
        strcat(line, " CRC OK");
    om_text_put(0, 0, line, 40);
    strcpy(line, "LBG ");
    strcat(line, om_itoa(n, (int)s_pre.got));
    strcat(line, " B");
    strcat(line, om_itoa(n, (int)s_pre.t_vbl));
    strcat(line, " RC");
    strcat(line, om_itoa(n, s_bg.rc));
    strcat(line, " H");
    strcat(line, om_itoa(n, (int)s_bg.held));
    strcat(line, " K");                                  /* one frame's DMA, timed blocking; worst staging DMA; decode */
    strcat(line, om_itoa(n, (int)(s_bg.xfer_block_us / 100u)));
    strcat(line, " G");
    strcat(line, om_itoa(n, (int)(s_bg.stage_max / 100u)));
    strcat(line, " D");
    strcat(line, om_itoa(n, (int)(s_bg.dec_max / 100u)));
    strcat(line, " S");                                  /* decodes split over V-blanks, V-blanks late */
    strcat(line, om_itoa(n, (int)s_bg.dec_split));
    strcat(line, "/");
    strcat(line, om_itoa(n, (int)s_bg.late));
    om_text_put(0, 1, line, 40);
    if (x->state < ST_SCREAMING) {                       /* V-blanks since the launch (A): fade-up, up, first flip */
        strcpy(line, "T VIS");
        strcat(line, om_itoa(n, (int)(x->fade_vbl - s_pre.t_vbl0)));
        strcat(line, " UP");
        strcat(line, om_itoa(n, x->up_vbl ? (int)(x->up_vbl - s_pre.t_vbl0) : -1));
        strcat(line, " ANIM");
        strcat(line, om_itoa(n, s_bg.first_vbl ? (int)(s_bg.first_vbl - s_pre.t_vbl0) : -1));
        strcat(line, " F");
        strcat(line, om_itoa(n, (int)s_bg.shown));
        strcat(line, " PRE");                            /* KB the panel's early read brought in */
        strcat(line, om_itoa(n, (int)(s_pre.early_got / 1024u)));
        strcat(line, " BK");                             /* and the V-blanks the rest took while black */
        strcat(line, om_itoa(n, (int)s_pre.black_vbl));
        strcat(line, " W");                              /* LW bytes found overwritten (rewinds) */
        strcat(line, om_itoa(n, (int)s_pre.lw_lost));
        om_text_put(0, 2, line, 40);
    }
}

static void frame(void *ctx)
{   /* one V-blank: flip to a finished frame, input, SD work (preload or launch), stage / decode / DMA, draw */
    ld_ctx *x = (ld_ctx *)ctx;
    uint16_t t0 = om_ut_frc();
    uint32_t vb = om_v2_vblanks();
    Uint16 p, held;
    if (x->vb_last && vb - x->vb_last > 1u) {
        if (x->crc_frame)                                /* the fake build's one-off CRC-32 of the wrap: counted apart */
            x->out->crc_misses += vb - x->vb_last - 1u;
        else {
            if (!x->out->misses) x->miss_state = x->prev_state;      /* the state the slow frame was in */
            x->out->misses += vb - x->vb_last - 1u;
        }
    }
    x->crc_frame = 0;
    x->prev_state = x->exiting ? 100 + x->state : x->state;
    x->vb_last = vb;
    x->out->frames++;
    bg_try_start();
    if (LD_VERBOSE && s_bg.started && !s_bg.wraps && s_bg.ph == P_DECODE && om_lbg_next(&s_bg.p, &s_bg.g) == 0u)
        x->crc_frame = 1;                                /* this frame checks the wrap's CRC-32 (~10 ms, fake only) */
    bg_flip_step();                                      /* first: a finished frame is flipped to at its V-blank */
    seg(x, 0, t0);
    p = om_pad_poll();
    held = om_pad_held();
    if (!x->exiting) {
        if (x->state == ST_FAILED) {
            if (p || om_pad_soft_reset(held)) {          /* any press, the cancel combination too: the rc stays named */
                x->out->result = OM_LOAD_FAILED;
                x->exiting = 1;
            }
        } else if (x->state < ST_COMMIT && om_pad_soft_reset(held)) {   /* A+B+C+Start: cancel, any time before the
                                                                        * fade-out that precedes the hand-off */
            om_launch_abort(&x->ld);
            if (s_pre.phase == 1) pre_fail(OM_E_FIELD);
            x->out->result = OM_LOAD_CANCEL;
            x->exiting = 1;
        } else if (x->state == ST_DONE) {
            x->out->result = OM_LOAD_HANDOFF;
            x->exiting = 1;
        }
    }
    /* the SD work next, so the launch is never starved by the background: the preload (one file open at a time),
     * then the launch; a step completed now shows from the next frame */
    if (s_pre.phase == 1)                                /* the loop waits for its bytes: more of the frame for them */
        preload_step(bg_busy() ? SD_END_US : PRELOAD_US, t0);
    else if (!x->exiting && x->state != ST_FAILED && x->state != ST_DONE)
        work(x, t0);
    if (x->state != ST_FAILED) pace(x);
    seg(x, 1, t0);
    if (!x->raster)                                      /* a frame that rasterizes a status pill (~4 ms) skips it */
        bg_work(LD_VERBOSE && s_bg.wraps == 0u, t0);      /* stage, decode, transfer (fake: the wrap's CRC) */
    x->raster = 0;
    if (s_bg.rc && !x->bg_notice) x->bg_notice = BG_NOTICE;   /* before or after the loop starts */
    om_tween_step(&x->cover);
    om_tween_step(&x->tfade);
    om_tween_step(&x->prog);
    x->mq++;
    if (x->bg_notice > 1u) x->bg_notice--;
    {
        uint16_t td = om_ut_frc();
        om_v1_begin();
        om_v1_set_safe(&g_om_safe);                      /* the loading screen follows the Safe area */
        om_v2_set_safe(&g_om_safe);
        draw_cover(x);
        seg(x, 2, t0);
        draw_text(x);
        om_v1_set_safe(NULL);
        om_v2_set_safe(NULL);
        seg(x, 3, t0);
        if ((uint32_t)us_since(td) > x->out->draw_us && !x->crc_frame) x->out->draw_us = (uint32_t)us_since(td);
    }
    {
        uint32_t us = (uint32_t)us_since(t0);
        if (us > x->out->frame_us && !x->crc_frame) x->out->frame_us = us;
    }
    diag(x);
}

void om_loading_run(const om_load_game *g, const om_launch_bufs *lb, const om_load_env *env, om_load_out *out)
{
    static ld_ctx x;
    char l1[160], l2[160];
    int i;
    memset(&x, 0, sizeof x);
    memset(out, 0, sizeof *out);
    memset(&s_bg, 0, sizeof s_bg);
    memset(s_seg, 0, sizeof s_seg);
    x.g = g; x.lb = lb; x.env = env; x.out = out;
    x.ld.fd = -1;
    x.failed_step = -1;
    x.state = ST_WAIT_BG;
    x.tool = g->label != NULL;
    x.marquee = env->anim != OM_ANIM_OFF;
    x.txt = env->sty->txt;
    x.acc = env->sty->acc;
    x.acc2 = env->sty->acc2;
    x.err = env->sty->err;
    if (LD_VERBOSE)                                      /* the menu's own diagnostic rows */
        for (i = 24; i < 28; i++) om_text_put(0, i, "", 40);
    {   /* the still first frame must be whole when the screen fades up: if the menu's fade-out did not bring the keyframe
         * in, read it now, while black (<= ~50 KB; bounded: 2 s, then the dark fallback and the loop's own start) */
        uint32_t v0 = om_v2_vblanks();
        while (s_pre.phase == 1 && om_v2_vblanks() - v0 < 120u) {
            preload_step(15000, om_ut_frc());
            bg_try_start();
            if (s_bg.started || s_bg.dead) break;
        }
    }
    /* the screen is black (the menu faded out): set it up. NBG2 first: the menu's panels go at once, not 8 cells a
     * frame (om_v2_rects_end's budget would take ~50 frames to clear them) */
    {   /* the drop shadow: an NBG2 soft rectangle, translucent over the moving background (slot 2, as the menu's), at
         * the settled cover's place, staged once: the cover slides into it during the fade-up (a moving soft edge
         * re-renders NBG2 cells every frame, measured ~3-4 ms on Mednafen, and the menu's per-frame cell budget of 8
         * would draw it over ~40 frames) */
        int16_t q[8];
        om_tween_jump(&x.cover, OM_Q);
        cover_quad(&x, q);
        om_v2_set_safe(&g_om_safe);                      /* the shadow follows the mapped cover */
        om_v2_rects_begin();
        om_v2_rect(1, 1, 0, 0, 0, 0);
        om_v2_rect(1, 1, 0, 0, 0, 0);
        if (x.tool && !g->art)
            om_v2_rect(1, 1, 0, 0, 0, 0);                /* no cover: no shadow */
        else
            om_v2_rect(q[0] + 4, q[3] + 8, q[2] + 4 > 302 ? 302 : q[2] + 4, q[5] + 6 > 206 ? 206 : q[5] + 6,
                       OM_V2_COL_SHADOW, 1);
        om_v2_rect(1, 1, 0, 0, 0, 0);
        om_v2_rects_end();
        om_v2_rects_flush();
        om_v2_set_safe(NULL);
    }
    bg_try_start();
    if (!s_bg.started) bg_fallback();
    text_bgs();
    x.dim = om_ui_mix(s_bgc[2], x.txt, 160);             /* mockup (150,165,190) over the scrim */
    x.faint = om_ui_mix(s_bgc[3], x.txt, 110);
    om_ut_budget_us(1 << 30);                            /* black: rasterize everything now */
    om_ut_text_tracked(&s_lab, OM_FACE_LABEL, g->label ? g->label : "NOW LOADING", OM_LD_TRACK8);
    x.lines = om_title_wrap(&om_face_hero, g->title, om_box_w(OM_B_LD_TITLE), l1, sizeof l1, l2, sizeof l2);
    om_ut_text(&s_title[0], OM_FACE_HERO, l1, x.marquee ? -om_box_w(OM_B_LD_TITLE) : om_box_w(OM_B_LD_TITLE));
    om_ut_text(&s_title[1], OM_FACE_HERO, l2, x.marquee ? -om_box_w(OM_B_LD_TITLE) : om_box_w(OM_B_LD_TITLE));
    om_ut_text(&s_meta[0], OM_FACE_SMALL, g->meta1, x.marquee ? -om_box_w(OM_B_LD_META1) : om_box_w(OM_B_LD_META1));
    om_ut_text(&s_meta[1], OM_FACE_SMALL, g->meta2, x.marquee ? -om_box_w(OM_B_LD_META2) : om_box_w(OM_B_LD_META2));
    om_ut_text(&s_hint, OM_FACE_SMALL, "Hold A+B+C+Start to cancel", 0);
    for (i = 0; i < 3; i++) {
        char s[40];
        strcpy(s, k_step[i]);
        om_ut_text(&s_step[i], OM_FACE_SMALL, s, 0);
        strcat(s, "\xE2\x80\xA6");                       /* "..." as one glyph: the step in progress */
        om_ut_text(&s_stepa[i], OM_FACE_SMALL, s, 0);
    }
    if (g->art) {                                        /* the translucent reflection (HWF-48), built while black */
        uint32_t tex;
        int w, h, tall;
        if (om_art_hero(&tex, &w, &h, &tall))
            refl_build(tall ? (int)REFL_ROWS_T : (int)REFL_ROWS_S, tall);
    } else
        s_refl_w = 0;
    refl_vdp2(1);
    om_tween_run(&x.cover, 0, OM_Q, om_anim_frames(300, env->anim, 0, env->fps));
    om_tween_run(&x.tfade, 0, 256, om_anim_frames(200, env->anim, 0, env->fps));
    om_tween_jump(&x.prog, 0);
    x.min_up = env->fps == 5000u ? MIN_UP_PAL : MIN_UP_NTSC;
    set_target(&x, OM_Q / 20);                           /* a sliver: the screen is up, nothing done yet */
    if (s_pre.early_got) {   /* eye-animation ruling, option b: a panel read ahead, so the rest
                              * (the music region's part, after the music's fade) is read now, while black: the whole
                              * file is in RAM before the fade-up and the loop plays at full speed from its first
                              * frame. Bounded at BLACK_CAP (0.3 s), after which it plays as the bytes
                              * arrive (as without a panel). */
        uint32_t v0 = om_v2_vblanks();
        while (s_pre.phase == 1 && om_v2_vblanks() - v0 < BLACK_CAP)   /* was 120 (2 s) */
            preload_step(15000, om_ut_frc());
        s_pre.black_vbl = om_v2_vblanks() - v0;
    }
    {   /* still black: read on until the loop's first entry is in too (frame 1, ~26 KB: ~0.1 s at the user's card
         * rate), so the background is moving from the first frame of the fade-up; bounded at 1 s, then it plays as
         * soon as the bytes arrive */
        uint32_t v0 = om_v2_vblanks();
        int k;
        while (s_bg.started && !s_bg.dead && s_pre.phase == 1 && !bg_entry_in() && om_v2_vblanks() - v0 < 60u)
            preload_step(15000, om_ut_frc());
        /* and decode it and move it into the bitmap off screen now (~10 ms + one DMA): the fade-up's first frame flips
         * to it, instead of waiting for a decode shared with the reads (~2 ms a V-blank on hardware) */
        for (k = 0; k < 8 && s_bg.started && !s_bg.dead && s_bg.ph != P_XFER && bg_entry_in(); k++)
            bg_work(0, om_ut_frc());
        if (s_bg.started && !s_bg.dead && s_bg.ph == P_XFER) {
            if (dma0_wait_bounded() == OM_OK) {
                bmp_xfer(s_bg.disp ^ 1u);
                if (dma0_wait_bounded() == OM_OK) {
                    s_bg.ready = 1;                      /* bg_flip_step flips to it at the first frame */
                    s_bg.ph = P_STAGE;
                } else
                    bg_fail(OM_LBG_E_DMA);
            } else
                bg_fail(OM_LBG_E_DMA);
        }
    }
    om_v1_begin();
    slSynch();                                           /* the setup ended mid-frame: start the loop on a V-blank */
    x.fade_vbl = om_v2_vblanks();
    om_fade_in(FADE_MASK, FADE_MS, env->anim, frame, &x);
    x.up_vbl = om_v2_vblanks();                          /* fully faded up: the minimum time starts */
    if (!x.up_vbl) x.up_vbl = 1;
    while (!x.exiting) {
        frame(&x);
        slSynch();
    }
    if (out->result != OM_LOAD_HANDOFF)                  /* a hand-off keeps the eye up: LAUNCHER.BIN continues it */
        om_fade_out(FADE_MASK, FADE_MS, env->anim, frame, &x);
    out->bg_frames = s_bg.shown;
    out->bg_wraps = s_bg.wraps;
    out->bg_held = s_bg.held;
    out->bg_rc = s_bg.rc;
    out->bg_late = s_bg.late;
    out->pre_vbl = s_pre.phase == 2 ? s_pre.t_vbl : 0;
    if (s_pre.phase == 1) pre_fail(OM_E_FIELD);          /* never leave the file open */
    if (LD_VERBOSE)                                      /* the menu's header shows again: its rows are cleared */
        for (i = 0; i < 3; i++) om_text_put(0, i, "", 40);
    slZoomNbg1(toFIXED(1.0), toFIXED(1.0));              /* the menu's NBG1 is 1:1 */
    refl_vdp2(0);                                        /* sprite color calculation off: the menu's VDP2 state */
    if (bg_dbuf_end() != OM_OK && out->result != OM_LOAD_CANCEL) {   /* level 0 still writing: */
        out->result = OM_LOAD_FAILED;                    /* never hand off over it */
        out->rc = OM_LBG_E_DMA;
        om_msg_rc(out->msg, sizeof out->msg, "LAUNCH STOPPED: BACKGROUND ", om_strerror(OM_LBG_E_DMA), OM_LBG_E_DMA);
    }
}
