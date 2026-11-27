/* menu/src/sndmix.h -- the portable arithmetic of the menu's sound: the streamed music's ring of blocks in
 * sound RAM, fades and ducking in SCSP TL units, SFX pitch shifts and lengths, and the resume-position state file.
 * menu/app/audio.c drives the SCSP with it; menu/host-tests/test_sndmix.c tests it. No division in the per-V-blank
 * paths (om_ring_ca, om_fade_tick, om_duck_tick): constant shifts and tables only. BSD-3-Clause. */
#ifndef OM_SNDMIX_H
#define OM_SNDMIX_H
#include <stdint.h>

/* --- the ring: per channel, OM_RING_SLOTS blocks of 4096 samples in sound RAM, played by one looping slot (LSA 0,
 * LEA = OM_RING_SLOTS x 4096). The SCSP's CA (HWF-51) says which 4096-sample block the slot is reading. `played` is
 * the absolute number of the block being played, `filled` the absolute count of blocks written: block k lives in slot
 * k % OM_RING_SLOTS and may be written only while k < played + OM_RING_SLOTS (its slot is not the one playing).
 * Assumption (review-sound M4): CA is read at least once per block (186 ms at 22,050 Hz). A V-blank masked for longer
 * would alias CA modulo 7 and let the writer overwrite the block playing; nothing in the menu masks that long. */
#define OM_RING_SLOTS 7u
typedef struct {
    uint32_t played, filled;
    uint8_t last;                    /* the slot CA showed last */
} om_ring;
void om_ring_reset(volatile om_ring *r);
void om_ring_ca(volatile om_ring *r, uint32_t ca);              /* a CA reading (V-blank); a value >= OM_RING_SLOTS reads as 0 */
uint32_t om_ring_ahead(const volatile om_ring *r);              /* whole blocks written beyond the one playing */
int om_ring_can_write(const volatile om_ring *r);               /* the next block's slot is free */
int om_ring_starved(const volatile om_ring *r);                 /* the slot is playing a block that was never written */
uint32_t om_ring_slot(uint32_t abs_block);             /* abs_block % OM_RING_SLOTS */

/* --- levels in SCSP TL units: TL bit 0 = 0.375 dB, bit 4 = 6 dB, bit 3 = 3 dB, 0xFF = -95.6 dB (HWF-52) */
#define OM_TL_MUTE   255u
#define OM_TL_DUCK   16u             /* -6 dB under each SFX (overrides M3-R17) */
#define OM_TL_HELD   16u             /* a held scroll's repeats, every other one, -6 dB */
#define OM_MUSIC_VOL_MAX 10u
#define OM_MUSIC_VOL_DEF 10u         /* the packer stores the mix at -14 dBFS mean: full level is the approved level */
uint8_t om_snd_vol_tl(uint32_t vol);                   /* 10 = 0, each step down -3 dB (8 units), 0 = mute */
uint8_t om_snd_amp_tl(uint32_t amp);                   /* amplitude Q16 (65536 = 1.0) -> TL units, 0 -> mute */
uint8_t om_snd_tl_sum(uint32_t a, uint32_t b, uint32_t c);   /* clamped at OM_TL_MUTE */

/* a linear-amplitude fade, stepped once per V-blank */
typedef struct { uint32_t amp, target, step; } om_fade;
void om_fade_set(volatile om_fade *f, uint32_t amp);             /* jump */
void om_fade_to(volatile om_fade *f, uint32_t target, uint32_t vblanks);   /* ramp over vblanks (>= 1) */
uint32_t om_fade_tick(volatile om_fade *f, uint32_t n);         /* n V-blanks elapsed; returns the amplitude */
int om_fade_done(const volatile om_fade *f);

/* ducking: toward OM_TL_DUCK while an SFX plays, 4 units (1.5 dB) per V-blank in both directions */
uint8_t om_duck_tick(uint8_t cur, int active, uint32_t n);

/* --- pitch: (OCT, FNS) shifted by semis semitones (HWF-36: rate = 2^OCT x (1024 + FNS) / 1024 x 44,100 Hz) */
void om_snd_shift(int8_t oct, uint16_t fns, int semis, int8_t *oct_out, uint16_t *fns_out);
/* V-blanks a one-shot of `samples` samples lasts at (oct, fns), rounded up; vps = V-blanks per second (60 or 50) */
uint32_t om_snd_vblanks(uint32_t samples, int8_t oct, uint16_t fns, uint32_t vps);

/* --- the music's resume position (/aether/MUSICPOS.TXT): "size=<file bytes>\nblock=<block>\n". parse returns the
 * block, or -1 when the text is malformed, names another file size, or a block past `blocks`. */
uint32_t om_mus_state_fmt(char *out, uint32_t cap, uint32_t file_size, uint32_t block);
int32_t om_mus_state_parse(const char *text, uint32_t len, uint32_t file_size, uint32_t blocks);
#endif
