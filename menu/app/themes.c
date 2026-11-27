/* menu/app/themes.c -- the theme list (spec §7.6): the two built-ins, then each directory under /aether/THEMES that
 * the settings overlay can apply. Applying loads THEME.INI and, if named, BG.BIN (staged in OM_LW_STAGE; the caller
 * cancels any art job first), then drives VDP2 (vdp2theme.c). Any failure keeps the theme's colours on the gradient,
 * or falls back to the Saturn built-in, and says so in msg (spec §5.6 spirit: degrade, never crash). BSD-3-Clause. */
#include <string.h>
#include "msgfmt.h"
#include "om_fmt.h"
#include "satiator.h"
#include "yield.h"
#include "sd.h"
#include "theme.h"
#include "themes.h"
#include "vdp2theme.h"

#define OM_THEMES_MAX 16
#define OM_THEME_INI_CAP (2048u + 4u)                          /* the old s_ini32's size: same file-size limit */
static char s_names[OM_THEMES_MAX][32];
static uint16_t s_n;
static uint32_t s_st32[76];                                    /* 4-aligned s_stat_t (sd.c header) */

uint16_t om_themes_count(void) { return s_n ? s_n : OM_THEME_BUILTINS; }
const char *om_themes_name(uint16_t i) { return i < OM_THEME_BUILTINS ? om_theme_builtin_name(i) : i < s_n ? s_names[i] : ""; }

static int same_name(const char *a, const char *b)
{   /* FAT names are case-insensitive, so a THEMES/SATURN folder is the built-in "saturn" */
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

int om_themes_find(const char *name)
{
    uint16_t i;
    for (i = 0; i < om_themes_count(); i++)
        if (same_name(om_themes_name(i), name))
            return i;
    if (same_name(name, "vibemenu"))                           /* the built-in's id before the Aether rename (SETTINGS.INI) */
        return OM_THEME_OPENMENU;
    return -1;
}

uint16_t om_themes_scan(void)
{
    int len;
    s_n = OM_THEME_BUILTINS;
    om_yield();                  /* the start-up screen's frame before each Satiator command (yield.h) */
    if (s_opendir(om_sd_path("/aether/THEMES")) < 0)
        return s_n;
    /* The whole listing is read even past OM_THEMES_MAX: the readdir iterator is global and each listing is drained
     * before any other call (satfs.c, HWF-22). */
    while ((len = s_stat(NULL, (s_stat_t *)s_st32, (int)sizeof s_st32 - 4)) > 0) {
        const s_stat_t *st = (const s_stat_t *)s_st32;
        om_yield();
        if (s_n >= OM_THEMES_MAX || !(st->attrib & AM_DIR) || len > 31 || st->name[0] == '.')
            continue;
        memcpy(s_names[s_n], st->name, (size_t)len);
        s_names[s_n][len] = 0;
        if (om_themes_find(s_names[s_n]) < 0)                  /* a user "saturn" never shadows the built-in */
            s_n++;
    }
    return s_n;
}

static void path3(char *out, const char *dir, const char *file)
{
    strcpy(out, "/aether/THEMES/");
    strcat(out, dir);
    strcat(out, "/");
    strcat(out, file);
}

static void builtin(int which)
{
    om_theme t;
    om_theme_builtin(&t, which);
    om_v2_bg_gradient(t.bg_top, t.bg_bottom);
    om_v2_apply_colors(&t);
}

int om_themes_apply(uint16_t i, uint8_t *stage, uint32_t stage_cap, char msg[41])
{
    om_theme t;
    char path[96];
    uint32_t len;
    int rc = 0;
    msg[0] = 0;
    if (i < OM_THEME_BUILTINS) {
        builtin(i);
        return OM_OK;
    }
    if (i >= s_n) {                                            /* Task 11 hand-off: never an empty message */
        builtin(OM_THEME_SATURN);
        om_msg_rc(msg, 41, "THEME: ", "no such theme", OM_E_FIELD);
        return OM_E_FIELD;
    }
    /* Task 24 reclaim (task-18b-review focus 6): THEME.INI is read into the stage's first 2 KB and parsed into t's value
     * arrays before BG.BIN reuses the stage (theme.h holds no pointers into the buffer). Stage +0..+0x804 is below
     * OM_LW_LBG (+0x8800, docs/ledger.md), so the INI never touches LOADBG's LW bytes; the cap is the old buffer's. */
    path3(path, s_names[i], "THEME.INI");
    rc = stage_cap < OM_THEME_INI_CAP ? OM_E_FULL : om_sd_read_all(path, stage, OM_THEME_INI_CAP, &len);
    if (!rc)
        rc = om_theme_parse(&t, (const char *)stage, len);
    if (rc) {
        om_msg_rc(msg, 41, "THEME.INI: ", om_strerror(rc), rc);
        builtin(OM_THEME_SATURN);
        return rc;
    }
    if (t.bg[0]) {
        path3(path, s_names[i], t.bg);
        rc = om_sd_read_all(path, stage, stage_cap, &len);
        if (!rc)
            rc = om_v2_bg_load(stage, len);
        if (rc)
            om_msg_rc(msg, 41, "THEME: BG.BIN ", om_strerror(rc), rc);
    }
    if (!t.bg[0] || rc)
        om_v2_bg_gradient(t.bg_top, t.bg_bottom);
    om_v2_apply_colors(&t);
    return rc;
}
