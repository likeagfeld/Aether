/* menu/src/loader.c -- see loader.h. BSD-3-Clause. */
#include <string.h>
#include "loader.h"
#include "settings.h"
#include "pergame.h"   /* OM_PG_* (om_igr_want's per_game encoding, pergame.h's own) */

static int forced(uint8_t l) { return l == OM_LDR_CWX || l == OM_LDR_JHL; }

om_loader_choice om_loader_pick(uint8_t per_game, uint8_t global, uint8_t db_hint, int cwx_available)
{
    om_loader_choice c;
    if (forced(per_game)) { c.loader = per_game; c.why = OM_LWHY_USER; }
    else if (forced(global)) { c.loader = global; c.why = OM_LWHY_GLOBAL; }
    else if (db_hint == OM_LH_JHL) { c.loader = OM_LDR_JHL; c.why = OM_LWHY_DB; }
    else if (db_hint == OM_LH_CWX || db_hint == OM_LH_BAD) { c.loader = OM_LDR_CWX; c.why = OM_LWHY_DB; }
    else { c.loader = OM_LDR_CWX; c.why = OM_LWHY_DEFAULT; }
    if (c.loader == OM_LDR_CWX && !cwx_available) { c.loader = OM_LDR_JHL; c.why = OM_LWHY_NOCWX; }
    c.bad = (uint8_t)(db_hint == OM_LH_BAD);
    return c;
}

const char *om_loader_why(const om_loader_choice *c)
{
    if (c->bad && c->why != OM_LWHY_NOCWX)
        return c->loader == OM_LDR_CWX ? "CWX (database: may fail)" : "JHL (database: may fail)";
    switch (c->why) {
    case OM_LWHY_USER:   return c->loader == OM_LDR_CWX ? "CWX (your choice)" : "JHL (your choice)";
    case OM_LWHY_GLOBAL: return c->loader == OM_LDR_CWX ? "CWX (settings)" : "JHL (settings)";
    case OM_LWHY_DB:     return c->loader == OM_LDR_CWX ? "CWX (database)" : "JHL (database)";
    case OM_LWHY_NOCWX:  return "JHL (CWX not available)";
    default:             return "CWX (auto)";
    }
}

/* OM_CF_NOIGR (gamesdb.h) forces it off; otherwise an explicit per-game choice (OM_PG_ON/OM_PG_OFF,
 * pergame.h) wins; otherwise the global setting. Out-of-range per_game bytes (a hand-edited PERGAME.INI) fall
 * through both explicit checks and read as the global setting, same as OM_PG_DEFAULT. */
int om_igr_want(int global_on, uint8_t per_game, uint8_t compat_flags)
{
    if (compat_flags & OM_CF_NOIGR) return 0;
    if (per_game == OM_PG_ON) return 1;
    if (per_game == OM_PG_OFF) return 0;
    return global_on != 0;
}

int om_saves_want(int global_on, uint8_t per_game, int savetool)
{
    if (savetool) return 0;
    if (per_game == OM_PG_ON) return 1;
    if (per_game == OM_PG_OFF) return 0;
    return global_on != 0;
}

/* save_tools.json (M6 Task 2 / Task 24): "product_codes" and the non-empty "title_substring"s */
static const char *const k_tool_prod[] = { "SGC", "SATAKORE", "T-060680", "610-6276" };
static const char *const k_tool_title[] = { "THE ROCKIN'-B", "SATURN SAVE DATA MANAGER", "FDD OPERATOR" };

static int has_ci(const char *h, const char *n)
{
    uint32_t i, j;
    for (i = 0; h[i]; i++) {
        for (j = 0; n[j]; j++) {
            char c = h[i + j];
            if (!c) return 0;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (c != n[j]) break;
        }
        if (!n[j]) return 1;
    }
    return 0;
}

int om_savetool_ip(const char *product, const char *title)
{
    uint32_t i;
    for (i = 0; product && i < sizeof k_tool_prod / sizeof k_tool_prod[0]; i++)
        if (!strcmp(product, k_tool_prod[i])) return 1;
    for (i = 0; title && i < sizeof k_tool_title / sizeof k_tool_title[0]; i++)
        if (has_ci(title, k_tool_title[i])) return 1;
    return 0;
}

uint8_t om_loader_db_hint(const om_gamesdb *db, int db_ok, const char *key, uint8_t disc_region_bits)
{
    uint32_t rec;
    om_compat cp;
    if (!db_ok || !key || !key[0] || !om_gamesdb_rec(db, key, &rec))
        return OM_LH_NONE;
    if (!om_gdb_compat(db, rec, disc_region_bits, &cp) || cp.hint > OM_LH_BAD)
        return OM_LH_NONE;
    return cp.hint;
}

static uint32_t cat(char *buf, uint32_t o, uint32_t cap, const char *s, uint32_t n)
{
    for (; n && *s && o + 1 < cap; n--) buf[o++] = *s++;
    buf[o] = 0;
    return o;
}

const char *om_loader_panel(uint8_t per_game, const om_loader_choice *a, char *buf, uint32_t cap)
{   /* om_loader_why is "<loader> (<reason>)": its reason is reused, so the panel and the loading screen agree */
    const char *why = om_loader_why(a), *r = why;
    uint32_t o = 0;
    if (!cap) return buf;
    buf[0] = 0;
    if (per_game == OM_LDR_CWX && a->why == OM_LWHY_NOCWX) {   /* the launch would fall back to JHL */
        cat(buf, 0, cap, "JHL (CWX not available)", 23);
        return buf;
    }
    if (per_game == OM_LDR_CWX || per_game == OM_LDR_JHL) {
        cat(buf, 0, cap, per_game == OM_LDR_CWX ? "CWX" : "JHL", 3);
        return buf;
    }
    o = cat(buf, o, cap, "Auto (", 6);
    o = cat(buf, o, cap, a->loader == OM_LDR_CWX ? "CWX" : "JHL", 3);
    if (a->why != OM_LWHY_DEFAULT || a->bad) {
        while (*r && *r != '(') r++;
        if (*r) r++;
        o = cat(buf, o, cap, ", ", 2);
        while (*r && *r != ')' && o + 1 < cap) buf[o++] = *r++;
        buf[o] = 0;
    }
    cat(buf, o, cap, ")", 1);
    return buf;
}

const char *om_igr_panel(uint8_t per_game, int global_on, uint8_t compat_flags, char *buf, uint32_t cap)
{
    uint32_t o;
    if (!cap) return buf;
    buf[0] = 0;
    if (per_game == OM_PG_ON) { cat(buf, 0, cap, "On", 2); return buf; }
    if (per_game == OM_PG_OFF) { cat(buf, 0, cap, "Off", 3); return buf; }
    o = cat(buf, 0, cap, "Default (", 9);
    o = cat(buf, o, cap, om_igr_want(global_on, per_game, compat_flags) ? "On" : "Off", 3);
    cat(buf, o, cap, ")", 1);
    return buf;
}
