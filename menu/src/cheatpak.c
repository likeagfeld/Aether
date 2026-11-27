/* menu/src/cheatpak.c -- see cheatpak.h. Check order matches tools/omsat/cheatpak.py. BSD-3-Clause. */
#include <string.h>
#include "cheatpak.h"
#include "om_fmt.h"
#include "pergame.h"

int om_cp_header(const uint8_t *b, om_cp_hdr *h)
{
    uint32_t tsize, i;
    if (memcmp(b, "OMSATCHT", 8))
        return OM_E_MAGIC;
    if (om_be16(b + 8) != 1)
        return OM_E_VERSION;
    if (om_be16(b + 10) != OM_HEADER_SIZE)
        return OM_E_HDRSIZE;
    h->count = om_be32(b + 0x0C);
    tsize = om_be32(b + 0x14);
    h->data_off = om_be32(b + 0x18);
    h->table_crc = om_be32(b + 0x24);
    if (om_be32(b + 0x10) != OM_HEADER_SIZE || h->count > OM_CP_MAX_FILE / OM_CP_ROW || tsize != h->count * OM_CP_ROW
        || h->data_off != OM_HEADER_SIZE + tsize)
        return OM_E_TABLESIZE;
    if (!om_in_range(h->data_off, om_be32(b + 0x1C), OM_CP_MAX_FILE))
        return OM_E_RANGE;
    h->data_end = h->data_off + om_be32(b + 0x1C);
    for (i = 0x28; i < OM_HEADER_SIZE; i++)
        if (b[i])
            return OM_E_RANGE;
    return OM_OK;
}

const uint8_t *om_cp_scan(const uint8_t *rows, uint32_t n, const char *key)
{
    char k[16];
    size_t len = strlen(key);
    if (len > 16)
        return 0;
    memset(k, 0, sizeof k);
    memcpy(k, key, len);
    for (; n; n--, rows += OM_CP_ROW)
        if (!memcmp(rows, k, 16))
            return rows;
    return 0;
}

int om_cp_row(const om_cp_hdr *h, const uint8_t *row, uint32_t *off, uint32_t *len)
{
    *off = om_be32(row + 16);
    *len = om_be32(row + 20);
    if ((*off & 3) || *len > OM_CP_MAX_REC || *off < h->data_off || !om_in_range(*off, *len, h->data_end))
        return OM_E_RANGE;
    return OM_OK;
}

static int name_ok(const uint8_t *p, uint32_t n)
{
    if (n < 1 || n > OM_CH_MAX_NAME)
        return 0;
    while (n--)
        if (p[n] < 0x20 || p[n] > 0x7E)
            return 0;
    return 1;
}

int om_cp_count(const uint8_t *rec, uint32_t len, uint32_t *on, uint32_t *n)
{
    uint32_t p = OM_CP_REC_HDR, i, nch, ln;
    *on = *n = 0;
    if (len < OM_CP_REC_HDR || (len & 3) || len > OM_CP_MAX_REC)
        return OM_E_FIELD;
    if (om_crc32(0, rec + 4, len - 4) != om_be32(rec))
        return OM_E_CRC;
    if ((nch = rec[11]) > OM_CH_MAX_CHEATS)
        return OM_E_FIELD;
    for (i = 0; i < nch; i++, p += 3 + ln) {
        if (p + 3 > len || p + 3 + (ln = rec[p + 2]) > len)
            return OM_E_FIELD;
        if (rec[p] & OM_CH_HEADER) continue;
        ++*n;
        if (rec[p] & OM_CH_ON) ++*on;
    }
    return OM_OK;
}

int om_cp_decode(const uint8_t *rec, uint32_t len, om_ch_file *f)
{
    uint32_t p = OM_CP_REC_HDR, i, j, sum = 0, nc;
    memset(f, 0, sizeof *f);
    if (len < OM_CP_REC_HDR || (len & 3) || len > OM_CP_MAX_REC)
        return OM_E_FIELD;
    if (om_crc32(0, rec + 4, len - 4) != om_be32(rec))
        return OM_E_CRC;
    f->master_addr = om_be32(rec + 4);
    f->master_value = om_be16(rec + 8);
    f->flags = rec[10];
    f->ncheats = rec[11];
    f->location = om_be32(rec + 12);
    f->ncodes = om_be16(rec + 16);
    f->cache4 = om_be16(rec + 18);
    if (f->ncheats > OM_CH_MAX_CHEATS || f->ncodes > OM_CH_MAX_CODES)
        return OM_E_FIELD;
    for (i = 0; i < f->ncheats; i++) {
        om_ch_cheat *c = &f->cheat[i];
        uint32_t ln;
        if (p + 3 > len)
            return OM_E_FIELD;
        c->flags = rec[p];
        nc = rec[p + 1];
        ln = rec[p + 2];
        if (p + 3 + ln > len || !name_ok(rec + p + 3, ln))
            return OM_E_FIELD;
        if (nc ? (c->flags & OM_CH_HEADER) != 0 : (c->flags & (OM_CH_HEADER | OM_CH_ON)) != OM_CH_HEADER)
            return OM_E_FIELD;
        memcpy(c->name, rec + p + 3, ln);
        c->first = (uint16_t)sum;
        c->count = (uint16_t)nc;
        sum += nc;
        p += 3 + ln;
    }
    if (sum != f->ncodes || p + 8 * sum > len || len - (p + 8 * sum) > 3)
        return OM_E_FIELD;
    for (j = 0; j < sum; j++, p += 8) {
        f->code[j].addr = om_be32(rec + p);
        f->code[j].value = om_be16(rec + p + 4);
        f->code[j].cmd = rec[p + 6];
    }
    for (; p < len; p++)
        if (rec[p])
            return OM_E_FIELD;
    return OM_OK;
}

int om_cp_encode(const om_ch_file *f, uint8_t *out, uint32_t cap, uint32_t *n)
{
    uint32_t p = OM_CP_REC_HDR, i, j, sum = 0;
    if (f->ncheats > OM_CH_MAX_CHEATS || f->ncodes > OM_CH_MAX_CODES)
        return OM_E_FIELD;
    for (i = 0; i < f->ncheats; i++) {
        const om_ch_cheat *c = &f->cheat[i];
        uint32_t ln = (uint32_t)strlen(c->name);
        if (!name_ok((const uint8_t *)c->name, ln) || c->count > 255 || c->first != sum
            || (c->count ? (c->flags & OM_CH_HEADER) != 0 : (c->flags & (OM_CH_HEADER | OM_CH_ON)) != OM_CH_HEADER))
            return OM_E_FIELD;
        if (p + 3 + ln > cap)
            return OM_E_FULL;
        out[p] = c->flags;
        out[p + 1] = (uint8_t)c->count;
        out[p + 2] = (uint8_t)ln;
        memcpy(out + p + 3, c->name, ln);
        p += 3 + ln;
        sum += c->count;
    }
    if (sum != f->ncodes)
        return OM_E_FIELD;
    if (p + 8 * sum + 3 > cap)
        return OM_E_FULL;
    for (j = 0; j < sum; j++, p += 8) {
        om_put32(out + p, f->code[j].addr);
        om_put16(out + p + 4, f->code[j].value);
        out[p + 6] = f->code[j].cmd;
        out[p + 7] = 0;
    }
    while (p & 3)
        out[p++] = 0;
    om_put32(out + 4, f->master_addr);
    om_put16(out + 8, f->master_value);
    out[10] = f->flags;
    out[11] = (uint8_t)f->ncheats;
    om_put32(out + 12, f->location);
    om_put16(out + 16, f->ncodes);
    om_put16(out + 18, f->cache4);
    om_put32(out, om_crc32(0, out + 4, p - 4));
    *n = p;
    return OM_OK;
}

uint32_t om_ch_enabled_codes(const om_ch_file *f, uint8_t out8[][8], uint32_t cap)
{
    uint32_t n = 0, i, j;
    for (i = 0; i < f->ncheats; i++) {
        const om_ch_cheat *c = &f->cheat[i];
        if (!(c->flags & OM_CH_ON))
            continue;
        for (j = 0; j < c->count; j++) {
            const om_ch_code *cd = &f->code[c->first + j];
            if (n >= cap)
                return n;
            om_put32(out8[n], cd->addr);
            om_put16(out8[n] + 4, cd->value);
            out8[n][6] = cd->cmd;
            out8[n][7] = 0;
            n++;
        }
    }
    return n;
}

int om_ch_want(int global_on, uint8_t per_game)
{
    if (per_game == OM_PG_ON)
        return 1;
    if (per_game == OM_PG_OFF)
        return 0;
    return global_on != 0;
}
