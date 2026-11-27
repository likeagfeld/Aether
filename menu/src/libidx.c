/* menu/src/libidx.c -- see libidx.h. When no string repeats, the writer reproduces
 * tools/omsat/libidx.py write_libidx byte for byte. Python dedups every string; this writer dedups only "",
 * which is always offset 0 (clarification 3). BSD-3-Clause. */
#include <string.h>
#include "carddir.h"
#include "libidx.h"
#include "msort.h"

int om_libidx_open(om_libidx *l, const uint8_t *buf, uint32_t len)
{
    om_header h;
    uint32_t i;
    int rc = om_header_read(buf, len, len, "OMSATLIB", &h);
    if (rc)
        return rc;
    /* DEVIATION (genuine bug, not in the brief's own code as written): the brief's Step 2 block has
     * `if (h.table_size != 32u * h.count) return OM_E_TABLESIZE;` -- the same count*size-overflow shape
     * fixed in Tasks 3+4 for artpak.c/gamesdb.c. h.count is an attacker-controlled u32 read straight from
     * the file (om_fmt.c om_header_read); `32u * h.count` wraps mod 2^32 (e.g. count = 0x08000000 gives
     * 32*count = 0x100000000 truncated to 0), which can slip a tiny, CRC-valid, in-range table_size past
     * the guard while l->count is left at the huge value. Every loop below that walks `l->table + 32*i`
     * for i < l->count would then read far out of bounds. h.table_size is already bounded (<= file_size
     * and <= buf_len, via om_in_range in om_header_read), so table_size/32u cannot overflow; only the
     * division form is safe -- same fix shape as artpak.c:34 and gamesdb.c:24. */
    if (h.table_size % 32u != 0 || h.table_size / 32u != h.count)
        return OM_E_TABLESIZE;
    if (h.sec_size[0] % 8 || !h.sec_size[0] || h.sec_size[2] != 4 || !h.sec_size[1])
        return OM_E_FIELD;
    if (buf[h.sec_off[1] + h.sec_size[1] - 1] != 0 || buf[h.sec_off[1]] != 0)
        return OM_E_FIELD;
    l->table = buf + h.table_off;
    l->count = h.count;
    l->fold = buf + h.sec_off[0];
    l->nfolders = h.sec_size[0] / 8;
    l->pool = (const char *)buf + h.sec_off[1];
    l->pool_size = h.sec_size[1];
    l->fingerprint = om_be32(buf + h.sec_off[2]);
    for (i = 0; i < l->count; i++) {
        const uint8_t *r = l->table + 32 * i;
        uint16_t fl = om_be16(r + 0x1A);
        if (om_be32(r + 0x14) >= l->pool_size || om_be16(r + 0x18) >= l->nfolders) return OM_E_FIELD;
        if (r[0x13] != 1 && r[0x13] != 2) return OM_E_FIELD;
        if ((fl & OM_LIB_TITLE) && om_be32(r + 0x1C) >= l->pool_size) return OM_E_FIELD;
    }
    for (i = 0; i < l->nfolders; i++) {
        const uint8_t *r = l->fold + 8 * i;
        uint16_t parent = om_be16(r + 4);
        if (om_be32(r) >= l->pool_size) return OM_E_FIELD;
        if (i == 0 ? parent != 0xFFFF : parent >= l->nfolders) return OM_E_FIELD;
    }
    return OM_OK;
}

void om_libidx_entry(const om_libidx *l, uint32_t i, om_lib_entry *e)
{
    const uint8_t *r = l->table + 32 * i;
    memcpy(e->key, r, 16);
    e->key[16] = 0;
    e->disc = r[0x10]; e->disc_total = r[0x11]; e->area = r[0x12]; e->image_type = r[0x13];
    e->path = l->pool + om_be32(r + 0x14);
    e->folder = om_be16(r + 0x18);
    e->flags = om_be16(r + 0x1A);
    e->title = (e->flags & OM_LIB_TITLE) ? l->pool + om_be32(r + 0x1C) : "";
}

void om_libidx_folder(const om_libidx *l, uint32_t i, om_lib_folder *f)
{
    const uint8_t *r = l->fold + 8 * i;
    f->path = l->pool + om_be32(r);
    f->parent = om_be16(r + 4);
}

void om_libw_init(om_libw *w, om_lib_entry *ent, uint32_t ent_cap, uint16_t *order, om_lib_folder *fold,
                  uint32_t fold_cap, char *pool, uint32_t pool_cap)
{
    w->ent = ent; w->ent_cap = ent_cap; w->nent = 0; w->order = order;
    w->fold = fold; w->fold_cap = fold_cap; w->nfold = 0;
    w->pool = pool; w->pool_cap = pool_cap; w->pool_len = 0;
}

static const char *dup_str(om_libw *w, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    char *p;
    if (n == 1)
        return "";
    if (n > 256 || w->pool_len + n > w->pool_cap)
        return NULL;                 /* spec §2: paths <= 255 bytes */
    p = w->pool + w->pool_len;
    memcpy(p, s, n);
    w->pool_len += n;
    return p;
}

int om_libw_folder(om_libw *w, const char *path, uint16_t parent)
{
    const char *p;
    if (w->nfold >= w->fold_cap || !(p = dup_str(w, path)))
        return OM_E_FULL;
    w->fold[w->nfold].path = p;
    w->fold[w->nfold].parent = parent;
    return (int)w->nfold++;
}

int om_libw_entry(om_libw *w, const om_lib_entry *e)
{
    om_lib_entry *d;
    if (w->nent >= w->ent_cap)
        return OM_E_FULL;
    d = &w->ent[w->nent];
    *d = *e;
    if (!(d->path = dup_str(w, e->path)) || !(d->title = dup_str(w, e->title ? e->title : "")))
        return OM_E_FULL;
    w->nent++;
    return OM_OK;
}

static int before(const om_lib_entry *a, const om_lib_entry *b)
{
    if (a->folder != b->folder)
        return a->folder < b->folder;
    return strcmp(a->path, b->path) < 0;
}

int om_libw_finish(om_libw *w, int sort, uint32_t fingerprint, uint32_t build_date, uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    uint32_t i, j, n = w->nent, nf = w->nfold;
    uint32_t t_off = 64, f_off = t_off + 32 * n, p_off = f_off + 8 * nf, plen = 1, fp_off;
    for (i = 0; i < n; i++)
        w->order[i] = (uint16_t)i;
    if (sort)
        for (i = 1; i < n; i++) {
            uint16_t k = w->order[i];
            for (j = i; j > 0 && before(&w->ent[k], &w->ent[w->order[j - 1]]); j--)
                w->order[j] = w->order[j - 1];
            w->order[j] = k;
        }
    if (p_off + 1 + 4 > cap)
        return OM_E_FULL;
    memset(out, 0, p_off + 1);
#define PUT(s, dst) do { uint32_t _n = (uint32_t)strlen(s); \
        if (!_n) (dst) = 0; \
        else { if (p_off + plen + _n + 1 + 4 > cap) return OM_E_FULL; \
               memcpy(out + p_off + plen, (s), _n + 1); (dst) = plen; plen += _n + 1; } } while (0)
    for (i = 0; i < n; i++) {
        const om_lib_entry *e = &w->ent[w->order[i]];
        uint8_t *r = out + t_off + 32 * i;
        uint32_t po, to;
        size_t kl = strlen(e->key);
        PUT(e->path, po);
        PUT(e->title, to);
        memcpy(r, e->key, kl > 16 ? 16 : kl);
        r[0x10] = e->disc; r[0x11] = e->disc_total; r[0x12] = e->area; r[0x13] = e->image_type;
        om_put32(r + 0x14, po);
        om_put16(r + 0x18, e->folder);
        om_put16(r + 0x1A, (uint16_t)((e->flags & (OM_LIB_BOX | 0xFFFCu)) | (to ? OM_LIB_TITLE : 0)));   /* bits 2-15: sets */
        om_put32(r + 0x1C, to);
    }
    for (i = 0; i < nf; i++) {
        uint32_t fo;
        PUT(w->fold[i].path, fo);
        om_put32(out + f_off + 8 * i, fo);
        om_put16(out + f_off + 8 * i + 4, w->fold[i].parent);
        om_put16(out + f_off + 8 * i + 6, 0);
    }
#undef PUT
    fp_off = p_off + plen;
    om_put32(out + fp_off, fingerprint);
    memcpy(out, "OMSATLIB", 8);
    om_put16(out + 8, 1);
    om_put16(out + 10, 64);
    om_put32(out + 0x0C, n);
    om_put32(out + 0x10, t_off);
    om_put32(out + 0x14, 32 * n);
    om_put32(out + 0x18, p_off);
    om_put32(out + 0x1C, plen);
    om_put32(out + 0x20, build_date);
    om_put32(out + 0x24, om_crc32(0, out + t_off, 32 * n));
    om_put32(out + 0x28, f_off); om_put32(out + 0x2C, 8 * nf);
    om_put32(out + 0x30, p_off); om_put32(out + 0x34, plen);
    om_put32(out + 0x38, fp_off); om_put32(out + 0x3C, 4);
    *out_len = fp_off + 4;
    return OM_OK;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return 0;
    return *a == *b;
}

static int ipre(const char *name, const char *p)
{   /* name starts with p (lower case), ASCII case-insensitively */
    for (; *p; p++, name++)
        if (!*name || lower(*name) != *p) return 0;
    return 1;
}

int om_is_hidden_name(const char *name, int at_root)
{
    if (name[0] == '.' || name[0] == '$' || ieq(name, "System Volume Information"))   /* '$': Windows' $RECYCLE.BIN */
        return 1;
    if (!at_root)
        return 0;
    if (ieq(name, OM_CARD_NAME) || ieq(name, OM_CARD_OLD_NAME) || ieq(name, "openmenu")   /* "openmenu" kept hidden -- an old card folder */
        || ieq(name, "menu.bin")
        /* the Card Manager's final review I-6: Pseudo Saturn Kai's save tree, /SAVES/BRAM/..., read only
         * by Settings > Import -- never a game folder */
        || ieq(name, "saves")
        /* files the official menu itself writes to the root (satiator-menu@75bf6e5 main.c:203-206, 390:
         * region.txt on EVERY boot, before autoboot; ar.c:44 ar_backup.bin from its Action Replay tools). Not hidden,
         * the first official-menu boot would make every LIBRARY.IDX stale and force a scan at each start-up. */
        || ieq(name, "region.txt") || ieq(name, "ar_backup.bin")
        /* the launch descriptors LAUNCHER.BIN writes into the game's folder, the root for a root-level game or a
         * multi-disc set of root folders: out.desc, and out1..out9.desc (the lid swap, 2026-11-24) */
        || ieq(name, "out.desc") || (ipre(name, "out") && name[3] >= '1' && name[3] <= '9' && ieq(name + 4, ".desc")))
        return 1;
    return ipre(name, "autoboot") || ipre(name, "satiator-rings") || ipre(name, "satiator-saves");   /* 2026-11-23:
                                                                         * prefixes, as Satiator Rings hides them */
}

uint32_t om_fingerprint(const char *const *names, uint32_t n, uint16_t *scratch)
{
    uint32_t i, j, k = 0, crc = 0;
    for (i = 0; i < n; i++)
        if (!om_is_hidden_name(names[i], 1)) {
            uint16_t v = (uint16_t)i;
            for (j = k; j > 0 && strcmp(names[v], names[scratch[j - 1]]) < 0; j--)
                scratch[j] = scratch[j - 1];
            scratch[j] = v;
            k++;
        }
    for (i = 0; i < k; i++) {
        if (i)
            crc = om_crc32(crc, "\n", 1);
        crc = om_crc32(crc, names[scratch[i]], (uint32_t)strlen(names[scratch[i]]));
    }
    return crc;
}

#define FP_ROW 8u      /* off (4) + idx (2) + msort's tmp (2) */
void om_fpacc_init(om_fpacc *a, void *arena, uint32_t len)
{
    uint32_t max = len / 48u;                 /* names up to 40 B on average (this card's root: 34.8 B) */
    uint8_t *p = (uint8_t *)arena;
    if (max > 0xFFFFu) max = 0xFFFFu;         /* msort's indices are uint16 */
    a->max = max;
    a->off = (uint32_t *)(void *)(p + ((len - max * FP_ROW) & ~3u));
    a->idx = (uint16_t *)(void *)(a->off + max);
    a->tmp = a->idx + max;
    a->buf = (char *)p;
    a->cap = (uint32_t)((uint8_t *)a->off - p);
    a->used = a->n = 0;
    a->overflow = 0;
}

int om_fpacc_add(om_fpacc *a, const char *name)
{
    uint32_t len = (uint32_t)strlen(name) + 1;
    if (!strcmp(name, ".") || !strcmp(name, "..") || om_is_hidden_name(name, 1))
        return OM_OK;
    if (a->n >= a->max || a->used + len > a->cap) { a->overflow = 1; return OM_E_FULL; }
    memcpy(a->buf + a->used, name, len);
    a->off[a->n] = a->used;
    a->idx[a->n] = (uint16_t)a->n;
    a->used += len;
    a->n++;
    return OM_OK;
}

static int fp_cmp(const void *ctx, uint16_t x, uint16_t y)
{
    const om_fpacc *a = (const om_fpacc *)ctx;
    return strcmp(a->buf + a->off[x], a->buf + a->off[y]);
}

uint32_t om_fpacc_finish(om_fpacc *a)
{   /* O(n log n): an insertion sort over 2,048 names is ~1M strcmp on the SH-2 (msort.h) */
    uint32_t i, crc = 0;
    om_msort(a->idx, a->n, a->tmp, fp_cmp, a);
    for (i = 0; i < a->n; i++) {
        const char *s = a->buf + a->off[a->idx[i]];
        if (i)
            crc = om_crc32(crc, "\n", 1);
        crc = om_crc32(crc, s, (uint32_t)strlen(s));
    }
    return crc;
}

uint32_t om_libidx_games(const om_libidx *l)
{
    uint32_t i, n = 0;
    for (i = 0; i < l->count; i++)
        if (l->table[32 * i + 0x10] <= 1u) n++;              /* disc 1 (or 0) of disc_total */
    return n;
}
