/* menu/host-tests/th.h -- tiny host test harness (BSD-3-Clause). A binary with zero passing checks fails. */
#ifndef TH_H
#define TH_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int th_pass, th_fail;
#define CHECK(c) do { if (c) th_pass++; else { th_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a == _b) th_pass++; \
    else { th_fail++; printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); if (_a && _b && !strcmp(_a, _b)) th_pass++; \
    else { th_fail++; printf("FAIL %s:%d: %s == %s (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a, #b, \
    _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)
#define FIX(name) "../../contract/fixtures/" name

static inline uint8_t *th_load(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;
    if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (uint8_t *)malloc((size_t)n + 1);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { printf("FAIL cannot read %s\n", path); exit(2); }
    fclose(f);
    *len = (uint32_t)n;
    return p;
}

static inline uint8_t *th_dup(const uint8_t *p, uint32_t n)
{
    uint8_t *q = (uint8_t *)malloc(n);
    memcpy(q, p, n);
    return q;
}

static inline int th_done(const char *name)
{
    printf("%s: %d passed, %d failed\n", name, th_pass, th_fail);
    if (th_pass == 0) { printf("%s: ZERO checks passed -- treated as failure\n", name); return 1; }
    return th_fail ? 1 : 0;
}
#endif
