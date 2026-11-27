/* menu/src/bootstream.h -- the boot animation's CD read-ahead: sectors move
 * from the drive (through callbacks: menu/launcher/cdblk.c on the Saturn, a fake in the host tests) into a ring, with
 * every wait measured in V-blanks, never in loop counts. Pure C. BSD-3-Clause.
 *
 * - A failed transfer may already have deleted its sectors from the CD buffer (GetdelSctData), so the stream restarts
 *   at the first sector not yet in the ring; `got` stays exact.
 * - No sector for stall_vb V-blanks while one is due (the file is not all in and the ring has room): one restart.
 *   No sector for abort_vb V-blanks, or more than max_restarts restarts: OM_BS_E_STALL.
 * - After the first restart or seek, every sector is checked against the file position it must fill before it is
 *   taken (io.info, when given; until then the commands sent are the same as before the check existed): after a
 *   restart the CD block may still deliver sectors of the OLD play -- ahead of the restart point, or the same ones a
 *   second time -- and those used to land in the ring as if they were the next ones, shifting the byte stream (the
 *   hardware run of 2026-11-19: RST1, then frame 76 failed with OM_BA_E_OPS; reproduced in test_bootanim). Wrong
 *   sectors at the head are dropped (io.drop), and a take stops where the run of right ones ends.
 * - A restart always starts at got, the first sector not in the ring, so it never overwrites bytes not yet decoded. */
#ifndef OM_BOOTSTREAM_H
#define OM_BOOTSTREAM_H
#include <stdint.h>

#define OM_BS_SECTOR  2048u
#define OM_BS_E_STALL -96                       /* the stream stopped: the caller skips the intro (named, non-fatal) */

typedef struct {
    int32_t (*count)(void *ctx);                        /* sectors waiting in the drive's buffer, < 0 = error */
    int (*take)(void *ctx, uint8_t *dst, uint32_t n);   /* move n of them to dst; < 0 = failed (they may be lost) */
    int (*start)(void *ctx, uint32_t first, uint32_t n);/* (re)start the play at file sector `first`, n sectors */
    void *ctx;
    int32_t (*info)(void *ctx, uint32_t spos);          /* address (FAD) of waiting sector spos, < 0 = error; optional */
    int (*drop)(void *ctx, uint32_t n);                 /* delete the first n waiting sectors, < 0 = failed */
} om_bs_io;
#define OM_BS_WHY_COUNT 1                       /* why the first restart happened (diagnostics) */
#define OM_BS_WHY_TAKE  2
#define OM_BS_WHY_STALL 3
#define OM_BS_WHY_INFO  4

typedef struct {
    om_bs_io io;
    uint8_t *ring;
    uint32_t ring_size;                         /* a power of two, a multiple of OM_BS_SECTOR */
    uint32_t nsec, got, keep;                   /* file sectors; bytes in (whole sectors); first byte still needed */
    uint32_t last_rx, restarts, io_errors;
    uint32_t stall_vb, abort_vb, max_restarts;
    uint8_t stall_restarted;
    uint8_t check;                              /* sector check on: set by the first restart or seek (see .c) */
    int err;                                    /* the last io error (diagnostics) */
    uint32_t base;                              /* info()'s address of file sector 0 (om_bs_init sets 0) */
    uint32_t dropped, seeks;                    /* wrong sectors deleted; om_bs_seek calls */
    uint32_t why, why_sector, why_vb;           /* the first restart: OM_BS_WHY_*, at file sector, at `now` */
    int why_err;                                /* and the io error that caused it (0 for a stall) */
} om_bs;

void om_bs_init(om_bs *s, const om_bs_io *io, uint8_t *ring, uint32_t ring_size, uint32_t nsec, uint32_t now);
/* Moves up to max sectors into the ring. now = the V-blank counter. Returns the bytes moved (>= 0) or OM_BS_E_STALL. */
int32_t om_bs_pump(om_bs *s, uint32_t now, uint32_t max);
int om_bs_done(const om_bs *s);                 /* the whole file is in */
/* Re-sync (a frame failed to decode): drop what is in, restart the play at the sector holding file byte `byte`, keep =
 * byte. Does not count against max_restarts (the caller bounds its re-syncs). Returns the io start result. */
int om_bs_seek(om_bs *s, uint32_t byte, uint32_t now);
#endif
