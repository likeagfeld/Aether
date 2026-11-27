/* menu/src/sndmix.c -- see sndmix.h. BSD-3-Clause. */
#include "sndmix.h"

void om_ring_reset(volatile om_ring *r) { r->played = r->filled = 0; r->last = 0; }

void om_ring_ca(volatile om_ring *r, uint32_t ca)
{
    uint32_t s = ca < OM_RING_SLOTS ? ca : 0u;        /* CA reads LEA's block (7) only on the loop sample itself */
    uint32_t d = s >= r->last ? s - r->last : s + OM_RING_SLOTS - r->last;
    r->played += d;                                   /* a block is 186 ms at 22,050 Hz: a V-blank never misses one */
    r->last = (uint8_t)s;
}

uint32_t om_ring_ahead(const volatile om_ring *r) { return r->filled > r->played + 1u ? r->filled - r->played - 1u : 0u; }
int om_ring_can_write(const volatile om_ring *r) { return r->filled < r->played + OM_RING_SLOTS; }
int om_ring_starved(const volatile om_ring *r) { return r->played >= r->filled; }
uint32_t om_ring_slot(uint32_t abs_block) { return abs_block % OM_RING_SLOTS; }

uint8_t om_snd_vol_tl(uint32_t vol)
{
    if (!vol) return OM_TL_MUTE;
    if (vol >= OM_MUSIC_VOL_MAX) return 0;
    return (uint8_t)((OM_MUSIC_VOL_MAX - vol) << 3);  /* 8 units = TL bit 3 = 3 dB per step (HWF-52) */
}

/* round(-20 log10(k / 64) / 0.375) for k = 0..64 (0 = mute): the amplitude fade in TL units */
static const uint8_t k_amp_tl[65] = {
    255, 96, 80, 71, 64, 59, 55, 51, 48, 45, 43, 41, 39, 37, 35, 34, 32, 31, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20,
    19, 18, 18, 17, 16, 15, 15, 14, 13, 13, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 4, 3, 3, 2, 2, 1,
    1, 1, 0, 0 };

uint8_t om_snd_amp_tl(uint32_t amp) { return k_amp_tl[amp >= 65536u ? 64u : amp >> 10]; }

uint8_t om_snd_tl_sum(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t t = a + b + c;
    return (uint8_t)(t > OM_TL_MUTE ? OM_TL_MUTE : t);
}

void om_fade_set(volatile om_fade *f, uint32_t amp) { f->amp = f->target = amp; f->step = 0; }

void om_fade_to(volatile om_fade *f, uint32_t target, uint32_t vblanks)
{
    uint32_t d = target > f->amp ? target - f->amp : f->amp - target;
    f->target = target;
    f->step = vblanks ? (d + vblanks - 1u) / vblanks : d;
    if (!f->step) f->amp = target;
}

uint32_t om_fade_tick(volatile om_fade *f, uint32_t n)
{
    while (n-- && f->amp != f->target) {
        if (f->amp < f->target) f->amp = f->target - f->amp > f->step ? f->amp + f->step : f->target;
        else f->amp = f->amp - f->target > f->step ? f->amp - f->step : f->target;
    }
    return f->amp;
}

int om_fade_done(const volatile om_fade *f) { return f->amp == f->target; }

uint8_t om_duck_tick(uint8_t cur, int active, uint32_t n)
{
    uint32_t c = cur, step = n << 2;
    if (active) c = c + step > OM_TL_DUCK ? OM_TL_DUCK : c + step;
    else c = c > step ? c - step : 0u;
    return (uint8_t)c;
}

static const uint32_t k_semi[12] = { 65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772,
                                     123715 };    /* round(65536 x 2^(i/12)) */

void om_snd_shift(int8_t oct, uint16_t fns, int semis, int8_t *oct_out, uint16_t *fns_out)
{
    int o = oct, q = semis >= 0 ? semis / 12 : -((11 - semis) / 12);
    uint32_t m = ((1024u + fns) * k_semi[semis - 12 * q]) >> 16;   /* 1024..2047 x < 2: m < 4094 */
    o += q;
    while (m >= 2048u) { m >>= 1; o++; }
    if (o < -8) { o = -8; m = 1024u; }
    if (o > 7) { o = 7; m = 2047u; }
    *oct_out = (int8_t)o;
    *fns_out = (uint16_t)(m - 1024u);
}

uint32_t om_snd_vblanks(uint32_t samples, int8_t oct, uint16_t fns, uint32_t vps)
{
    uint32_t rate = 44100u * (1024u + fns);           /* x 1024: <= 90,272,700 */
    int sh = 10 - oct;                                /* 2^oct / 1024 */
    if (sh > 31) sh = 31;
    rate = sh >= 0 ? rate >> sh : rate << -sh;
    if (!rate) return 0xFFFFu;
    return (samples * vps + rate - 1u) / rate;        /* samples x vps <= 65,535 x 60 */
}

static uint32_t put_u(char *o, uint32_t cap, uint32_t at, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n && at + 1u < cap) o[at++] = t[--n];
    return at;
}

static uint32_t put_s(char *o, uint32_t cap, uint32_t at, const char *s)
{
    while (*s && at + 1u < cap) o[at++] = *s++;
    return at;
}

uint32_t om_mus_state_fmt(char *out, uint32_t cap, uint32_t file_size, uint32_t block)
{
    uint32_t n = 0;
    if (!cap) return 0;
    n = put_s(out, cap, n, "size=");
    n = put_u(out, cap, n, file_size);
    n = put_s(out, cap, n, "\nblock=");
    n = put_u(out, cap, n, block);
    n = put_s(out, cap, n, "\n");
    out[n] = 0;
    return n;
}

static int num(const char *t, uint32_t len, uint32_t *i, uint32_t *v)
{
    uint32_t x = 0, d = 0, c;
    while (*i < len && t[*i] >= '0' && t[*i] <= '9') {
        c = (uint32_t)(t[*i] - '0');
        if (x > (0xFFFFFFFFu - c) / 10u) return 0;   /* over 32 bits */
        x = x * 10u + c;
        (*i)++;
        d++;
    }
    *v = x;
    return d > 0;
}

static int key(const char *t, uint32_t len, uint32_t *i, const char *k)
{
    while (*k) { if (*i >= len || t[*i] != *k) return 0; (*i)++; k++; }
    return 1;
}

int32_t om_mus_state_parse(const char *text, uint32_t len, uint32_t file_size, uint32_t blocks)
{
    uint32_t i = 0, size, block;
    if (!key(text, len, &i, "size=") || !num(text, len, &i, &size)) return -1;
    while (i < len && (text[i] == '\r' || text[i] == '\n')) i++;
    if (!key(text, len, &i, "block=") || !num(text, len, &i, &block)) return -1;
    if (size != file_size || block >= blocks) return -1;
    return (int32_t)block;
}
