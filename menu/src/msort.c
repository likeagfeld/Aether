/* menu/src/msort.c -- see msort.h. BSD-3-Clause. */
#include <string.h>
#include "msort.h"
#include "yield.h"

void om_msort(uint16_t *a, uint32_t n, uint16_t *tmp, om_cmp_fn cmp, const void *ctx)
{
    uint16_t *src = a, *dst = tmp, *t;
    uint32_t w, i, c = 0;
    for (w = 1; w < n; w *= 2) {
        for (i = 0; i < n; i += 2 * w) {
            uint32_t l = i, m = i + w < n ? i + w : n, r = i + 2 * w < n ? i + 2 * w : n, k = i, j = m;
            while (l < m && j < r) {
                dst[k++] = cmp(ctx, src[j], src[l]) < 0 ? src[j++] : src[l++];   /* take left on ties: stable */
                if (!(++c & 63u)) om_yield();       /* the start-up screen's frames (yield.h): an 800-game sort */
            }
            while (l < m) dst[k++] = src[l++];
            while (j < r) dst[k++] = src[j++];
        }
        t = src; src = dst; dst = t;
    }
    if (src != a)
        memcpy(a, src, n * sizeof *a);
}
