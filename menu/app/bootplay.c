/* menu/app/bootplay.c -- see bootplay.h. BSD-3-Clause.
 *
 * Data path: ISO 9660 lookup of BOOTANIM.BIN (menu/src/isodir.c) -> one CD-block play of the whole file into buffer
 * partition 0 (menu/launcher/cdblk.c, ST-38-R1 via SEGA's LIBCD) -> a 128 KB ring in the borrowed arena -> byte-op
 * decode in place (menu/src/bootanim.c) -> the changed 32-bit words written into the back one of two 176x128 8 bpp
 * textures in VDP1 VRAM (OM_VRAM_BOOTANIM, the slots the retired placeholder freed; docs/ledger.md) -> one sprite in
 * 256-color bank mode, bank 0x300, so dot d reads CRAM word 768 + d (HWF-44). The CD block keeps its own 200-sector
 * buffer filling while the ring is full, so the two together absorb the peaks.
 *
 * Double buffering follows art.c: the slot written in this iteration is the one VDP1 drew two lists ago, never the one
 * on screen. */
#include <string.h>
#include "sgl.h"
#include "audio.h"
#include "bootplay.h"
#include "bootanim.h"
#include "cdblk.h"
#include "isodir.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "pad.h"
#include "text.h"
#include "uitext.h"
#include "v1.h"
#include "vdp2theme.h"
#include "vertag.h"
#include "anim.h"
#include "bootstream.h"
#include "fade.h"

#define RING        (128u * 1024u)                 /* power of two: file byte x sits at ring[x & (RING - 1)] */
#define HEAD        8192u                          /* header + info + palette + index: 592 + 12 x 512 frames */
#define FAD0        150                            /* LBA 0 = FAD 150 (track 1 at 00:02:00) */
#define PUMP_MAX    4u                             /* sectors moved per V-blank (2.3 ms worst at 8 on Mednafen) */
#define PREROLL     (80u * 1024u)                  /* before frame 0 (or the whole file): +7 V-blanks over 48 KB */
#define START_VB    240u                           /* V-blanks from entry to frame 0 (4 s), then the named skip */
#define TOTAL_VB    900u                           /* the whole intro, 30 fps fallback included (15 s) */
#define BOOT_SPINS  100000u                        /* HIRQ polls before a LIBCD command (the launcher keeps 20,000,000) */
#define BANK        0x0300u                        /* CMDCOLR: CRAM 768..1023 (HWF-44; ledger D4: unclaimed) */
#define FADE_VB     9u                             /* 150 ms at 60 Hz */
#define SX          ((320 - (int)OM_BA_MAX_W) / 2)
/* 2026-11-25 hardware report ("a little lower than screen center"): BOOTANIM.BIN's 176x127 frames hold the logo low --
 * the sphere and rings span rows ~19..121 (bounding-box centre 69.5 over the frames, median; the frame's 63), so a
 * frame centred on the 224 lines put the logo 6 lines low (Mednafen: +5.5 lines). The frame is lifted by that much;
 * the asset is unchanged. */
#define LIFT        6
#define RESYNC_MAX  3u                             /* frames that failed to decode, each re-read once (HW report 11-19) */

typedef struct {
    uint8_t ring[RING];
    uint8_t head[HEAD];
    uint8_t slots[2u * OM_BA_MAX_PIX];
    uint8_t lin[OM_BA_MAX_FRAME];
    uint8_t shadow[2][OM_BA_MAX_PIX];              /* what each VDP1 texture slot holds: only changed words are sent */
} arena_t;
typedef char om_bp_arena_size[sizeof(arena_t) == OM_BP_ARENA ? 1 : -1];   /* bootplay.h's figure is the real one */

static arena_t *A;

/* VDP1 VRAM is written 16 bits at a time (HWF-23) and each write crosses the B-bus, which made a full 22 KB upload cost
 * 8.7 ms on Mednafen (measured). The slot's previous contents are known (shadow), so compare 32-bit words in
 * HWRAM and write only those that changed. Returns the 16-bit writes made. */
typedef uint32_t __attribute__((may_alias)) u32a;
static uint32_t upload(uint32_t slot, const uint8_t *pix, uint32_t bytes)
{
    const u32a *src = (const u32a *)pix;
    u32a *sh = (u32a *)A->shadow[slot];
    uint32_t base = OM_CT(OM_VDP1_VRAM) + OM_VRAM_BOOTANIM + slot * OM_BA_MAX_PIX;
    volatile Uint16 *v = (volatile Uint16 *)base;
    uint32_t i, words = bytes / 4u, n = 0;
    for (i = 0; i < words; i++) {
        uint32_t w = src[i];
        if (w != sh[i]) {
            sh[i] = w;
            v[2u * i] = (Uint16)(w >> 16);
            v[2u * i + 1u] = (Uint16)w;
            n += 2u;
        }
    }
    return n;
}

om_bootplay_diag g_bootplay;

void om_bootanim_event(int ev)
{   /* the chant starts here (menu/app/audio.c); the frame and V-blank are in g_bootplay */
    if (ev == OM_BOOTEV_CHANT) { g_bootplay.chant_events++; om_audio_chant(); }
    else if (ev == OM_BOOTEV_DONE) g_bootplay.done_events++;
}

static uint32_t s_file_size, s_nsec, s_fad;        /* the file: bytes, sectors, first FAD */
static uint32_t s_win[60], s_win_i, s_win_sum, s_last_rx_vb, s_first_rx_vb;
static om_bs s_bs;                                 /* the read-ahead (menu/src/bootstream.c) */
static Uint16 s_pad0;                              /* buttons already held: only a new press skips */
static uint32_t s_t0;                              /* om_v2_vblanks() at entry: every budget is in real V-blanks */

static int32_t s_sh_frame[2];                      /* the frame each shadow (VDP1 slot) holds; -2 = black */
static uint8_t s_rst_cap;                          /* the first restart's om_cd_last_error() is in g_bootplay */

static int32_t io_count(void *c) { (void)c; return om_cd_stream_count(); }
static int io_take(void *c, uint8_t *dst, uint32_t n) { (void)c; return om_cd_take(dst, (int32_t)n); }
static int32_t io_info(void *c, uint32_t spos) { (void)c; return om_cd_sector_fad((int32_t)spos); }
static int io_drop(void *c, uint32_t n) { (void)c; return om_cd_drop((int32_t)n); }
/* A restart (or re-sync). HWF-56: the reset (AbortFile .. ResetSelector) only empties the buffer and disconnects the
 * drive from filter 0; the play goes on, and the old code's CdSetCon(0) before the new CdPlay let it land sectors of
 * the OLD play in partition 0 (the hardware run of 2026-11-19). So: pause the play first, then the reset and the new
 * play; whatever of the old play still arrives is dropped by om_bs_pump's sector check (io_info). */
static int io_start(void *c, uint32_t first, uint32_t n)
{
    int rc[4];
    (void)c;
    if (!s_rst_cap) { s_rst_cap = 1; g_bootplay.rst_cdc = om_cd_last_error(); }   /* the first: which LIBCD call */
    om_cd_pause();
    om_cd_reset(rc);
    return om_cd_stream_start((int32_t)(s_fad + first), (int32_t)n);
}

static uint32_t now(void) { return om_v2_vblanks(); }

static int pressed(void)                           /* any button pressed since the intro began */
{
    Uint16 pad = om_pad_held(), fresh = (Uint16)(pad & ~s_pad0);
    s_pad0 = pad;
    if (fresh) g_bootplay.skipped = 1;
    return fresh != 0;
}

static int32_t pump(void)
{
    uint32_t t = now(), got = s_bs.got;
    int32_t r = om_bs_pump(&s_bs, t, PUMP_MAX);
    uint32_t in = s_bs.got - got;
    if (in) {
        if (!g_bootplay.bytes) s_first_rx_vb = t - s_t0;
        s_last_rx_vb = t - s_t0;
        g_bootplay.bytes = s_bs.got;
    }
    s_win_sum += in;                                   /* bytes received in the last 60 calls (1 s at 60 fps) */
    s_win_sum -= s_win[s_win_i];
    s_win[s_win_i] = in;
    if (++s_win_i == 60u) s_win_i = 0;
    if (s_win_sum > g_bootplay.peak_window) g_bootplay.peak_window = s_win_sum;
    g_bootplay.restarts = s_bs.restarts;
    g_bootplay.dropped = s_bs.dropped;
    g_bootplay.rst_why = s_bs.why;
    g_bootplay.rst_sector = s_bs.why_sector;
    g_bootplay.rst_vb = s_bs.why ? s_bs.why_vb - s_t0 : 0;
    g_bootplay.rst_err = s_bs.why_err;
    return r;
}

/* Frame `want` (at file byte `off`) would not decode: its data was bad. Put the decoder back on the chain (an odd frame
 * left E alone; an even one gets ref(want) back from the shadow of the slot it was uploaded to: at 60 and at 30 fps
 * one of the two holds want - 2) and re-read the stream from that frame's sector. Bounded; 0 = retrying. */
static int resync(om_ba_dec *dec, const om_ba *a, uint32_t want, uint32_t off)
{
    if (g_bootplay.resyncs >= RESYNC_MAX) return -1;
    if (!(want & 1u)) {
        int32_t ref = (int32_t)want - 2;
        int j = s_sh_frame[0] == ref ? 0 : s_sh_frame[1] == ref ? 1 : -1;
        if (j < 0) return -1;
        om_ba_dec_restore(dec, a, ref, A->shadow[j]);
    } else if (dec->e_frame != (int32_t)want - 1) return -1;
    g_bootplay.resyncs++;
    om_bs_seek(&s_bs, off, now());
    return 0;
}

/* Blocking sector read for the ISO lookup, bounded by the start budget and skippable. 0, a CD error, or WAIT_* */
#define WAIT_SKIP  1
#define WAIT_LATE  2
static int read_now(uint8_t *dst, uint32_t fad, uint32_t n, uint32_t deadline)
{
    int rc;
    if ((rc = om_cd_stream_start((int32_t)fad, (int32_t)n)) != 0) return rc;
    for (;;) {
        int32_t c = om_cd_stream_count();
        if (c < 0) return c;
        if ((uint32_t)c >= n) return om_cd_take(dst, (int32_t)n);
        if (pressed()) return WAIT_SKIP;
        if ((int32_t)(now() - deadline) >= 0) return WAIT_LATE;
        om_v1_begin();
        slSynch();
    }
}

static int find_file(uint32_t deadline)
{
    uint32_t lba, size, n;
    int rc;
    if ((rc = read_now(A->lin, FAD0 + OM_ISO_PVD_LBA, 1, deadline)) != 0) { g_bootplay.step = "pvd read"; return rc; }
    if ((rc = om_iso_root(A->lin, &lba, &size)) != 0) { g_bootplay.step = "pvd"; return rc; }
    n = (size + OM_SECTOR - 1u) / OM_SECTOR;
    if (n > sizeof A->lin / OM_SECTOR) n = sizeof A->lin / OM_SECTOR;
    if ((rc = read_now(A->lin, FAD0 + lba, n, deadline)) != 0) { g_bootplay.step = "root dir read"; return rc; }
    if ((rc = om_iso_find(A->lin, n * OM_SECTOR, "BOOTANIM.BIN", &lba, &size)) != 0) {
        g_bootplay.step = "BOOTANIM.BIN not on the disc";
        return rc;
    }
    s_fad = FAD0 + lba;
    s_file_size = size;
    s_nsec = (size + OM_SECTOR - 1u) / OM_SECTOR;
    return 0;
}

static void counter(int on, uint32_t f, uint32_t v)
{
#ifndef OMSAT_FAKECARD                               /* the developer counter exists in the fake build only */
    (void)on; (void)f; (void)v;
#else
    char a[12], b[12];
    if (!on) return;
    om_text_put(1, 26, "BOOT FRAME", 10);
    om_text_put(12, 26, om_itoa(a, (int)f), 4);
    om_text_put(17, 26, "VBLANK", 6);
    om_text_put(24, 26, om_itoa(b, (int)v), 4);
    if (g_bootplay.chant_vbl != OM_BA_NONE) {                    /* from the chant event on: where it fired */
        om_text_put(1, 25, "CHANT EVENT AT FRAME", 20);
        om_text_put(22, 25, om_itoa(a, (int)g_bootplay.chant_frame), 4);
        om_text_put(26, 25, "VBLANK", 6);
        om_text_put(33, 25, om_itoa(b, (int)g_bootplay.chant_vbl), 4);
    }
#endif
}

static int s_draw_slot = -1;
static uint32_t s_draw_h;
static void draw(void *ctx)
{
    (void)ctx;
    om_v1_begin();
    if (s_draw_slot >= 0)
        om_v1_sprite_bank(SX, (224 - (int)s_draw_h) / 2 - LIFT, OM_VRAM_BOOTANIM + (uint32_t)s_draw_slot * OM_BA_MAX_PIX,
                          (int)OM_BA_MAX_W, (int)s_draw_h, BANK, 100);
    om_vertag_draw(0x0000u, 256u, 90);               /* "FARKUS 0.1" bottom right, on the black backdrop */
}

/* Pump until `bytes` are in: 0, WAIT_SKIP (a button), WAIT_LATE (the deadline passed) or OM_BS_E_STALL. */
static int wait_for(uint32_t bytes, uint32_t deadline)
{
    for (;;) {
        int32_t r = pump();
        if (r < 0) return (int)r;
        if (s_bs.got >= bytes) return 0;
        if (pressed()) return WAIT_SKIP;
        if ((int32_t)(now() - deadline) >= 0) return WAIT_LATE;
        om_v1_begin();
        om_vertag_draw(0x0000u, 256u, 90);
        slSynch();
    }
}

static const char *wait_step(int w, const char *late)
{
    return w == WAIT_LATE ? late : w == OM_BS_E_STALL ? "stream stalled" : "CD read";
}

int om_bootplay(uint8_t *arena, uint32_t len, int show_counter)
{
    om_ba a;
    om_ba_dec dec;
    om_ba_pacer p;
    om_bs_io io = { io_count, io_take, io_start, 0, io_info, io_drop };
    uint32_t i, need, prev_vb, start_by, end_by, off0, len0, crc0;
    int rc = 0, w, started = 0, back = 0, cdrc[4];
    memset(&g_bootplay, 0, sizeof g_bootplay);
    g_bootplay.fallback_frame = g_bootplay.chant_frame = g_bootplay.chant_vbl = g_bootplay.done_vbl = OM_BA_NONE;
    g_bootplay.step = "";
    s_draw_slot = -1;
    om_ba_pacer_init(&p, 0, 0);
    if (len < sizeof(arena_t) || ((uint32_t)arena & 3u)) { g_bootplay.step = "arena"; rc = OM_BP_E_ARENA; goto fail; }
    A = (arena_t *)arena;
    s_win_i = s_win_sum = 0;
    memset(s_win, 0, sizeof s_win);
    slIntFunction(om_v2_vblank);                     /* the V-blank counter every budget below reads */
    slScrDisp(NBG1OFF | NBG2OFF | (show_counter ? NBG0ON : NBG0OFF) | NBG3OFF | RBG0OFF);
    slBack1ColSet((void *)BACK_CRAM, 0x0000);
    om_text_clear();
    om_vertag_init();                                /* rasterised once, here, outside every wait */
    slSynch();
    s_t0 = now();
    start_by = s_t0 + START_VB;                      /* one budget from here to frame 0 */
    end_by = s_t0 + TOTAL_VB;                        /* and one for the whole intro */
    s_pad0 = om_pad_held();
    om_cd_spin_cap(BOOT_SPINS);                      /* a wedged CD block costs ms per command, not s */
    om_cd_clear_error();
    g_bootplay.hirq0 = om_cd_hirq();
    while (!om_cd_ready()) {                         /* the short ready check: CMOK, a V-blank at a time */
        if (pressed()) goto stop;
        if ((int32_t)(now() - start_by) >= 0) { g_bootplay.step = "CD block busy"; rc = OM_CD_E_CMOK; goto fail; }
        slSynch();
    }
    om_cd_reset(cdrc);                               /* not om_cd_init_for_boot: its BIOS work words are the launcher's */
    for (i = 0; i < 4u; i++) g_bootplay.reset_rc[i] = cdrc[i];
    started = 1;                                     /* from here a play may be running: always stop it */
    if ((rc = find_file(start_by)) != 0) {
        if (rc == WAIT_SKIP) { rc = 0; goto stop; }
        if (rc == WAIT_LATE) { g_bootplay.step = "intro start timed out"; rc = OM_BP_E_TIME; }
        goto fail;
    }
    io.ctx = 0;
    om_bs_init(&s_bs, &io, A->ring, RING, s_nsec, now());
    s_bs.base = s_fad;                               /* io_info answers FADs */
    s_rst_cap = 0;
    if ((rc = om_cd_stream_start((int32_t)s_fad, (int32_t)s_nsec)) != 0) { g_bootplay.step = "stream start"; goto fail; }
    if ((w = wait_for(OM_BA_INDEX_OFF, start_by)) != 0) goto waited;
    need = om_ba_head_bytes(A->ring, s_bs.got);
    if (need > HEAD) need = HEAD;
    if ((w = wait_for(need, start_by)) != 0) goto waited;
    memcpy(A->head, A->ring, need);
    if ((rc = om_ba_open(&a, A->head, need, s_file_size)) != 0) { g_bootplay.step = "BOOTANIM.BIN header"; goto fail; }
    if (a.data_end > s_file_size) { g_bootplay.step = "file size"; rc = OM_BP_E_SIZE; goto fail; }
    for (i = 0; i < 256u; i++)                       /* CRAM 768..1023 (HWF-30 mode 0, HWF-44) */
        ((volatile Uint16 *)OM_CRAM)[768u + i] = om_ba_color(&a, i);
    memset(A->shadow, 0, sizeof A->shadow);          /* and the slots to match: index 0 = black */
    s_sh_frame[0] = s_sh_frame[1] = -2;
    for (i = 0; i < 2u * OM_BA_MAX_PIX / 2u; i++)
        ((volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_BOOTANIM))[i] = 0;
    om_ba_dec_init(&dec, &a, A->slots);
    om_ba_pacer_init(&p, a.count, om_ba_chant_frame(&a));
    g_bootplay.frames = a.count;
    s_bs.keep = a.data_off;
    om_ba_entry(&a, 0, &off0, &len0, &crc0);
    w = wait_for(s_file_size < a.data_off + PREROLL ? s_file_size : a.data_off + PREROLL, start_by);
    if (w == WAIT_LATE && s_bs.got >= off0 + len0) w = 0;   /* out of pre-roll time but frame 0 is in: play */
    if (w != 0) goto waited;
    g_bootplay.preroll_vblanks = now() - s_t0;
    s_draw_h = a.h;
    prev_vb = now();
    while (!p.done) {
        uint32_t want = om_ba_pacer_want(&p), off = 0, flen = 0, crc;
        uint16_t t0 = om_ut_frc(), t1 = t0, t2 = t0;
        unsigned ev;
        int ready = 0;
        if (pump() < 0) { g_bootplay.step = "stream stalled"; rc = OM_BP_E_STALL; goto stop; }
        {
            uint16_t tp = om_ut_frc();
            uint32_t pu = om_ut_frc_us((uint16_t)(tp - t0));
            if (pu > g_bootplay.pump_worst_us) g_bootplay.pump_worst_us = pu;
            t0 = t1 = t2 = tp;
        }
        if (want != OM_BA_NONE) {
            om_ba_entry(&a, want, &off, &flen, &crc);
            if (s_bs.got >= off + flen) {
                uint32_t pos = off & (RING - 1u);
                const uint8_t *src = A->ring + pos;
                if (pos + flen > RING) {                 /* wraps: linearise into lin (rare) */
                    memcpy(A->lin, A->ring + pos, RING - pos);
                    memcpy(A->lin + (RING - pos), A->ring, flen - (RING - pos));
                    src = A->lin;
                }
                if ((rc = om_ba_decode(&dec, &a, want, src, flen)) != 0) {
                    if (!g_bootplay.resync_rc) { g_bootplay.resync_rc = rc; g_bootplay.resync_frame = want; }
                    if (resync(&dec, &a, want, off) != 0) { g_bootplay.step = "frame decode"; goto stop; }
                    rc = 0;                          /* not ready: the pacer holds the last frame (an underrun) */
                } else {
                    t1 = om_ut_frc();
                    back = s_draw_slot == 0 ? 1 : 0;
                    g_bootplay.vram_writes += upload((uint32_t)back, dec.pix, a.w * a.h);
                    s_sh_frame[back] = (int32_t)want;
                    t2 = om_ut_frc();
                    s_bs.keep = off + flen;
                    ready = 1;
                }
            }
        }
        ev = om_ba_pacer_tick(&p, ready);
        if (ev & OM_BA_EV_SHOW) {
            uint32_t d = om_ut_frc_us((uint16_t)(t1 - t0)), u = om_ut_frc_us((uint16_t)(t2 - t1));
            s_draw_slot = back;
            g_bootplay.shown++;
            if (d > g_bootplay.dec_worst_us) { g_bootplay.dec_worst_us = d; g_bootplay.dec_worst_frame = want; }
            if (u > g_bootplay.up_worst_us) g_bootplay.up_worst_us = u;
            g_bootplay.dec_sum_us += d;
            g_bootplay.up_sum_us += u;
            if (d + u > g_bootplay.frame_worst_us) g_bootplay.frame_worst_us = d + u;
        }
        if (ev & OM_BA_EV_CHANT) {                       /* ruling M3-R38 item 5: the V-blank frame 137 goes up */
            g_bootplay.chant_frame = want;
            g_bootplay.chant_vbl = p.chant_vbl;
        }
        if (ev & OM_BA_EV_FALLBACK) g_bootplay.fallback_frame = p.fallback_frame;
        draw(0);
        counter(show_counter, p.shown == OM_BA_NONE ? 0u : p.shown, p.vbl - 1u);   /* pacer V-blank: frame f at f */
        if (pressed()) break;                            /* any newly pressed button skips to the fade */
        slSynch();
        if (ev & OM_BA_EV_CHANT)                         /* after the slSynch that hands frame 137's list to VDP1 */
            om_bootanim_event(OM_BOOTEV_CHANT);
        {
            uint32_t t = now();
            if (t - prev_vb > 1u) {                      /* the iteration overran a V-blank: fall back to 30 fps */
                g_bootplay.missed_vblanks += t - prev_vb - 1u;
                if (p.shown != OM_BA_NONE && (om_ba_pacer_slow(&p) & OM_BA_EV_FALLBACK))
                    g_bootplay.fallback_frame = p.fallback_frame;
            }
            prev_vb = t;
            if ((int32_t)(t - end_by) >= 0) { g_bootplay.step = "intro too slow"; rc = OM_BP_E_SLOW; goto stop; }
        }
    }
    if (p.done) {                                    /* the even chain end to end on the SH-2: frame 214's CRC-32 */
        uint32_t off, flen, crc;
        om_ba_entry(&a, (uint32_t)dec.e_frame, &off, &flen, &crc);
        g_bootplay.crc_last = om_crc32(0, dec.e, a.w * a.h) == crc ? 1 : -1;
    }
    goto stop;
waited:                                              /* a start-up wait ended without its data */
    if (w == WAIT_SKIP) goto stop;
    rc = w == WAIT_LATE ? OM_BP_E_TIME : OM_BP_E_STALL;
    g_bootplay.step = wait_step(w, "intro start timed out");
    goto fail;
stop:                                                /* played, skipped, or cut short after frames were shown */
    if (rc && g_bootplay.shown && !g_bootplay.skipped && !g_bootplay.chant_events) {   /* cut short before 137: chant */
        g_bootplay.chant_late = 1;
        om_bootanim_event(OM_BOOTEV_CHANT);
    }
    om_bootanim_event(OM_BOOTEV_DONE);
    om_fade_out(SPRON | BACKON | NBG0ON, 150, OM_ANIM_FULL, draw, 0);   /* anim= is unknown before API mode */
    s_draw_slot = -1;
    draw(0);
    slSynch();
    goto out;
fail:                                                /* nothing (more) to show: black at once */
    om_bootanim_event(OM_BOOTEV_DONE);               /* DONE on every exit */
    om_fade_black(SPRON | BACKON | NBG0ON);
out:
    if (rc == OM_CD_E_CDC || rc == OM_BP_E_STALL) g_bootplay.cdc = om_cd_last_error();   /* review-13b M2 */
    if (started && om_cd_stream_stop() != 0 && !rc) g_bootplay.cdc = om_cd_last_error();  /* I3: always reset */
    om_cd_spin_cap(0);
    g_bootplay.underruns = p.underruns;
    g_bootplay.fallback_reason = p.reason;
    g_bootplay.done_vbl = p.done_vbl;
    g_bootplay.vblanks = now() - s_t0;
    g_bootplay.read_vblanks = s_last_rx_vb - s_first_rx_vb + 1u;
    if (len >= sizeof(arena_t)) memset(arena, 0, sizeof(arena_t));   /* the scan work area is bss: hand it back zeroed */
    om_pad_prime();                                  /* a held skip button is not the menu's first press */
    return g_bootplay.rc = rc;
}

/* not inlined: om_bootplay_line calls these ~60 times (HWRAM, the 11-19 fix's line 4) */
__attribute__((noinline)) static uint32_t app(char *o, uint32_t i, const char *s)
{
    while (*s && i < 40u) o[i++] = *s++;
    return i;
}

__attribute__((noinline)) static uint32_t appn(char *o, uint32_t i, int v)
{
    char n[12];
    return app(o, i, om_itoa(n, v));
}

void om_bootplay_line(char out[41], int which)
{
    const om_bootplay_diag *d = &g_bootplay;
    uint32_t i = 0;
    if (which == 0 && d->rc) {
        i = app(out, i, "INTRO SKIPPED: ");
        i = app(out, i, d->step);
        i = app(out, i, " rc=");
        i = appn(out, i, d->rc);
        if (d->cdc) { i = app(out, i, " cdc="); i = appn(out, i, (int)d->cdc); }
    } else if (which == 1) {
        i = app(out, i, "INTRO ");
        i = appn(out, i, (int)d->shown);
        i = app(out, i, "/");
        i = appn(out, i, (int)d->frames);
        i = app(out, i, d->skipped ? " SKIP" : "");
        i = app(out, i, " CHANT F");
        i = appn(out, i, d->chant_frame == OM_BA_NONE ? -1 : (int)d->chant_frame);
        i = app(out, i, " V");
        i = appn(out, i, d->chant_vbl == OM_BA_NONE ? -1 : (int)d->chant_vbl);
        if (d->fallback_frame != OM_BA_NONE) {
            i = app(out, i, " 30FPS@");
            i = appn(out, i, (int)d->fallback_frame);
            i = app(out, i, d->fallback_reason == 2 ? "CPU" : "CD");
        }
        else i = app(out, i, " 60FPS");
    } else if (which == 2) {                                     /* microseconds: worst@frame/average */
        i = app(out, i, "DEC ");
        i = appn(out, i, (int)d->dec_worst_us);
        i = app(out, i, "@");
        i = appn(out, i, (int)d->dec_worst_frame);
        i = app(out, i, "/");
        i = appn(out, i, d->shown ? (int)(d->dec_sum_us / d->shown) : 0);
        i = app(out, i, " UP ");
        i = appn(out, i, (int)d->up_worst_us);
        i = app(out, i, "/");
        i = appn(out, i, d->shown ? (int)(d->up_sum_us / d->shown) : 0);
        i = app(out, i, " PUMP ");
        i = appn(out, i, (int)d->pump_worst_us);
    } else if (which == 3) {                                     /* KB/s; V-blank counts */
        i = app(out, i, "CD ");
        i = appn(out, i, d->read_vblanks ? (int)(d->bytes * 60u / 1024u / d->read_vblanks) : 0);
        i = app(out, i, " PK");
        i = appn(out, i, (int)(d->peak_window / 1024u));
        i = app(out, i, " PRE");
        i = appn(out, i, (int)d->preroll_vblanks);
        i = app(out, i, " MISS");
        i = appn(out, i, (int)d->missed_vblanks);
        i = app(out, i, " UND");
        i = appn(out, i, (int)d->underruns);
        i = app(out, i, " RST");
        i = appn(out, i, (int)d->restarts);
        i = app(out, i, d->crc_last > 0 ? " CRC OK" : d->crc_last < 0 ? " CRC BAD" : " CRC -");
    } else if (which == 4 && (d->restarts || d->dropped || d->resyncs || d->chant_late)) {   /* HW report 11-19 */
        static const char why[] = "-CTSI";                       /* OM_BS_WHY_*: count, take, stall, sector info */
        char c[2];
        c[0] = why[d->rst_why < 5u ? d->rst_why : 0]; c[1] = 0;
        if (d->restarts) {
            i = app(out, i, "RST ");
            i = app(out, i, c);
            i = appn(out, i, (int)d->rst_sector);
            i = app(out, i, "V");
            i = appn(out, i, (int)d->rst_vb);
            i = app(out, i, " E");
            i = appn(out, i, d->rst_err);
            i = app(out, i, "/");
            i = appn(out, i, (int)d->rst_cdc);
            i = app(out, i, " ");
        }
        i = app(out, i, "DRP");
        i = appn(out, i, (int)d->dropped);
        i = app(out, i, " RSY");
        i = appn(out, i, (int)d->resyncs);
        if (d->resyncs) { i = app(out, i, "F"); i = appn(out, i, (int)d->resync_frame); }
        if (d->chant_late) i = app(out, i, " CL");
        i = app(out, i, d->crc_last > 0 ? " CRC OK" : d->crc_last < 0 ? " CRC BAD" : "");   /* line 3 may be cut */
    }
    out[i] = 0;
}
