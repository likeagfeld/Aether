/* menu/src/impscan.c -- see impscan.h. BSD-3-Clause. */
#include <string.h>
#include "impscan.h"
#include "om_fmt.h"
#include "satiator.h"
#include "sdwrite.h"

static int pchar(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }

int om_pgs_imp_folder(const char *name, char out[OM_GID_MAX + 1])
{
    uint32_t n = 0, a = 0;
    const char *s = name;
    out[0] = 0;
    if (!name[0] || name[0] == '_' || name[0] == '[') return OM_E_FIELD;
    for (; *s && *s != '['; s++) {                   /* the product: Kai's sanitizing */
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        if (n >= OM_GID_PRODUCT) return OM_E_FIELD;
        out[n++] = pchar(c) ? c : '_';
    }
    while (n && out[n - 1] == '_' && name[n - 1] == ' ') n--;     /* Kai trims trailing spaces before sanitizing */
    if (!n) return OM_E_FIELD;
    if (*s == '[') {
        out[n++] = '[';
        for (s++; *s >= 'A' && *s <= 'Z' && a < OM_GID_AREAS; s++, a++) out[n++] = *s;
        if (!a || *s != ']' || s[1]) return out[0] = 0, OM_E_FIELD;
        out[n++] = ']';
    }
    out[n] = 0;
    return OM_OK;
}

uint32_t om_pgs_rings_slot(const char *n)
{
    if (strncmp(n, "[SLOT", 5) || n[5] < '0' || n[5] > '9' || n[6] < '0' || n[6] > '9' || n[7] != ']' || n[8])
        return 0;
    return (uint32_t)(n[5] - '0') * 10u + (uint32_t)(n[6] - '0');
}

/* --- the scan --------------------------------------------------------------------------------------------------- */
typedef struct { om_isc_row *r; uint32_t cap, n; om_isc_name *nm; uint32_t ncap; uint32_t *al, *st; } scan_t;

static const char *alp(scan_t *s, const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n > 255) n = 255;
    memcpy(s->al, p, n);
    ((char *)s->al)[n] = 0;
    return (const char *)s->al;
}

static int bup(const char *n, int len)
{
    return len > 4 && n[len - 4] == '.' && (n[len - 3] | 32) == 'b' && (n[len - 2] | 32) == 'u' && (n[len - 1] | 32) == 'p';
}

/* p's entries: *files = its "*.BUP"; dirs (first..first+*nd) = its folder names (into s->nm from index first) */
static int lst(scan_t *s, const char *p, uint32_t *files, uint32_t first, uint32_t *nd)
{
    s_stat_t *st = (s_stat_t *)s->st;
    int len, rc;
    *files = 0;
    if (nd) *nd = 0;
    if ((rc = s_opendir(alp(s, p))) < 0) return rc == -FR_NO_FILE || rc == -FR_NO_PATH ? 0 : OM_SDW_ERR(rc);
    while ((len = s_stat(NULL, st, 300)) > 0) {
        st->name[len] = 0;
        if (st->name[0] == '.' && (!st->name[1] || (st->name[1] == '.' && !st->name[2]))) continue;
        if (st->attrib & AM_DIR) {
            if (nd && len < (int)sizeof(om_isc_name) && first + *nd < s->ncap) memcpy(s->nm[first + (*nd)++], st->name, (uint32_t)len + 1);
        } else if (bup(st->name, len))
            (*files)++;
    }
    return len == SAT_ETIMEDOUT ? OM_SDW_ERR(len) : 0;
}

static void add(scan_t *s, const char *key, uint32_t n)
{
    uint32_t i;
    if (!n) return;
    for (i = 0; i < s->n && strcmp(s->r[i].key, key); i++) ;
    if (i == s->n) {
        if (s->n == s->cap) return;
        memset(&s->r[i], 0, sizeof s->r[i]);
        strcpy(s->r[i].key, key);
        s->r[i].on = 1;
        s->n++;
    }
    s->r[i].n = (uint16_t)(s->r[i].n + n > 65535u ? 65535u : s->r[i].n + n);
}

static void prod(const char *name, char out[OM_GID_MAX + 1])
{
    char *b;
    if (om_pgs_imp_folder(name, out)) { out[0] = 0; return; }
    if ((b = strchr(out, '[')) != 0) *b = 0;
}

/* a game folder (base/name): its "*.BUP", and those of its 01..09 */
static int count_game(scan_t *s, const char *base, const char *name, uint32_t at, uint32_t *total)
{
    char p[256], q[256];
    uint32_t f, nd, k;
    int rc;
    strcpy(p, base); strcat(p, "/"); strcat(p, name);
    if ((rc = lst(s, p, total, at, &nd)) != 0) return rc;
    for (k = 0; k < nd; k++) {
        const char *d = s->nm[at + k];
        if (!(d[0] == '0' && d[1] >= '1' && d[1] <= '9' && !d[2])) continue;
        strcpy(q, p); strcat(q, "/"); strcat(q, d);
        if ((rc = lst(s, q, &f, 0, 0)) != 0) return rc;
        *total += f;
    }
    return 0;
}

int om_isc_scan(om_isc_row *rows, uint32_t cap, uint32_t *n, om_isc_name *names, uint32_t ncap, uint32_t *al,
                uint32_t *st)
{
    scan_t s;
    char pr[OM_GID_MAX + 1], f[OM_GID_MAX + 1];
    uint32_t nk, nr, i, j, c, ni;
    int rc;
    s.r = rows; s.cap = cap; s.n = 0; s.nm = names; s.ncap = ncap; s.al = al; s.st = st;
    *n = 0;
    if (ncap < 32) return OM_E_FULL;
    /* Kai: the game folders (names [0, nk)) */
    if ((rc = lst(&s, "/SAVES/BRAM", &c, 0, &nk)) != 0) return rc;
    for (i = 0; i < nk; i++) {
        if (om_pgs_imp_folder(names[i], f) || strcmp(f, names[i])) continue;   /* not one of Kai's game folders */
        if ((rc = count_game(&s, "/SAVES/BRAM", names[i], nk, &c)) != 0) return rc;
        prod(names[i], pr);
        add(&s, pr, c);
    }
    if ((rc = lst(&s, "/SAVES/LATEST", &c, 0, 0)) != 0) return rc;
    add(&s, OM_ISC_GENERAL, c);
    /* Rings: the id folders and [SLOTnn]/<id> (names [0, nr), then each slot's ids after them) */
    if ((rc = lst(&s, "/satiator-saves", &c, 0, &nr)) != 0) return rc;
    for (i = 0; i < nr; i++) {
        uint32_t sl = om_pgs_rings_slot(names[i]);
        char p[256];
        if (!sl) {
            if (om_pgs_imp_folder(names[i], f) || strchr(f, '[')) continue;
            strcpy(p, "/satiator-saves/"); strcat(p, names[i]);
            if ((rc = lst(&s, p, &c, 0, 0)) != 0) return rc;
            prod(names[i], pr);
            add(&s, pr, c);
            continue;
        }
        if (sl > 19) continue;
        strcpy(p, "/satiator-saves/"); strcat(p, names[i]);
        if ((rc = lst(&s, p, &c, nr, &ni)) != 0) return rc;
        for (j = 0; j < ni; j++) {
            char q[256];
            if (om_pgs_imp_folder(names[nr + j], f) || strchr(f, '[')) continue;
            strcpy(q, p); strcat(q, "/"); strcat(q, names[nr + j]);
            if ((rc = lst(&s, q, &c, 0, 0)) != 0) return rc;
            prod(names[nr + j], pr);
            add(&s, pr, c);
        }
    }
    *n = s.n;
    return 0;
}

/* --- IMPORT.SEL ------------------------------------------------------------------------------------------------- */
static char *put_u(char *o, uint32_t v)
{
    char t[11];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *o++ = t[--k];
    return o;
}

int om_isel_fmt(const om_isc_row *rows, uint32_t n, char *out, uint32_t cap, uint32_t *crc)
{
    uint32_t i, on = 0, len;
    char *o;
    for (i = 0; i < n; i++) on += rows[i].on != 0;
    if (cap < 16 + on * (OM_ISC_KEY + 1)) return OM_E_FULL;
    o = out;
    memcpy(o, "VMSEL1 ", 7); o += 7;
    o = put_u(o, on);
    *o++ = '\n';
    for (i = 0; i < n; i++) {
        uint32_t k;
        if (!rows[i].on) continue;
        k = (uint32_t)strlen(rows[i].key);
        memcpy(o, rows[i].key, k); o += k;
        *o++ = '\n';
    }
    len = (uint32_t)(o - out);
    *crc = om_crc32(0, out, len);
    return (int)len;
}

static int hexv(const char *d, uint32_t *v)
{
    int k;
    *v = 0;
    for (k = 0; k < 8; k++) {
        char c = d[k];
        uint32_t x = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A') + 10u : 99u;
        if (x > 15) return 0;
        *v = *v << 4 | x;
    }
    return !d[8];
}

int om_isel_check(const char *t, uint32_t len, const char *gid)
{
    uint32_t want, n = 0, i, lines = 0;
    if (strncmp(gid, "SEL", 3) || !hexv(gid + 3, &want)) return OM_E_FIELD;
    if (len < 9 || memcmp(t, "VMSEL1 ", 7) || t[len - 1] != '\n') return OM_E_FIELD;
    for (i = 7; i < len && t[i] >= '0' && t[i] <= '9' && n < 10000u; i++) n = n * 10u + (uint32_t)(t[i] - '0');
    if (i == 7 || i >= len || t[i] != '\n') return OM_E_FIELD;
    for (i++; i < len; i++) {
        uint32_t a = i;
        while (i < len && t[i] != '\n') {
            char c = t[i];
            if (!(pchar(c) || c == '_')) return OM_E_FIELD;
            i++;
        }
        if (i - a == 0 || i - a >= OM_ISC_KEY) return OM_E_FIELD;
        lines++;
    }
    if (lines != n) return OM_E_FIELD;
    return om_crc32(0, t, len) == want ? OM_OK : OM_E_CRC;
}

int om_isel_has(const char *t, uint32_t len, const char *key)
{
    uint32_t i, k = (uint32_t)strlen(key);
    for (i = 0; i < len && t[i] != '\n'; i++) ;
    for (i++; i + k < len + 1 && i < len; ) {
        uint32_t a = i;
        while (i < len && t[i] != '\n') i++;
        if (i - a == k && !memcmp(t + a, key, k)) return 1;
        i++;
    }
    return 0;
}
