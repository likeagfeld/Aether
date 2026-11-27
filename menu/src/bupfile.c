/* menu/src/bupfile.c -- see bupfile.h (contract/SPEC.md §5.16). BSD-3-Clause. */
#include <string.h>
#include "bupfile.h"
#include "om_fmt.h"

static int name_len(const char *n)
{   /* 1..11 printable bytes up to a NUL or byte 11, which is never part of the name (Save Game Copier writes '.'
     * there after an 11-character name); 0 for an empty or unprintable name */
    int i;
    for (i = 0; i < 11 && n[i]; i++)
        if ((uint8_t)n[i] < 0x20 || (uint8_t)n[i] > 0x7E)
            return 0;
    return i;
}

int om_bup_name_ok(const char name[12])
{
    int i, n = name_len(name);
    if (!n)
        return 0;
    for (i = n; i < 12; i++)
        if (name[i])
            return 0;
    return 1;
}

int om_bup_name_norm(char out[12], const char in[12])
{
    int n = name_len(in);
    if (!n)
        return 0;
    memmove(out, in, (uint32_t)n);
    memset(out + n, 0, 12u - (uint32_t)n);
    return 1;
}

int om_bup_encode_hdr(const om_bup_dir *d, uint8_t out[OM_BUP_HDR])
{
    char nm[12];
    if (!om_bup_name_norm(nm, d->name))
        return OM_E_FIELD;
    memset(out, 0, OM_BUP_HDR);
    memcpy(out, OM_BUP_MAGIC, 4);                    /* 0x04 save_id, 0x08 stats, 0x0C unused: 0 */
    memcpy(out + 0x10, nm, OM_BUP_NAME);
    memcpy(out + 0x1C, d->comment, OM_BUP_COMMENT);
    out[0x27] = d->language;
    om_put32(out + 0x28, d->date);
    om_put32(out + 0x2C, d->datasize);
    om_put16(out + 0x30, d->blocks);                 /* 0x32 padding: 0 */
    om_put32(out + 0x34, d->date);                   /* Kai: its start-up date; ours: the record's */
    return OM_OK;                                    /* 0x38..0x3F unused: 0 */
}

int om_bup_decode_hdr(const uint8_t *in, uint32_t len, om_bup_dir *d)
{
    int n;
    uint32_t size;
    if (len < OM_BUP_HDR)
        return OM_E_SHORT;
    if (memcmp(in, OM_BUP_MAGIC, 4))
        return OM_E_MAGIC;
    if (!(n = name_len((const char *)in + 0x10)))
        return OM_E_FIELD;
    size = om_be32(in + 0x2C);
    if (size > len - OM_BUP_HDR)                     /* no 64 + size: it could wrap */
        return OM_E_SHORT;
    memset(d->name, 0, OM_BUP_NAME);
    memcpy(d->name, in + 0x10, (uint32_t)n);
    memcpy(d->comment, in + 0x1C, OM_BUP_COMMENT);
    d->language = in[0x27];
    d->date = om_be32(in + 0x28);
    d->datasize = size;
    d->blocks = om_be16(in + 0x30);
    return OM_OK;
}

static int put(char *out, uint32_t cap, uint32_t *o, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (*o + n + 1 > cap)
        return OM_E_FULL;
    memcpy(out + *o, s, n);
    *o += n;
    out[*o] = 0;
    return OM_OK;
}

int om_bup_slot_path(char *out, uint32_t cap, const char *base, const char *folder, uint32_t slot,
                     const char name[12])
{
    char nm[13], sl[5] = { '/', '0', 0, '/', 0 };
    uint32_t o = 0;
    int i;
    if (slot >= OM_BUP_SLOTS)
        return OM_E_RANGE;
    if (!om_bup_name_norm(nm, name))
        return OM_E_FIELD;
    nm[12] = 0;
    for (i = 0; nm[i]; i++)
        if (strchr("\"*/:<>?\\|", nm[i]))
            return OM_E_FIELD;
    sl[2] = (char)('0' + slot);
    if (!cap)
        return OM_E_FULL;
    out[0] = 0;
    if (put(out, cap, &o, base) || put(out, cap, &o, "/") || put(out, cap, &o, folder)
        || (slot && put(out, cap, &o, sl)) || (!slot && put(out, cap, &o, "/"))
        || put(out, cap, &o, nm) || put(out, cap, &o, ".BUP")) {
        out[0] = 0;
        return OM_E_FULL;
    }
    return OM_OK;
}

void om_bup_msg(char *out, uint32_t cap, const char *op, int rc)
{
    static const char *const k_named[9] = { 0, "NOT CONNECTED", "NOT FORMATTED", "WRITE PROTECTED", "FULL",
                                            "NOT FOUND", "NAME EXISTS", "VERIFY FAILED", "RECORD DAMAGED" };
    char num[12], *q = num + sizeof num;
    uint32_t o = 0, v = rc < 0 ? 0u - (uint32_t)rc : (uint32_t)rc;
    if (!cap)
        return;
    out[0] = 0;
    *--q = 0;
    do *--q = (char)('0' + v % 10u); while (v /= 10u);
    if (rc < 0) *--q = '-';
    (void)put(out, cap, &o, "BRAM: ");                      /* a piece that does not fit is left out whole */
    if (rc > 0 && rc <= 8)
        (void)put(out, cap, &o, k_named[rc]);
    else
        (void)(put(out, cap, &o, op) || put(out, cap, &o, " FAILED rc=") || put(out, cap, &o, q));
}

uint32_t om_bup_mm_size(uint32_t bytes)
{
    return (bytes + 32u + 63u) / 64u;                        /* TechBulletins #9: rounded up */
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

int om_bup_prefer(const uint8_t *a, const char *fa, const uint8_t *b, const char *fb)
{   /* SPEC §5.16 duplicates: date, then save_id, then the own-name file, then file-name byte order */
    uint32_t da = om_be32(a + 0x28), db = om_be32(b + 0x28), sa = om_be32(a + 0x04), sb = om_be32(b + 0x04);
    char own[16];
    int n = name_len((const char *)a + 0x10), ma, mb;
    if (da != db) return da > db;
    if (sa != sb) return sa > sb;
    memcpy(own, a + 0x10, (uint32_t)n);
    memcpy(own + n, ".BUP", 5);
    ma = ieq(fa, own);
    mb = ieq(fb, own);
    if (ma != mb) return ma;
    return strcmp(fa, fb) < 0;
}
