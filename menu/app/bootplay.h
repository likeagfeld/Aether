/* menu/app/bootplay.h -- the boot animation player: streams BOOTANIM.BIN from the
 * menu disc through the CD block before API mode, decodes it (menu/src/bootanim.c) into two 8 bpp VDP1 textures and
 * shows one frame per V-blank, with the 30 fps fallback, a button skip and a 150 ms color-offset fade to black.
 * BSD-3-Clause. */
#ifndef OM_BOOTPLAY_H
#define OM_BOOTPLAY_H
#include <stdint.h>

enum { OM_BOOTEV_CHANT = 1, OM_BOOTEV_DONE = 2 };
/* CHANT: called once, right after the slSynch that hands the list with the chant frame (137, or the first frame >= 137
 * at 30 fps) to VDP1, i.e. the V-blank it is submitted for; SGL's VDP1 frame buffers then show it one frame change
 * later (latency not measured: Task 15 should check the audio sync by ear on hardware). DONE: once, when the animation
 * ends by itself, by a skip, or on a stream error, before the 150 ms fade. CHANT starts the "Se-ga" chant
 * (om_audio_chant) with no latency compensation; both calls are counted in g_bootplay. */
void om_bootanim_event(int ev);

#define OM_BP_E_ARENA  -90                  /* the borrowed buffer is smaller than the player needs */
#define OM_BP_E_HEAD   -91                  /* the first sectors never arrived (CD read timeout) */
#define OM_BP_E_STALL  -92                  /* no sector for 1 s, or the restarts ran out (menu/src/bootstream.c) */
#define OM_BP_E_SIZE   -93                  /* BOOTANIM.BIN's directory size disagrees with its header */
#define OM_BP_E_TIME   -94                  /* frame 0 not ready within the 4 s start budget */
#define OM_BP_E_SLOW   -95                  /* the whole intro passed its 15 s budget */
typedef struct {
    int rc;                                 /* 0 = played or skipped; < 0 = not played (or cut short), see step */
    const char *step;                       /* the step that failed: "pvd", "root dir", "find", "stream", ... */
    int32_t cdc;                            /* om_cd_last_error() at the failure: which LIBCD call, which CdcErr */
    uint16_t hirq0;                         /* CD HIRQ when the player started */
    int32_t reset_rc[4];                    /* AbortFile, DataEnd, ResetSelector, SetSctLen */
    uint32_t frames, shown, vblanks, underruns, fallback_frame, chant_frame, chant_vbl, done_vbl, skipped;
    uint32_t chant_events, done_events;     /* om_bootanim_event calls (each must be 1) */
    uint32_t fallback_reason;               /* 0 none, 1 ring underrun, 2 an iteration overran its V-blank */
    uint32_t missed_vblanks;                /* loop iterations that took longer than one V-blank */
    uint32_t dec_worst_us, dec_worst_frame, up_worst_us, frame_worst_us;   /* decode, VDP1 upload, both (FRT) */
    uint32_t pump_worst_us;                 /* moving sectors from the CD block to the ring, per V-blank */
    uint32_t dec_sum_us, up_sum_us;         /* totals over the frames shown (averages = sum / shown) */
    uint32_t vram_writes;                   /* 16-bit VDP1 VRAM writes (a full frame is 11,176) */
    int crc_last;                           /* 1 = the last even frame decoded on the SH-2 matches its index CRC-32 */
    uint32_t bytes, read_vblanks, peak_window, restarts, preroll_vblanks;   /* CD: bytes in, V-blanks it took, 1 s peak */
    /* hardware report 2026-11-19 (RST1, then frame 76 failed): what the first restart was, and what was recovered */
    uint32_t rst_why, rst_sector, rst_vb;   /* OM_BS_WHY_* (bootstream.h), file sector, V-blank from entry */
    int32_t rst_err, rst_cdc;               /* its io error, and om_cd_last_error() when the restart began */
    uint32_t dropped;                       /* sectors of an earlier play the sector check deleted */
    uint32_t resyncs, resync_frame;         /* frames re-read after a decode failure; the first such frame */
    int32_t resync_rc;                      /* and its decode error */
    uint32_t chant_late;                    /* 1 = the intro was cut short before frame 137, the chant played at the end */
} om_bootplay_diag;
extern om_bootplay_diag g_bootplay;         /* Task 16a's diagnostic screen reads this */

/* arena: a buffer the menu needs only later (main.c passes the card-scan work area), at least OM_BP_ARENA bytes,
 * 4-byte aligned; it is zeroed again before return. counter != 0 prints frame / V-blank numbers (fake build). */
#define OM_BP_ARENA 245760u          /* ring 128 KB + head 8 KB + 2 frames + wrap 16 KB + 2 shadows (bootplay.c) */
int om_bootplay(uint8_t *arena, uint32_t len, int counter);
/* After om_bootplay the screen is black through the color offset; the start-up interstitial (menu/views/interstitial.c,
 * Task 13d) fades up from there (om_bootplay_fade_in was removed with it). */
/* 40-column lines. which 0: "INTRO SKIPPED: <step> rc=<n> [cdc=<n>]" after a failure, else empty. Diagnostics (the fake
 * build prints them): 1 frames shown, chant frame and V-blank, 60 FPS or 30FPS@<fallback frame>; 2 worst decode and
 * VDP1 upload in microseconds, missed V-blanks, underruns; 3 CD average and 1 s peak in KB/s, pre-roll V-blanks,
 * stream restarts; 4 (empty when the stream had no trouble) "[RST <why><sector>V<vblank> E<io err>/<cdc>] DRP<dropped>
 * RSY<re-syncs>[F<first frame>] [CL] [CRC OK|BAD]", why = C count error, T take error, S stall, I sector-info error. */
void om_bootplay_line(char out[41], int which);
#endif
