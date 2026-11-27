/* menu/src/pergame.c -- see pergame.h (SPEC §5.14). BSD-3-Clause. */
#include <string.h>
#include "pergame.h"
#include "ini.h"
#include "om_fmt.h"
#include "settings.h"

enum { K_TITLE, K_LOADER, K_SAVES, K_SLOT, K_CHEATS, K_IGR, K_COUNT };
static const char *const k_key[K_COUNT] = { "title", "loader", "saves", "slot", "cheats", "igr" };
static const char *const k_pg[3] = { "default", "on", "off" };
enum { L_OTHER, L_BLANK, L_HEAD, L_KEY };

typedef struct {
    char buf[OM_INI_LINE + 2];
    const char *name, *val;      /* L_HEAD: the section name; L_KEY: the value */
    uint32_t ls, pos;            /* the raw line is text[ls, pos), terminator included */
    uint8_t term, kind, key;     /* term: 0 / 1 (LF) / 2 (CRLF) bytes; key: K_* or K_COUNT */
} line_t;

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a | 32) != *b) return 0;
    return *a == *b;
}

static int next(const char *t, uint32_t len, line_t *l)
{
    char *s, *p;
    int r;
    l->ls = l->pos;
    if ((r = om_text_line(t, len, &l->pos, l->buf)) <= 0)
        return r;
    l->term = (uint8_t)(t[l->pos - 1] != '\n' ? 0 : l->pos - l->ls >= 2 && t[l->pos - 2] == '\r' ? 2 : 1);
    s = trim(l->buf);
    l->kind = L_OTHER;
    l->key = K_COUNT;
    if (!*s)
        l->kind = L_BLANK;
    else if (*s == '[') {
        l->kind = L_HEAD;
        if ((p = strchr(s, ']')) != NULL) *p = 0;
        l->name = trim(s + 1);
    } else if (*s != '#' && (p = strchr(s, '=')) != NULL) {
        *p = 0;
        l->kind = L_KEY;
        l->val = trim(p + 1);
        s = trim(s);
        for (l->key = 0; l->key < K_COUNT && strcmp(s, k_key[l->key]); l->key++) ;
    }
    return 1;
}

void om_pergame_default(om_pergame *g, const char *id)
{
    memset(g, 0, sizeof *g);
    strncpy(g->id, id, OM_GID_MAX - 1);
}

static void set_key(om_pergame *g, uint8_t k, const char *v)
{
    uint8_t i;
    if (k == K_TITLE) {
        strncpy(g->title, v, OM_PG_TITLE_MAX);
        g->title[OM_PG_TITLE_MAX] = 0;
    } else if (k == K_SLOT) {
        if (v[0] >= '0' && v[0] <= '9' && !v[1]) g->slot = (uint8_t)(v[0] - '0');
        else if (ieq(v, "ask")) g->slot = OM_PG_SLOT_ASK;
    } else if (k == K_LOADER) {
        for (i = 0; i < OM_LDR_COUNT; i++)
            if (ieq(v, om_loader_key(i))) g->loader = i;
    } else {
        uint8_t *f = k == K_SAVES ? &g->saves : k == K_CHEATS ? &g->cheats : &g->igr;
        for (i = 0; i < 3; i++)
            if (ieq(v, k_pg[i])) *f = i;
    }
}

/* The value the writer puts for key k, or NULL when it is the default (the line is dropped). */
static const char *val_of(const om_pergame *g, uint8_t k, char digit[2])
{
    uint8_t v;
    switch (k) {
    case K_TITLE: return g->title[0] ? g->title : NULL;
    case K_LOADER: return g->loader && g->loader < OM_LDR_COUNT ? om_loader_key(g->loader) : NULL;
    case K_SLOT:
        if (g->slot == OM_PG_SLOT_ASK) return "ask";
        if (!g->slot || g->slot > 9) return NULL;
        digit[0] = (char)('0' + g->slot); digit[1] = 0;
        return digit;
    default:
        v = k == K_SAVES ? g->saves : k == K_CHEATS ? g->cheats : g->igr;
        return v == OM_PG_ON || v == OM_PG_OFF ? k_pg[v] : NULL;
    }
}

/* Walks the whole file (so a corrupt line anywhere is an error). want: the section read into g (the first copy;
 * *found set), or NULL; nth_name: receives section nth's name. Returns the section count or < 0. */
static int scan(const char *t, uint32_t len, const char *want, om_pergame *g, uint8_t *found, uint32_t nth,
                char *nth_name)
{
    line_t l;
    uint32_t count = 0;
    uint8_t in = 0, done = 0, seen = 0;
    int r;
    if (len > OM_PERGAME_MAX_BYTES)
        return OM_E_FULL;
    l.pos = 0;
    while ((r = next(t, len, &l)) > 0) {
        if (l.kind == L_HEAD) {
            if (nth_name && count == nth) {           /* a name that cannot be an id is never truncated into one */
                if (strlen(l.name) < OM_GID_MAX) strcpy(nth_name, l.name);
                else nth_name[0] = 0;
            }
            count++;
            done |= in;
            in = !done && want && !strcmp(l.name, want);
            if (in) *found = 1;
        } else if (in && l.kind == L_KEY && l.key < K_COUNT && !(seen & (1u << l.key))) {
            seen |= (uint8_t)(1u << l.key);
            set_key(g, l.key, l.val);
        }
    }
    return r < 0 ? r : (int)count;
}

int om_pergame_get(const char *text, uint32_t len, const char *id, om_pergame *out)
{
    uint8_t found = 0;
    int r;
    om_pergame_default(out, id);
    r = scan(text, len, id, out, &found, 0, NULL);
    if (r < 0)
        om_pergame_default(out, id);
    return r < 0 ? r : found;
}

int om_pergame_count(const char *text, uint32_t len)
{
    return scan(text, len, NULL, NULL, NULL, 0, NULL);
}

int om_pergame_nth(const char *text, uint32_t len, uint32_t n, om_pergame *out)
{
    char name[OM_GID_MAX];
    int r = scan(text, len, NULL, NULL, NULL, n, name);
    if (r < 0) return r;
    if (n >= (uint32_t)r) return 0;
    if (om_gameid_check(name)) { om_pergame_default(out, ""); return OM_PG_NTH_BAD; }
    return om_pergame_get(text, len, name, out);
}

typedef struct { char *out; uint32_t o, cap; } sink;

static void emit(sink *s, const char *p, uint32_t n)
{
    if (s->o + n <= s->cap) memcpy(s->out + s->o, p, n);
    s->o += n;
}

static void emit_s(sink *s, const char *p) { emit(s, p, (uint32_t)strlen(p)); }

static void emit_kv(sink *s, uint8_t k, const char *v, const char *eol)
{
    emit_s(s, k_key[k]); emit_s(s, "="); emit_s(s, v); emit_s(s, eol);
}

/* the last byte written is a line end (or nothing was written) */
static void end_line(sink *s, const char *eol)
{
    if (s->o && s->o <= s->cap && s->out[s->o - 1] != '\n') emit_s(s, eol);
}

static int finish(sink *s, uint32_t *n, int r)
{
    if (r < 0) return r;
    if (s->o > s->cap || s->o > OM_PERGAME_MAX_BYTES) return OM_E_FULL;
    *n = s->o;
    return OM_OK;
}

int om_pergame_put(const char *old, uint32_t old_len, const om_pergame *g, char *out, uint32_t cap, uint32_t *n)
{
    sink s;
    line_t l;
    char d[2];
    const char *eol = "\n", *v;
    uint32_t count = 0, blank = 0, blank_end = 0;
    uint8_t in = 0, found = 0, written = 0, k;
    int r;
    if (om_gameid_check(g->id)) return OM_E_FIELD;
    if (old_len > OM_PERGAME_MAX_BYTES) return OM_E_FULL;
    s.out = out; s.o = 0; s.cap = cap;
    l.pos = 0;
    for (;;) {
        r = next(old, old_len, &l);
        if (in && (r <= 0 || l.kind == L_HEAD)) {       /* leaving the target: add its missing keys, then blanks */
            for (k = 0; k < K_COUNT; k++)
                if (!(written & (1u << k)) && (v = val_of(g, k, d)) != NULL) {
                    end_line(&s, eol);
                    emit_kv(&s, k, v, eol);
                }
            if (blank_end) emit(&s, old + blank, blank_end - blank);
            blank_end = 0;
            in = 0;
        }
        if (r <= 0) break;
        if (l.kind == L_HEAD) {
            count++;
            if (!found && !strcmp(l.name, g->id)) {
                found = in = 1;
                eol = l.term == 2 ? "\r\n" : "\n";
            }
        } else if (in) {
            if (l.kind == L_BLANK) {
                if (!blank_end) blank = l.ls;
                blank_end = l.pos;
                continue;
            }
            if (blank_end) emit(&s, old + blank, blank_end - blank);
            blank_end = 0;
            if (l.kind == L_KEY && l.key < K_COUNT) {
                k = l.key;
                if (!(written & (1u << k)) && (v = val_of(g, k, d)) != NULL) {
                    emit_kv(&s, k, v, l.term == 2 ? "\r\n" : l.term ? "\n" : "");
                }
                written |= (uint8_t)(1u << k);
                continue;
            }
        }
        emit(&s, old + l.ls, l.pos - l.ls);
    }
    if (r >= 0 && !found) {
        if (count >= OM_PERGAME_MAX_SECTIONS) return OM_E_FULL;
        end_line(&s, "\n");
        emit_s(&s, "["); emit_s(&s, g->id); emit_s(&s, "]\n");
        for (k = 0; k < K_COUNT; k++)
            if ((v = val_of(g, k, d)) != NULL) emit_kv(&s, k, v, "\n");
    }
    return finish(&s, n, r);
}

int om_pergame_del(const char *old, uint32_t old_len, const char *id, char *out, uint32_t cap, uint32_t *n)
{
    sink s;
    line_t l;
    uint8_t in = 0;
    int r;
    if (om_gameid_check(id)) return OM_E_FIELD;
    if (old_len > OM_PERGAME_MAX_BYTES) return OM_E_FULL;
    s.out = out; s.o = 0; s.cap = cap;
    l.pos = 0;
    while ((r = next(old, old_len, &l)) > 0) {
        if (l.kind == L_HEAD) in = !strcmp(l.name, id);
        if (!in) emit(&s, old + l.ls, l.pos - l.ls);
    }
    return finish(&s, n, r);
}
