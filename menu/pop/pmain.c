/* menu/pop/pmain.c -- POPUP.BIN's entry points (popabi.h): the header the menu checks, open / key / draw / close, and
 * the I/O around the models (pcheat.h, psave.h; About's, pabout.h, needs none). Runs in LWRAM at OM_LW_LBG, with the
 * menu's code (menu.elf's symbols) underneath; its buffers are the lent card-scan work area and its own .bss.
 * BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "audio.h"
#include "carddir.h"
#include "omlayout.h"
#include "pdraw.h"
#include "popabi.h"
#include "satiator.h"
#include "smpcpad.h"
#include "vdp2theme.h"

extern char __pop_img_len[], __pop_bss_start[], __pop_bss_end[];
static int p_open(const om_pop_req *r, char *why, uint32_t why_cap);
static uint32_t p_key(uint16_t press, uint16_t rep);
static void p_draw(uint16_t vis, uint32_t frame);
static void p_close(void);
int om_psetup(const om_pop_setup_req *q);   /* psetup.c */

__attribute__((section(".pophdr"), used)) const om_pop_hdr om_pop_header = {
    OM_POP_MAGIC, OM_POP_VERSION, (uint32_t)__pop_img_len, (uint32_t)__pop_bss_start, (uint32_t)__pop_bss_end,
    p_open, p_key, p_draw, p_close, om_psetup };

om_pop_state g_pop;                                   /* everything the view reads (pdraw.h) */
static om_pgs_env s_env;
/* in the lent decode canvas (req.tables): the saves listed, the folder names (Settings), one game's cheats */
#define T_ITEMS ((om_sv_item *)(void *)r->tables)
#define T_FOLD  ((char (*)[OM_PGS_DIR])(void *)(r->tables + OM_PS_ROWS * sizeof(om_sv_item)))
#define T_CHEAT ((om_ch_file *)(void *)r->tables)
_Static_assert(OM_PS_ROWS * sizeof(om_sv_item) + OM_POP_FOLDERS * OM_PGS_DIR <= 53760u, "saves tables > the canvas");
_Static_assert(sizeof(om_ch_file) <= 53760u, "cheats > the canvas");
_Static_assert(sizeof(om_sv_item) % 4u == 0, "the folder table must stay 4-aligned");
static uint32_t s_al[16];                             /* names handed to libsatiator: 4-aligned (HWF) */
static char s_date[16];

static const char *al(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n >= sizeof s_al) n = sizeof s_al - 1u;
    memcpy(s_al, p, n);
    ((char *)s_al)[n] = 0;
    return (const char *)s_al;
}

/* between SD write chunks and the BRAM backup's steps: the music's refill (the menu's frame is not running) */
static void gap(void) { om_audio_tick(); }

/* --- the backup library's reset guard, through SGL (bram.c's smpcpad.h calls): never a raw SMPC command while SGL's
 * per-frame INTBACK owns the SMPC. RESD is read from SGL's status copy (refreshed every frame, rtc.c HWF-40); a change
 * is requested through slRequestCommand and confirmed in that copy within ~10 V-blanks, else it is refused (bram.c then
 * writes nothing). ---------------------------------------------------------------------------------------------- */
int om_smpc_resd(void) { return (Smpc_Status->cond & SMPC_MASK_RESD) ? 1 : 0; }

int om_smpc_cmd_b(uint8_t cmd)
{
    int want = cmd == OM_SMPC_RESDISA ? 1 : cmd == OM_SMPC_RESENAB ? 0 : -1;
    uint32_t v0, t;
    if (want < 0) return OM_SMPC_E_BUSY;
    if (om_smpc_resd() == want) return 0;
    for (t = 0; t < 4u && slRequestCommand(want ? SMPC_RESDIS : SMPC_RESENA, SMPC_NO_WAIT) != OK; t++) {
        v0 = om_v2_vblanks();
        while (om_v2_vblanks() == v0) om_audio_tick_low();   /* the music refilled while it waits */
    }
    for (t = 0; t < 12u; t++) {
        v0 = om_v2_vblanks();
        while (om_v2_vblanks() == v0) om_audio_tick_low();   /* the music refilled while it waits */
        if (om_smpc_resd() == want) return 0;
    }
    return OM_SMPC_E_BUSY;
}

static uint8_t bcd(uint8_t b) { return (uint8_t)((b >> 4) * 10u + (b & 15u)); }
static void put2(char *o, uint32_t v) { o[0] = (char)('0' + v / 10u % 10u); o[1] = (char)('0' + v % 10u); }

static void clock_date(void)
{   /* "YYYYMMDD-HHMMSS" from SGL's status copy, "" when the console clock was never set (STE) */
    const SmpcDateTime *t = &Smpc_Status->rtc;
    s_date[0] = 0;
    if (!(Smpc_Status->cond & SMPC_MASK_STE)) return;
    put2(s_date, bcd((uint8_t)(t->year >> 8)));
    put2(s_date + 2, bcd((uint8_t)t->year));
    put2(s_date + 4, t->month & SMPC_MASK_MONTH);
    put2(s_date + 6, bcd(t->date));
    s_date[8] = '-';
    put2(s_date + 9, bcd(t->hour));
    put2(s_date + 11, bcd(t->minute));
    put2(s_date + 13, bcd(t->second));
    s_date[15] = 0;
}

static int p_open(const om_pop_req *r, char *why, uint32_t why_cap)
{
    om_pop_state *g = &g_pop;
    uint8_t *a = r->arena;
    int rc;
    memset(g, 0, sizeof *g);
    g->req = *r;
    g->kind = r->kind;
    if (r->kind == OM_POP_ABOUT) {                     /* no card, no lent memory: the credits and the logo only */
        if (om_pd_open(g) != 0) {
            strncpy(why, "Popup: no room for its text", why_cap - 1);
            return OM_E_FULL;
        }
        return 0;
    }
    if (!r->tables || r->tables_cap < OM_PS_ROWS * sizeof(om_sv_item) + OM_POP_FOLDERS * OM_PGS_DIR ||
        r->tables_cap < sizeof(om_ch_file) || ((uintptr_t)r->tables & 3u) || ((uintptr_t)a & 3u)) {
        strncpy(why, "Popup: no memory", why_cap - 1);
        return OM_E_FULL;
    }
    if (om_pd_open(g) != 0) {                          /* the text slots (VDP1 VRAM's idle saver range) */
        strncpy(why, "Popup: no room for its text", why_cap - 1);
        return OM_E_FULL;
    }
    if (s_chdir(al(OM_CARD_DIR)) < 0) {               /* svmgr.h names everything from there */
        strncpy(why, "Cannot open " OM_CARD_DIR, why_cap - 1);
        return OM_E_FIELD;
    }
    g->chdir = 1;
    if (r->kind == OM_POP_CHEATS) {
        om_pc_open(&g->pc, r->key, T_CHEAT, a, r->arena_cap);
        return 0;
    }
    if (r->arena_cap < OM_PGS_ARENA + OM_POP_BIGMIN) {
        strncpy(why, "Popup: no memory", why_cap - 1);
        return OM_E_FULL;
    }
    om_pgs_arena_split(&s_env, a);
    s_env.gap = gap;
    s_env.rel = 1;
    clock_date();
    rc = om_sv_open(&g->sv, &s_env, r->kind == OM_POP_SAVES ? r->gid : "", a + OM_PGS_ARENA,
                    r->arena_cap - OM_PGS_ARENA, T_ITEMS, OM_PS_ROWS, s_date);
    g->open_rc = rc;
    om_ps_open(&g->ps, &g->sv, r->kind == OM_POP_SAVES_ALL, T_FOLD, OM_POP_FOLDERS, OM_PD_SAVE_ROWS);
    return 0;
}

static uint32_t sfx_of(uint32_t f)
{
    return (f & OM_PS_CLOSE) || (f & OM_PS_BACK) ? OM_POP_SFX_BACK : (f & OM_PS_OK) ? OM_POP_SFX_OK
         : (f & OM_PS_MOVED) ? OM_POP_SFX_MOVE : 0;
}

static uint32_t key_cheats(uint16_t k)
{
    om_pop_state *g = &g_pop;
    om_ced *e = &g->pc.ed;
    uint8_t page = e->page, msg;
    uint16_t row = e->row, crow = e->crow, field = e->field;
    uint32_t ckey = k & (OM_CK_U | OM_CK_D | OM_CK_L | OM_CK_R | OM_CK_A | OM_CK_B | OM_CK_X | OM_CK_Y | OM_CK_Z);
    if (g->saving) {                                   /* B was pressed last frame: "Saving..." is on screen */
        g->saving = 0;
        if (om_pc_save(&g->pc, gap, g->msg, sizeof g->msg) == 0) return OM_POP_CLOSED | OM_POP_SAVED | OM_POP_SFX_BACK;
        g->save_failed = 1;                            /* shown; a second B leaves without saving */
        return OM_POP_SFX_ERR;
    }
    if (!ckey) return 0;
    if (g->save_failed && !(k & OM_CK_B)) g->save_failed = 0;   /* any other key: B saves again */
    if (k & OM_CK_REP) ckey |= OM_CK_REP;
    g->msg[0] = 0;
    if (om_ced_key(e, (uint16_t)ckey)) {               /* B on the list */
        if (e->dirty && !e->ro && !g->save_failed) { g->saving = 1; return OM_POP_SFX_OK; }
        return OM_POP_CLOSED | OM_POP_SFX_BACK;
    }
    msg = e->msg;
    if (msg == OM_CEM_FULL || msg == OM_CEM_BAD || msg == OM_CEM_READONLY || msg == OM_CEM_KAI ||
        msg == OM_CEM_FILEFULL || msg == OM_CEM_NAME || msg == OM_CEM_LABEL || msg == OM_CEM_LAST)
        return OM_POP_SFX_ERR;
    if (e->page != page) return (k & OM_CK_B) ? OM_POP_SFX_BACK : OM_POP_SFX_OK;
    if (k & (OM_CK_A | OM_CK_X | OM_CK_Y | OM_CK_Z)) return OM_POP_SFX_OK;
    return (e->row != row || e->crow != crow || e->field != field || (k & (OM_CK_U | OM_CK_D))) ? OM_POP_SFX_MOVE : 0;
}

static uint32_t p_key(uint16_t press, uint16_t rep)
{
    om_pop_state *g = &g_pop;
    uint16_t k = press;
    uint32_t f;
    if ((rep & press) & (OM_CK_U | OM_CK_D)) k |= OM_CK_REP;
    g->frames++;
    if (g->kind == OM_POP_CHEATS) return key_cheats(k);
    if (g->kind == OM_POP_ABOUT) {
        f = om_pa_key(&g->pa, (uint16_t)(k & (OM_PA_K_U | OM_PA_K_D | OM_PA_K_L | OM_PA_K_R | OM_PA_K_B | OM_PA_K_REP)));
        return (f & OM_PA_CLOSE) ? OM_POP_CLOSED | OM_POP_SFX_BACK : (f & OM_PA_MOVED) ? OM_POP_SFX_MOVE : 0;
    }
    if (om_ps_pending(&g->ps)) {                       /* "Working..." was drawn last frame */
        om_ps_run(&g->ps);
        return g->ps.msgk == OM_PSM_ERR ? OM_POP_SFX_ERR : OM_POP_SFX_OK;
    }
    if (!press) return 0;
    f = om_ps_key(&g->ps, (uint16_t)(k & (OM_PS_K_U | OM_PS_K_D | OM_PS_K_L | OM_PS_K_R | OM_PS_K_A | OM_PS_K_B |
                                          OM_PS_K_REP)));
    return ((f & OM_PS_CLOSE) ? OM_POP_CLOSED : 0) | ((f & OM_PS_IMPORT) ? OM_POP_CLOSED | OM_POP_IMPORT : 0) | sfx_of(f);
}

static void p_draw(uint16_t vis, uint32_t frame) { om_pd_draw(&g_pop, vis, frame); }

static void p_close(void)
{
    if (g_pop.chdir) s_chdir(al("/"));                 /* the menu names everything from the root */
    g_pop.chdir = 0;
}
