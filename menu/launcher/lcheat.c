/* menu/launcher/lcheat.c -- see lcheat.h. The editor's screens are the cheats mockup on the loading screen's look
 * (lgfx.h): the eye moving behind, the cover on the right, the list of five rows with the highlighted name scrolling
 * when long. A press is a rising edge of one pad read (one field each, smpcpad.h); the D-pad repeats after ~1/3 s
 * held. B on the list saves (when anything changed) through om_sdw_replace -- CHEATS.PAK.NEW written and read back,
 * then renamed over CHEATS.PAK (sdwrite.h), with the launcher's V-blank gap between chunks -- then resets the console
 * to Aether (SMPC SYSRES, as Import does). BSD-3-Clause. */
#include <stdlib.h>
#include <string.h>
#include "lcheat.h"
#include "chedit.h"
#include "lgap.h"
#include "marquee.h"
#include "msgfmt.h"
#include "satiator.h"
#include "sdwrite.h"
#include "settings.h"
#include "smpcpad.h"
#include "vdp2txt.h"
#include "lgfx.h"
#include "lscreen.h"

#define COLS     40
#define BUF_CAP  (OM_CP_MAX_FILE + 4u)

/* heap, 4-aligned (the launcher's .bss is the save swap's floor, launcher.ld): the file as read, the file to write
 * (the old size + one record + one row, chedit.h OM_CPK_GROW), the record, its encode scratch, sdwrite's 2 KB */
static uint8_t *s_in, *s_out, *s_rec, *s_scratch;
static uint32_t s_in_len;
static om_ch_file *s_f;

static void *s_raw[5];
static uint32_t s_nraw;
static uint8_t *alloc4(uint32_t n)
{
    uint8_t *p = (uint8_t *)malloc(n + 4u);
    if (p && s_nraw < 5u) s_raw[s_nraw++] = p;
    return p ? (uint8_t *)(((uintptr_t)p + 3u) & ~(uintptr_t)3u) : 0;
}

/* the whole file into s_in: 0 read, 1 none, < 0 an SD error */
static int read_pak(void)
{
    int rc = om_sdw_read_file(OM_LCH_PATH, s_in, BUF_CAP, &s_in_len);
    if (rc == OM_SDW_NOFILE) { s_in_len = 0; return 1; }
    return rc < 0 ? rc : 0;
}

static void put_u(char **o, uint32_t v)
{
    char d[12];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *(*o)++ = d[--k];
}

void om_lch_load(const om_handoff *h, om_chk_plan *p, int x, int y)
{
    char l[24], *o;
    int rc = 0;
    memset(p, 0, sizeof *p);
    if (!h->cheats)
        return;
    s_nraw = 0;
    s_raw[0] = s_raw[1] = 0;
    if (h->loader != OM_LDR_CWX)
        p->status = OM_CHS_NEED_CWX;
    else if (!h->ckey[0])
        p->status = OM_CHS_NOREC;
    else if (!(s_in = alloc4(BUF_CAP)) || !(s_f = (om_ch_file *)alloc4(sizeof *s_f)))
        p->status = OM_CHS_ERR;
    else {
        if ((rc = read_pak()) == 1)
            p->status = OM_CHS_NOREC;
        else if (rc < 0 || (rc = om_cpk_find(s_in, s_in_len, h->ckey, s_f)) < 0)
            p->status = OM_CHS_ERR;
        else if (rc == 0)
            p->status = OM_CHS_NOREC;
        else
            om_chk_plan_make(s_f, p);
    }
    free(s_raw[0]);                                    /* the heap back for the boot (nothing else allocates after) */
    free(s_raw[1]);
    s_raw[0] = s_raw[1] = 0;
    s_in = 0;
    s_f = 0;
    o = l;
    memcpy(o, "CHEATS: ", 8); o += 8;
    switch (p->status) {
    case OM_CHS_ARMED:    put_u(&o, (uint32_t)p->ncache + p->nboot + (p->en_addr != 0)); strcpy(o, " CODES"); break;
    case OM_CHS_NEED_CWX: strcpy(o, "NEED CWX"); break;
    case OM_CHS_JHL_ONLY: strcpy(o, "KAI: JHL ONLY"); break;
    case OM_CHS_NOTSUP:   strcpy(o, "NOT HERE"); break;
    case OM_CHS_ERR:      strcpy(o, "FILE ERROR"); break;
    case OM_CHS_NONE:     strcpy(o, "NONE ON"); break;
    default:              strcpy(o, "NONE"); break;
    }
    if (om_lg_on)
        om_ls_meta(1, l, p->status == OM_CHS_ARMED ? om_lg_c.ok : p->status == OM_CHS_ERR ? om_lg_c.err : om_lg_c.dim);
    else
        om_txt_puts(x, y, l, p->status == OM_CHS_ARMED ? 1 : p->status == OM_CHS_ERR ? 2 : 0);
}

/* --- the editor ---------------------------------------------------------------------------------------------- */
/* The cheats mockup on the eye background (lgfx.h): label, underline, the game's name, a meta line, five rows, the
 * last key's message, two hint lines under the (compact) cover, the wordmark. */
#define X0       16
#define X1       181
#define ROWS     5
#define ROW_Y    99                    /* row tops, 12 px apart */
#define ROW_H    12
#define MSG_BASE 171
#define HINT1    184
#define HINT2    196
#define HX1      308
#define INK      ((uint16_t)0x8000u | 0x2442u)   /* a lit digit's dark ink */

static uint32_t s_vtop;                 /* the view's first row (the model scrolls 15, the screen shows 5) */

static uint32_t vtop(uint32_t row, uint32_t n)
{
    if (row < s_vtop) s_vtop = row;
    if (row >= s_vtop + ROWS) s_vtop = row - ROWS + 1u;
    if (s_vtop + ROWS > n) s_vtop = n > ROWS ? n - ROWS : 0;
    return s_vtop;
}

static const char *msg_text(const om_ced *e)
{
    switch (e->msg) {
    case OM_CEM_FULL:     return "NO ROOM: TURN ANOTHER CHEAT OFF FIRST";
    case OM_CEM_BAD:      return om_chk_why(e->why);
    case OM_CEM_READONLY: return "CHEATS.PAK DOES NOT READ: FIX IT ON A PC";
    case OM_CEM_KAI:      return "BUILT-IN CHEAT: ITS CODES ARE READ ONLY";
    case OM_CEM_FILEFULL: return "NO ROOM FOR MORE CHEATS OR CODES";
    case OM_CEM_ASK:      return "PRESS Z AGAIN TO DELETE";
    case OM_CEM_NAME:     return "TYPE A NAME FIRST";
    case OM_CEM_LABEL:    return "A HEADING, NOT A CHEAT";
    case OM_CEM_LAST:     return "A CHEAT NEEDS ONE CODE: DELETE THE CHEAT";
    default:              return "";
    }
}

static void code_row(const om_ch_code *c, char *l)
{
    om_ced t;
    memset(&t, 0, sizeof t);
    t.addr = c->addr; t.val = c->value; t.cmd = c->cmd;
    om_ced_code_text(&t, l);
}

static const char *game_of(const char *image)
{   /* the menu hands the shown name over as "x/<name>" */
    const char *s_ = strrchr(image, '/');
    return s_ ? s_ + 1 : image;
}

static int32_t mq(uint32_t t, const char *s, int face, int boxw)
{
    return (int32_t)om_marquee_off(t, om_lg_width(face, s), boxw, 5994u);
}

static int32_t s_title_off = -1, s_row_off = -1;

static void title(const char *t_, int32_t off)
{
    if (off == s_title_off) return;
    s_title_off = off;
    om_lg_marquee(OM_LGF_HERO, X0, 77, X1 - X0 + 1, t_, om_lg_c.txt, 0, off);
}

static void head(const char *label, const char *t_, const char *meta)
{
    om_lg_clear(0, 40, 186, 97);
    om_lg_tracked(X0, 55, label, om_lg_c.acc2);
    om_lg_fill(X0, 58, 46, 59, om_lg_c.acc);
    s_title_off = -1;
    title(t_, 0);
    if (meta) om_lg_text(OM_LGF_SMALL, X0, 93, X1 - X0 + 1, meta, om_lg_c.dim, 0, OM_LG_FADE);
}

/* one list row; the highlighted one is its bar (the mockup's blue fade with a lit left edge) and its name in one pass */
static void list_row(const om_ced *e, uint32_t i, int y, int32_t off)
{
    const om_ch_cheat *c = &e->f->cheat[i];
    int sel = i == e->row, hdr = (c->flags & OM_CH_HEADER) != 0, tx = hdr ? X0 : X0 + 12, boxw = hdr ? X1 - X0 + 1 : X1 - X0 - 18;
    uint16_t fg = hdr ? om_lg_c.acc2 : sel ? om_lg_c.txt : om_lg_mix(om_lg_c.dim, om_lg_c.txt, 128), bg = 0;
    if (sel) {
        uint16_t u = om_lg_under(X0, y, X1, y + ROW_H - 1);
        bg = om_lg_mix(u, om_lg_c.acc, 110);
        om_lg_bartext(X0 - 4, y, X1, y + ROW_H - 1, om_lg_mix(u, om_lg_c.acc, 150), om_lg_mix(u, om_lg_c.acc, 30),
                      om_lg_c.acc2, OM_LGF_SMALL, tx, y + 10, boxw, c->name, fg, off);
    } else {
        om_lg_clear(X0 - 4, y, X1, y + ROW_H - 1);
        om_lg_text(OM_LGF_SMALL, tx, y + 10, boxw, c->name, fg, 0, OM_LG_FADE);
    }
    if (hdr) return;
    {   /* the box: an outline, green with a lit center when on */
        uint16_t bc = (c->flags & OM_CH_ON) ? om_lg_c.ok : om_lg_c.faint;
        om_lg_fill(X0, y + 2, X0 + 7, y + 9, bc);
        om_lg_fill(X0 + 1, y + 3, X0 + 6, y + 8, bg ? bg : om_lg_under(X0, y + 2, X0 + 7, y + 9));
        if (c->flags & OM_CH_ON) om_lg_fill(X0 + 2, y + 4, X0 + 5, y + 7, om_lg_c.ok);
    }
    if (c->flags & OM_CH_USER) om_lg_text(OM_LGF_SMALL, X1, y + 10, 0, "*", om_lg_c.gold, bg, OM_LG_RIGHT);
}

static void row_bar(int y, int sel)
{   /* a codes row: its bar, the 8x8 text drawn over it */
    om_lg_clear(X0 - 4, y, X1, y + ROW_H - 1);
    if (sel) {
        uint16_t u = om_lg_under(X0, y, X1, y + ROW_H - 1);
        om_lg_bartext(X0 - 4, y, X1, y + ROW_H - 1, om_lg_mix(u, om_lg_c.acc, 150), om_lg_mix(u, om_lg_c.acc, 30),
                      om_lg_c.acc2, OM_LGF_SMALL, X0, y + 10, 1, "", om_lg_c.txt, 0);
    }
}

static void msg_line(const om_ced *e)
{
    om_lg_clear(X0, MSG_BASE - 10, 196, MSG_BASE + 3);
    om_lg_text(OM_LGF_SMALL, X0, MSG_BASE, 196 - X0, msg_text(e), e->msg == OM_CEM_ASK ? om_lg_c.gold : om_lg_c.err, 0,
               OM_LG_FADE);
}

static void hints(const char *a, const char *b)
{
    om_lg_clear(X0, HINT1 - 11, HX1, HINT2 + 3);
    om_ls_hints(X0, HINT1, HX1, a, om_lg_c.dim);              /* the menu's badges (lscreen.h, 2026-11-25) */
    om_ls_hints(X0, HINT2, HX1, b, om_lg_c.dim);
}

static void mono_lit(int x, int y, char ch, int lit)
{
    char one[2] = { ch, 0 };
    if (lit) {
        om_lg_fill(x - 1, y - 2, x + 8, y + 9, om_lg_c.acc2);
        om_lg_mono(x, y, one, INK, om_lg_c.acc2);
    } else
        om_lg_mono(x, y, one, om_lg_c.txt, 0);
}

/* the code line in the 8x8 font: ADDRESS VALUE TYPE, the digit being changed lit */
static void code_line(const om_ced *e, int y)
{
    char r[18];
    int k, x = X0, cur = e->field + (e->field >= 8) + (e->field >= 12);
    om_ced_code_text(e, r);
    for (k = 0; r[k]; k++) {
        if (r[k] == ':') { x += 6; continue; }
        mono_lit(x, y, r[k], k == cur || (e->field >= 12 && k == cur + 1));
        x += 9;
    }
}

/* full: after a key; else only what moves (the title's and the highlighted row's marquee, when its offset moved) */
static void draw(const om_ced *e, const char *game, uint32_t t, int full)
{
    const om_ch_file *f = e->f;
    char l[48], meta[64], *o;
    uint32_t i;
    if (e->page == OM_CE_LIST) {
        uint32_t top = vtop(e->row, f->ncheats);
        int32_t toff = mq(t, game, OM_LGF_HERO, X1 - X0 + 1), roff = 0;
        if (f->ncheats) {
            const om_ch_cheat *c = &f->cheat[e->row];
            roff = mq(t, c->name, OM_LGF_SMALL, (c->flags & OM_CH_HEADER) ? X1 - X0 + 1 : X1 - X0 - 18);
        }
        if (!full) {
            title(game, toff);
            if (f->ncheats && roff != s_row_off) list_row(e, e->row, ROW_Y + (int)(e->row - top) * ROW_H, roff);
            s_row_off = roff;
            return;
        }
        o = meta;
        put_u(&o, om_ced_active(e));
        memcpy(o, " of ", 4); o += 4;
        put_u(&o, e->cap);
        strcpy(o, " on  \xC2\xB7  ");
        o += strlen(o);
        om_ced_slots(e, o, (uint32_t)(sizeof meta - (size_t)(o - meta)));
        head("CHEATS", game, meta);
        title(game, toff);
        om_lg_clear(X0 - 4, ROW_Y, X1, ROW_Y + ROWS * ROW_H - 1);
        if (!f->ncheats)
            om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 10, X1 - X0, e->ro ? "CHEATS.PAK does not read" : "No cheats yet: X adds one",
                       om_lg_c.dim, 0, OM_LG_FADE);
        for (i = top; i < f->ncheats && i < top + ROWS; i++) list_row(e, i, ROW_Y + (int)(i - top) * ROW_H, i == e->row ? roff : 0);
        s_row_off = roff;
        msg_line(e);
        hints("A: on/off   X: add   Y: codes   Z: delete   * added by you",
              e->dirty ? "B: save and back to Aether" : "B: back to Aether");
        return;
    }
    if (!full) return;
    om_lg_clear(X0 - 4, ROW_Y - 2, 186, ROW_Y + 74);
    if (e->page == OM_CE_CODES) {
        const om_ch_cheat *c = &f->cheat[e->cheat];
        uint32_t top = vtop(e->crow, c->count);
        head("CODES", c->name, "Address  Value  Type");
        for (i = top; i < c->count && i < top + ROWS; i++) {
            int y = ROW_Y + (int)(i - top) * ROW_H, sel = i == e->crow;
            row_bar(y, sel);
            code_row(&f->code[c->first + i], l);
            om_lg_mono(X0, y + 2, l, sel ? om_lg_c.txt : om_lg_c.dim, 0);
        }
        msg_line(e);
        hints("A: edit   X: add code   Y: rename   Z: delete code", "B: back to the list");
    } else if (e->page == OM_CE_NAME) {
        int k, x = X0;
        head(e->is_new ? "NEW CHEAT \xC2\xB7 ITS NAME" : "RENAME", game, 0);
        for (k = 0; k < (int)OM_CH_MAX_NAME && (e->name[k] || k <= e->npos) && x <= 300; k++, x += 9) {
            char ch = e->name[k] ? e->name[k] : ' ';
            if (k == e->npos) {
                static const char wh[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.!%&'()/:+?";   /* chedit.c's wheel */
                const char *w = strchr(wh, ch);
                uint32_t wi = w ? (uint32_t)(w - wh) : 0, wn = sizeof wh - 1u;
                char up[2] = { wh[(wi + 1u) % wn], 0 }, dn[2] = { wh[(wi + wn - 1u) % wn], 0 };   /* Up, Down give */
                om_lg_mono(x, ROW_Y + 4, up, om_lg_c.faint, 0);
                mono_lit(x, ROW_Y + 16, ch, 1);
                om_lg_mono(x, ROW_Y + 28, dn, om_lg_c.faint, 0);
            } else
                mono_lit(x, ROW_Y + 16, ch, 0);
        }
        msg_line(e);
        hints("Up/Down: letter   Left/Right: move", "A: next   B: cancel");
    } else {
        static const char *const tn[4] = { "00  write once at boot (16-bit)", "01  keep writing 16 bits",
                                           "03  keep writing 8 bits", "0D  only while this word matches" };
        int ti;
        head("CODE FOR", e->is_new == 1 ? e->name : f->cheat[e->cheat].name, "Address  Value  Type");
        code_line(e, ROW_Y + 2);
        for (ti = 0; ti < 4; ti++)
            if (om_ced_types[ti] == e->cmd)
                om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 24, X1 - X0, tn[ti], om_lg_c.acc2, 0, OM_LG_FADE);
        om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 36, X1 - X0, "Game RAM: 06004000-060FFFFF,", om_lg_c.dim, 0, OM_LG_FADE);
        om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 47, X1 - X0, "00200000-002FFFFF; 16-bit codes even.", om_lg_c.dim, 0,
                   OM_LG_FADE);
        om_lg_text(OM_LGF_SMALL, X0, ROW_Y + 58, X1 - X0, "Type 00 at 002xxxxx: game program only.", om_lg_c.dim, 0,
                   OM_LG_FADE);
        msg_line(e);
        hints("Left/Right: digit   Up/Down: change it", "A: done (checked first)   B: cancel");
    }
}

/* one field's pad: rising edges, plus the D-pad's repeat (20 fields, then every 4th) */
static uint16_t s_prev;
static uint32_t s_held;
static uint16_t pad_press(void)
{
    uint16_t p = om_smpc_pad(), dir = OM_CK_U | OM_CK_D | OM_CK_L | OM_CK_R, k;
    if (!om_smpc_pad_ok()) return 0;
    k = (uint16_t)(p & ~s_prev);
    if ((p & dir) && (p & dir) == (s_prev & dir)) {
        if (++s_held > 20u && !(s_held & 3u)) k |= (uint16_t)((p & dir) | OM_CK_REP);
    } else
        s_held = 0;
    s_prev = p;
    return k;
}

static void gap(void);
static int save(const om_ced *e, const char *key, char *msg)
{
    om_sdw_stats st;
    uint32_t n = 0;
    int rc;
    uint32_t cap = s_in_len + OM_CPK_GROW < OM_CP_MAX_FILE ? s_in_len + OM_CPK_GROW : OM_CP_MAX_FILE;
    if (!(s_out = alloc4(cap)) || !(s_scratch = alloc4(2048))) {
        strcpy(msg, "NOT SAVED: NO MEMORY");
        return OM_E_FULL;
    }
    if ((rc = om_cpk_put(s_in_len ? s_in : 0, s_in_len, key, e->f, 20261124u, s_out, cap, s_rec, &n)) < 0) {
        om_msg_rc(msg, COLS + 1, "NOT SAVED: ", rc == OM_E_FULL ? "CHEATS.PAK TOO BIG" : "BAD RECORD", rc);
        return rc;
    }
    memset(&st, 0, sizeof st);
    if ((rc = om_sdw_replace(OM_LCH_PATH, s_out, n, s_scratch, gap, &st)) < 0) {
        om_msg_rc(msg, COLS + 1, "NOT SAVED: SD ", "", rc);
        return rc;
    }
    strcpy(msg, "SAVED");
    return 0;
}

#ifdef OM_L1_HARNESS
/* the L1 harness's emulator capture (menu/spikes/l1_cheats): no Satiator, so no CHEATS.PAK -- a record in Kai's own
 * shape (Golden Axe: The Duel's from Kai's 2024 tables, codes as there) and a key script that walks the screens: a
 * toggle, the slots line, a refused code with its reason, a code taken, the list again */
static void demo_cheat(om_ch_file *f, const char *name, uint8_t flags, uint32_t a, uint16_t v)
{
    om_ch_cheat *c = &f->cheat[f->ncheats++];
    strcpy(c->name, name);
    c->flags = flags;
    c->first = f->ncodes;
    c->count = flags & OM_CH_HEADER ? 0 : 1;
    if (c->count) {
        f->code[f->ncodes].addr = a; f->code[f->ncodes].value = v; f->code[f->ncodes].cmd = 1;
        f->ncodes++;
    }
}
static void demo_file(om_ch_file *f)
{
    memset(f, 0, sizeof *f);
    f->master_addr = 0x06000914u; f->master_value = 0xC305; f->location = 0x060FF000u; f->cache4 = 125;
    f->flags = OM_CP_G_MASTER | OM_CP_G_CWX;
    demo_cheat(f, "PLAYER 1", OM_CH_HEADER, 0, 0);
    demo_cheat(f, "Unl Energy for P1", 0, 0x06078A5Cu, 0x0080);
    demo_cheat(f, "50% Energy for P1", 0, 0x06078A5Cu, 0x0040);
    demo_cheat(f, "PLAYER 2", OM_CH_HEADER, 0, 0);
    demo_cheat(f, "Unl Energy for P2", 0, 0x06078B50u, 0x0080);
    demo_cheat(f, "No Energy for P2", 0, 0x06078B50u, 0x0000);
    demo_cheat(f, "P1 MAGIC POTS", OM_CH_USER, 0x060423CCu, 0x00C8);
}
static const uint16_t k_demo[][2] = {
    { 60, OM_CK_D }, { 90, OM_CK_A }, { 150, OM_CK_D }, { 180, OM_CK_D }, { 210, OM_CK_D }, { 240, OM_CK_A },
    { 330, OM_CK_X }, { 360, OM_CK_U }, { 362, OM_CK_U }, { 364, OM_CK_U }, { 380, OM_CK_R }, { 390, OM_CK_U },
    { 400, OM_CK_A }, { 480, OM_CK_A }, { 600, OM_CK_R }, { 602, OM_CK_R }, { 604, OM_CK_R }, { 606, OM_CK_U },
    { 608, OM_CK_U }, { 610, OM_CK_U }, { 612, OM_CK_U }, { 614, OM_CK_U }, { 616, OM_CK_U }, { 618, OM_CK_U },
    { 680, OM_CK_A }, { 780, OM_CK_B }, { 840, OM_CK_B }, { 900, OM_CK_B }, { 960, OM_CK_B }, { 0, 0 } };
static uint16_t demo_key(uint32_t t)
{
    uint32_t i;
    om_txt_wait_vblank();
    for (i = 0; k_demo[i][1]; i++)
        if (k_demo[i][0] == t) return k_demo[i][1];
    return 0;
}
#endif

static void gap(void)
{   /* between SD write chunks: the V-blank gap, and the background (no reads) */
    om_lgap();
    om_lg_pump(0);
}

static void say(int line, const char *s_, uint16_t c)
{
    om_ls_line(line, s_, c);
}

int om_run_cheats(const om_handoff *h, om_lerr *e)
{
    static om_ced ed;
    static char msg[48];
    const char *game = game_of(h->image);
    int rc, state = 0;
    uint32_t t = 0, t0;
#ifdef OM_L1_HARNESS
    if (h->flags & OM_HO_TEST_NO_SATIATOR) {
        om_ch_file *demo = (om_ch_file *)alloc4(sizeof *demo);
        om_lg_start(h, 0);
        om_lg_clear(0, 0, 319, 223);
        om_lg_cover(0, 1);
        om_lg_brand();
        if (!demo) for (;;) om_txt_wait_vblank();
        demo_file(demo);
        om_ced_open(&ed, demo, 0);
        for (t = 0, t0 = 0; ; t++) {
            uint16_t k = demo_key(t);
            if (k && om_ced_key(&ed, k)) break;
            if (k) t0 = t;
            draw(&ed, game, t - t0, k || !t);
            om_lg_pump(1);
        }
        om_ls_page("CHEATS", game, 0);                              /* B on the list: the way back, as below */
        om_ls_status("Returning to Aether...", om_lg_c.dim);
        return om_go_home(h, e);
    }
#endif
    if ((rc = s_mode(s_api)) != 0) {
        e->step = 1; e->rc = rc; e->what = "Satiator API mode failed"; e->detail = "";
        return rc;
    }
    om_lg_start(h, 1);                                              /* the menu's last eye frame, continued */
    om_lg_clear(0, 0, 319, 223);
    om_lg_cover(0, 1);
    om_lg_brand();
    s_in = alloc4(BUF_CAP);
    s_f = (om_ch_file *)alloc4(sizeof *s_f);
    s_rec = alloc4(OM_CP_MAX_REC);
    if (!s_in || !s_f || !s_rec) {
        e->step = 1; e->rc = OM_E_FULL; e->what = "CHEATS: NO MEMORY"; e->detail = "";
        return OM_E_FULL;
    }
    if ((rc = read_pak()) == 1) state = 1;
    else if (rc < 0) state = 2;
    else if ((rc = om_cpk_find(s_in, s_in_len, h->ckey, s_f)) < 0) state = 2;
    else if (rc == 0) state = 1;
    if (!h->ckey[0]) state = 2;                                     /* no key: nothing to save under */
    om_ced_open(&ed, s_f, state);
    for (t = 0; t < 30; t++) { (void)om_smpc_pad(); om_lg_pump(1); }   /* the menu's A, still held: let it go */
    s_prev = 0xFFFFu;
    for (t = 0, t0 = 0; ; t++) {
        uint16_t k = pad_press();                                   /* one field */
        if (k && om_ced_key(&ed, k))
            break;
        if (k) t0 = t;                                              /* a new highlight starts its marquee over */
        draw(&ed, game, t - t0, k || !t);
        om_lg_pump(1);
    }
    om_ls_page("CHEATS", game, 0);
    if (ed.dirty && !ed.ro) {
        om_lg_quiet();                                              /* one file at a time while writing */
        say(0, "Saving CHEATS.PAK - keep the power on", om_lg_c.acc2);
        rc = save(&ed, h->ckey, msg);
        say(1, msg, rc ? om_lg_c.err : om_lg_c.txt);
        if (rc) {
            say(2, "Your old CHEATS.PAK is unchanged.", om_lg_c.dim);
            om_ls_status("Press a button to return to Aether", om_lg_c.txt);
            for (t = 0; t < 30; t++) { (void)om_smpc_pad(); om_lg_pump(0); }
            for (;;) {
                uint16_t p = om_smpc_pad();
                om_lg_pump(0);
                if (om_smpc_pad_ok() && (p & (OM_PAD_A | OM_PAD_B | OM_PAD_C))) break;
            }
        }
    }
    om_ls_status("Returning to Aether...", om_lg_c.dim);
    return om_go_home(h, e);                                        /* the menu again, reopened here (lseq.h) */
}
