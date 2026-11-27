/* menu/src/savegen.c -- see savegen.h. BSD-3-Clause. */
#include <string.h>
#include "savegen.h"
#include "satiator.h"
#include "sdwrite.h"
#include "yield.h"
#include "om_fmt.h"

static const char *al(om_gen_scr *s, const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n >= sizeof s->al) n = sizeof s->al - 1;
    memcpy(s->al, p, n);
    ((char *)s->al)[n] = 0;
    return (const char *)s->al;
}

static char *put_d(char *o, uint32_t v, uint32_t digits)
{
    uint32_t k;
    for (k = digits; k > 0; k--) { o[k - 1] = (char)('0' + v % 10u); v /= 10u; }
    o[digits] = 0;
    return o + digits;
}

void om_gen_shard(char *out, const char *d, uint32_t g)
{
    uint32_t n = (uint32_t)strlen(d);
    memmove(out, d, n);
    out[n++] = '/'; out[n++] = 'G';
    put_d(out + n, g / 100u, 3);
}

void om_gen_dir(char *out, const char *d, uint32_t g)
{
    uint32_t n;
    om_gen_shard(out, d, g);
    n = (uint32_t)strlen(out);
    out[n++] = '/'; out[n++] = 'G';
    put_d(out + n, g, 5);
}

int om_gen_num(const char *name, uint32_t digits)
{
    uint32_t k;
    int v = 0;
    if (name[0] != 'G' && name[0] != 'g') return -1;
    for (k = 1; k <= digits; k++) {
        if (name[k] < '0' || name[k] > '9') return -1;
        v = v * 10 + (name[k] - '0');
    }
    return name[k] ? -1 : v;
}

/* d's entries that are folders named "G" + digits: *max = the highest number below lim (-1 for none); digits 5: each
 * generation of shard sh sets its bit (n % 100) in bits */
static int scan(om_gen_scr *s, const char *d, uint32_t digits, int lim, uint32_t sh, uint8_t bits[13], int *max)
{
    s_stat_t *st = (s_stat_t *)s->st;
    int len, rc;
    uint32_t k = 0;
    *max = -1;
    om_yield();                                       /* the launch's eye (yield.h): before each path command */
    if ((rc = s_opendir(al(s, d))) < 0)
        return rc == -FR_NO_FILE || rc == -FR_NO_PATH ? 0 : OM_SDW_ERR(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s->st - 4)) > 0) {
        int v;
        if (!(++k & 15u)) om_yield_draw();            /* inside a listing: a frame, no SD command */
        st->name[len] = 0;
        if (!(st->attrib & AM_DIR) || (v = om_gen_num(st->name, digits)) < 0) continue;
        if (digits == 5) {
            if ((uint32_t)v / 100u != sh || !v) continue;
            bits[(v % 100) >> 3] |= (uint8_t)(1u << ((v % 100) & 7));
        }
        if (v < lim && v > *max) *max = v;
    }
    return len == SAT_ETIMEDOUT ? OM_SDW_ERR(len) : 0;
}

int om_gen_cur(const char *d, char *out, uint32_t *g, om_gen_scr *s)
{
    int lim = 1000, sh, mx, rc;
    uint8_t bits[13];
    *g = 0;
    for (;;) {
        int k;
        if ((rc = scan(s, d, 3, lim, 0, bits, &sh)) != 0) return rc;
        if (sh < 0) break;
        memset(bits, 0, sizeof bits);
        om_gen_shard(out, d, (uint32_t)sh * 100u);
        if ((rc = scan(s, out, 5, 100000, (uint32_t)sh, bits, &mx)) != 0) return rc;
        for (k = 99; k >= 0; k--) {
            uint32_t n = (uint32_t)sh * 100u + (uint32_t)k, e;
            if (!(bits[k >> 3] & (1u << (k & 7)))) continue;
            om_gen_dir(out, d, n);
            e = (uint32_t)strlen(out);
            memcpy(out + e, "/" OM_GEN_DONE, sizeof OM_GEN_DONE + 1);
            om_yield();
            rc = s_stat(al(s, out), (s_stat_t *)s->st, (int)sizeof s->st - 4);
            out[e] = 0;
            if (rc >= 0) { *g = n; return 0; }
            if (rc != -FR_NO_FILE && rc != -FR_NO_PATH) return OM_SDW_ERR(rc);
        }
        lim = sh;                                     /* no complete generation in this shard: the one below */
    }
    memmove(out, d, strlen(d) + 1);
    return 0;
}

int om_gen_max(const char *d, uint32_t *max, om_gen_scr *s)
{
    char p[256];
    uint8_t bits[13];
    int sh, mx, rc;
    *max = 0;
    if (strlen(d) + OM_GEN_SUFFIX >= sizeof p) return OM_E_FIELD;
    if ((rc = scan(s, d, 3, 1000, 0, bits, &sh)) != 0 || sh < 0) return rc;
    om_gen_shard(p, d, (uint32_t)sh * 100u);
    memset(bits, 0, sizeof bits);
    if ((rc = scan(s, p, 5, 100000, (uint32_t)sh, bits, &mx)) != 0) return rc;
    /* an empty shard (a cut between its mkdir and the generation's): every generation is below it */
    *max = mx >= 0 ? (uint32_t)mx : sh ? (uint32_t)sh * 100u - 1u : 0u;
    return 0;
}
