/* menu/ui/vdp2theme.c -- see vdp2theme.h.
 * Why VDP2 colour calculation and not VDP1 (decision D1, docs/decisions/m3-transparency.md): VDP1 half-transparency
 * replaces where the frame-buffer MSB is 0 (HWF-32; ST-013-R3 §6.3), which is every pixel where VDP2 shows through --
 * a VDP1 panel would be opaque over the theme background. MESH is out too: it draws a fixed 50 % checkerboard with no
 * ratio (VDP1_Manual.txt:3549-3551), so it cannot follow panel_alpha. NBG2's colour calculation blends with the image
 * below it (NBG1) at the theme's ratio (HWF-31). VRAM writes are 16-bit (HWF-28). BSD-3-Clause.
 * SGL boots with RBG0 on and A0/B0 reserved for it in RAMCTL (HWF-41); RDBS only applies while RBG0 is displayed, so
 * om_v2_init turns RBG0 off. _slInitScroll also DMAs font into A0 for RBG0 at slInitSystem; om_v2_init runs after it. */
#include <string.h>
#include "bgfmt.h"                                     /* before sgl.h: SGL.H:28 #defines `pal` (om_bg.pal) */
#include "sgl.h"
#include "omlayout.h"
#include "vdp2theme.h"
#include "yield.h"

static volatile Uint16 *cram(uint32_t n) { return (volatile Uint16 *)(OM_CRAM + 2u * n); }          /* HWF-30 */

/* Panels: pixel-exact rectangles on NBG2's 8x8 cells. Chars 0-15 are solid in palette-2 colour 0-15 (0 =
 * transparent); a cell that a rectangle only partly covers gets a char drawn for it from a cache of chars 16-255 (the
 * rest of A0 below the map at +0x2000, plan D4). Rectangles are staged between om_v2_rects_begin and om_v2_rects_end;
 * an unchanged list costs nothing, and end writes only the map cells that differ from what is shown (review-13
 * minor 3: no mid-frame clear). A soft rectangle (the cover's drop shadow) fades over 3 px with a 2x2 ordered dither,
 * since NBG2 has one colour-calculation ratio (HWF-31) and so no per-pixel alpha. */
#define V2_W 40
#define V2_H 28
#define V2_RECTS 8
#define V2_DYN0 16u
#define V2_DYN_N 240u
typedef struct { int16_t x0, y0, x1, y1; uint8_t col, soft; } v2rect;
static v2rect s_rq[V2_RECTS], s_rq_shown[V2_RECTS];
static int s_nrq, s_nrq_shown = -1;
static uint16_t s_shown[V2_H][V2_W];
static uint32_t s_dyn_hash[V2_DYN_N];
static uint32_t s_dyn_live[V2_DYN_N / 32u];  /* a bit a cached char (collected, never restarted) */
static uint32_t s_ndyn, s_nlive;             /* high-water mark; live entries */
#define LIVE(i) ((s_dyn_live[(i) >> 5] >> ((i) & 31u)) & 1u)

static void map_put(int i, int j, unsigned cell)
{
    volatile Uint16 *map = (volatile Uint16 *)OM_V2_N2MAP;
    map[j * 64 + i] = (Uint16)((OM_V2_N2PAL << 12) | (cell & 0xFFFu));   /* 1-word PN: pal 15-12, char 11-0 (HWF-09) */
}

/* Review-13a fix-plan 6: rects_end queues its map writes; the V-blank hook writes them, so a panel never changes while
 * the display reads it. Writes are idempotent, so the CPU may also flush a list the hook has not run yet. */
#define PEND_MAX 512u                     /* more changed cells than this in one end: written at once */
static uint16_t s_pend_idx[PEND_MAX], s_pend_val[PEND_MAX];
static volatile uint32_t s_pend_n, s_pend_ready;

static void pend_flush(void)
{
    volatile Uint16 *map = (volatile Uint16 *)OM_V2_N2MAP;
    uint32_t k, n = s_pend_n;
    for (k = 0; k < n; k++)
        map[s_pend_idx[k]] = s_pend_val[k];
}

static void char_put(unsigned c, const uint8_t *pat)
{
    volatile Uint16 *cel = (volatile Uint16 *)OM_V2_N2CEL + 16u * c;   /* 32 B a char, 4 bpp (HWF-05, HWF-28) */
    unsigned i;
    for (i = 0; i < 16; i++)
        cel[i] = (Uint16)((pat[2 * i] << 8) | pat[2 * i + 1]);
}

static volatile uint32_t s_vblanks;
uint32_t om_v2_vblanks(void) { return s_vblanks; }

/* Hardware flicker instrument (vdp2theme.h). TVSTAT bit 3 = VBLANK (HWF-10). EDSR (VDP1 0x100010) bit 0 = BEF, "the end
 * bit in previous frame has been fetched", copied from CEF at each frame change (VDP1_Manual.txt:2359-2385); SGL's own
 * slDynamicFrame reads that register (LIBSGL sglC16.o: `mov.w @(16,r2)`, r2 = 0x25D00000). No FRT read here: the main
 * loop's two-byte FRC reads (uitext.c) would be torn by an interrupt that reads it too. */
#define TVSTAT (*(volatile Uint16 *)0x25F80004)
#define EDSR (*(volatile Uint16 *)0x25D00010)
static om_v2_stat s_st;
static uint32_t s_run;
void om_v2_hwstat_reset(void) { memset(&s_st, 0, sizeof s_st); s_run = 0; }
const om_v2_stat *om_v2_hwstat(void) { return &s_st; }

void om_v2_vblank(void)
{   /* registered with slIntFunction (ST-238-R1: "a function to be executed during blanking"); LIBSGL sglI04.o stores it
     * at GBR+20 and sglI00.o's _BlankIn calls it after its scroll-register DMA (disassembly read 2026-11-19) */
    s_vblanks++;
    s_st.vbl++;
    if (!(TVSTAT & 8u)) s_st.late_in++;
    if (!(EDSR & 1u)) s_st.v1_over++;
    if (s_pend_ready) {
        uint32_t n = s_pend_n;
        pend_flush();
        s_pend_ready = 0;
        if (n > s_st.max_w) s_st.max_w = n;
        if (!(TVSTAT & 8u)) s_st.late_out++;
    }
}

void om_v2_rects_begin(void) { s_nrq = 0; }

/* the safe-area transform (NULL = identity) for om_v2_rect and om_v2_bg_at, set around a view's draw
 * group */
static const om_safe *s_safe;
void om_v2_set_safe(const om_safe *t) { s_safe = t && !t->ident ? t : 0; }

void om_v2_rect(int x0, int y0, int x1, int y1, unsigned col, int soft)
{
    if (s_nrq >= V2_RECTS)
        return;
    if (x1 < x0 || y1 < y0) {                          /* an absent rectangle keeps its index, */
        x0 = 1; x1 = 0; y0 = 1; y1 = 0; col = 0; soft = 0;   /* before mapping, which would make it a real 1 px one */
    } else if (s_safe) {
        int r[4];
        om_safe_rect_u(s_safe, x0, y0, x1, y1, r);
        x0 = r[0]; y0 = r[1]; x1 = r[2]; y1 = r[3];
    }
    s_rq[s_nrq].x0 = (int16_t)x0; s_rq[s_nrq].y0 = (int16_t)y0;
    s_rq[s_nrq].x1 = (int16_t)x1; s_rq[s_nrq].y1 = (int16_t)y1;
    s_rq[s_nrq].col = (uint8_t)col; s_rq[s_nrq].soft = (uint8_t)(soft != 0);
    s_nrq++;
}

/* Review-13a round 2: a cell's pattern depends only on the touching rectangles' edges relative to the cell (clamped:
 * beyond 4 px outside or 12 px in, an edge no longer changes it), their colours and order, and the solid below. A key
 * over those finds the char without building and hashing the pattern -- an edge's cells along a row or column all
 * share one. Measured on Mednafen: an overlay frame recomputed 184 cells (66 partial) in ~11 ms before this. The memo
 * holds cache indices, so it is cleared whenever the cache frees slots (rects_end). */
#define V2_MEMO 64u
typedef struct { uint32_t key; uint16_t idx, ok; } v2memo;
static v2memo s_memo[V2_MEMO];

static unsigned memo_put(uint32_t key, uint32_t idx)
{
    v2memo *m = &s_memo[(key ^ (key >> 16)) & (V2_MEMO - 1u)];
    m->key = key; m->idx = (uint16_t)idx; m->ok = 1;
    return V2_DYN0 + idx;
}

static int clampo(int d) { return d < -4 ? -4 : d > 12 ? 12 : d; }

static unsigned cell_char(int cx, int cy)
{   /* measured (Mednafen): a per-pixel version of this cost up to 87 ms a frame; rows are filled as spans,
     * a soft rectangle (quarters of coverage: 4 inside, then 3, 2, 1 from 1 px inside to 2 px outside, 2x2 ordered
     * dither) is per pixel, and only rectangles touching the cell are visited */
    static const uint8_t bayer[2][2] = { { 0, 2 }, { 3, 1 } };
    int px0 = cx * 8, py0 = cy * 8, k, partial = 0, x, y, nt = 0;
    unsigned solid = 0;
    uint8_t pat[32], v[8], touch[V2_RECTS];
    uint32_t h, i;
    for (k = 0; k < s_nrq; k++) {
        const v2rect *r = &s_rq[k];
        int g = r->soft ? 2 : 0, in = r->soft ? 1 : 0;
        if (r->x1 < r->x0)
            continue;                                              /* absent */
        if (px0 + 7 < r->x0 - g || px0 > r->x1 + g || py0 + 7 < r->y0 - g || py0 > r->y1 + g)
            continue;                                              /* untouched */
        if (px0 >= r->x0 + in && px0 + 7 <= r->x1 - in && py0 >= r->y0 + in && py0 + 7 <= r->y1 - in) {
            solid = r->col;                                        /* fully covered: later rectangles win */
            partial = 0;
            nt = 0;
        } else {
            partial = 1;
            touch[nt++] = (uint8_t)k;
        }
    }
    if (!partial)
        return solid;
    for (h = 2166136261u ^ solid, i = 0; i < (uint32_t)nt; i++) {
        const v2rect *r = &s_rq[touch[i]];
        h = (h ^ (uint32_t)((r->col << 1) | r->soft)) * 16777619u;
        h = (h ^ (uint32_t)(clampo(r->x0 - px0) + 4)) * 16777619u;
        h = (h ^ (uint32_t)(clampo(r->x1 - px0) + 4)) * 16777619u;
        h = (h ^ (uint32_t)(clampo(r->y0 - py0) + 4)) * 16777619u;
        h = (h ^ (uint32_t)(clampo(r->y1 - py0) + 4)) * 16777619u;
    }
    {
        v2memo *m = &s_memo[(h ^ (h >> 16)) & (V2_MEMO - 1u)];
        if (m->ok && m->key == h)
            return V2_DYN0 + m->idx;
        k = (int)h;                                                /* the key, stored below */
    }
    for (y = 0; y < 8; y++) {
        int py = py0 + y;
        for (x = 0; x < 8; x++)
            v[x] = (uint8_t)solid;
        for (i = 0; i < (uint32_t)nt; i++) {
            const v2rect *r = &s_rq[touch[i]];
            if (!r->soft) {
                int xa = r->x0 - px0, xb = r->x1 - px0;
                if (py < r->y0 || py > r->y1)
                    continue;
                if (xa < 0) xa = 0;
                if (xb > 7) xb = 7;
                for (x = xa; x <= xb; x++)
                    v[x] = r->col;
            } else {                                               /* soft: d = min(x distance, y distance) */
                int dy = py - r->y0, e = r->y1 - py;
                if (e < dy) dy = e;
                if (dy <= -3)
                    continue;
                for (x = 0; x < 8; x++) {
                    int d = px0 + x - r->x0, a;
                    if ((e = r->x1 - px0 - x) < d) d = e;
                    if (dy < d) d = dy;
                    a = d >= 1 ? 4 : d <= -3 ? 0 : d + 3;
                    if (a > bayer[y & 1][x & 1])
                        v[x] = r->col;
                }
            }
        }
        for (x = 0; x < 8; x += 2)
            pat[y * 4 + x / 2] = (uint8_t)((v[x] << 4) | v[x + 1]);
    }
    for (h = 2166136261u, i = 0; i < 32; i++)
        h = (h ^ pat[i]) * 16777619u;
    for (i = 0; i < s_ndyn; i++)
        if (s_dyn_hash[i] == h && LIVE(i))                         /* hash only (saves 7,680 B) */
            break;
    if (i < s_ndyn)
        return memo_put((uint32_t)k, i);
    if (s_ndyn < V2_DYN_N)
        i = s_ndyn++;
    else {
        for (i = 0; i < V2_DYN_N && LIVE(i); i++)
            ;
        if (i >= V2_DYN_N)
            return solid;                                          /* cache full this frame: fall back to the solid */
    }
    s_dyn_hash[i] = h;
    s_dyn_live[i >> 5] |= 1u << (i & 31u);
    s_nlive++;
    char_put(V2_DYN0 + i, pat);                                    /* a free slot: no shown cell uses it */
    return memo_put((uint32_t)k, i);
}

static uint32_t s_dirty[V2_H][2];                                  /* a bit a cell (32-bit: no libgcc shifts) */
static int s_dirty_any, s_nobudget;
#define V2_CELL_BUDGET 8                                           /* cells recomputed a frame (review-13a fix-plan 6) */
#define V2_RECT_OVERLAY 3          /* the view's overlay panel slot: its cells are never budgeted */

#define V2_RECT_SHADOW 2           /* the cover's drop shadow slot: never budgeted either (hw-flicker-report.md) */

static int overlay_cell(int cx, int cy)
{   /* under the overlay slot now or at the last end: a sliding panel must arrive and leave with its VDP1 text (a
     * 12 px step touches ~2 cell columns, span fills, cheap). 2026-11-19 (real-hardware flicker while scrolling): the
     * same for the shadow slot, whose 2 px soft margin counts. Budgeted, a tall <-> square cover swap re-did ~173 cells
     * at 8 a frame (~22 frames): the old shadow stayed up as a dark band beside the new cover, and held scrolling
     * through mixed shapes kept NBG2 behind the VDP1 cover for 93 frames in a row (measured on Mednafen, diag page 2) */
    int k, s, px0 = cx * 8, py0 = cy * 8;
    for (s = V2_RECT_SHADOW; s <= V2_RECT_OVERLAY; s++)
        for (k = 0; k < 2; k++) {
            const v2rect *r = k ? &s_rq_shown[s] : &s_rq[s];
            int g;
            if ((k ? s_nrq_shown : s_nrq) <= s || r->x1 < r->x0) continue;
            g = r->soft ? 2 : 0;
            if (!(px0 + 7 < r->x0 - g || px0 > r->x1 + g || py0 + 7 < r->y0 - g || py0 > r->y1 + g)) return 1;
        }
    return 0;
}

typedef struct { int c0, c1, r0, r1, i0, i1, j0, j1; } v2span;   /* touched cells; interior cells (may be empty) */

static void span_of(const v2rect *r, v2span *q)
{
    int g = r->soft ? 2 : 0, in = r->soft ? 1 : 0;
    q->c0 = (r->x0 - g) >> 3; q->c1 = (r->x1 + g) >> 3; q->r0 = (r->y0 - g) >> 3; q->r1 = (r->y1 + g) >> 3;
    q->i0 = (r->x0 + in + 7) >> 3; q->i1 = ((r->x1 - in + 1) >> 3) - 1;           /* cells wholly inside */
    q->j0 = (r->y0 + in + 7) >> 3; q->j1 = ((r->y1 - in + 1) >> 3) - 1;
    if (q->c0 < 0) q->c0 = 0;
    if (q->r0 < 0) q->r0 = 0;
    if (q->c1 > V2_W - 1) q->c1 = V2_W - 1;
    if (q->r1 > V2_H - 1) q->r1 = V2_H - 1;
}

static void mark(const v2rect *o, const v2rect *n)
{   /* review-13a fix-plan 6: the symmetric difference of a moved rectangle -- cells either one touches, except cells in
     * the solid interior of both (same colour): a slide recomputes its moving edges, not the whole union box. Integer
     * cell ranges only (measured: a per-cell test cost ~3 ms a change on Mednafen) */
    v2span so, sn, *q[2];
    int k, i, j, both;
    if (o && o->x1 < o->x0) o = 0;                                 /* absent rectangles touch nothing */
    if (n && n->x1 < n->x0) n = 0;
    both = o && n && o->col == n->col && o->soft == n->soft;
    if (o) span_of(o, &so);
    if (n) span_of(n, &sn);
    q[0] = o ? &so : 0; q[1] = n ? &sn : 0;
    for (k = 0; k < 2; k++) {
        if (!q[k]) continue;
        for (j = q[k]->r0; j <= q[k]->r1; j++) {
            int rowin = both && j >= so.j0 && j <= so.j1 && j >= sn.j0 && j <= sn.j1;
            int ia = so.i0 > sn.i0 ? so.i0 : sn.i0, ib = so.i1 < sn.i1 ? so.i1 : sn.i1;
            for (i = q[k]->c0; i <= q[k]->c1; i++) {
                if (rowin && i >= ia && i <= ib)
                    continue;
                s_dirty[j][i >> 5] |= 1u << (i & 31);
            }
        }
    }
}

void om_v2_rects_end(void)
{
    int i, j, n = s_nrq > s_nrq_shown ? s_nrq : s_nrq_shown, all = 0;
    uint32_t q = 0;
    int budget = V2_CELL_BUDGET;
    if (!s_dirty_any && s_nrq == s_nrq_shown && !memcmp(s_rq, s_rq_shown, sizeof s_rq[0] * (uint32_t)s_nrq))
        return;
    if (s_pend_ready) {                                            /* the hook has not run since the last end */
        s_pend_ready = 0;
        pend_flush();
        s_st.cpu_flush++;
    }
    if (s_nlive > V2_DYN_N - 64u) {
        /* keep room: free the chars no shown cell uses. The map equals s_shown here (the pending
         * list was flushed above), so a freed char is on no screen cell and may be overwritten. Restarting the cache
         * instead redrew all 1,120 cells in one frame (~11 ms measured on Mednafen: a dropped frame) */
        memset(s_dyn_live, 0, sizeof s_dyn_live);
        memset(s_memo, 0, sizeof s_memo);
        s_nlive = 0;
        for (j = 0; j < V2_H; j++)
            for (i = 0; i < V2_W; i++) {
                uint32_t d = (uint32_t)s_shown[j][i] - V2_DYN0;
                if (s_shown[j][i] != 0xFFFFu && d < V2_DYN_N && !LIVE(d)) {
                    s_dyn_live[d >> 5] |= 1u << (d & 31u);
                    s_nlive++;
                }
            }
    }
    if (s_nrq_shown < 0)
        all = 1;
    if (!all)
        for (i = 0; i < n; i++) {
            int had = i < s_nrq_shown, has = i < s_nrq;
            if (had && has && !memcmp(&s_rq[i], &s_rq_shown[i], sizeof s_rq[0]))
                continue;
            mark(had ? &s_rq_shown[i] : 0, has ? &s_rq[i] : 0);
        }
    s_dirty_any = 0;
    /* walk the dirty bits with 1-bit shifts; SH-2 has no barrel shifter, so a variable shift is a
     * libgcc call */
    for (j = 0; j < V2_H; j++) {
        int hh;
        for (hh = 0; hh < 2; hh++) {
            uint32_t w = all ? (hh ? 0xFFu : 0xFFFFFFFFu) : s_dirty[j][hh], keep = 0, m = 1;
            for (i = 32 * hh; w; i++, w >>= 1, m <<= 1) {
                unsigned c;
                if (!(w & 1u))
                    continue;
                if (!all && !s_nobudget && !overlay_cell(i, j) && budget-- <= 0) {   /* the rest next frame */
                    keep |= m;
                    s_dirty_any = 1;
                    continue;
                }
                c = cell_char(i, j);
                if (c != s_shown[j][i]) {
                    if (q < PEND_MAX) {
                        s_pend_idx[q] = (uint16_t)(j * 64 + i);
                        s_pend_val[q] = (uint16_t)((OM_V2_N2PAL << 12) | (c & 0xFFFu));   /* 1-word PN (HWF-09) */
                        q++;
                    } else {
                        map_put(i, j, c);                          /* a full redraw (first frame) */
                        s_st.direct++;
                    }
                    s_shown[j][i] = (uint16_t)c;
                }
            }
            if (!all)
                s_dirty[j][hh] = keep;
        }
    }
    if (all)
        memset(s_dirty, 0, sizeof s_dirty);
    if (s_dirty_any) {                                             /* the instrument: frames NBG2 is still behind */
        s_st.carry++;
        if (++s_run > s_st.carry_run) s_st.carry_run = s_run;
    } else
        s_run = 0;
    memcpy(s_rq_shown, s_rq, sizeof s_rq);
    s_nrq_shown = s_nrq;
    s_pend_n = q;
    s_pend_ready = q ? 1u : 0u;                                    /* the V-blank hook writes them */
}

void om_v2_init(void)
{
    uint8_t pat[32];
    uint32_t c, i, j;
    for (c = 0; c < V2_DYN0; c++) {                            /* chars 0-15: solid colour c; 0 is transparent */
        for (i = 0; i < 32; i++)
            pat[i] = (uint8_t)(c * 0x11u);                     /* 4 bpp, high nibble = left pixel (HWF-05) */
        char_put(c, pat);
    }
    for (j = 0; j < 64; j++)                                   /* the whole 64x64 page clear */
        for (i = 0; i < 64; i++)
            map_put((int)i, (int)j, 0);
    for (j = 0; j < V2_H; j++)
        for (i = 0; i < V2_W; i++)
            s_shown[j][i] = 0;
    slColRAMMode(CRM16_1024);                                  /* HWF-30 */
    slBitMapNbg1(COL_TYPE_256, BM_512x256, (void *)OM_V2_BMP); /* HWF-28 */
    slBMPaletteNbg1(OM_V2_BMP_PAL);                            /* HWF-30 */
    slCharNbg2(COL_TYPE_16, CHAR_SIZE_1x1);
    /* coladr 0: no supplementary palette bits, so the palette is PN bits 15-12 = OM_V2_N2PAL (HWF-41) */
    slPageNbg2((void *)OM_V2_N2CEL, 0, PNB_1WORD | CN_12BIT);
    slPlaneNbg2(PL_SIZE_1x1);
    slMapNbg2((void *)OM_V2_N2MAP, (void *)OM_V2_N2MAP, (void *)OM_V2_N2MAP, (void *)OM_V2_N2MAP);
    slScrPosNbg1(toFIXED(0.0), toFIXED(0.0));
    slScrPosNbg2(toFIXED(0.0), toFIXED(0.0));
    slPriorityNbg0(7);                                         /* text over everything */
    slPrioritySpr0(6);                                         /* covers and the logo over panels */
    slPriorityNbg2(5);                                         /* panels over the background */
    slPriorityNbg1(4);
    slScrCycleSet(OM_V2_CYC_A0, OM_V2_CYC_A1, OM_V2_CYC_B0, OM_V2_CYC_B1);   /* HWF-29; not slScrAutoDisp (gotcha #7) */
    slColorCalc(CC_RATE | CC_TOP | NBG2ON);                    /* HWF-31: CCRTMD=0, ratio from the top screen (NBG2) */
    slColorCalcOn(NBG2ON);                                     /* HWF-31: N2CCEN (ST-238's slColorCalc/On split) */
    /* slScrDisp only sets the ON bits and clears the OFF bits it is given, and SGL boots with RBG0 on (HWF-41), so
     * RBG0OFF must be explicit or RBG0's RDBS claim on A0/B0 hides NBG1/NBG2. SPRON is not a BGON field. */
    slScrDisp(NBG0ON | NBG1ON | NBG2ON | NBG3OFF | RBG0OFF);
}

static om_theme s_theme;
const om_theme *om_v2_theme(void) { return &s_theme; }

void om_v2_apply_colors(const om_theme *t)
{
    s_theme = *t;
    *cram(OM_CRAM_TEXT) = t->text;                             /* SGL font dots are {0,1} (HWF-30) */
    *cram(OM_CRAM_TEXT_HI) = t->text_hi;                       /* slCurColor(OM_TEXT_HI_PAL), see vdp2theme.h */
    *cram(16u * OM_V2_N2PAL + OM_V2_COL_PANEL) = t->panel;     /* CRAM 33 */
    *cram(16u * OM_V2_N2PAL + OM_V2_COL_CURSOR) = t->cursor;   /* CRAM 34 */
    *cram(16u * OM_V2_N2PAL + OM_V2_COL_SHADOW) = 0x0000;      /* CRAM 35: the cover's drop shadow, black at the ratio */
    slColRateNbg2(om_cc_ratio(t->panel_alpha));                /* HWF-31 */
    /* ratio 0 is still 31:1, so a 100 % panel turns NBG2's colour calculation off to be truly opaque.
     * slColorCalcOn writes the whole per-screen enable byte of CCCTL (LIBSGL sglB021.o); NBG2 is the only CC screen. */
    slColorCalcOn(t->panel_alpha >= 100 ? 0 : NBG2ON);
}

static void fill_rows(const uint8_t *idx_of_row, int per_row_index)
{
    volatile Uint16 *bmp = (volatile Uint16 *)OM_V2_BMP;
    uint32_t y, x;
    for (y = 0; y < OM_BG_H; y++) {
        const uint8_t *row = per_row_index ? 0 : idx_of_row + y * OM_BG_W;
        for (x = 0; x < OM_BG_W; x += 2) {
            uint8_t a = row ? row[x] : idx_of_row[y], b = row ? row[x + 1] : idx_of_row[y];
            bmp[y * 256u + x / 2u] = (Uint16)((a << 8) | b);    /* 512-byte stride; left pixel = high byte (HWF-28) */
        }
    }
}

void om_v2_bg_gradient(uint16_t top, uint16_t bottom)
{   /* a dark gradient spans only a few RGB555 steps (e.g. blue 5 -> 1), which showed as hard bands (one
     * near y 113). Each pixel now takes a 4x4 ordered dither between the two nearest steps of each channel, in 1/16
     * steps; the few distinct colours that result become the bitmap's palette (HWF-28, HWF-30). */
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    volatile Uint16 *bmp = (volatile Uint16 *)OM_V2_BMP;
    uint16_t cols[256] = { 0 };
    uint32_t npal = 1, x, y, k;                                 /* index 0 is NBG1's transparent code: never a colour (it showed the black back screen as dark lines across the top rows) */
    uint8_t row[4];
    for (y = 0; y < OM_BG_H; y++) {
        uint32_t c16[3];
        if (!(y & 15u)) om_yield();       /* the start-up screen's frames (yield.h); a new theme's is dipped to black */
        for (k = 0; k < 3; k++) {                                  /* channel value in 1/16 steps */
            uint32_t a = (top >> (5 * k)) & 31u, b = (bottom >> (5 * k)) & 31u;
            c16[k] = (a * 16u * (OM_BG_H - 1u - y) + b * 16u * y) / (OM_BG_H - 1u);
        }
        {   /* the row's 8 possible colours (each channel at its step or one above), looked up once */
            uint8_t idx[8];
            uint32_t m;
            for (m = 0; m < 8; m++) {
                uint32_t c = 0, i, v;
                for (k = 0; k < 3; k++) {
                    v = (c16[k] >> 4) + ((m >> k) & 1u);
                    c |= (v > 31u ? 31u : v) << (5 * k);
                }
                for (i = 1; i < npal && cols[i] != c; i++) ;
                if (i == npal && npal < 256)
                    cols[npal++] = (uint16_t)c;
                idx[m] = (uint8_t)(i < 256 ? i : 255);
            }
            for (x = 0; x < 4; x++) {                              /* the dither repeats every 4 px */
                uint32_t t = bayer[y & 3][x], m2 = 0;
                for (k = 0; k < 3; k++)
                    if ((c16[k] & 15u) > t) m2 |= 1u << k;
                row[x] = idx[m2];
            }
        }
        {
            Uint16 w0 = (Uint16)((row[0] << 8) | row[1]), w1 = (Uint16)((row[2] << 8) | row[3]);
            for (x = 0; x < OM_BG_W / 2u; x += 2) {                /* 512-byte stride, left pixel high (HWF-28) */
                bmp[y * 256u + x] = w0;
                bmp[y * 256u + x + 1u] = w1;
            }
        }
    }
    for (k = 0; k < npal; k++)
        *cram(256u * OM_V2_BMP_PAL + k) = cols[k];
}

uint16_t om_v2_bg_at(int x, int y)
{   /* the NBG1 bitmap's colour at (x, y): 8 bpp index (512-byte stride, HWF-28) -> palette 1 (HWF-30). Under a
     * safe-area transform (M6 Task 5) (x, y) is a design point: the pixel actually behind it on screen is sampled. */
    volatile Uint16 *bmp = (volatile Uint16 *)OM_V2_BMP;
    Uint16 w;
    if (s_safe) {
        x = om_safe_xu(s_safe, x);
        y = om_safe_yu(s_safe, y);
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)OM_BG_W) x = (int)OM_BG_W - 1;
    if (y >= (int)OM_BG_H) y = (int)OM_BG_H - 1;
    w = bmp[(uint32_t)y * 256u + (uint32_t)x / 2u];
    return *cram(256u * OM_V2_BMP_PAL + ((x & 1) ? (w & 0xFFu) : (w >> 8)));
}

int om_v2_bg_load(const uint8_t *buf, uint32_t len)
{
    om_bg b;
    uint32_t i;
    int rc = om_bg_open(&b, buf, len);
    if (rc)
        return rc;
    for (i = 0; i < 256; i++)
        *cram(256u * OM_V2_BMP_PAL + i) = om_bg_color(&b, i);
    fill_rows(b.pix, 0);
    return 0;
}

void om_v2_rects_flush(void)
{   /* what the per-frame cell budget left, all at once (a screen change while the display is faded to black:
     * the loading screen clears the menu's panels, the menu redraws them on return; ~420 cells, else ~50 frames) */
    if (!s_dirty_any)
        return;
    s_nobudget = 1;
    om_v2_rects_end();
    s_nobudget = 0;
}
