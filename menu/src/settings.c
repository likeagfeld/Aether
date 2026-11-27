/* menu/src/settings.c -- see settings.h. BSD-3-Clause. */
#include <string.h>
#include "ini.h"
#include "om_fmt.h"
#include "anim.h"
#include "safearea.h"
#include "settings.h"
#include "title.h"

static const char *const k_sort_key[OM_SORT_COUNT] = { "az", "genre", "region", "year" };
static const char *const k_sort_name[OM_SORT_COUNT] = { "A-Z", "GENRE", "REGION", "YEAR" };
static const char *const k_anim_key[OM_ANIM_COUNT] = { "full", "reduced", "off" };
static const char *const k_vol[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static const char *const k_managed[] = { "theme", "sort", "filter_genre", "filter_region", "music", "music_vol", "sfx",
                                         "clock", "anim", "overscan", "loader", "igr", "saves", "saves_first",
                                         "cheats", "reselect", "saver", "fw_check", "pad_ok", "pad_back",
                                         "pad_info", "pad_fav", "pad_sort", "pad_filter", "pad_pgup", "pad_pgdn" };
static const char *const k_ldr_key[OM_LDR_COUNT] = { "auto", "cwx", "jhl" };
static const char *const k_ldr_label[OM_LDR_COUNT] = { "Auto", "CWX", "JHL" };
static const char *const k_svf_key[3] = { "", "move", "share" };
static const uint8_t k_saver[4] = { 0, 1, 3, 5 };                          /* saver= minutes */
static const char *const k_saver_key[4] = { "off", "1", "3", "5" };

static void cpy(char *d, const char *s, uint32_t cap)
{
    uint32_t i;
    for (i = 0; i + 1 < cap && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

const char *om_sort_name(uint8_t mode) { return k_sort_name[mode < OM_SORT_COUNT ? mode : 0]; }
const char *om_anim_name(uint8_t mode) { return k_anim_key[mode < OM_ANIM_COUNT ? mode : 0]; }
const char *om_loader_key(uint8_t l) { return k_ldr_key[l < OM_LDR_COUNT ? l : 0]; }
const char *om_loader_label(uint8_t l) { return k_ldr_label[l < OM_LDR_COUNT ? l : 0]; }

const char *om_saver_key(uint8_t minutes)
{
    uint32_t i;
    for (i = 0; i < 4; i++) if (k_saver[i] == minutes) return k_saver_key[i];
    return "5";
}

uint8_t om_saver_next(uint8_t minutes, int dir)
{
    uint32_t i;
    for (i = 0; i < 4 && k_saver[i] != minutes; i++) ;
    if (i == 4) i = 3;
    return k_saver[(i + 4u + (dir < 0 ? 3u : 1u)) % 4u];
}

uint8_t om_region_mask(const char *letters)
{
    static const char L[5] = { 'J', 'T', 'U', 'E', 'B' };
    uint8_t m = 0;
    int i;
    for (; *letters; letters++) {
        char c = (char)(*letters >= 'a' && *letters <= 'z' ? *letters - 32 : *letters);
        for (i = 0; i < 5; i++)
            if (c == L[i])
                m = (uint8_t)(m | (1u << i));
    }
    return m;
}

void om_settings_default(om_settings *s)
{
    memset(s, 0, sizeof *s);
    cpy(s->theme, "saturn", sizeof s->theme);
    s->sort = OM_SORT_AZ;
    s->music = s->sfx = 1;  /* clock24 0 from the memset: 12h by default (user, 2026-11-27); a clock= line keeps its value */
    s->anim = OM_ANIM_FULL;
    s->music_vol = 10;
    s->overscan = OM_OVS_STD;
    s->loader = OM_LDR_AUTO;
    s->igr = 1;
    s->cheats = 1;
    s->saves = 1;           /* user decision 2026-11-21: per-game saves on from the start, no prompt; */
    s->saves_first = OM_SVF_SHARE;  /* Share: each slot's first use gets a copy of the general set (no save "lost") */
    s->reselect = 1;        /* M6 Task 10; pad_bad = 0 from the memset */
    s->saver_min = 1;                                                          /* user decision 2026-11-23 */
    s->fw_check = 1;
    om_remap_default(&s->pad);
}

static void bad(om_settings *s, const char *k) { if (!s->bad_key[0]) cpy(s->bad_key, k, sizeof s->bad_key); }

static uint8_t onoff(const char *v, uint8_t keep)
{
    if (!strcmp(v, "on") || !strcmp(v, "1")) return 1;
    if (!strcmp(v, "off") || !strcmp(v, "0")) return 0;
    return keep;
}

/* like onoff(), but also names the key on the status line (bad_key) when the value is not recognized: for the
 * M6 keys only (igr/saves/cheats; reselect/fw_check) -- music/sfx/clock keep the M3 onoff() behavior unchanged */
static uint8_t onoff_n(om_settings *s, const char *k, const char *v, uint8_t keep)
{
    if (strcmp(v, "on") && strcmp(v, "1") && strcmp(v, "off") && strcmp(v, "0"))
        bad(s, k);
    return onoff(v, keep);
}

static int on_kv(void *ctx, const char *sec, const char *k, const char *v)
{
    om_settings *s = (om_settings *)ctx;
    uint8_t i;
    if (sec[0])
        return 0;
    if (!strcmp(k, "theme")) cpy(s->theme, v, sizeof s->theme);
    else if (!strcmp(k, "sort")) { for (i = 0; i < OM_SORT_COUNT; i++) if (!strcmp(v, k_sort_key[i])) s->sort = i; }
    else if (!strcmp(k, "filter_genre")) cpy(s->filter_genre, v, sizeof s->filter_genre);
    else if (!strcmp(k, "filter_region")) s->filter_region = om_region_mask(v);
    else if (!strcmp(k, "music")) s->music = onoff(v, s->music);
    else if (!strcmp(k, "music_vol")) {                       /* 1..10; anything else keeps the previous value */
        uint32_t n = 0, i2;
        for (i2 = 0; v[i2] >= '0' && v[i2] <= '9' && i2 < 3; i2++) n = n * 10u + (uint32_t)(v[i2] - '0');
        if (i2 && !v[i2] && n >= 1u && n <= 10u) s->music_vol = (uint8_t)n;
    }
    else if (!strcmp(k, "sfx")) s->sfx = onoff(v, s->sfx);
    else if (!strcmp(k, "clock")) s->clock24 = (uint8_t)(strcmp(v, "12") != 0);
    else if (!strcmp(k, "anim")) {
        for (i = 0; i < OM_ANIM_COUNT && strcmp(v, k_anim_key[i]); i++) ;
        if (i < OM_ANIM_COUNT) { s->anim = i; s->anim_bad[0] = 0; }
        else cpy(s->anim_bad, v[0] ? v : "(empty)", sizeof s->anim_bad);   /* kept: the previous value */
    }
    else if (!strcmp(k, "overscan")) {
        for (i = 0; i < OM_OVS_COUNT && strcmp(v, om_safe_key(i)); i++) ;
        if (i < OM_OVS_COUNT) s->overscan = i; else bad(s, k);
    }
    else if (!strcmp(k, "loader")) {
        for (i = 0; i < OM_LDR_COUNT && strcmp(v, k_ldr_key[i]); i++) ;
        if (i < OM_LDR_COUNT) s->loader = i; else bad(s, k);
    }
    else if (!strcmp(k, "saves_first")) {
        for (i = 0; i < 3 && strcmp(v, k_svf_key[i]); i++) ;
        if (i < 3) s->saves_first = i; else bad(s, k);
    }
    else if (!strcmp(k, "igr")) s->igr = onoff_n(s, k, v, s->igr);
    else if (!strcmp(k, "saves")) s->saves = onoff_n(s, k, v, s->saves);
    else if (!strcmp(k, "cheats")) s->cheats = onoff_n(s, k, v, s->cheats);
    else if (!strcmp(k, "reselect")) s->reselect = onoff_n(s, k, v, s->reselect);     /* M6 Task 10 */
    else if (!strcmp(k, "fw_check")) s->fw_check = onoff_n(s, k, v, s->fw_check);
    else if (!strcmp(k, "saver")) {
        for (i = 0; i < 4 && strcmp(v, k_saver_key[i]); i++) ;
        if (i < 4) s->saver_min = k_saver[i]; else bad(s, k);
    }
    else {
        for (i = 0; i < OM_ACT_COUNT; i++)
            if (!strcmp(k, om_act_key(i))) {
                int b = om_btn_parse(v);
                if (b >= 0) s->pad.btn[i] = (uint8_t)b; else bad(s, k);
                break;
            }
    }
    return 0;
}

int om_settings_parse(om_settings *s, const char *text, uint32_t len)
{
    int rc = om_ini_parse(text, len, on_kv, s);
    /* "saves_first=" (none) means never decided -- what every card the M6 writer touched before this task
     * holds next to its "saves=off" -- so it reads as the new default (on, Share). The Settings toggle always writes an
     * explicit saves_first, so an Off chosen there stays off. */
    if (s->saves_first == OM_SVF_NONE) {
        s->saves = 1;
        s->saves_first = OM_SVF_SHARE;
    }
    if (!om_remap_valid(&s->pad)) {            /* one button named twice -> the defaults, named */
        om_remap_default(&s->pad);
        s->pad_bad = 1;
    }
    return rc;
}

static int put(char *out, uint32_t cap, uint32_t *o, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (*o + n > cap)
        return OM_E_FULL;
    memcpy(out + *o, s, n);
    *o += n;
    return OM_OK;
}

#define PUT(x) do { if ((r = put(out, cap, &o, (x))) != 0) return r; } while (0)

int om_settings_merge(const om_settings *s, const char *old, uint32_t old_len, char *out, uint32_t cap, uint32_t *out_len)
{
    char line[OM_INI_LINE + 2], reg[6];
    uint32_t o = 0, pos = 0, i, k;
    int r, global = 1, skip;
    om_region_letters(s->filter_region, reg);
    PUT("theme="); PUT(s->theme); PUT("\n");
    PUT("sort="); PUT(k_sort_key[s->sort < OM_SORT_COUNT ? s->sort : 0]); PUT("\n");
    PUT("filter_genre="); PUT(s->filter_genre); PUT("\n");
    PUT("filter_region="); PUT(reg); PUT("\n");
    PUT("music="); PUT(s->music ? "on" : "off"); PUT("\n");
    PUT("music_vol="); PUT(s->music_vol >= 1 && s->music_vol <= 9 ? k_vol[s->music_vol] : "10"); PUT("\n");
    PUT("sfx="); PUT(s->sfx ? "on" : "off"); PUT("\n");
    PUT("clock="); PUT(s->clock24 ? "24" : "12"); PUT("\n");
    PUT("anim="); PUT(om_anim_name(s->anim)); PUT("\n");
    PUT("overscan="); PUT(om_safe_key(s->overscan)); PUT("\n");
    PUT("loader="); PUT(om_loader_key(s->loader)); PUT("\n");
    PUT("igr="); PUT(s->igr ? "on" : "off"); PUT("\n");
    PUT("saves="); PUT(s->saves ? "on" : "off"); PUT("\n");
    PUT("saves_first="); PUT(k_svf_key[s->saves_first < 3 ? s->saves_first : 0]); PUT("\n");
    PUT("cheats="); PUT(s->cheats ? "on" : "off"); PUT("\n");
    PUT("reselect="); PUT(s->reselect ? "on" : "off"); PUT("\n");          /* M6 Task 10 */
    PUT("saver="); PUT(om_saver_key(s->saver_min)); PUT("\n");
    PUT("fw_check="); PUT(s->fw_check ? "on" : "off"); PUT("\n");
    for (i = 0; i < OM_ACT_COUNT; i++) {
        char l[2];
        l[0] = om_btn_letter(s->pad.btn[i]); l[1] = 0;
        PUT(om_act_key((uint8_t)i)); PUT("="); PUT(l); PUT("\n");
    }
    while ((r = om_text_line(old, old_len, &pos, line)) > 0) {
        const char *p = line;
        /* om_text_line strips the line's own CR, so the original terminator (CRLF vs. LF) has to be
         * recovered from *pos (the byte just consumed) to keep an untouched line byte-for-byte (SPEC §5.5). */
        int crlf = pos >= 2 && pos <= old_len && old[pos - 1] == '\n' && old[pos - 2] == '\r';
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '[')
            global = 0;
        skip = 0;
        if (global && *p && *p != '#') {
            for (k = 0; p[k] && p[k] != '=' && p[k] != ' ' && p[k] != '\t'; k++) ;
            for (i = 0; i < sizeof k_managed / sizeof k_managed[0]; i++)
                if (strlen(k_managed[i]) == k && !strncmp(p, k_managed[i], k))
                    skip = 1;
        }
        if (!skip) { PUT(line); PUT(crlf ? "\r\n" : "\n"); }
    }
    if (r < 0)
        return r;
    *out_len = o;
    return OM_OK;
}
