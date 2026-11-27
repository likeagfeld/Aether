/* menu/src/sysid.c -- see sysid.h. Mirrors tools/omsat/iphdr.py. BSD-3-Clause. */
#include <string.h>
#include "sysid.h"

static void field(const uint8_t *b, uint32_t off, uint32_t n, char *out)
{
    memcpy(out, b + off, n);
    out[n] = 0;
    while (n && (out[n - 1] == ' ' || out[n - 1] == 0))
        out[--n] = 0;
}

uint8_t om_area_flags(const char *area)
{
    uint8_t f = 0;
    for (; *area; area++) {
        char c = (*area >= 'a' && *area <= 'z') ? (char)(*area - 32) : *area;
        f |= c == 'J' ? 1 : c == 'T' ? 2 : c == 'U' ? 4 : c == 'E' ? 8 : c == 'B' ? 16 : 0;
    }
    return f;
}

void om_disc_n_m(const char *d, uint8_t *n, uint8_t *m)
{
    uint32_t i;
    *n = *m = 1;
    for (i = 0; d[i] && d[i + 1] && d[i + 2]; i++) {
        uint32_t a = 0, b = 0, j = i + 3;
        if ((d[i] | 32) != 'c' || (d[i + 1] | 32) != 'd' || d[i + 2] != '-')
            continue;
        if (d[j] < '0' || d[j] > '9') continue;
        while (d[j] >= '0' && d[j] <= '9') a = a * 10 + (uint32_t)(d[j++] - '0');
        if (d[j] != '/') continue;
        j++;
        if (d[j] < '0' || d[j] > '9') continue;
        while (d[j] >= '0' && d[j] <= '9') b = b * 10 + (uint32_t)(d[j++] - '0');
        *n = (uint8_t)(a > 255 ? 255 : a);
        *m = (uint8_t)(b > 255 ? 255 : b);
        return;
    }
}

int om_sysid_parse(const uint8_t *buf, uint32_t len, om_sysid *s)
{
    static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    uint32_t off = 0;
    const uint8_t *b;
    if (len >= 16 && !memcmp(buf, sync, 12))
        off = buf[15] == 1 ? 16 : 24;
    if (len < off + 256)
        return OM_E_SHORT;
    b = buf + off;
    if (memcmp(b, "SEGA SEGASATURN ", 16))
        return OM_E_MAGIC;
    field(b, 0x10, 16, s->maker); field(b, 0x20, 10, s->product); field(b, 0x2A, 6, s->version);
    field(b, 0x30, 8, s->date); field(b, 0x38, 8, s->device); field(b, 0x40, 10, s->area);
    field(b, 0x50, 16, s->periph); field(b, 0x60, 112, s->title);
    s->area_flags = om_area_flags(s->area);
    om_disc_n_m(s->device, &s->disc, &s->disc_total);
    return OM_OK;
}

/* One FILE line from *pos on (the next line whose first non-blank word is FILE, any case): its file name into out, as
 * written (path components kept); *pos moves past the line. 1 = a name, 0 = no more FILE lines, OM_E_FIELD = a FILE
 * line with no name or an unclosed quote (*pos still moves past it). */
static int cue_next(const char *t, uint32_t len, uint32_t *pos, char *out, uint32_t cap)
{
    uint32_t i = *pos;
    while (i < len) {
        uint32_t j = 0;
        while (i < len && (t[i] == ' ' || t[i] == '\t')) i++;
        if (i + 5 <= len && (t[i] | 32) == 'f' && (t[i + 1] | 32) == 'i' && (t[i + 2] | 32) == 'l' && (t[i + 3] | 32) == 'e'
            && (t[i + 4] == ' ' || t[i + 4] == '\t')) {
            int bad = 0;
            i += 4;
            while (i < len && (t[i] == ' ' || t[i] == '\t')) i++;
            if (i < len && t[i] == '"') {
                for (i++; i < len && t[i] != '"' && t[i] != '\n'; i++)
                    if (j + 1 < cap) out[j++] = t[i];
                bad = i >= len || t[i] != '"';
            } else {
                for (; i < len && t[i] != ' ' && t[i] != '\t' && t[i] != '\r' && t[i] != '\n'; i++)
                    if (j + 1 < cap) out[j++] = t[i];
            }
            out[j] = 0;
            while (i < len && t[i] != '\n') i++;
            *pos = i + 1;
            return bad || !j ? OM_E_FIELD : 1;
        }
        while (i < len && t[i] != '\n') i++;
        i++;
    }
    *pos = len;
    return 0;
}

int om_cue_first_file(const char *t, uint32_t len, char *out, uint32_t cap)
{
    uint32_t pos = 0;
    int r = cue_next(t, len, &pos, out, cap);
    return r == 1 ? OM_OK : OM_E_FIELD;
}

int om_cue_files(const char *t, uint32_t len, char *out, uint32_t cap)
{
    uint32_t pos = 0, o = 0;
    int r, n = 0;
    char name[256];
    while ((r = cue_next(t, len, &pos, name, sizeof name)) != 0) {
        const char *b = name, *q;
        uint32_t k;
        if (r < 0) continue;                               /* a FILE line with no name: skipped */
        for (q = name; *q; q++)
            if (*q == '/' || *q == '\\') b = q + 1;
        k = (uint32_t)strlen(b) + 1;
        if (k < 2) continue;
        if (o + k > cap) break;
        memcpy(out + o, b, k);
        o += k;
        n++;
    }
    return n;
}
