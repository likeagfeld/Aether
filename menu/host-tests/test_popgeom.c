/* menu/host-tests/test_popgeom.c -- the popups' layout (menu/pop/pgeom.c): every box inside the panel, the header's
 * parts inside the header, no two text boxes overlapping (the user caught overlapping text in a draft of the mockup),
 * the rows filling their lists exactly at the small face's height, and every fixed string at its widest -- the spaced
 * labels with the widest meter beside them, the tabs, the save options (armed too), each footer's hints -- fitting the
 * box it is drawn in; then the date and size formats. BSD-3-Clause. */
#include "th.h"
#include "pgeom.h"
#include "psave.h"

static int inside(const om_pbox *a, const om_pbox *b)
{
    return a->x0 >= b->x0 && a->x1 <= b->x1 && a->y0 >= b->y0 && a->y1 <= b->y1;
}
static int apart(const om_pbox *a, const om_pbox *b)
{
    return a->x1 < b->x0 || b->x1 < a->x0 || a->y1 < b->y0 || b->y1 < a->y0;
}
#define BX(i) (&om_pd_box[i])
static int w(const char *s) { return om_uf_width(&om_face_small, s); }
static int bw(int i) { return BX(i)->x1 - BX(i)->x0 + 1; }

static void disjoint(const int *set, int n)
{
    int i, k;
    for (i = 0; i < n; i++)
        for (k = i + 1; k < n; k++) {
            if (!apart(BX(set[i]), BX(set[k]))) printf("overlap: box %d and box %d\n", set[i], set[k]);
            CHECK(apart(BX(set[i]), BX(set[k])));
        }
}

int main(void)
{
    static const int hdr[] = { PB_COVER, PB_LABEL, PB_TITLE, PB_METER_T, PB_METER_B };
    static const int saves[] = { PB_HDR, PB_TABS, PB_LIST_S, PB_SIDE_S, PB_FOOT };
    static const int cheats[] = { PB_HDR, PB_LIST_C, PB_SIDE_C, PB_FOOT };
    static const char *const tabs[3] = { "Console", "Cartridge", "Card" };
    static const char *const meters[][2] = { { "MANAGE SAVES", "8192 / 8192 blocks" }, { "MANAGE SAVES", "20 save sets" },
                                             { "MANAGE SAVES", "300 saves" }, { "CHEATS", "Code slots 30 / 30" } };
    int i, x;
    char d[12];

    for (i = 1; i < PB_N; i++) CHECK(inside(BX(i), BX(PB_PANEL)));
    for (i = 0; i < 5; i++) CHECK(inside(BX(hdr[i]), BX(PB_HDR)));
    disjoint(hdr, 5);
    disjoint(saves, 5);
    disjoint(cheats, 4);
    CHECK(BX(PB_SIDE_S)->x0 - 3 > BX(PB_LIST_S)->x1);             /* the divider between list and side panel */
    CHECK(BX(PB_SIDE_C)->x0 - 3 > BX(PB_LIST_C)->x1);
    /* rows: the small face's full height, exactly the lists' heights */
    CHECK(OM_PD_ROW_H >= om_uf_height(&om_face_small));
    CHECK_INT(OM_PD_SAVE_ROWS * OM_PD_ROW_H, BX(PB_LIST_S)->y1 - BX(PB_LIST_S)->y0 + 1);
    CHECK_INT(OM_PD_CHEAT_ROWS * OM_PD_ROW_H, BX(PB_LIST_C)->y1 - BX(PB_LIST_C)->y0 + 1);
    CHECK(om_uf_height(&om_face_title) <= BX(PB_TITLE)->y1 - BX(PB_TITLE)->y0 + 1);
    CHECK(om_uf_height(&om_face_small) <= BX(PB_TABS)->y1 - BX(PB_TABS)->y0 + 2);
    CHECK(BX(PB_COVER)->x1 + 4 < BX(PB_LABEL)->x0);                /* the cover (tall or square) inside its box */
    /* the spaced label, and the widest meter right-aligned after it, 6 px apart */
    for (i = 0; i < 4; i++) {
        int lw = om_uf_width_tracked(&om_face_small, meters[i][0], OM_PD_TRACK8);
        CHECK(lw <= bw(PB_LABEL));
        x = BX(PB_LABEL)->x0 + lw + 6;
        if (x < BX(PB_METER_T)->x0) x = BX(PB_METER_T)->x0;
        if (w(meters[i][1]) > BX(PB_METER_T)->x1 - x + 1) printf("meter too wide: %s\n", meters[i][1]);
        CHECK(w(meters[i][1]) <= BX(PB_METER_T)->x1 - x + 1);
    }
    for (x = 0, i = 0; i < 3; i++) x += w(tabs[i]) + 14 + 4;
    CHECK(x <= bw(PB_TABS));
    /* the options, armed or not */
    {
        om_ps p;
        memset(&p, 0, sizeof p);
        p.nact = OM_PSA_N;
        for (i = 0; i < OM_PSA_N; i++) p.acts[i] = (uint8_t)i;
        for (i = 0; i < OM_PSA_N; i++) {
            const char *s = om_ps_act_label(&p, (uint16_t)i);
            if (w(s) > bw(PB_SIDE_S) - OM_PD_SIDE_PAD) printf("option too wide: %s\n", s);
            CHECK(w(s) <= bw(PB_SIDE_S) - OM_PD_SIDE_PAD);
        }
        p.armed = 1;
        for (i = 0; i < OM_PSA_N; i++) {
            p.act = (uint16_t)i;
            CHECK(w(om_ps_act_label(&p, (uint16_t)i)) <= bw(PB_SIDE_S) - OM_PD_SIDE_PAD);
        }
    }
    /* the side panel holds the options page: label, comment, up to 4 options, a 2-line hint */
    CHECK(2 + 8 * OM_PD_ROW_H <= BX(PB_SIDE_S)->y1 - BX(PB_SIDE_S)->y0 + 1);
    for (i = 0; i < PF_N; i++) {
        if (om_pd_footer_w(&om_pd_footers[i]) > bw(PB_FOOT)) printf("footer %d too wide: %d\n", i, om_pd_footer_w(&om_pd_footers[i]));
        CHECK(om_pd_footer_w(&om_pd_footers[i]) <= bw(PB_FOOT));
    }

    om_pd_bup_date(0, d);                        CHECK_STR(d, "01/01 00:00");
    om_pd_bup_date(60u * 1440u, d);              CHECK_STR(d, "03/01 00:00");   /* 1980 is a leap year */
    om_pd_bup_date(366u * 1440u, d);             CHECK_STR(d, "01/01 00:00");
    om_pd_bup_date(7364u * 1440u + 754u, d);     CHECK_STR(d, "02/29 12:34");   /* 2000-02-29 */
    om_pd_bup_date(7365u * 1440u + 1439u, d);    CHECK_STR(d, "03/01 23:59");
    {
        om_sv_item it;
        memset(&it, 0, sizeof it);
        it.d.blocks = 12;
        om_pd_size(&it, d);                      CHECK_STR(d, "12 blk");
        it.d.blocks = 0; it.d.datasize = 3000;
        om_pd_size(&it, d);                      CHECK_STR(d, "3 KB");
    }
    printf("test_popgeom: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
