/* menu/pop/pgeom.c -- see pgeom.h. BSD-3-Clause. */
#include <string.h>
#include "pgeom.h"

const om_pbox om_pd_box[PB_N] = {
    [PB_PANEL]   = {  12,  12, 307, 211 },
    [PB_HDR]     = {  13,  13, 306,  50 },
    [PB_COVER]   = {  18,  16,  41,  47 },
    [PB_LABEL]   = {  48,  16, 131,  29 },
    [PB_TITLE]   = {  48,  31, 300,  48 },
    [PB_METER_T] = { 136,  16, 257,  29 },
    [PB_METER_B] = { 262,  21, 301,  24 },
    [PB_TABS]    = {  16,  53, 303,  65 },
    [PB_LIST_S]  = {  13,  68, 186, 193 },
    [PB_LIST_C]  = {  13,  53, 186, 192 },
    [PB_SIDE_S]  = { 192,  68, 306, 193 },
    [PB_SIDE_C]  = { 192,  53, 306, 192 },
    [PB_FOOT]    = {  13, 196, 306, 210 },
};

static const om_pd_hint k_cheat_list[4] = { { &om_icon_badge_a, "On / off" }, { &om_icon_badge_x, "Add" },
                                            { &om_icon_badge_y, "Edit codes" }, { &om_icon_badge_z, "Delete" } };
static const om_pd_hint k_cheat_codes[4] = { { &om_icon_badge_a, "Edit" }, { &om_icon_badge_x, "Add code" },
                                             { &om_icon_badge_y, "Rename" }, { &om_icon_badge_z, "Delete" } };
static const om_pd_hint k_next[1] = { { &om_icon_badge_a, "Next" } };
static const om_pd_hint k_done[1] = { { &om_icon_badge_a, "Done" } };
static const om_pd_hint k_save_list[2] = { { 0, "Left/Right: tabs" }, { &om_icon_badge_a, "Options" } };
static const om_pd_hint k_save_sets[2] = { { 0, "Left/Right: tabs" }, { &om_icon_badge_a, "Open" } };
static const om_pd_hint k_choose[1] = { { &om_icon_badge_a, "Choose" } };
static const om_pd_hint k_open[1] = { { &om_icon_badge_a, "Open" } };
static const om_pd_hint k_about[2] = { { 0, "Up/Down: scroll" }, { 0, "Left/Right: page" } };

const om_pd_footer om_pd_footers[PF_N] = {
    [PF_CHEAT_LIST]      = { k_cheat_list, 4, { &om_icon_badge_b, "Done" } },
    [PF_CHEAT_LIST_SAVE] = { k_cheat_list, 4, { &om_icon_badge_b, "Save" } },
    [PF_CHEAT_CODES]     = { k_cheat_codes, 4, { &om_icon_badge_b, "Back" } },
    [PF_CHEAT_NAME]      = { k_next, 1, { &om_icon_badge_b, "Cancel" } },
    [PF_CHEAT_CODE]      = { k_done, 1, { &om_icon_badge_b, "Cancel" } },
    [PF_SAVE_LIST]       = { k_save_list, 2, { &om_icon_badge_b, "Done" } },
    [PF_SAVE_LIST_B]     = { k_save_list, 2, { &om_icon_badge_b, "Back" } },
    [PF_SAVE_SETS]       = { k_save_sets, 2, { &om_icon_badge_b, "Done" } },
    [PF_SAVE_SETS_B]     = { k_save_sets, 2, { &om_icon_badge_b, "Back" } },
    [PF_SAVE_ACT]        = { k_choose, 1, { &om_icon_badge_b, "Back" } },
    [PF_SAVE_GAMES]      = { k_open, 1, { &om_icon_badge_b, "Done" } },
    [PF_ABOUT]           = { k_about, 2, { &om_icon_badge_b, "Done" } },
};

int om_pd_footer_w(const om_pd_footer *f)
{
    int w = 8, i;
    for (i = 0; i <= f->n; i++) {
        const om_pd_hint *h = i < f->n ? &f->left[i] : &f->right;
        if (h->ic) w += h->ic->w + 3;
        w += om_uf_width(&om_face_small, h->label);
        if (i < f->n) w += 9;
    }
    return w;
}

static void put_u(char **o, uint32_t v)
{
    char d[12];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *(*o)++ = d[--k];
}

static int leap(uint32_t y) { return y % 4u == 0 && (y % 100u || y % 400u == 0); }

void om_pd_bup_date(uint32_t m, char out[12])
{   /* minutes since 1980-01-01 00:00 (ST-162-R1 BupDate) */
    static const uint8_t dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t days = m / 1440u, y = 1980, mo, hm = m % 1440u;
    while (days >= 365u + (uint32_t)leap(y)) { days -= 365u + (uint32_t)leap(y); y++; }
    for (mo = 0; mo < 11u; mo++) {
        uint32_t d = dm[mo] + (mo == 1u && leap(y));
        if (days < d) break;
        days -= d;
    }
    out[0] = (char)('0' + (mo + 1u) / 10u); out[1] = (char)('0' + (mo + 1u) % 10u); out[2] = '/';
    out[3] = (char)('0' + (days + 1u) / 10u); out[4] = (char)('0' + (days + 1u) % 10u); out[5] = ' ';
    out[6] = (char)('0' + hm / 600u); out[7] = (char)('0' + hm / 60u % 10u); out[8] = ':';
    out[9] = (char)('0' + hm % 60u / 10u); out[10] = (char)('0' + hm % 10u); out[11] = 0;
}

void om_pd_size(const om_sv_item *x, char out[12])
{
    char *o = out;
    if (x->d.blocks) { put_u(&o, x->d.blocks); strcpy(o, " blk"); }
    else { put_u(&o, (x->d.datasize + 1023u) / 1024u); strcpy(o, " KB"); }
}
