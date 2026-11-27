/* menu/src/handoff.c -- see handoff.h. BSD-3-Clause. */
#include <stddef.h>
#include <string.h>
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"

static int put_str(char dst[256], const char *s)
{
    size_t n = strlen(s);
    if (n > 255)
        return OM_E_FIELD;   /* spec §2: paths <= 255 bytes */
    memset(dst, 0, 256);
    memcpy(dst, s, n);
    return OM_OK;
}

static int loader_ok(uint8_t l) { return l == OM_LDR_CWX || l == OM_LDR_JHL; }

_Static_assert(sizeof(om_handoff) <= 0x800u, "the hand-off block runs into the L1 harness's words (l1gate.h)");

/* v5: ndisc 0 with cur 0 and an all-zero pool, or 2..9 discs: that many non-empty NUL-terminated paths of at most
 * 255 B inside the pool, zeros after them, and cur 1..ndisc */
static int discs_ok(const om_handoff *h)
{
    uint32_t k, o = 0, n;
    if (!h->ndisc) {
        if (h->cur) return 0;
    } else if (h->ndisc < 2 || h->ndisc > OM_HO_DISCS_MAX || !h->cur || h->cur > h->ndisc)
        return 0;
    for (k = 0; k < h->ndisc; k++) {
        const char *z = memchr(h->discs + o, 0, OM_HO_DISC_POOL - o);
        if (!z) return 0;
        n = (uint32_t)(z - (h->discs + o));
        if (!n || n > 255) return 0;
        o += n + 1;
        if (o >= OM_HO_DISC_POOL && k + 1 < h->ndisc) return 0;
    }
    for (; o < OM_HO_DISC_POOL; o++)
        if (h->discs[o]) return 0;
    return 1;
}

const char *om_handoff_disc(const om_handoff *h, uint32_t k)
{
    uint32_t i, o = 0;
    if (!k || k > h->ndisc) return 0;
    for (i = 1; i < k; i++)
        o += (uint32_t)strlen(h->discs + o) + 1;
    return h->discs + o;
}

int om_handoff_fill2(om_handoff *h, const char *dir, const char *image, uint8_t area_code, uint32_t flags,
                     const om_launch_opts *o)
{
    memset(h, 0, sizeof *h);
    if (!o || !loader_ok(o->loader) || o->slot > 9 || o->saves > OM_HO_SAVES_SHARE || !memchr(o->gid, 0, sizeof o->gid)
        || !memchr(o->ckey, 0, sizeof o->ckey))
        return OM_E_FIELD;
    memcpy(h->magic, OM_HANDOFF_MAGIC, 8);
    h->version = OM_HANDOFF_VERSION;
    h->flags = flags;
    if (put_str(h->dir, dir) || put_str(h->image, image))
        return OM_E_FIELD;
    h->area_code = area_code;
    h->loader = o->loader;
    h->igr = (uint8_t)(o->igr && !(flags & OM_HO_NO_IGR));
    h->saves = o->saves;
    h->slot = o->slot;
    h->cheats = o->cheats;
    h->netlink = o->netlink;
    h->savetool = o->savetool;
    memcpy(h->gid, o->gid, sizeof h->gid);
    memcpy(h->ckey, o->ckey, sizeof h->ckey);
    h->art_w = o->art_w;
    h->art_h = o->art_h;
    memcpy(h->sty, o->sty, sizeof h->sty);
    h->art = o->art;
    h->crc = om_crc32(0, h, (uint32_t)offsetof(om_handoff, crc));
    return OM_OK;
}

int om_handoff_check(const om_handoff *h)
{
    if (memcmp(h->magic, OM_HANDOFF_MAGIC, 8))
        return OM_E_MAGIC;
    if (h->version != OM_HANDOFF_VERSION)
        return OM_E_VERSION;
    if (om_crc32(0, h, (uint32_t)offsetof(om_handoff, crc)) != h->crc)
        return OM_E_CRC;
    if (h->dir[255] || h->image[255] || !h->image[0] || !loader_ok(h->loader) || h->slot > 9
        || h->saves > OM_HO_SAVES_SHARE
        || !memchr(h->gid, 0, sizeof h->gid) || !memchr(h->ckey, 0, sizeof h->ckey) || (h->flags & ~(uint32_t)OM_HO_KNOWN_FLAGS)
        || !discs_ok(h))
        return OM_E_FIELD;
    return OM_OK;
}

int om_lnch_parse(const uint8_t *hdr, uint32_t file_size, om_lnch_info *out)
{
    uint32_t size;
    if (file_size < OM_LNCH_HDR)
        return OM_E_SHORT;
    if (memcmp(hdr, OM_LNCH_MAGIC, 8))
        return OM_E_MAGIC;
    if (om_crc32(0, hdr, 0x18) != om_be32(hdr + 0x18))
        return OM_E_CRC;
    if (om_be32(hdr + 0x08) != 1)
        return OM_E_VERSION;
    if (om_be32(hdr + 0x0C) != OM_LAUNCHER_LOAD)
        return OM_E_FIELD;
    size = om_be32(hdr + 0x10);
    if (size == 0 || size > OM_LAUNCHER_MAX || file_size != OM_LNCH_HDR + size)
        return OM_E_RANGE;
    out->body_size = size;
    out->body_crc = om_be32(hdr + 0x14);
    return OM_OK;
}

void om_lnch_make(uint8_t *hdr, uint32_t body_size, uint32_t body_crc)
{
    memset(hdr, 0, OM_LNCH_HDR);
    memcpy(hdr, OM_LNCH_MAGIC, 8);
    om_put32(hdr + 0x08, 1);
    om_put32(hdr + 0x0C, OM_LAUNCHER_LOAD);
    om_put32(hdr + 0x10, body_size);
    om_put32(hdr + 0x14, body_crc);
    om_put32(hdr + 0x18, om_crc32(0, hdr, 0x18));
}

/* --- the return block (handoff.h) --------------------------------------------------------------------------------- */
_Static_assert(OM_LW_RETURN + sizeof(om_retblk) <= OM_LW_SNAPSHOT, "the return block runs into the VDP1 snapshot");
_Static_assert(OM_LW_RETURN >= OM_LW_HANDOFF + 0x820u, "the return block overlaps the L1 harness's words");

static uint32_t ret_crc(const om_retblk *r) { return om_crc32(0, r, (uint32_t)offsetof(om_retblk, crc)); }

void om_ret_seal(om_retblk *r)
{
    memcpy(r->magic, OM_RET_MAGIC, 8);
    r->version = OM_RET_VERSION;
    r->crc = ret_crc(r);
}

int om_ret_ok(const om_retblk *r)
{
    return !memcmp(r->magic, OM_RET_MAGIC, 8) && r->version == OM_RET_VERSION && r->depth < OM_RET_LOCS &&
           r->armed <= 1u && memchr(r->key, 0, sizeof r->key) && r->crc == ret_crc(r);
}

int om_ret_arm(om_retblk *r)
{
    if (!om_ret_ok(r)) return 0;
    r->armed = 1;
    r->crc = ret_crc(r);
    return 1;
}

_Static_assert(sizeof(om_retblk) % 2u == 0u, "om_ret_copy16 copies whole 16-bit words");

void om_ret_copy16(volatile void *dst, const volatile void *src)
{
    volatile uint16_t *d = (volatile uint16_t *)dst;
    const volatile uint16_t *s = (const volatile uint16_t *)src;
    uint32_t i;
    for (i = 0; i < sizeof(om_retblk) / 2u; i++)
        d[i] = s ? s[i] : 0u;
}

int om_ret_take(om_retblk *blk, om_retblk *out)
{
    int ok;
    memcpy(out, blk, sizeof *out);
    memset(blk, 0, sizeof *blk);                      /* read once: a later start (a reset, a game's) is ordinary */
    ok = om_ret_ok(out) && out->armed == 1u;
    if (!ok) memset(out, 0, sizeof *out);
    return ok;
}
