/* menu/src/msort.h -- stable bottom-up merge sort of uint16 indices (O(n log n), no recursion, caller's tmp).
 * Insertion sort over 2,048 entries would cost ~2M comparisons on the SH-2; this costs ~22,500. BSD-3-Clause. */
#ifndef OM_MSORT_H
#define OM_MSORT_H
#include <stdint.h>
typedef int (*om_cmp_fn)(const void *ctx, uint16_t a, uint16_t b);
void om_msort(uint16_t *a, uint32_t n, uint16_t *tmp, om_cmp_fn cmp, const void *ctx);
#endif
