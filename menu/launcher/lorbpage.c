/* menu/launcher/lorbpage.c -- see lorb.h: the orbiting logo's art (BIGLOGO.BIN, the interstitial's, blended over the
 * eye frame) and the step-9 stash, and the save import's pages on it. .lui (High Work RAM): never after step 9.
 * BSD-3-Clause. */
#include <string.h>
#include "biglogo.h"
#include "lgap.h"
#include "lgfx.h"
#include "lorb.h"
#include "lorbpage.h"
#include "om_fmt.h"
#include "omlayout.h"

__asm__(".section .lui.biglogo,\"a\"\n.align 2\n_s_blg: .incbin \"menu/app/assets/BIGLOGO.BIN\"\n_s_blg_end:\n.text\n");
extern const uint8_t s_blg[], s_blg_end[];

#define VRAM16   ((volatile uint16_t *)0x25E00000u)
#define VRAM8    ((volatile uint8_t *)0x25E00000u)
#define CRAM16   ((volatile uint16_t *)0x25F00000u)
#define LW       OM_BIGLOGO_W
#define LH       OM_BIGLOGO_H
#define SCRATCH  ((uint8_t *)OM_LG_FREE)                         /* the decode's indices, 15,352 B */
#define BLEND    ((volatile uint16_t *)OM_CT(OM_LG_FREE + 0x4000u))   /* the blended logo, 30,704 B */
#define ST_LOGO  16u
#define ST_FACE  (ST_LOGO + 76u * 50u * 2u)

static uint16_t s_bg;
static uint16_t backdrop(void *ctx, int x, int y) { (void)ctx; (void)x; (void)y; return s_bg; }

uint16_t om_orbp_bg(int compact)
{   /* every 4th pixel of the frame shown on NBG1 (8 bpp, 512 B a row, palette 1 = CRAM 256-511) */
    uint32_t bank = om_lg_disp ? 0x60000u : 0x40000u, r = 0, gg = 0, b = 0, n = 0;
    int x, y, x0 = compact ? 100 : 56, x1 = compact ? 220 : 264, y0 = compact ? 12 : 46, y1 = compact ? 62 : 146;
    for (y = y0; y <= y1; y += 4)
        for (x = x0; x <= x1; x += 4) {
            uint16_t c = CRAM16[256 + VRAM8[bank + (uint32_t)y * 512u + (uint32_t)x]];
            r += c & 31u; gg += (c >> 5) & 31u; b += (c >> 10) & 31u; n++;
        }
    r /= n; gg /= n; b /= n;                                   /* as shown: dimmed (lorb.h OM_ORB_DIM) */
    r = r > OM_ORB_DIM / 8 ? r - OM_ORB_DIM / 8 : 0;
    gg = gg > OM_ORB_DIM / 8 ? gg - OM_ORB_DIM / 8 : 0;
    b = b > OM_ORB_DIM / 8 ? b - OM_ORB_DIM / 8 : 0;
    return (uint16_t)(0x8000u | (b << 10) | (gg << 5) | r);
}

static void vput(uint32_t off, const void *src, uint32_t n)
{   /* 16-bit writes, as everywhere in VDP2 VRAM here */
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;
    for (i = 0; i < n; i += 2) VRAM16[(off + i) / 2u] = (uint16_t)((p[i] << 8) | p[i + 1]);
}

/* the logo blended over bg, halved (2x2, transparent counted as bg) into the stash; full: also whole into the hidden
 * columns (the import's full-size scene) */
static int logo(uint16_t bg, int full)
{
    const uint8_t *tbl;
    uint32_t tlen, x, y;
    int rc;
    s_bg = bg;
    if ((rc = om_biglogo_open(s_blg, (uint32_t)(s_blg_end - s_blg), &tbl, &tlen)) != 0 ||
        (rc = om_biglogo_blend(tbl, tlen, SCRATCH, LW * LH, 0, 0, backdrop, 0, BLEND)) != 0)
        return rc;
    for (y = 0; full && y < LH; y++)
        for (x = 0; x < LW; x++) VRAM16[y * 512u + OM_ORB_LOGO_X + x] = BLEND[y * LW + x];
    for (y = 0; y < LH / 2u; y++)
        for (x = 0; x < LW / 2u; x++) {
            uint32_t r = 0, g = 0, b = 0, k, op = 0;
            for (k = 0; k < 4; k++) {
                uint16_t c = BLEND[(2u * y + (k >> 1)) * LW + 2u * x + (k & 1u)];
                if (c & 0x8000u) op = 1; else c = bg;
                r += c & 31u; g += (c >> 5) & 31u; b += (c >> 10) & 31u;
            }
            VRAM16[(OM_ORB_STASH + ST_LOGO) / 2u + y * (LW / 2u) + x] =
                op ? (uint16_t)(0x8000u | ((b / 4u) << 10) | ((g / 4u) << 5) | (r / 4u)) : 0;
        }
    return 0;
}

int om_orbp_stash(uint16_t bg)
{   /* the half logo, then the three faces (each om_face, its glyphs and its bits, pointers made VRAM ones), the CRC */
    static const int faces[OM_ORB_NFACE] = { OM_FACE_LABEL, OM_FACE_ITEM, OM_FACE_SMALL };
    uint32_t off = ST_FACE + OM_ORB_NFACE * sizeof(om_face), i, k;
    int rc;
    if ((rc = logo(bg, 0)) != 0) return rc;
    for (i = 0; i < OM_ORB_NFACE; i++) {
        const om_face *f = om_face_get(faces[i]);
        om_face c = *f;
        uint32_t nb = 0, need;
        for (k = 0; k < OM_UF_NGLYPH; k++) {
            uint32_t e = f->g[k].off + (uint32_t)((f->g[k].w + 1) / 2) * f->g[k].h;
            if (e > nb) nb = e;
        }
        need = OM_UF_NGLYPH * sizeof(om_glyph) + ((nb + 3u) & ~3u);
        if (off + need > 0x8000u) return OM_E_FULL;            /* never past rows 224-255 (B0 follows) */
        c.g = (const om_glyph *)(0x25E00000u + OM_ORB_STASH + off);
        vput(OM_ORB_STASH + off, f->g, OM_UF_NGLYPH * sizeof(om_glyph));
        off += OM_UF_NGLYPH * sizeof(om_glyph);
        c.bits = (const uint8_t *)(0x25E00000u + OM_ORB_STASH + off);
        vput(OM_ORB_STASH + off, f->bits, (nb + 3u) & ~3u);
        off += (nb + 3u) & ~3u;
        vput(OM_ORB_STASH + ST_FACE + i * sizeof(om_face), &c, sizeof c);
    }
    {
        uint32_t h[4] = { 0x4F524231u, off, 0, bg };
        h[2] = om_crc32(0, (const void *)(0x25E00000u + OM_ORB_STASH + ST_LOGO), off - ST_LOGO);
        vput(OM_ORB_STASH, h, sizeof h);
    }
    return 0;
}

/* --- the import ---------------------------------------------------------------------------------------------------- */
static void bar(int x0, int x1, int y, uint32_t num, uint32_t den)
{   /* a ~15 % white track, the accent fill, the corners cut */
    int x, r, fx = x0 + (int)(den ? (uint32_t)(x1 - x0) * (num > den ? den : num) / den : 0);
    uint16_t tc = om_lg_mix(s_bg, 0xFFFFu, 40);
    for (r = 0; r < 3; r++)
        for (x = x0; x <= x1; x++)
            VRAM16[(y + r) * 512 + x] = (r != 1 && (x == x0 || x == x1)) ? 0 : x <= fx && fx > x0 ? om_lg_c.acc : tc;
}

#define FL OM_ORB_LABEL
static const om_face *face(int i)
{
    static const int k[OM_ORB_NFACE] = { OM_FACE_LABEL, OM_FACE_ITEM, OM_FACE_SMALL };
    return om_face_get(k[i]);
}
static struct { uint32_t game, saves, kb, bar, at; char name[24], file[20]; uint8_t cancel, on; } s_ip;

static char *dec(char *o, uint32_t v)
{
    char d[10];
    int n = 0;
    do { d[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *o++ = d[--n];
    *o = 0;
    return o;
}

int om_orbp_import_begin(void)
{   /* the menu's "LOADING YOUR LIBRARY" screen: the logo at its place, the label, two lines, the bar, the hint */
    uint16_t bg = om_orbp_bg(OM_ORB_FULL);
    if (logo(bg, 1) != 0) return -1;
    om_orb_scene(OM_ORB_FULL, bg);
    om_orb_text(face(FL), 16, 303, 159, "IMPORTING SAVES", om_lg_c.acc2, OM_ORB_CENTER | OM_ORB_TRACK);
    om_orb_text(face(OM_ORB_SMALL), 16, 303, 214, "Hold B to cancel", om_lg_c.dim, OM_ORB_CENTER | OM_ORB_SOLID);
    memset(&s_ip, 0xFF, sizeof s_ip);
    s_ip.name[0] = s_ip.file[0] = 0;
    s_ip.cancel = 0;
    s_ip.on = 1;
    return 0;
}

void om_orbp_import_prog(const om_pgs_prog *p, int canceling)
{   /* what changed is drawn again, each line in one pass; the comet moves on every call */
    char l[64], *o;
    uint32_t g = p->games_done < p->games_total ? p->games_done + 1u : p->games_done, kb = p->file_done / 1024u, pc;
    if (!s_ip.on) return;
    if (g != s_ip.game || strcmp(p->game, s_ip.name)) {
        o = l;
        memcpy(o, "Game ", 5); o = dec(o + 5, g);
        memcpy(o, " of ", 4); o = dec(o + 4, p->games_total);
        if (p->game[0]) { memcpy(o, "  \xC2\xB7  ", 6); strcpy(o + 6, p->game); }
        om_orb_text(face(OM_ORB_ITEM), 16, 303, 176, l, om_lg_c.txt, OM_ORB_CENTER | OM_ORB_SOLID);
        s_ip.game = g;
        strcpy(s_ip.name, p->game);
    }
    if (p->saves_done != s_ip.saves || strcmp(p->file, s_ip.file) ||
        (kb != s_ip.kb && om_lclock_us() - s_ip.at >= 100000u)) {   /* the KB alone: 10 times a second at most */
        o = dec(l, p->saves_done);
        strcpy(o, p->saves_done == 1u ? " save handled" : " saves handled");
        if (p->file[0]) {
            o += strlen(o);
            memcpy(o, "  \xC2\xB7  ", 6); o += 6;
            strcpy(o, p->file); o += strlen(o);
            *o++ = ' '; o = dec(o, kb);
            *o++ = '/'; o = dec(o, (p->file_total + 1023u) / 1024u);
            strcpy(o, " KB");
        }
        om_orb_text(face(OM_ORB_SMALL), 16, 303, 192, l, om_lg_c.dim, OM_ORB_CENTER | OM_ORB_SOLID);
        s_ip.saves = p->saves_done;
        s_ip.kb = kb;
        s_ip.at = om_lclock_us();
        strcpy(s_ip.file, p->file);
    }
    pc = p->games_total ? (p->games_done * 100u + (p->file_total ? p->file_done * 100u / p->file_total : 0u)) /
                           p->games_total : 0u;
    if (pc != s_ip.bar) {
        bar(110, 210, 199, pc, 100);
        s_ip.bar = pc;
    }
    if (canceling && !s_ip.cancel) {
        om_orb_text(face(OM_ORB_SMALL), 16, 303, 214, "Canceling at the next safe point...", om_lg_c.err,
                    OM_ORB_CENTER | OM_ORB_SOLID);
        s_ip.cancel = 1;
    }
    om_orb_tick();
}

void om_orbp_import_result(const om_pgs_imp *r, const char *note, const char *first_skip)
{   /* a header-size logo, then the counts in one column, as the text screen lists them */
    static const char *const k_lab[8] = { "Copied from Kai", "Copied from Rings", "Already on the card",
                                          "Conflicts (yours kept)", "Older copies to quarantine",
                                          "Skipped (see IMPORT.TXT)", "Cart-only (in CART/)", "Kept as alternates" };
    uint32_t v[8], i;
    char n[12];
    s_ip.on = 0;
    s_bg = om_orbp_bg(OM_ORB_COMPACT);
    (void)logo(s_bg, 0);                                       /* the half logo's edges over this scene's background */
    om_orb_scene(OM_ORB_COMPACT, s_bg);
    om_orb_text(face(FL), 16, 303, 80, r->canceled ? "IMPORT CANCELED" : r->rc ? "IMPORT STOPPED" : "IMPORT DONE",
                r->rc && !r->canceled ? om_lg_c.err : om_lg_c.acc2, OM_ORB_CENTER | OM_ORB_TRACK);
    if (r->canceled)
        om_orb_text(face(OM_ORB_SMALL), 16, 303, 95, "Copies made are kept: run it again to finish", om_lg_c.dim,
                    OM_ORB_CENTER);
    else if (r->rc)
        om_orb_text(face(OM_ORB_SMALL), 16, 303, 95, note, om_lg_c.err, OM_ORB_CENTER);
    v[0] = r->kai; v[1] = r->rings; v[2] = r->present; v[3] = r->conflicts;
    v[4] = r->older; v[5] = r->skipped; v[6] = r->cart; v[7] = r->alternates;
    for (i = 0; i < 8; i++) {
        int base = 108 + 11 * (int)i;
        dec(n, v[i]);
        om_orb_text(face(OM_ORB_SMALL), 64, 214, base, k_lab[i], v[i] ? om_lg_c.txt : om_lg_c.dim, 0);
        om_orb_text(face(OM_ORB_SMALL), 215, 255, base, n, v[i] ? om_lg_c.txt : om_lg_c.dim, OM_ORB_RIGHT);
    }
    if (r->skipped && first_skip && first_skip[0]) {
        char l[48];
        size_t k = strlen(first_skip);
        strcpy(l, "First skipped: ");
        strncat(l, k > 28 ? first_skip + k - 28 : first_skip, 28);
        om_orb_text(face(OM_ORB_SMALL), 16, 303, 199, l, om_lg_c.err, OM_ORB_CENTER);
    }
    om_orb_text(face(OM_ORB_SMALL), 16, 303, 213, "Press a button to return to Aether", om_lg_c.txt, OM_ORB_CENTER);
}
