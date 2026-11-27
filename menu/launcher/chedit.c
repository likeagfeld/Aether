/* menu/launcher/chedit.c -- see chedit.h. BSD-3-Clause. */
#include <string.h>
#include "chedit.h"
#include "chkai.h"
#include "om_fmt.h"

const uint8_t om_ced_types[4] = { 0x00, 0x01, 0x03, 0x0D };
static const char k_wheel[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.!%&'()/:+?";
#define WHEEL (sizeof k_wheel - 1u)

static void clamp_list(om_ced *e)
{
    uint16_t n = e->f->ncheats;
    if (e->row >= n) e->row = n ? (uint16_t)(n - 1u) : 0;
    if (e->row < e->top) e->top = e->row;
    if (e->row >= e->top + OM_CE_VIS) e->top = (uint16_t)(e->row - OM_CE_VIS + 1u);
}

static void clamp_codes(om_ced *e)
{
    uint16_t n = e->f->cheat[e->cheat].count;
    if (e->crow >= n) e->crow = n ? (uint16_t)(n - 1u) : 0;
    if (e->crow < e->ctop) e->ctop = e->crow;
    if (e->crow >= e->ctop + OM_CE_VIS) e->ctop = (uint16_t)(e->crow - OM_CE_VIS + 1u);
}

void om_ced_open(om_ced *e, om_ch_file *f, int file_state)
{
    memset(e, 0, sizeof *e);
    e->f = f;
    if (file_state == 2) {
        memset(f, 0, sizeof *f);                       /* nothing shown, nothing saved */
        e->ro = 1;
    } else if (file_state == 1)
        memset(f, 0, sizeof *f);
    e->cap = (uint16_t)om_chk_cap(f);
}

uint32_t om_ced_active(const om_ced *e) { return om_chk_active(e->f); }

static void put_u(char **o, uint32_t v)
{
    char d[12];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *(*o)++ = d[--k];
}

void om_ced_slots(const om_ced *e, char *out, uint32_t cap)
{
    char t[48], *o = t;
    uint32_t a = om_ced_active(e), left = a < e->cap ? e->cap - a : 0;
    put_u(&o, e->cap);
    memcpy(o, " - ", 3); o += 3;
    put_u(&o, a);
    memcpy(o, " = ", 3); o += 3;
    put_u(&o, left);
    strcpy(o, left == 1 ? " SLOT LEFT" : " SLOTS LEFT");
    strncpy(out, t, cap - 1);
    out[cap - 1] = 0;
}

void om_ced_code_text(const om_ced *e, char out[18])
{
    static const char hx[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; i++) out[i] = hx[(e->addr >> (28 - 4 * i)) & 15u];
    out[8] = ':';
    for (i = 0; i < 4; i++) out[9 + i] = hx[(e->val >> (12 - 4 * i)) & 15u];
    out[13] = ':';
    out[14] = hx[e->cmd >> 4];
    out[15] = hx[e->cmd & 15u];
    out[16] = 0;
}

static void changed(om_ced *e)
{
    e->dirty = 1;
    om_chk_default_master(e->f);                       /* a record with no master gets Kai's default (chkai.h) */
    e->cap = (uint16_t)om_chk_cap(e->f);
}

/* remove `count` codes at `at` from the code table, and every cheat's first index after them */
static void codes_cut(om_ch_file *f, uint16_t at, uint16_t count)
{
    uint16_t i;
    memmove(&f->code[at], &f->code[at + count], (size_t)(f->ncodes - at - count) * sizeof f->code[0]);
    f->ncodes = (uint16_t)(f->ncodes - count);
    for (i = 0; i < f->ncheats; i++)
        if (f->cheat[i].first > at) f->cheat[i].first = (uint16_t)(f->cheat[i].first - count);
}

/* open a gap of one code at `at`; cheats starting at or after it move on (the owner, ending there, grows) */
static void codes_gap(om_ch_file *f, uint16_t at, uint16_t owner)
{
    uint16_t i;
    memmove(&f->code[at + 1], &f->code[at], (size_t)(f->ncodes - at) * sizeof f->code[0]);
    f->ncodes++;
    for (i = 0; i < f->ncheats; i++)
        if (i != owner && f->cheat[i].first >= at) f->cheat[i].first++;
}

static void start_code(om_ced *e, int is_new, const om_ch_code *c)
{
    e->page = OM_CE_CODE;
    e->field = 0;
    e->is_new = (uint8_t)is_new;
    if (c) {
        e->addr = c->addr; e->val = c->value; e->cmd = c->cmd;
    } else {
        e->addr = 0x06000000u; e->val = 0; e->cmd = 0x01;
    }
}

static int key_list(om_ced *e, uint16_t k)
{
    om_ch_file *f = e->f;
    om_ch_cheat *c = f->ncheats ? &f->cheat[e->row] : 0;
    int armed = e->armed;
    e->armed = 0;
    if (k & OM_CK_B)
        return 1;
    if (k & OM_CK_U) { if (e->row) e->row--; else if (!(k & OM_CK_REP) && f->ncheats) e->row = f->ncheats - 1u;
                       clamp_list(e); return 0; }
    if (k & OM_CK_D) { if (e->row + 1u < f->ncheats) e->row++; else if (!(k & OM_CK_REP)) e->row = 0;
                       clamp_list(e); return 0; }
    if (!(k & (OM_CK_A | OM_CK_X | OM_CK_Y | OM_CK_Z)))
        return 0;
    if (e->ro) { e->msg = OM_CEM_READONLY; return 0; }
    if (k & OM_CK_X) {
        if (f->ncheats >= OM_CH_MAX_CHEATS || f->ncodes >= OM_CH_MAX_CODES) { e->msg = OM_CEM_FILEFULL; return 0; }
        e->page = OM_CE_NAME;
        e->is_new = 1;
        e->name[0] = 0;
        e->npos = 0;
        return 0;
    }
    if (!c)
        return 0;
    if (k & OM_CK_A) {
        if (c->flags & OM_CH_HEADER) { e->msg = OM_CEM_LABEL; return 0; }
        if (!(c->flags & OM_CH_ON) && om_ced_active(e) + c->count > e->cap) { e->msg = OM_CEM_FULL; return 0; }
        c->flags ^= OM_CH_ON;
        changed(e);
        return 0;
    }
    if (!(c->flags & OM_CH_USER)) { e->msg = OM_CEM_KAI; return 0; }
    if (k & OM_CK_Y) {
        e->page = OM_CE_CODES;
        e->cheat = e->row;
        e->crow = e->ctop = 0;
        return 0;
    }
    if (!armed) { e->armed = 1; e->msg = OM_CEM_ASK; return 0; }   /* Z: asks twice */
    codes_cut(f, c->first, c->count);
    memmove(c, c + 1, (size_t)(f->ncheats - e->row - 1u) * sizeof *c);
    f->ncheats--;
    clamp_list(e);
    changed(e);
    return 0;
}

static void key_name(om_ced *e, uint16_t k)
{
    size_t n = strlen(e->name);
    if (k & OM_CK_B) { e->page = e->is_new ? OM_CE_LIST : OM_CE_CODES; return; }
    if (k & (OM_CK_U | OM_CK_D)) {
        const char *w = strchr(k_wheel, e->npos < n ? e->name[e->npos] : ' ');
        uint32_t i = w ? (uint32_t)(w - k_wheel) : 0;
        i = (k & OM_CK_U) ? (i + 1u) % WHEEL : (i + WHEEL - 1u) % WHEEL;
        e->name[e->npos] = k_wheel[i];
        if (e->npos >= n) e->name[e->npos + 1] = 0;
        return;
    }
    if (k & OM_CK_L) { if (e->npos) e->npos--; return; }
    if (k & OM_CK_R) { if (e->npos < n && e->npos + 1u < OM_CH_MAX_NAME) e->npos++; return; }
    if (k & OM_CK_A) {
        while (n && e->name[n - 1] == ' ') e->name[--n] = 0;
        if (!n) { e->msg = OM_CEM_NAME; return; }
        if (e->is_new) { start_code(e, 1, 0); return; }
        strcpy(e->f->cheat[e->cheat].name, e->name);
        changed(e);
        e->page = OM_CE_CODES;
    }
}

static void key_code(om_ced *e, uint16_t k)
{
    om_ch_file *f = e->f;
    int v;
    if (k & OM_CK_B) { e->page = e->is_new == 1 ? OM_CE_LIST : OM_CE_CODES; return; }
    if (k & OM_CK_L) { if (e->field) e->field--; return; }
    if (k & OM_CK_R) { if (e->field + 1u < OM_CE_FIELDS) e->field++; return; }
    if (k & (OM_CK_U | OM_CK_D)) {
        int up = (k & OM_CK_U) != 0;
        if (e->field < 8) {
            uint32_t sh = 28u - 4u * e->field, d = (e->addr >> sh) & 15u;
            d = (d + (up ? 1u : 15u)) & 15u;
            e->addr = (e->addr & ~(15u << sh)) | (d << sh);
        } else if (e->field < 12) {
            uint32_t sh = 12u - 4u * (e->field - 8u), d = ((uint32_t)e->val >> sh) & 15u;
            d = (d + (up ? 1u : 15u)) & 15u;
            e->val = (uint16_t)((e->val & ~(15u << sh)) | (d << sh));
        } else {
            uint32_t i;
            for (i = 0; i < 4 && om_ced_types[i] != e->cmd; i++) ;
            e->cmd = om_ced_types[(i + (up ? 1u : 3u)) % 4u];
        }
        return;
    }
    if (!(k & OM_CK_A))
        return;
    if ((v = om_chk_code_ok(e->addr, e->cmd)) != OM_CHV_OK) {
        e->msg = OM_CEM_BAD;
        e->why = (uint8_t)v;
        return;
    }
    if (e->is_new == 1) {                              /* a new cheat with its first code, added off */
        om_ch_cheat *c = &f->cheat[f->ncheats];
        memset(c, 0, sizeof *c);
        strcpy(c->name, e->name);
        c->flags = OM_CH_USER;
        c->first = f->ncodes;
        c->count = 1;
        f->code[f->ncodes].addr = e->addr;
        f->code[f->ncodes].value = e->val;
        f->code[f->ncodes].cmd = e->cmd;
        f->ncodes++;
        f->ncheats++;
        e->row = (uint16_t)(f->ncheats - 1u);
        clamp_list(e);
        e->page = OM_CE_LIST;
    } else {
        om_ch_cheat *c = &f->cheat[e->cheat];
        uint16_t at;
        if (e->is_new == 2) {                          /* a code added after the cheat's last */
            at = (uint16_t)(c->first + c->count);
            codes_gap(f, at, e->cheat);
            c->count++;
            e->crow = (uint16_t)(c->count - 1u);
        } else
            at = (uint16_t)(c->first + e->crow);
        f->code[at].addr = e->addr;
        f->code[at].value = e->val;
        f->code[at].cmd = e->cmd;
        clamp_codes(e);
        e->page = OM_CE_CODES;
    }
    changed(e);
}

static void key_codes(om_ced *e, uint16_t k)
{
    om_ch_file *f = e->f;
    om_ch_cheat *c = &f->cheat[e->cheat];
    int armed = e->armed;
    e->armed = 0;
    if (k & OM_CK_B) { e->page = OM_CE_LIST; return; }
    if (k & OM_CK_U) { if (e->crow) e->crow--; else if (!(k & OM_CK_REP) && c->count) e->crow = c->count - 1u;
                       clamp_codes(e); return; }
    if (k & OM_CK_D) { if (e->crow + 1u < c->count) e->crow++; else if (!(k & OM_CK_REP)) e->crow = 0;
                       clamp_codes(e); return; }
    if (k & OM_CK_A) { start_code(e, 0, &f->code[c->first + e->crow]); return; }
    if (k & OM_CK_X) {
        if (f->ncodes >= OM_CH_MAX_CODES || c->count >= 255) { e->msg = OM_CEM_FILEFULL; return; }
        if ((c->flags & OM_CH_ON) && om_ced_active(e) + 1u > e->cap) { e->msg = OM_CEM_FULL; return; }
        start_code(e, 2, 0);
        return;
    }
    if (k & OM_CK_Y) {
        strcpy(e->name, c->name);
        e->npos = 0;
        e->is_new = 0;
        e->page = OM_CE_NAME;
        return;
    }
    if (k & OM_CK_Z) {
        if (c->count <= 1) { e->msg = OM_CEM_LAST; return; }
        if (!armed) { e->armed = 1; e->msg = OM_CEM_ASK; return; }
        codes_cut(f, (uint16_t)(c->first + e->crow), 1);
        c->count--;
        clamp_codes(e);
        changed(e);
    }
}

int om_ced_key(om_ced *e, uint16_t keys)
{
    if (!keys)
        return 0;
    e->msg = OM_CEM_NONE;
    switch (e->page) {
    case OM_CE_LIST:  return key_list(e, keys);
    case OM_CE_NAME:  key_name(e, keys); break;
    case OM_CE_CODE:  key_code(e, keys); break;
    default:          key_codes(e, keys); break;
    }
    return 0;
}

/* --- CHEATS.PAK -------------------------------------------------------------------------------------------------- */

static int index_ok(const uint8_t *pak, uint32_t len, om_cp_hdr *h)
{
    int rc;
    if (len < OM_HEADER_SIZE || len > OM_CP_MAX_FILE)
        return OM_E_RANGE;
    if ((rc = om_cp_header(pak, h)) < 0)
        return rc;
    if (h->data_end > len)
        return OM_E_RANGE;
    if (om_crc32(0, pak + OM_HEADER_SIZE, h->count * OM_CP_ROW) != h->table_crc)
        return OM_E_CRC;
    return OM_OK;
}

static void key16(char k[16], const char *key)
{
    size_t n = strlen(key);
    memset(k, 0, 16);
    memcpy(k, key, n > 16 ? 16 : n);
}

int om_cpk_find(const uint8_t *pak, uint32_t len, const char *key, om_ch_file *f)
{
    om_cp_hdr h;
    const uint8_t *row;
    uint32_t off, n;
    int rc;
    if ((rc = index_ok(pak, len, &h)) < 0)
        return rc;
    if (!(row = om_cp_scan(pak + OM_HEADER_SIZE, h.count, key)))
        return 0;
    if ((rc = om_cp_row(&h, row, &off, &n)) < 0 || (rc = om_cp_decode(pak + off, n, f)) < 0)
        return rc;
    return 1;
}

int om_cpk_put(const uint8_t *pak, uint32_t len, const char *key, const om_ch_file *f, uint32_t date, uint8_t *out,
               uint32_t cap, uint8_t *s_rec, uint32_t *n)
{
    om_cp_hdr h;
    char k[16];
    uint32_t rec_n = 0, rows = 0, i, d, data_off, total;
    const uint8_t *old = 0;
    int rc, have = 0, put = f->ncheats != 0, placed = 0;
    memset(&h, 0, sizeof h);
    key16(k, key);
    if (pak && len) {
        if ((rc = index_ok(pak, len, &h)) < 0)
            return rc;
        old = pak + OM_HEADER_SIZE;
        date = om_be32(pak + 0x20);
        have = om_cp_scan(old, h.count, key) != 0;
    }
    if (put && (rc = om_cp_encode(f, s_rec, OM_CP_MAX_REC, &rec_n)) < 0)
        return rc;
    rows = h.count - (uint32_t)have + (uint32_t)put;
    data_off = OM_HEADER_SIZE + rows * OM_CP_ROW;
    if (data_off > cap)
        return OM_E_FULL;
    /* rows in key order: the old ones with ours replaced, added or dropped */
    total = data_off;
    for (i = 0, d = 0; i <= h.count; i++) {
        const uint8_t *r = i < h.count ? old + i * OM_CP_ROW : 0, *blob;
        uint32_t off = 0, blen, j;
        int mine = 0;
        uint8_t *w;
        if (r && !memcmp(r, k, 16)) {
            if (!put) continue;
            mine = 1;
        } else if (put && !placed && !have && (!r || memcmp(r, k, 16) > 0))
            mine = 1;                                  /* the new key goes before the first greater one */
        else if (!r)
            break;
        if (mine) {
            blob = s_rec;
            blen = rec_n;
            placed = 1;
            if (r && memcmp(r, k, 16)) i--;            /* this old row still comes next */
        } else {
            if ((rc = om_cp_row(&h, r, &off, &blen)) < 0)
                return rc;
            blob = pak + off;
        }
        w = out + OM_HEADER_SIZE + d * OM_CP_ROW;
        if (w + OM_CP_ROW > out + data_off)
            return OM_E_FIELD;
        memcpy(w, mine ? k : (const char *)r, 16);
        for (j = 0; j < d; j++) {                      /* identical records are stored once (write_pak) */
            const uint8_t *q = out + OM_HEADER_SIZE + j * OM_CP_ROW;
            uint32_t qo = om_be32(q + 16), ql = om_be32(q + 20);
            if (ql == blen && !memcmp(out + qo, blob, blen))
                break;
        }
        if (j < d) {
            om_put32(w + 16, om_be32(out + OM_HEADER_SIZE + j * OM_CP_ROW + 16));
        } else {
            if (total + blen > cap || total + blen > OM_CP_MAX_FILE)
                return OM_E_FULL;
            memmove(out + total, blob, blen);
            om_put32(w + 16, total);
            total += blen;
        }
        om_put32(w + 20, blen);
        d++;
    }
    if (d != rows)
        return OM_E_FIELD;
    memset(out, 0, OM_HEADER_SIZE);
    memcpy(out, "OMSATCHT", 8);
    om_put16(out + 8, 1);
    om_put16(out + 10, OM_HEADER_SIZE);
    om_put32(out + 0x0C, rows);
    om_put32(out + 0x10, OM_HEADER_SIZE);
    om_put32(out + 0x14, rows * OM_CP_ROW);
    om_put32(out + 0x18, data_off);
    om_put32(out + 0x1C, total - data_off);
    om_put32(out + 0x20, date);
    om_put32(out + 0x24, om_crc32(0, out + OM_HEADER_SIZE, rows * OM_CP_ROW));
    *n = total;
    return OM_OK;
}
