/* menu/src/overlay.c -- see overlay.h. BSD-3-Clause. */
#include <string.h>
#include "anim.h"
#include "overlay.h"
#include "remap.h"
#include "safearea.h"

void om_overlay_open(om_overlay *o, uint8_t kind, uint16_t theme, uint16_t ntheme, int16_t genre_rank, uint16_t ngenre)
{
    o->kind = kind;
    o->defs = 0;
    o->row = o->top = o->count = 0;
    o->rcur = o->dirty = 0;
    o->page = OM_OVP_MAIN;
    o->theme = theme;
    o->ntheme = ntheme ? ntheme : 1;
    o->genre = genre_rank;
    o->ngenre = ngenre;
}

static int close_ov(om_overlay *o)
{
    int f = OM_OV_CLOSED | (o->dirty ? OM_OV_CHANGED : 0);
    o->kind = OM_OV_NONE;
    return f;
}

static void follow(om_overlay *o)
{
    if (o->row < o->top) o->top = o->row;
    if (o->row >= o->top + OM_OV_VIS) o->top = (uint16_t)(o->row - OM_OV_VIS + 1);
}

/* Up/Down in a list of n rows: a fresh press past either end wraps to the other, a held repeat (OM_K_REP) stops there */
static void move_row(om_overlay *o, uint16_t k, uint16_t n)
{
    if (k & OM_K_UP) o->row = o->row ? (uint16_t)(o->row - 1) : (k & OM_K_REP) ? 0 : (uint16_t)(n - 1);
    else if (o->row + 1 < n) o->row++;
    else if (!(k & OM_K_REP)) o->row = 0;
    follow(o);
}

/* the Controls page. Back returns to the main page, Start closes; the D-pad alone reaches and runs
 * "Reset all" (safety: a scrambled map can always be undone without a face button). */
static int controls_key(om_overlay *o, uint16_t k, om_settings *s)
{
    int step = (k & OM_K_RIGHT) ? 1 : (k & OM_K_LEFT) ? -1 : 0;
    if (k & OM_K_START) return close_ov(o);
    if (k & OM_K_B) { o->page = OM_OVP_MAIN; o->row = OM_OVR_CONTROLS; o->top = 0; follow(o); return 0; }
    if (k & (OM_K_UP | OM_K_DOWN)) { move_row(o, k, OM_OVC_COUNT); return 0; }
    if (!step && !(k & OM_K_A)) return 0;
    if (o->row == OM_OVC_RESET) {
        om_remap d;
        om_remap_default(&d);
        if (!memcmp(&d, &s->pad, sizeof d)) return 0;
        s->pad = d;
        o->dirty = 1;
        return OM_OV_PAD;
    }
    om_remap_cycle(&s->pad, om_ovc_act(o->row), step < 0 ? -1 : 1);
    o->dirty = 1;
    return OM_OV_PAD;
}

static int settings_key(om_overlay *o, uint16_t k, om_settings *s)
{
    int step = (k & OM_K_RIGHT) ? 1 : (k & OM_K_LEFT) ? -1 : 0;
    if (o->page == OM_OVP_CONTROLS) return controls_key(o, k, s);
    if (k & (OM_K_B | OM_K_START)) return close_ov(o);
    if (k & (OM_K_UP | OM_K_DOWN)) { move_row(o, k, OM_OVR_COUNT); return 0; }
    if (o->row == OM_OVR_SAVEMGR)                     /* the save manager: runs only when chosen */
        return (k & (OM_K_A | OM_K_RIGHT)) ? OM_OV_SAVEMGR : 0;
    if (o->row == OM_OVR_ABOUT)                            /* About: show the page only when chosen, never dirties */
        return (k & (OM_K_A | OM_K_RIGHT)) ? OM_OV_ABOUT : 0;
    if (o->row == OM_OVR_CONTROLS) {                  /* OK or D-pad right opens the page */
        if (k & (OM_K_A | OM_K_RIGHT)) { o->page = OM_OVP_CONTROLS; o->row = 0; o->top = 0; }
        return 0;
    }
    if (o->row == OM_OVR_RESELECT || o->row == OM_OVR_SAVER) {
        if (!step && !(k & OM_K_A)) return 0;
        o->dirty = 1;
        if (o->row == OM_OVR_RESELECT) { s->reselect = (uint8_t)!s->reselect; return 0; }
        s->saver_min = om_saver_next(s->saver_min, step < 0 ? -1 : 1);
        return OM_OV_SAVER;
    }
    if (o->row == OM_OVR_IGR) {                       /* Soft reset On/Off, saved on close */
        if (!step && !(k & OM_K_A)) return 0;
        o->dirty = 1;
        s->igr = (uint8_t)!s->igr;
        return 0;
    }
    if (o->row == OM_OVR_SAVES) {                     /* per-game saves On/Off, saved on close; no prompt
                                                       * (user decision 2026-11-21): an explicit Move/Share is kept, a
                                                       * missing one becomes Share so the choice is written down */
        if (!step && !(k & OM_K_A)) return 0;
        o->dirty = 1;
        s->saves = (uint8_t)!s->saves;
        if (s->saves_first == OM_SVF_NONE) s->saves_first = OM_SVF_SHARE;
        return 0;
    }
    if (o->row == OM_OVR_SAFE || o->row == OM_OVR_LOADER) {
        if (!step && !(k & OM_K_A)) return 0;
        o->dirty = 1;
        if (o->row == OM_OVR_LOADER) {
            s->loader = (uint8_t)((s->loader + OM_LDR_COUNT + (step < 0 ? -1 : 1)) % OM_LDR_COUNT);
            return 0;
        }
        s->overscan = (uint8_t)((s->overscan + OM_OVS_COUNT + (step < 0 ? -1 : 1)) % OM_OVS_COUNT);
        return OM_OV_SAFE;
    }
    if (o->row == OM_OVR_THEME) {
        if (!step) return 0;
        o->theme = (uint16_t)((o->theme + o->ntheme + (unsigned)step) % o->ntheme);
        o->dirty = 1;
        return OM_OV_THEME;
    }
    if (!step && !(k & OM_K_A)) return 0;
    if (o->row == OM_OVR_MUSIC && !(k & OM_K_A) &&
        ((step > 0 && s->music && s->music_vol >= 10) || (step < 0 && !s->music)))
        return 0;                                      /* Music at the top already, or left while off: no change */
    o->dirty = 1;
    if (o->row == OM_OVR_CLOCK) { s->clock24 = (uint8_t)!s->clock24; return 0; }
    if (o->row == OM_OVR_ANIM) {                       /* L/R cycle, A steps forward: full -> reduced -> off */
        s->anim = (uint8_t)((s->anim + OM_ANIM_COUNT + (step < 0 ? -1 : 1)) % OM_ANIM_COUNT);
        return OM_OV_ANIM;
    }
    if (o->row == OM_OVR_MUSIC) {                      /* one row for on/off and the volume 1..10 */
        if (k & OM_K_A) s->music = (uint8_t)!s->music; /* A toggles */
        else if (step > 0) {                           /* right: on, then louder */
            if (!s->music) s->music = 1;
            else s->music_vol++;
        } else if (s->music_vol > 1)                   /* left: quieter, and off below 1 */
            s->music_vol--;
        else
            s->music = 0;
    } else if (o->row == OM_OVR_SFX)
        s->sfx = (uint8_t)!s->sfx;
    return OM_OV_SOUND;
}

static int filter_key(om_overlay *o, uint16_t k, om_settings *s)
{
    if (k & (OM_K_Z | OM_K_B)) return close_ov(o);
    if (k & (OM_K_UP | OM_K_DOWN)) { move_row(o, k, 2); return 0; }
    if (o->row == 0) {
        if (!o->ngenre) return 0;
        if (k & OM_K_RIGHT) { o->genre = (int16_t)(o->genre + 1 >= (int)o->ngenre ? -1 : o->genre + 1); o->dirty = 1; }
        if (k & OM_K_LEFT) { o->genre = (int16_t)(o->genre <= -1 ? (int)o->ngenre - 1 : o->genre - 1); o->dirty = 1; }
        return 0;
    }
    if ((k & OM_K_RIGHT) && o->rcur < 4) o->rcur++;
    if ((k & OM_K_LEFT) && o->rcur > 0) o->rcur--;
    if (k & OM_K_A) { s->filter_region = (uint8_t)(s->filter_region ^ (1u << o->rcur)); o->dirty = 1; }
    return 0;
}

/* --- M6 Task 18: the per-game panel ------------------------------------------------------------------------------ */
void om_overlay_open_game(om_overlay *o, om_pergame *g)
{   /* armed = 0: the A that opened the panel is still down (om_overlay_game_held); count: its rows -- Launch alone for
     * a game with no id (no product code: nothing to remember the other rows by) */
    om_overlay_open(o, OM_OV_GAME, 0, 1, -1, 0);
    o->armed = 0;
    o->count = g->id[0] ? OM_OGR_COUNT : 1;
}

void om_overlay_game_held(om_overlay *o, uint16_t held)
{
    if (!(held & OM_K_A)) o->armed = 1;
}

static uint8_t cycle3(uint8_t v, int step) { return (uint8_t)((v % 3u + 3u + (unsigned)step) % 3u); }
/* On <-> Off from the value shown: the game's own choice, else the global default (overlay.h OM_OVD_*) */
static uint8_t flip(uint8_t v, unsigned def_on)
{
    return (uint8_t)(v == OM_PG_ON || (v != OM_PG_OFF && def_on) ? OM_PG_OFF : OM_PG_ON);
}

int om_overlay_key_game(om_overlay *o, uint16_t k, om_pergame *g)
{
    int step = (k & OM_K_RIGHT) ? 1 : (k & OM_K_LEFT) ? -1 : (k & OM_K_A) ? 1 : 0;
    if (o->kind != OM_OV_GAME) return 0;
    if (!o->armed) {                                  /* the opening A (or a read-ahead peek's re-edge of it) */
        k &= (uint16_t)~OM_K_A;
        if (!k) return 0;
        step = (k & OM_K_RIGHT) ? 1 : (k & OM_K_LEFT) ? -1 : 0;
    }
    if (k & (OM_K_B | OM_K_START)) {
        int f = OM_OV_CLOSED | (o->dirty ? OM_OV_PGSAVE : 0);
        o->kind = OM_OV_NONE;
        return f;
    }
    if (k & (OM_K_UP | OM_K_DOWN)) {
        move_row(o, k, o->count);
        o->rcur = 0;                                  /* moving away disarms Reset to defaults */
        return 0;
    }
    if (o->row == OM_OGR_LAUNCH)                       /* Launch: A starts the game now; Left/Right do nothing */
        return (k & OM_K_A) ? OM_OV_LAUNCH : 0;
    if (o->row == OM_OGR_CHEATS && (k & OM_K_A))       /* A opens the cheat list; Left/Right cycle */
        return OM_OV_CHEATS;
    if (o->row == OM_OGR_SAVEMGR)                      /* A opens this game's saves; Left/Right do nothing */
        return (k & OM_K_A) ? OM_OV_SAVEMGR : 0;
    if (!step) return 0;
    switch (o->row) {
    case OM_OGR_LOADER: g->loader = (uint8_t)((g->loader % OM_LDR_COUNT + OM_LDR_COUNT + step) % OM_LDR_COUNT); break;
    case OM_OGR_SAVES:  g->saves = flip(g->saves, o->defs & OM_OVD_SAVES); break;
    case OM_OGR_CHEATS: g->cheats = flip(g->cheats, o->defs & OM_OVD_CHEATS); break;
    case OM_OGR_IGR:    g->igr = cycle3(g->igr, step); break;
    case OM_OGR_PAD:    return 0;                      /* static note: nothing to cycle, never dirties */
    case OM_OGR_SLOT: {                               /* 0, 1 .. 9, Ask, 0 ... */
        int v = g->slot == OM_PG_SLOT_ASK ? 10 : g->slot <= 9 ? g->slot : 0;
        v = (v + 11 + step) % 11;
        g->slot = (uint8_t)(v == 10 ? OM_PG_SLOT_ASK : v);
        break;
    }
    default:                                          /* Reset to defaults: A arms, a second A resets; Left/Right do
                                                       * nothing */
        if (!(k & OM_K_A)) return 0;
        if (!o->rcur) { o->rcur = 1; return 0; }
        g->id[0] = 0;
        o->dirty = 1;
        o->kind = OM_OV_NONE;
        return OM_OV_CLOSED | OM_OV_PGSAVE;
    }
    o->dirty = 1;
    return 0;
}

int om_overlay_key(om_overlay *o, uint16_t keys, om_settings *s)
{
    if (o->kind == OM_OV_SETTINGS) return settings_key(o, keys, s);
    if (o->kind == OM_OV_FILTER) return filter_key(o, keys, s);
    return 0;
}

