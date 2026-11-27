/* menu/ui/art.c -- see art.h. Since Task 13a the view draws the art as raw VDP1 commands (menu/ui/v1.c): the hero as a
 * scaled RGB sprite, CL32KRGB|ECdis (HWF-01, HWF-03), into the layout table's cover box; the JP title as a 16-colour
 * lookup-table sprite, CL16Look|ECdis, CMDCOLR = LUT address / 8 (HWF-04, HWF-05), whose LUT ramps from the details
 * card's colour to the text colour. VRAM writes are 16-bit only (HWF-23). The NO ART card is vector-drawn by the view.
 *
 * Ruling M3-R52, the cover cache: the art lives in up to 5 VDP1 VRAM slots (the old A/B pair plus C0, C1 below SGL's
 * GouraudRAM and, if the UI pool leaves room, C2 at its top; omlayout.h), each a hero + a JP bitmap, with LRU bookkeeping
 * in menu/src/artcache.c. A cache hit just points the cover sprite at its slot: no SD read, no upload, no debounce. A
 * miss waits the 150 ms rest (spec §7.4), then the job reads and decodes into the HWRAM canvas and uploads it in 4
 * chunks (review-13a fix-plan 8) into a slot that is neither shown nor holding anything newer -- the previous image
 * stays on screen until the new one is complete (spec §7.4). While the cursor rests, the idle frames prefetch the
 * items around it (om_art_prefetch, cur, +1, -1, +2, -2); a prefetch is dropped the moment the cursor moves, unless
 * it is for the new selection. One art file at a time (the 2-file rule: the music + this): ART.PAK stays open between
 * jobs, so a cursor move costs no path resolution (big-card stutter, 2026-11-23); om_art_invalidate closes it before any
 * other SD file opens. BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "art.h"
#include "artcache.h"
#include "artjob.h"
#include "anim.h"
#include "artsched.h"
#include "browse.h"
#include "msgfmt.h"
#include "omlayout.h"
#include "text.h"
#include "uitext.h"
#include "vdp1tex.h"
#include "vdp2theme.h"
#include "vrtile.h"

#define UP_CHUNKS 4u                           /* hero upload: 4 x 12,800 B */
#define NSLOT     5
#define M_TALL    1u
#define M_JP      2u
#define PF_RADIUS 2

static const uint32_t k_hero[NSLOT] = { OM_VRAM_HERO_A, OM_VRAM_HERO_B, OM_VRAM_CACHE0, OM_VRAM_CACHE1, OM_VRAM_CACHE2 };
static const uint32_t k_jp[NSLOT] = { OM_VRAM_JP_A, OM_VRAM_JP_B, OM_VRAM_CACHE0 + 51200u, OM_VRAM_CACHE1 + 51200u,
                                      OM_VRAM_CACHE2 + 51200u };
typedef char cache_fits[OM_VRAM_CACHE0 + OM_VRAM_CACHE_SLOT <= OM_VRAM_GOUR &&
                        OM_VRAM_CACHE1 + OM_VRAM_CACHE_SLOT <= OM_VRAM_CACHE0 &&
                        OM_VRAM_CACHE2 + OM_VRAM_CACHE_SLOT <= OM_VRAM_CACHE1 &&
                        OM_VRAM_CACHE1 >= OM_VRAM_UI_END &&
                        OM_VRAM_GOUR + 8u * OM_VRAM_GOUR_N <= 0x7FF00u ? 1 : -1];   /* SGL GouraudRAM (HWF-08) */

static om_artcache s_ac;
static om_artjob s_job;
static om_art_out s_out __attribute__((aligned(4)));
typedef char lend_size[sizeof s_out == OM_ART_LEND_BYTES ? 1 : -1];
static char s_msg[41];
static uint32_t s_serial;                      /* bumped whenever the shown image changes (the view's fade-in) */
static uint16_t s_debounce, s_still;
static uint32_t s_want = OM_ART_NONE;          /* the selection */
static uint32_t s_shown = OM_ART_NONE;         /* what the panel shows: a cached slot, or NO ART */
static int s_slot = -1;                        /* the shown slot, -1 = NO ART */
/* the one job in flight: SD (or tile) phase, then the upload phase into s_up_slot */
static uint32_t s_job_key = OM_ART_NONE;
static uint8_t s_job_on, s_job_pf, s_upload, s_up_chunk, s_job_meta;
static int s_up_slot = -1;
static uint32_t s_pf_hits;                     /* diagnostics: selections served from the cache */

static void vram16(uint32_t off, const uint16_t *src, uint32_t words)
{
    volatile Uint16 *d = (volatile Uint16 *)(OM_CT(OM_VDP1_VRAM) + off);
    uint32_t i;
    for (i = 0; i < words; i++)
        d[i] = src[i];
}

int om_art_init(void)
{
    uint16_t lut[16];
    uint32_t i, v;
    for (i = 0; i < 16; i++) {                                  /* 0 transparent; 1..15 black -> white (HWF-02) */
        v = (i * 31u + 7u) / 15u;
        lut[i] = i ? (uint16_t)(0x8000u | (v << 10) | (v << 5) | v) : 0;
    }
    vram16(OM_VRAM_LUT, lut, 16);
    s_debounce = om_debounce_frames(om_tv_is_pal() ? 5000u : 5994u, 150u);   /* HWF-10, HWF-11 */
    if (!s_debounce) s_debounce = 1;
    om_ac_init(&s_ac, NSLOT - 1);                               /* C2 joins once the UI pool has been laid out */
    s_job.fd = -1;
    return OM_OK;
}

int om_art_cache_claim(void)
{   /* after every UI pool allocation (folders.c): C2 is the pool's unused top, if it is unused */
    if (om_ut_pool_cap(OM_VRAM_CACHE2) != OM_OK) return 0;
    s_ac.n = NSLOT;
    return 1;
}

int om_art_cache_slots(void) { return s_ac.n; }
uint32_t om_art_cache_hits(void) { return s_pf_hits; }

static void show(uint32_t key, int slot)
{
    if (s_shown != key || s_slot != slot) s_serial++;
    s_shown = key;
    s_slot = slot;
    if (slot >= 0) om_ac_touch(&s_ac, slot);
}

static void job_cancel(void)
{   /* the SD phase only: a started upload finishes (it reads the HWRAM canvas, no file) */
    if (s_job_on && !s_upload) {
        om_artjob_cancel(&s_job);                               /* closes its fd if one is open (fd -1 otherwise) */
        s_job_on = 0;
        s_job_key = OM_ART_NONE;
    }
}

void om_art_invalidate(void)
{   /* before a synchronous SD write, a launch, the diagnostics screen or a theme load (which stages through OM_LW_STAGE):
     * the job's file closes, and ART.PAK, which stays open across jobs (artjob.c); the VRAM cache stays valid (nothing
     * here writes VDP1 VRAM) */
    job_cancel();
    om_artjob_release();
}

uint8_t *om_art_lend(void)
{   /* the dropped upload's slot was already taken out of the cache (job_done), so nothing half-written shows */
    om_art_invalidate();
    s_upload = 0;
    s_job_on = 0;
    s_job_key = OM_ART_NONE;
    s_up_slot = -1;
    return (uint8_t *)&s_out;
}

static int is_tile(uint32_t key) { return (key >> 16) == OM_IT_VROOT; }

void om_art_theme_changed(void)
{   /* the virtual-folder tiles are drawn in the theme's colors: drop them, the shown one is redone at once */
    job_cancel();
    om_ac_drop_if(&s_ac, is_tile);
    if (is_tile(s_shown)) s_shown = OM_ART_NONE;               /* the old tile stays up until the new one is in */
}

static int tile_start(const om_card *c, unsigned vr)
{   /* a virtual root's built-in tile, decoded straight into the canvas from VRTILES.BIN, which card.c read into LWRAM
     * with GAMES.DB (no longer HWRAM .rodata; no SD read here); colors from the current theme. Without the file
     * the cover is NO ART and the status line names it ("ART: VRTILES.BIN <reason> rc <n>"). */
    const om_theme *th = om_v2_theme();
    uint16_t roles[OM_VRT_ROLES];
    int rc;
    roles[0] = om_c555_rgb(0x202030);
    roles[1] = th->cursor;
    roles[2] = th->text_hi;
    roles[3] = om_c555_rgb(0xF5C542);
    roles[4] = om_c555_rgb(0xF0F0F5);
    rc = c->vrt_rc ? c->vrt_rc : om_vrtile_decode(c->vrt, c->vrt_len, vr, roles, s_out.hero);
    s_job.fd = -1;
    s_job.err = rc;
    s_job.errmsg = "ART: VRTILES.BIN";
    s_job.has_jp = 0;
    s_job.tall = 0;
    s_job.hero_src = rc ? OM_HERO_PLACEHOLDER : OM_HERO_PAK;
    return rc;
}

static uint32_t key_of(const om_item *it)
{
    return it && (it->kind <= OM_IT_GAME || it->kind == OM_IT_VROOT) ? ((uint32_t)it->kind << 16) | it->idx : OM_ART_NONE;
}

static void job_done(void);

static void job_start(const om_card *c, const om_tree *t, uint32_t key, int pf)
{
    const om_artpak *art = c->art_rc ? NULL : &c->art;
    uint8_t *stage = (uint8_t *)OM_CT(OM_LW_STAGE);             /* HWF-12 */
    uint16_t idx = (uint16_t)(key & 0xFFFFu);
    s_job_key = key;
    s_job_pf = (uint8_t)pf;
    s_job_on = 1;
    s_upload = 0;
    if ((key >> 16) == OM_IT_VROOT) {
        tile_start(c, idx);
        job_done();
        return;
    }
    if ((key >> 16) == OM_IT_FOLDER)
        om_artjob_start(&s_job, art, NULL, t->fpath[idx], stage, OM_LW_STAGE_MAX);
    else {
        om_lib_entry e;
        om_libidx_entry(&c->lib, idx, &e);
        om_artjob_start(&s_job, art, &e, NULL, stage, OM_LW_STAGE_MAX);
    }
}

static void job_done(void)
{   /* the SD (or tile) phase is over: no art -> remembered; art -> upload into a free slot */
    int prefetch = s_job_pf && s_job_key != s_want;
    if (s_job.err && !prefetch)
        om_msg_rc(s_msg, sizeof s_msg, s_job.errmsg && s_job.errmsg[0] == 'A' ? "" : "ART: BOX.TGA ",
                  s_job.errmsg ? s_job.errmsg : "", s_job.err);
    else if (!prefetch)
        s_msg[0] = 0;
    if (s_job.hero_src == OM_HERO_PLACEHOLDER && !s_job.has_jp) {
        if (!s_job.err) om_ac_noart_add(&s_ac, s_job_key);     /* an error is not remembered: it may be transient */
        if (!prefetch) show(s_job_key, -1);
        s_job_on = 0;
        s_job_key = OM_ART_NONE;
        return;
    }
    s_up_slot = om_ac_victim(&s_ac, s_slot >= 0 ? 1u << s_slot : 0u);   /* never the shown slot */
    if (s_up_slot < 0) { s_job_on = 0; s_job_key = OM_ART_NONE; return; }
    om_ac_drop(&s_ac, s_up_slot);                               /* being overwritten: no hit on it until it is whole */
    s_job_meta = (uint8_t)((s_job.hero_src != OM_HERO_PLACEHOLDER && s_job.tall ? M_TALL : 0u) |
                           (s_job.has_jp ? M_JP : 0u));
    if (s_job.hero_src == OM_HERO_PLACEHOLDER) s_job_meta |= 0x80u;   /* a JP title only: the cover stays NO ART */
    s_upload = 1;
    s_up_chunk = 0;
}

static void upload_step(void)
{   /* one hero chunk per frame, then the JP bitmap; the slot is then entered and, if it is the selection, shown */
    if (!(s_job_meta & 0x80u) && s_up_chunk < UP_CHUNKS) {
        uint32_t n = OM_HERO_PIXELS / UP_CHUNKS, o = n * s_up_chunk;
        vram16(k_hero[s_up_slot] + 2u * o, s_out.hero + o, n);
        s_up_chunk++;
        return;
    }
    if (s_job_meta & M_JP)
        vram16(k_jp[s_up_slot], (const uint16_t *)s_out.jp, sizeof s_out.jp / 2);
    om_ac_set(&s_ac, s_up_slot, s_job_key, s_job_meta);
    if (s_job_key == s_want) show(s_job_key, s_up_slot);
    s_upload = 0;
    s_job_on = 0;
    s_job_key = OM_ART_NONE;
    s_up_slot = -1;
}

static void job_tick(void)
{
    if (!s_job_on) return;
    if (s_upload) upload_step();
    else if (om_artjob_step(&s_job, &s_out, OM_AJ_READS_PER_STEP)) job_done();
}

void om_art_tick(const om_card *c, const om_tree *t, const om_item *sel)
{
    uint32_t id = key_of(sel);
    if (id != s_want) { s_want = id; s_still = 0; }
    else if (s_still < 0xFFFFu) s_still++;
    if (s_job_on && !s_upload && s_job_key != id &&
        (!s_job_pf || (id != OM_ART_NONE && om_ac_find(&s_ac, id) < 0 && !om_ac_noart(&s_ac, id))))
        job_cancel();                                           /* the cursor moved: a load for another item goes, and
                                                                 * a prefetch yields to a selection that needs reading
                                                                 * (one still in the window is kept: om_art_prefetch
                                                                 * drops it once it leaves the window) */
    if (s_job_on && s_job_key == id) s_job_pf = 0;              /* a prefetch for the new selection becomes its load */
    if (id == OM_ART_NONE) {                                    /* a bucket: no art (spec §5.6 step 4) */
        if (s_shown != OM_ART_NONE || s_slot >= 0) { show(OM_ART_NONE, -1); s_msg[0] = 0; }
    } else if (s_shown != id) {
        int k = om_ac_find(&s_ac, id);
        if (k >= 0) {                                           /* a hit: shown now, no read, no rest */
            show(id, k);
            s_msg[0] = 0;
            s_pf_hits++;
        } else if (om_ac_noart(&s_ac, id)) {
            show(id, -1);
            s_msg[0] = 0;
        } else if (!s_job_on && s_still >= s_debounce)
            job_start(c, t, id, 0);
    }
    job_tick();
}

static const om_item *s_pit;

static int need(void *ctx, int i)
{
    uint32_t k = key_of(&s_pit[i]);
    (void)ctx;
    return k != OM_ART_NONE && om_ac_find(&s_ac, k) < 0 && !om_ac_noart(&s_ac, k);
}

void om_art_prefetch(const om_card *c, const om_tree *t, const om_item *items, uint16_t n, uint16_t cur)
{   /* idle frames only: the selection is settled and nothing is loading */
    int i;
    if (!items) return;
    s_pit = items;
    if (s_job_on && s_job_pf && !s_upload && s_job_key != s_want) {   /* still within cur +- radius? else dropped */
        int k, in = 0;
        for (k = (int)cur - PF_RADIUS; k <= (int)cur + PF_RADIUS; k++)
            if (k >= 0 && k < (int)n && key_of(&items[k]) == s_job_key) in = 1;
        if (!in) job_cancel();
    }
    if (s_job_on || s_shown != s_want || s_still < s_debounce) return;
    if ((i = om_pf_next((int)cur, (int)n, PF_RADIUS, need, 0)) < 0) return;
    job_start(c, t, key_of(&items[i]), 1);
}

int om_art_hero(uint32_t *tex, int *w, int *h, int *tall)
{   /* the view draws it (raw VDP1 scaled sprite, menu/ui/v1.c) into the mockup's 100x156 slot. Returns 0 for
     * "no art" (the view draws NO ART) */
    if (s_slot < 0 || (s_ac.meta[s_slot] & 0x80u)) {
        *tex = 0; *w = *h = 0; *tall = 0;
        return 0;
    }
    *tall = (s_ac.meta[s_slot] & M_TALL) != 0;
    *tex = k_hero[s_slot];
    *w = *tall ? 128 : 160;
    *h = *tall ? 200 : 160;
    return 1;
}

int om_art_jp(uint32_t *tex, int *w, int *h)
{
    if (s_slot < 0 || !(s_ac.meta[s_slot] & M_JP))
        return 0;
    *tex = k_jp[s_slot];
    *w = OM_JP_CANVAS_W;
    *h = OM_JP_CANVAS_H;
    return 1;
}

void om_art_jp_lut(uint16_t fg, uint16_t bg)
{   /* the JP-title LUT ramps from the details card's colour to the text colour, like the UI text (no dark
     * halo); 0 stays transparent (HWF-02) */
    uint16_t lut[16];
    uint32_t i;
    lut[0] = 0;
    for (i = 1; i < 16; i++)
        lut[i] = om_c555_mix((uint16_t)(bg | 0x8000u), fg, (i * 256u + 7u) / 15u);
    vram16(OM_VRAM_LUT, lut, 16);
}

int om_art_loading(void) { return s_want != OM_ART_NONE && s_want != s_shown; }
uint32_t om_art_serial(void) { return s_serial; }

const char *om_art_message(void) { return s_msg; }
