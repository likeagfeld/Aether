/* menu/launcher/lgread.h -- the loading screen's reads of LOADBG.BIN (lgfx.c): where the next 2 KB read starts and
 * which parts of the file are in RAM.
 *
 * The file lives at its own offsets from `file` (lgfx.h OM_LG_FILE), so a read of offset k lands at file + k. s_read
 * moves the data in 32-bit words (satiator.c buffer_xfer), so its buffer -- file + k -- must be word-aligned, or the
 * store is a CPU address error. LOADBG.BIN packs its frames end to end, so most frame entries start at an unaligned
 * offset (the release file: 43 of 64). Hardware 2026-11-25 (Mortal Kombat II, FW163): the launch adopted the menu's
 * last frame and read on from the next frame's entry; that entry started at an unaligned offset, and s_read faulted
 * at step 1 (PC 0x218E08, s_read's copy loop). Every read here starts at a word boundary; a start inside a word
 * re-reads up to 3 bytes that are already in place (the same bytes of the same file). BSD-3-Clause. */
#ifndef OM_LGREAD_H
#define OM_LGREAD_H
#include <stdint.h>
#include "loadbg.h"

typedef struct {
    uint8_t *file;                  /* the file's home in RAM, word-aligned: offset k is at file + k */
    int fd;                         /* the open file, < 0 none */
    uint32_t size;                  /* the file's length */
    uint32_t rp;                    /* the next byte to read */
    uint32_t s1, e1;                /* [s1, e1) is in: from where the reads started (0, or an adopted frame's entry) */
    uint32_t e0;                    /* after the wrap, [ws, e0) is in too (the part before s1) */
    uint32_t wrap;                  /* 1 once the reads went back to the head */
    uint32_t w0;                    /* where the wrap starts when past the head (0: the head): a launch that adopted
                                     * a frame never needs entry 0, the 45 KB keyframe, so it starts at entry 1 */
    uint32_t ws;                    /* where it started (set at the wrap) */
} om_lgr;

/* one read of up to cap bytes from rp up to end, at file + rp; rp is first taken down to a word boundary (and the
 * file seeked there). The bytes read (> 0), else < 0. */
int om_lgr_read(om_lgr *c, uint32_t end, uint32_t cap);
/* read on from off (an adopted frame's entry): s1 = e1 = rp = off taken down to a word boundary, the file seeked
 * there. 0, else < 0. */
int om_lgr_from(om_lgr *c, uint32_t off);
/* the next read of the whole file (lgfx's pump): to the end, then (after an adopted start) the part between the head
 * -- or w0, taken down to a word -- and s1. 1 = more to read, 0 = all of it is in, < 0 = an error. */
int om_lgr_some(om_lgr *c, uint32_t cap);
/* 1 if [off, off + len) is in RAM (lgfx's entry_in). head = the header + index bytes, where the wrap starts (or w0).
 * Once the wrap has reached s1 the two parts join, so an entry across s1 (the frame before an adopted one, when s1 was
 * taken down to a word) is in too. Bytes before w0 are never counted as in once the reads have wrapped. */
int om_lgr_has(const om_lgr *c, uint32_t head, uint32_t off, uint32_t len);

/* The launch's pacing (hardware 2026-11-27, Aether 0.1: the eye stalled through the save swap, then ran at full speed
 * in the last half second). The launch pumps the eye from its yield hook, between two whole Satiator calls
 * (host-tested in test_lgpace.c). The launch adopts the menu's last frame with nothing of the file after it in RAM,
 * and a 30 fps loop of ~7 KB frames needs ~70% of the card's 296 KB/s, which the swap's own commands also need: so the
 * eye's reads earn a share of the time that passes (OM_LGR_SHARE percent, up to OM_LGR_CREDIT_MAX of SD time banked)
 * and stop once OM_LGR_AHEAD frames after the one shown are in RAM. The swap then takes ~1/(1 - share) as long at
 * most, and the eye moves at the rate its share reads: ~share x 37 fps (296 KB/s of ~8 KB frames), 30 fps is out of
 * reach while the file streams. User decision 2026-11-27: 12-15 fps, the swap up to ~1.5 s; on the model clock
 * (test_lgpace.c) 50% gives 10-12 fps for a swap ~1.8x as long. */
#define OM_LGR_AHEAD      3u
#define OM_LGR_SHARE      50u
#define OM_LGR_CREDIT_MAX 14000
/* the credit (us of SD time the eye may spend reading; a read takes its time off, so it can go below 0) after
 * elapsed_us more of the launch */
int32_t om_lgr_credit(int32_t credit, uint32_t elapsed_us);
/* the frames after `frame` (the one on screen) whose entries are in RAM, in play order, counted up to max */
uint32_t om_lgr_ahead(const om_lgr *c, const om_lbg *g, uint32_t frame, uint32_t max);
/* the flip of a frame that is decoded and in the hidden bitmap: us since the last flip, the loop's frame period and
 * the V-blank flag (TVSTAT). LATER: return (a later pump flips); FLIP: now (in the V-blank); WAIT: wait for the
 * V-blank, then flip -- only once the frame is OM_LGR_LATE_US late, so a pump that keeps missing the V-blank still
 * moves the eye. The launch's pumps land in the V-blank ~15% of the time; waiting at once would hold the swap up to
 * a V-blank a frame, waiting only after 100 ms held the eye near 7 fps (test_lgpace.c) */
enum { OM_LGR_LATER, OM_LGR_FLIP, OM_LGR_WAIT };
#define OM_LGR_LATE_US   20000u
int om_lgr_flip(uint32_t us, uint32_t period, int vblank);
#endif
