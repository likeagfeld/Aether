/* menu/launcher/lsaves.c -- see lsaves.h. The screens of "Manage saves" on the loading screen's look (lgfx.h):
 *  - LIST: one device at a time (Left/Right: Console, Cartridge, Card), its saves in five rows, the highlighted save's
 *    details under the title (comment, language, date, size and blocks). Opened for a game, only that game's saves;
 *    else the card shows its save folders first (A opens one, B goes back up).
 *  - ACT: what A on a save can do there -- copy to the console or the cartridge, back up to the card, delete.
 *    Delete asks twice; a copy over a save of the same name asks again first (it is backed up before).
 * Up/Down wrap from the last row to the first and back on a fresh press; a held D-pad stops at the ends. B on the
 * list ends: a console reset to Aether (SMPC SYSRES). BSD-3-Clause. */
#include <stdlib.h>
#include "carddir.h"
#include <string.h>
#include "bram.h"
#include "lgap.h"
#include "lgfx.h"
#include "lsaves.h"
#include "lscreen.h"
#include "marquee.h"
#include "satiator.h"
#include "smpcpad.h"
#include "smpcwait.h"
#include "svmgr.h"
#include "vdp2txt.h"
#include "chedit.h"

#define X0     16
#define X1     181
#define ROWS   5
#define ROW_Y  99
#define ROW_H  12
#define MAXIT  300u
#define NFOLD  200u
#define ITEMS  ((om_sv_item *)OM_LG_FREE)
#define FOLD   ((char (*)[OM_PGS_DIR])(OM_LG_FREE + MAXIT * sizeof(om_sv_item)))
#define BIG    (OM_LG_FREE + MAXIT * sizeof(om_sv_item) + NFOLD * OM_PGS_DIR + 64u)

static om_sv s_v;
static uint32_t s_row, s_top, s_nfold, s_act, s_nact, s_sel;   /* s_sel: the save the actions are for */
static int s_dev, s_page, s_infold, s_armed;
static char s_msg[64], s_game[64];
static uint16_t s_msgc;
static uint8_t s_acts[5];
enum { PG_LIST, PG_ACT };
enum { A_TO_CON, A_TO_CART, A_BACKUP, A_DELETE, A_CANCEL };
static const char *const k_dev[3] = { "Console", "Cartridge", "Card" };
static const char *const k_act[5] = { "Copy to the console", "Copy to the cartridge", "Back up to the card",
                                      "Delete", "Cancel" };

static void gap(void) { om_lgap(); om_lg_pump(0); }

/* --- pad: rising edges; the D-pad repeats after ~1/3 s, and a repeat never wraps a list ------------------------- */
static uint16_t s_prev;
static uint32_t s_held;
static int s_rep;
static uint16_t pad_press(void)
{
    uint16_t p = om_smpc_pad(), dir = OM_CK_U | OM_CK_D | OM_CK_L | OM_CK_R, k;
    s_rep = 0;
    if (!om_smpc_pad_ok()) return 0;
    k = (uint16_t)(p & ~s_prev);
    if ((p & dir) && (p & dir) == (s_prev & dir)) {
        if (++s_held > 20u && !(s_held & 3u)) { k |= (uint16_t)(p & dir); s_rep = 1; }
    } else
        s_held = 0;
    s_prev = p;
    return k;
}
static uint32_t move(uint32_t cur, uint32_t n, int down)
{   /* one step; a fresh press wraps at the ends, a held repeat stops there */
    if (!n) return 0;
    if (down) return cur + 1u < n ? cur + 1u : s_rep ? cur : 0;
    return cur ? cur - 1u : s_rep ? 0 : n - 1u;
}

/* --- text helpers -------------------------------------------------------------------------------------------- */
static char *put_u(char *o, uint32_t v, int w)
{
    char d[12];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k < w) d[k++] = '0';
    while (k) *o++ = d[--k];
    *o = 0;
    return o;
}
static void date_of(uint32_t m, char *o)
{   /* the backup library's date: minutes since 1980-01-01 00:00 */
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t d = m / 1440u, y = 1980, mo = 0, mi = m % 1440u;
    for (;;) {
        uint32_t yd = (y % 4u == 0u) ? 366u : 365u;
        if (d < yd) break;
        d -= yd; y++;
    }
    for (;;) {
        uint32_t k = md[mo] + (mo == 1 && y % 4u == 0u);
        if (d < k || mo == 11) break;
        d -= k; mo++;
    }
    o = put_u(o, y, 4); *o++ = '-';
    o = put_u(o, mo + 1u, 2); *o++ = '-';
    o = put_u(o, d + 1u, 2); *o++ = ' ';
    o = put_u(o, mi / 60u, 2); *o++ = ':';
    put_u(o, mi % 60u, 2);
}
static const char *lang(uint8_t l)
{
    static const char *const k[6] = { "Japanese", "English", "French", "German", "Spanish", "Italian" };
    return l < 6 ? k[l] : "?";
}

static uint32_t count(void)
{
    if (s_page == PG_ACT) return s_nact;
    return s_dev == OM_SV_SD && !s_infold ? s_nfold : s_v.n;
}

static void row(uint32_t i, int y, int sel)
{
    char l[48], *o = l;
    uint16_t u, bg = 0, fg = sel ? om_lg_c.txt : om_lg_mix(om_lg_c.dim, om_lg_c.txt, 128);
    const char *name;
    if (s_page == PG_ACT) name = k_act[s_acts[i]];
    else if (s_dev == OM_SV_SD && !s_infold) name = FOLD[i];
    else {
        const om_sv_item *x = &s_v.it[i];
        memcpy(l, x->d.name, 11);
        l[11] = 0;
        o = l + strlen(l);
        if (x->dev == OM_SV_SD) {
            if (s_v.set[x->set].slot) { strcpy(o, "  slot "); o += 7; *o++ = (char)('0' + s_v.set[x->set].slot); *o = 0; }
            if (x->alt) strcpy(o, "  (kept copy)");
        }
        if (!x->mine) fg = om_lg_c.faint;
        name = l;
    }
    if (sel) {
        u = om_lg_under(X0, y, X1, y + ROW_H - 1);
        bg = om_lg_mix(u, om_lg_c.acc, 110);
        om_lg_bartext(X0 - 4, y, X1, y + ROW_H - 1, om_lg_mix(u, om_lg_c.acc, 150), om_lg_mix(u, om_lg_c.acc, 30),
                      om_lg_c.acc2, OM_LGF_SMALL, X0, y + 10, X1 - X0 - 36, name, fg, 0);
    } else {
        om_lg_clear(X0 - 4, y, X1, y + ROW_H - 1);
        om_lg_text(OM_LGF_SMALL, X0, y + 10, X1 - X0 - 36, name, fg, 0, OM_LG_FADE);
    }
    if (s_page == PG_LIST && !(s_dev == OM_SV_SD && !s_infold)) {   /* the size on the right, in blocks */
        put_u(l, s_v.it[i].d.blocks ? s_v.it[i].d.blocks : (s_v.it[i].d.datasize + 63u) / 64u, 0);
        strcat(l, " bl");
        om_lg_text(OM_LGF_SMALL, X1, y + 10, 0, l, om_lg_c.dim, bg, OM_LG_RIGHT);
    }
}

static int details(void)
{   /* the highlighted save's details on the message line; 0 when there is nothing to show */
    char l[80], *o = l;
    const om_sv_item *x;
    if (s_page != PG_LIST || !count() || (s_dev == OM_SV_SD && !s_infold)) return 0;
    x = &s_v.it[s_row];
    memcpy(o, x->d.comment, 10);
    o[10] = 0;
    {   /* the comment is Shift-JIS in many files: shown only when it is plain ASCII */
        uint32_t k;
        for (k = 0; o[k]; k++) if ((uint8_t)o[k] < 0x20 || (uint8_t)o[k] > 0x7E) { o[0] = 0; break; }
    }
    o += strlen(o);
    if (o != l) { strcpy(o, "  \xC2\xB7  "); o += strlen(o); }
    strcpy(o, lang(x->d.language));
    o += strlen(o);
    strcpy(o, "  \xC2\xB7  ");
    o += strlen(o);
    date_of(x->d.date, o);
    o += strlen(o);
    strcpy(o, "  \xC2\xB7  ");
    o = put_u(o + strlen(o), x->d.datasize, 0);
    strcpy(o, " B");
    om_lg_clear(X0, 160, 196, 174);
    om_lg_text(OM_LGF_SMALL, X0, 171, 196 - X0, l, om_lg_c.dim, 0, OM_LG_FADE);
    return 1;
}

static void tabs(void)
{   /* the device line under the title: the one shown lit, then where on the card */
    int i, x = X0;
    om_lg_clear(0, 84, 186, 97);
    for (i = 0; i < 3; i++)
        x += om_lg_text(OM_LGF_SMALL, x, 93, 0, k_dev[i], i == s_dev ? om_lg_c.acc2 : om_lg_c.dim, 0, 0) + 9;
    if (s_dev == OM_SV_SD && s_infold && !s_v.gid[0])
        om_lg_text(OM_LGF_SMALL, x, 93, X1 - x, s_v.folder, om_lg_c.dim, 0, OM_LG_FADE);
}

static void hints(void)
{   /* 2026-11-25 (hardware photo): the menu's badges and plain words, the device choice on a line of its own */
    const char *a, *b;
    om_lg_clear(X0, 173, 308, 199);
    if (s_page == PG_ACT) { a = "Up/Down: choose"; b = "A: do it   B: back"; }
    else {
        a = "Left/Right: console \xC2\xB7 cartridge \xC2\xB7 card";
        b = s_dev == OM_SV_SD && !s_infold ? "A: open   B: back to Aether"
          : s_infold && !s_v.gid[0] ? "A: options   B: back to the folders" : "A: options   B: back to Aether";
    }
    om_ls_hints(X0, 184, 300, a, om_lg_c.dim);
    om_ls_hints(X0, 197, 300, b, om_lg_c.dim);
}

/* 2026-11-25 (hardware photo: "Mortal Kombat II (U" cut at the cover): a title wider than its box scrolls, as the cheat
 * editor's and the menu's do (marquee.h); one that fits is drawn once */
static const char *s_title;
static int32_t s_title_off = -1;
static void title(uint32_t t)
{
    int32_t off = (int32_t)om_marquee_off(t, om_lg_width(OM_LGF_HERO, s_title), X1 - X0 + 1, 5994u);
    if (off == s_title_off) return;
    s_title_off = off;
    om_lg_marquee(OM_LGF_HERO, X0, 77, X1 - X0 + 1, s_title, om_lg_c.txt, 0, off);
}

static void msgline(void)
{
    if (!s_msg[0] && details()) return;
    om_lg_clear(X0, 160, 196, 174);
    om_lg_text(OM_LGF_SMALL, X0, 171, 196 - X0, s_msg, s_msgc, 0, OM_LG_FADE);
}

static void draw(void)
{
    uint32_t i, n = count();
    if (s_row >= n) s_row = n ? n - 1u : 0;
    if (s_row < s_top) s_top = s_row;
    if (s_row >= s_top + ROWS) s_top = s_row - ROWS + 1u;
    tabs();
    om_lg_clear(X0 - 4, ROW_Y, X1, ROW_Y + ROWS * ROW_H - 1);
    if (!n)
        om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 10, X1 - X0, s_page == PG_LIST && s_dev == OM_SV_SD && !s_infold
                   ? "No save folders on the card" : "No saves here", om_lg_c.dim, 0, 0);
    for (i = s_top; i < n && i < s_top + ROWS; i++) row(i, ROW_Y + (int)(i - s_top) * ROW_H, i == s_row);
    msgline();
    hints();
}

static void say(const char *m, uint16_t c)
{
    strncpy(s_msg, m, sizeof s_msg - 1);
    s_msg[sizeof s_msg - 1] = 0;
    s_msgc = c;
}

static void relist(void)
{
    int rc;
    if (s_dev == OM_SV_SD && !s_infold) {
        rc = om_sv_folders(&s_v, FOLD, NFOLD, &s_nfold);
        s_v.n = 0;
    } else
        rc = om_sv_list(&s_v, s_dev);
    if (rc == OM_SVR_NODEV) say(s_dev == OM_SV_CART ? "No cartridge (or not formatted)" : "Console memory not usable",
                                om_lg_c.err);
    else if (rc) say(om_sv_why(rc), om_lg_c.err);
}

static void actions(void)
{   /* what this save can do where it is */
    const om_sv_item *x = &s_v.it[s_sel];
    s_nact = 0;
    if (x->dev != OM_SV_CON) s_acts[s_nact++] = A_TO_CON;
    if (x->dev != OM_SV_CART) s_acts[s_nact++] = A_TO_CART;
    if (x->dev != OM_SV_SD) s_acts[s_nact++] = A_BACKUP;
    s_acts[s_nact++] = A_DELETE;
    s_acts[s_nact++] = A_CANCEL;
}

static void work(const char *what)
{
    om_ls_status(what, om_lg_c.acc2);
    om_lg_quiet();                                            /* one file at a time while writing */
}

static void act(uint32_t pick, int confirm)
{
    om_sv_item x = s_v.it[s_sel];
    int r;
    uint32_t seq = 0;
    char l[64];
    switch (s_acts[pick]) {
    case A_TO_CON: case A_TO_CART:
        work("Copying - keep the power on");
        r = om_sv_copy(&s_v, &x, s_acts[pick] == A_TO_CON ? OM_SV_CON : OM_SV_CART, confirm);
        if (r == OM_SV_ASK) { say("A save of that name is there: A again replaces it (kept on the card first)",
                                  om_lg_c.gold); s_armed = (int)pick + 1; om_ls_status("", 0); return; }
        say(r ? om_sv_why(r) : "Copied", r ? om_lg_c.err : om_lg_c.ok);
        break;
    case A_BACKUP:
        work("Backing up - keep the power on");
        r = om_sv_backup(&s_v, &x, &seq);
        if (!r) { strcpy(l, "Backed up: SAVES/_MANAGER/"); put_u(l + strlen(l), seq, 5); say(l, om_lg_c.ok); }
        else say(om_sv_why(r), om_lg_c.err);
        break;
    case A_DELETE:
        if (!confirm) { say("Press A again to delete it", om_lg_c.gold); s_armed = (int)pick + 1; return; }
        work("Deleting - keep the power on");
        r = om_sv_delete(&s_v, &x);
        if (!r && x.dev == OM_SV_SD) {
            om_sv_item c;
            say(om_sv_in_console(&s_v, &x, &c) ? "Also in console memory: delete it there too" : "Deleted",
                om_sv_in_console(&s_v, &x, &c) ? om_lg_c.gold : om_lg_c.ok);
        } else
            say(r ? om_sv_why(r) : x.dev == OM_SV_CON ? "Deleted (a copy is on the card)" : "Deleted",
                r ? om_lg_c.err : om_lg_c.ok);
        break;
    default:
        break;
    }
    om_ls_status("", 0);
    s_page = PG_LIST;
    s_row = s_sel;
    relist();
}

#ifdef OM_L1_HARNESS
/* the L1 harness's emulator capture: no Satiator, so saves made up in memory and a key script -- the console's list,
 * a save's details, its actions, the cartridge, the card's sets */
static void demo_item(int dev, const char *n, const char *c, uint32_t size, uint32_t date, uint8_t set)
{
    om_sv_item *x = &s_v.it[s_v.n++];
    memset(x, 0, sizeof *x);
    strncpy(x->d.name, n, 11);
    strncpy(x->d.comment, c, 10);
    x->d.language = 1;
    x->d.date = date;
    x->d.datasize = size;
    x->d.blocks = (uint16_t)((size + 4u + 57u) / 58u);
    x->dev = (uint8_t)dev;
    x->set = set;
    x->mine = 1;
}
static void demo_list(void)
{
    s_v.n = 0;
    if (s_dev == OM_SV_CON) {
        demo_item(OM_SV_CON, "PANZER_DRAG", "Episode 4", 2540, 24508800u, 0);
        demo_item(OM_SV_CON, "PANZER_SAV1", "Episode 5", 2540, 24512400u, 0);
        demo_item(OM_SV_CON, "PANZER_CONF", "Config", 120, 24508000u, 0);
    } else if (s_dev == OM_SV_CART) {
        demo_item(OM_SV_CART, "PANZER_DRAG", "Episode 2", 2540, 24400000u, 0);
    } else {
        demo_item(OM_SV_SD, "PANZER_DRAG", "Episode 4", 2540, 24508800u, 0);
        demo_item(OM_SV_SD, "PANZER_CONF", "Config", 120, 24508000u, 0);
        demo_item(OM_SV_SD, "PANZER_DRAG", "Episode 1", 2540, 24300000u, 1);
    }
}
static const uint16_t k_demo[][2] = {
    { 60, OM_CK_D }, { 150, OM_CK_A }, { 240, OM_CK_D }, { 270, OM_CK_D }, { 360, OM_CK_B }, { 390, OM_CK_R },
    { 480, OM_CK_R }, { 660, OM_CK_B }, { 0, 0 } };
static int demo(const om_handoff *h, om_lerr *e)
{
    uint32_t t, i;
    s_v.it = ITEMS;
    s_v.cap = MAXIT;
    s_v.nset = 2;
    s_v.set[1].slot = 1;
    strcpy(s_v.gid, h->gid);
    s_infold = 1;
    demo_list();
    draw();
    for (t = 1; ; t++) {
        uint16_t k = 0;
        om_txt_wait_vblank();
        om_lg_pump(1);
        title(t);
        for (i = 0; k_demo[i][1]; i++) if (k_demo[i][0] == t) k = k_demo[i][1];
        if (!k) continue;
        if (s_page == PG_ACT) {
            if (k & (OM_CK_U | OM_CK_D)) s_row = s_act = move(s_act, s_nact, (k & OM_CK_D) != 0);
            else { s_page = PG_LIST; s_row = s_sel; }
        } else if (k & OM_CK_B) {                                 /* B on the list: the way back, as the real one */
            om_ls_status("Returning to Aether...", om_lg_c.dim);
            return om_go_home(h, e);
        } else if (k & (OM_CK_U | OM_CK_D)) s_row = move(s_row, s_v.n, (k & OM_CK_D) != 0);
        else if (k & OM_CK_R) { s_dev = (s_dev + 1) % 3; s_row = s_top = 0; demo_list(); }
        else if (k & OM_CK_A) { s_sel = s_row; actions(); s_page = PG_ACT; s_act = s_row = s_top = 0; }
        draw();
    }
    return 0;
}
#endif

int om_run_saves(const om_handoff *h, om_lerr *e)
{
    static char date[16];
    uint8_t *raw;
    om_pgs_env *env = (om_pgs_env *)malloc(sizeof *env);
    uint32_t t;
    int rc;
    {
        const char *g = strrchr(h->image, '/');
        strncpy(s_game, g ? g + 1 : h->image, sizeof s_game - 1);
    }
#ifndef OM_L1_HARNESS
    if ((rc = s_mode(s_api)) != 0) {
        e->step = 1; e->rc = rc; e->what = "Satiator API mode failed"; e->detail = "";
        return rc;
    }
#endif
    om_lg_start(h, 1);                                              /* the menu's last eye frame, continued */
    om_lg_clear(0, 0, 319, 223);
    if (h->gid[0]) om_lg_cover(0, 1);
    om_lg_brand();
    om_lg_tracked(X0, 55, "MANAGE SAVES", om_lg_c.acc2);
    om_lg_fill(X0, 58, 46, 59, om_lg_c.acc);
    s_title = h->gid[0] ? s_game : "All saves";
    s_title_off = -1;
    title(0);
#ifdef OM_L1_HARNESS
    if (h->flags & OM_HO_TEST_NO_SATIATOR) return demo(h, e);
    if ((rc = s_mode(s_api)) != 0) {
        e->step = 1; e->rc = rc; e->what = "Satiator API mode failed"; e->detail = "";
        return rc;
    }
#endif
    if (!env || !(raw = (uint8_t *)malloc(OM_PGS_ARENA + 4u))) {
        e->step = 1; e->rc = OM_E_FULL; e->what = "SAVES: NO MEMORY"; e->detail = "";
        return OM_E_FULL;
    }
    memset(env, 0, sizeof *env);
    om_pgs_arena_split(env, (uint8_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)3u));
    env->gap = gap;
    env->nonce = om_lnonce;
    om_smpc_rtc(date);
    {
        static uint32_t vm[4];
        memcpy(vm, OM_CARD_DIR, sizeof OM_CARD_DIR);
        static uint32_t st[(300 + 4) / 4], ln[4];
        memcpy(ln, "LAUNCHER.BIN", 13);
        if (s_chdir((const char *)vm) < 0 || s_stat((const char *)ln, (s_stat_t *)st, (int)sizeof st - 4) < 0) {
            e->step = 1; e->rc = OM_E_FIELD;                      /* svmgr.h names everything from /aether */
            e->what = "SAVES: CANNOT OPEN " OM_CARD_DIR; e->detail = "";
            om_lg_stop();
            return OM_E_FIELD;
        }
    }
    om_ls_status("Reading the saves...", om_lg_c.dim);
    rc = om_sv_open(&s_v, env, h->gid, (uint8_t *)BIG, OM_LG_FREE_END - BIG, ITEMS, MAXIT, date);
    om_ls_status("", 0);
    s_dev = OM_SV_CON;
    s_page = PG_LIST;
    s_infold = h->gid[0] != 0;
    if (rc) say(om_sv_why(rc), om_lg_c.err);
    else if (s_v.journal) say("A save swap is unfinished: launch a game first", om_lg_c.gold);
    relist();
    draw();
    for (t = 0; t < 30; t++) { (void)om_smpc_pad(); om_lg_pump(1); }   /* the menu's A, still held: let it go */
    s_prev = 0xFFFFu;
    for (t = 0; ; t++) {
        uint16_t k = pad_press();
        uint32_t n = count();
        om_lg_pump(1);
        title(t);
        if (!k) continue;
        if (k != OM_CK_A) s_armed = 0;                            /* only a lone A confirms */
        if (!s_armed) s_msg[0] = 0;
        if (s_page == PG_ACT) {
            if (k & (OM_CK_U | OM_CK_D)) { s_act = move(s_act, s_nact, (k & OM_CK_D) != 0); s_row = s_act; }
            else if (k & OM_CK_B) { s_page = PG_LIST; s_row = s_sel; s_top = 0; }
            else if (k & OM_CK_A) {
                if (s_acts[s_act] == A_CANCEL) { s_page = PG_LIST; s_row = s_sel; s_top = 0; }
                else {
                    int conf = s_armed == (int)s_act + 1;         /* the second A on the action that asked */
                    s_armed = 0;
                    act(s_act, conf);
                }
            }
        } else if (k & (OM_CK_U | OM_CK_D)) s_row = move(s_row, n, (k & OM_CK_D) != 0);
        else if (k & (OM_CK_L | OM_CK_R)) {
            s_dev = (s_dev + ((k & OM_CK_R) ? 1 : 2)) % 3;
            s_row = s_top = 0;
            s_infold = s_dev != OM_SV_SD || h->gid[0];
            relist();
        } else if (k & OM_CK_B) {
            if (s_dev == OM_SV_SD && s_infold && !h->gid[0]) { s_infold = 0; s_row = s_top = 0; relist(); }
            else break;
        } else if ((k & OM_CK_A) && n) {
            if (s_dev == OM_SV_SD && !s_infold) {
                om_sv_set_folder(&s_v, FOLD[s_row]);
                s_infold = 1;
                s_row = s_top = 0;
                relist();
            } else {
                s_sel = s_row;
                actions();
                s_page = PG_ACT;
                s_act = s_row = s_top = 0;
            }
        }
        draw();
    }
    om_ls_status("Returning to Aether...", om_lg_c.dim);
    return om_go_home(h, e);                                        /* the menu again, reopened here (lseq.h) */
}
