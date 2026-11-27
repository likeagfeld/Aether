/* menu/src/theme.c -- see theme.h. The aether built-in ("vibemenu" before the rename; themes.c still accepts that id) is modelled on openMenu's default look (mrneo240 / ateam,
 * credited in CREDITS.md); no openMenu asset is copied. BSD-3-Clause. */
#include <string.h>
#include "ini.h"
#include "om_fmt.h"
#include "theme.h"

static const struct {
    const char *name;
    uint32_t text, text_hi, panel, cursor, top, bottom;
    uint8_t alpha;
} k_builtin[OM_THEME_BUILTINS] = {
    /* saturn = the user-approved mockup palette (build/menu_mockup_x3.png): text (232,238,248),
     * highlight (120,200,255), panel (8,12,26) at 55 %, accent/cursor (64,156,255), gradient (10,16,40) -> (4,6,14), lifted after the first hardware run (2026-11-19: near-black on a real TV) to (28,60,140) -> (10,20,56).
     * The mockup's dim text (140,152,175) has no om_theme field yet (visual-polish task). */
    { "saturn",   0xE8EEF8, 0x78C8FF, 0x080C1A, 0x409CFF, 0x1C3C8C, 0x0A1438, 55 },
    { "aether",   0xE6E6E6, 0x5AC8FA, 0x1E1E1E, 0x3C3C3C, 0x202020, 0x383838, 80 },
};

static void cpy(char *d, const char *s, uint32_t cap)
{
    uint32_t i;
    for (i = 0; i + 1 < cap && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

uint16_t om_rgb555(uint32_t rgb)        /* 0xRRGGBB -> B<<10 | G<<5 | R, 5 bits each (HWF-30) */
{
    return (uint16_t)((((rgb >> 3) & 0x1Fu) << 10) | (((rgb >> 11) & 0x1Fu) << 5) | ((rgb >> 19) & 0x1Fu));
}

int om_hex_rgb555(const char *s, uint16_t *out)
{
    uint32_t v = 0, d;
    int i;
    if (*s == '#')
        s++;
    for (i = 0; i < 6; i++) {
        char c = s[i];
        d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10)
          : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 16u;
        if (d > 15)
            return OM_E_FIELD;
        v = (v << 4) | d;
    }
    if (s[6])
        return OM_E_FIELD;
    *out = om_rgb555(v);
    return OM_OK;
}

/* HWF-31: ratio r weights top (panel) : second (NBG1) as (31-r):(r+1) of 32, so 0 = 31:1 (not fully opaque) and 31 = 0:32.
 * A truly opaque panel (pct 100) is drawn with NBG2's colour calculation off instead (vdp2theme.c om_v2_apply_colors). */
uint8_t om_cc_ratio(uint8_t pct)
{
    if (pct > 100)
        pct = 100;
    return (uint8_t)(((100u - pct) * 31u + 50u) / 100u);
}

const char *om_theme_builtin_name(int which)
{
    return which >= 0 && which < OM_THEME_BUILTINS ? k_builtin[which].name : "";
}

void om_theme_builtin(om_theme *t, int which)
{
    if (which < 0 || which >= OM_THEME_BUILTINS)
        which = OM_THEME_SATURN;
    memset(t, 0, sizeof *t);
    cpy(t->name, k_builtin[which].name, sizeof t->name);
    cpy(t->author, "Aether", sizeof t->author);
    t->text = om_rgb555(k_builtin[which].text);
    t->text_hi = om_rgb555(k_builtin[which].text_hi);
    t->panel = om_rgb555(k_builtin[which].panel);
    t->cursor = om_rgb555(k_builtin[which].cursor);
    t->bg_top = om_rgb555(k_builtin[which].top);
    t->bg_bottom = om_rgb555(k_builtin[which].bottom);
    t->panel_alpha = k_builtin[which].alpha;
    t->builtin = 1;
}

static int pct(const char *v, uint8_t *out)
{
    uint32_t n = 0;
    if (!*v)
        return OM_E_FIELD;
    for (; *v; v++) {
        if (*v < '0' || *v > '9')
            return OM_E_FIELD;
        n = n * 10u + (uint32_t)(*v - '0');
        if (n > 100)
            return OM_E_FIELD;
    }
    *out = (uint8_t)n;
    return OM_OK;
}

static int on_kv(void *ctx, const char *sec, const char *k, const char *v)
{
    om_theme *t = (om_theme *)ctx;
    uint16_t *col = NULL;
    if (sec[0]) return 0;
    if (!strcmp(k, "name")) { cpy(t->name, v, sizeof t->name); return 0; }
    if (!strcmp(k, "author")) { cpy(t->author, v, sizeof t->author); return 0; }
    if (!strcmp(k, "bg")) { cpy(t->bg, v, sizeof t->bg); return 0; }
    if (!strcmp(k, "panel_alpha")) return pct(v, &t->panel_alpha);
    if (!strcmp(k, "text")) col = &t->text;
    else if (!strcmp(k, "text_hi")) col = &t->text_hi;
    else if (!strcmp(k, "panel")) col = &t->panel;
    else if (!strcmp(k, "cursor")) col = &t->cursor;
    else if (!strcmp(k, "bg_top")) col = &t->bg_top;
    else if (!strcmp(k, "bg_bottom")) col = &t->bg_bottom;
    return col ? om_hex_rgb555(v, col) : 0;     /* font= (plan Q5) and unknown keys are ignored */
}

int om_theme_parse(om_theme *t, const char *text, uint32_t len)
{
    om_theme_builtin(t, OM_THEME_SATURN);       /* unspecified keys inherit the Saturn built-in */
    t->builtin = 0;
    t->name[0] = t->author[0] = 0;
    return om_ini_parse(text, len, on_kv, t);
}
