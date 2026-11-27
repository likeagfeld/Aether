/* menu/src/bootanim.h -- BOOTANIM.BIN (contract/SPEC.md §5.10): reader, frame decoder and the V-blank pacer of the boot
 * animation (Task 13b, controller ruling M3-R38). Pure C: the Saturn player is menu/app/bootplay.c; the host tests drive
 * the same code. BSD-3-Clause.
 *
 * Codec 2: frame i is a list of byte ops (SKIP, LIT, COPY, FILL; tools/omsat/bootanim.py) applied IN PLACE over its
 * reference frame ref(i) = i-2 for even i, i-1 for odd i (frame -2 = all index 0). The decoder keeps two slots: E holds
 * the last even frame and is decoded in place (its reference is already there), Q receives a copy of E and then the
 * odd frame. The even frames alone are a complete chain, which is what the 30 fps fallback decodes. A SKIP costs
 * nothing; the stores (HWRAM is written through the cache) are only for changed pixels, in 32-bit words where the run
 * allows. */
#ifndef OM_BOOTANIM_H
#define OM_BOOTANIM_H
#include <stdint.h>

#define OM_BA_MAX_W      176u
#define OM_BA_MAX_H      128u
#define OM_BA_MAX_PIX    (OM_BA_MAX_W * OM_BA_MAX_H)
#define OM_BA_MAX_FRAMES 512u
#define OM_BA_MAX_FRAME  16384u                 /* compressed bytes per frame (tools/omsat/bootanim.py MAX_FRAME_BYTES) */
#define OM_BA_INDEX_OFF  592u                   /* header 64 + info 16 + palette 512 */
#define OM_BA_CODEC      2u                     /* info codec field: byte ops in place, reference i-2 / i-1 */
#define OM_BA_E_INFO     -81                    /* info block or index out of the menu's limits */
#define OM_BA_E_REF      -82                    /* decode out of chain order (a caller bug, never a file error) */
#define OM_BA_E_OPS      -83                    /* an op runs past the frame or past its data */
#define OM_BA_E_DIST     -84                    /* a COPY reaches before the start of the frame */
#define OM_BA_E_TRAIL    -85                    /* data left after the frame is complete */

typedef struct {
    const uint8_t *index, *palette;                /* point into the caller's copy of the file head */
    uint32_t count, w, h, fps, chant_x100, data_off, data_end, file_size;
} om_ba;
/* buf holds the first `have` bytes of a file of file_size bytes; it must reach the end of the frame index. */
int om_ba_open(om_ba *a, const uint8_t *buf, uint32_t have, uint32_t file_size);
uint32_t om_ba_head_bytes(const uint8_t *buf, uint32_t have);   /* bytes om_ba_open needs (0 if have < 64) */
void om_ba_entry(const om_ba *a, uint32_t i, uint32_t *off, uint32_t *len, uint32_t *crc);
uint16_t om_ba_color(const om_ba *a, uint32_t i);               /* RGB555, bit 15 clear */
int32_t om_ba_ref(uint32_t i);                                  /* -2 = the black frame */
uint32_t om_ba_chant_frame(const om_ba *a);                     /* chant_x100 / 100: the frame on screen when it starts */

typedef struct {
    uint8_t *e, *q;                             /* two slots of w*h bytes: the last even frame, the last odd frame */
    int32_t e_frame, q_frame;                   /* frames held (-2 = black, -3 = none) */
    const uint8_t *pix;                         /* the frame just decoded (e or q) */
} om_ba_dec;
void om_ba_dec_init(om_ba_dec *d, const om_ba *a, uint8_t *slots);
/* Decodes frame i (len bytes of ops at src). The chain must be respected: ref(i) must be in e. */
int om_ba_decode(om_ba_dec *d, const om_ba *a, uint32_t i, const uint8_t *src, uint32_t len);
/* After a failed decode (the data was bad): E becomes `frame` again from pix, a copy of it (bootplay.c keeps the last
 * two frames it uploaded, and one of them is always ref(i) of an even frame i that failed), so decoding can resume at
 * the next frame whose reference is `frame`. frame -2 = black (pix ignored). Q is dropped. */
void om_ba_dec_restore(om_ba_dec *d, const om_ba *a, int32_t frame, const uint8_t *pix);
/* The ops of one frame applied in place to buf (n pixels). Exposed for the tests. */
int om_ba_ops(uint8_t *buf, uint32_t n, const uint8_t *src, uint32_t len);

/* One call per V-blank. want() = the frame the player should have decoded for this V-blank, or OM_BA_NONE (a 30 fps
 * hold V-blank, or the end); tick() reports whether it was ready and returns OM_BA_EV_* bits. A frame not ready is an
 * underrun: the previous frame stays up for that V-blank and the pacer drops to 30 fps (even frames only, each for two
 * V-blanks) for the rest of the animation. */
#define OM_BA_NONE        0xFFFFFFFFu
#define OM_BA_EV_SHOW     1u
#define OM_BA_EV_CHANT    2u
#define OM_BA_EV_DONE     4u
#define OM_BA_EV_FALLBACK 8u
typedef struct {
    uint32_t count, chant, next, vbl, shown;
    uint32_t underruns, fallback_frame, chant_vbl, done_vbl;   /* diagnostics; OM_BA_NONE = never */
    uint32_t slow;                              /* V-blank overruns reported by the player */
    uint8_t half, hold, chant_done, done, reason;   /* reason: 1 = underrun, 2 = slow CPU (why it fell back) */
} om_ba_pacer;
void om_ba_pacer_init(om_ba_pacer *p, uint32_t count, uint32_t chant_frame);
uint32_t om_ba_pacer_want(const om_ba_pacer *p);
unsigned om_ba_pacer_tick(om_ba_pacer *p, int ready);
/* The player's iteration took more than one V-blank (a CPU slower than measured): the same 30 fps fallback as an
 * underrun, so the timeline keeps up instead of stretching. Returns OM_BA_EV_FALLBACK the first time, else 0. */
unsigned om_ba_pacer_slow(om_ba_pacer *p);
#endif
