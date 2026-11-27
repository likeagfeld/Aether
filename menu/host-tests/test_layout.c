/* menu/host-tests/test_layout.c -- controller addenda 2-4: the one layout table, per screen state (list,
 * settings open, filter open): every child inside its parent minus the parent's padding, no sibling overlap unless
 * declared layered, everything inside the 8 px overscan-safe area; and every text element measured with the real UI
 * faces either fits its box or is a marquee/fade element -- never cut with an ellipsis. BSD-3-Clause. */
#include "th.h"
#include "clock.h"
#include "layout.h"
#include "marquee.h"
#include "msgfmt.h"
#include "safearea.h"
#include "uiline.h"
#include "version.h"
#include "istlgeom.h"

static int layered(int a, int b)
{
    uint32_t i;
    for (i = 0; i < om_box_layered_count; i++)
        if ((om_box_layered[i].a == a && om_box_layered[i].b == b) || (om_box_layered[i].a == b && om_box_layered[i].b == a))
            return 1;
    return 0;
}

static int overlap(const om_box *a, const om_box *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void check_state(uint8_t st, const char *name)
{
    int i, j;
    for (i = 0; i < OM_B_COUNT; i++) {
        const om_box *b = &om_boxes[i];
        if (!(b->states & st))
            continue;
        CHECK(b->x0 <= b->x1 && b->y0 <= b->y1);
        if (b->parent == OM_B_NONE)
            continue;
        {
            const om_box *p = &om_boxes[(int)b->parent];
            int px = b->kind == OM_BK_DECOR ? p->decor_pad : p->pad_x;
            int py = b->kind == OM_BK_DECOR ? p->decor_pad : p->pad_y;
            int ok = (p->states & st) && b->x0 >= p->x0 + px && b->x1 <= p->x1 - px && b->y0 >= p->y0 + py &&
                     b->y1 <= p->y1 - py;
            if (!ok)
                printf("%s: %s (%d,%d)-(%d,%d) not inside %s minus %d/%d\n", name, b->name, b->x0, b->y0, b->x1, b->y1,
                       p->name, px, py);
            CHECK(ok);
        }
        if (i != OM_B_SCREEN) {                                   /* the 8 px overscan-safe area */
            int ok = b->x0 >= 8 && b->x1 <= 311 && b->y0 >= 8 && b->y1 <= 215;
            if (!ok) printf("%s: %s outside the safe area\n", name, b->name);
            CHECK(ok);
        }
        for (j = i + 1; j < OM_B_COUNT; j++) {
            const om_box *c = &om_boxes[j];
            if (!(c->states & st) || c->parent != b->parent)
                continue;
            if (overlap(b, c) && !layered(i, j)) {
                printf("%s: siblings %s and %s overlap\n", name, b->name, c->name);
                CHECK(0);
            }
        }
    }
}

static void check_level(uint8_t lv)
{   /* every menu box, mapped, lies inside that level's margin, and inside its mapped parent */
    om_safe t;
    int i, r[4], p[4], m;
    om_safe_set(&t, lv);
    m = t.m;
    for (i = 0; i < OM_B_COUNT; i++) {
        const om_box *b = &om_boxes[i];
        if (i == OM_B_SCREEN || !(b->states & (OM_S_MENU | OM_S_LOADING))) continue;
        om_safe_rect(&t, b->x0, b->y0, b->x1, b->y1, r);
        if (r[0] < m || r[2] > 319 - m || r[1] < m || r[3] > 223 - m) {
            printf("level %d: %s outside the %d px margin\n", lv, b->name, m);
            CHECK(0);
        } else CHECK(1);
        if (b->parent != OM_B_NONE && b->parent != OM_B_SCREEN) {
            const om_box *q = &om_boxes[(int)b->parent];
            om_safe_rect(&t, q->x0, q->y0, q->x1, q->y1, p);
            CHECK(r[0] >= p[0] && r[2] <= p[2] && r[1] >= p[1] && r[3] <= p[3]);
        }
    }
}

static void check_boot(void)
{   /* the boot screens' boxes lie inside the 24 px area (they show before SETTINGS.INI is read) */
    int i, n = 0;
    for (i = 0; i < OM_B_COUNT; i++) {
        const om_box *b = &om_boxes[i];
        int ok;
        if (!(b->states & OM_S_BOOT) || i == OM_B_SCREEN) continue;
        n++;
        ok = b->x0 >= OM_SAFE_BOOT && b->x1 <= 319 - OM_SAFE_BOOT && b->y0 >= OM_SAFE_BOOT &&
             b->y1 <= 223 - OM_SAFE_BOOT;
        if (!ok) printf("boot: %s (%d,%d)-(%d,%d) outside the %d px area\n", b->name, b->x0, b->y0, b->x1, b->y1,
                        OM_SAFE_BOOT);
        CHECK(ok);
    }
    CHECK_INT(n, 3);                                               /* VERTAG, ISTL_ART, ISTL_TEXT */
}

static void fits(int face, const char *s, int box)
{
    int w = om_uf_width(om_face_get(face), s);
    if (w > om_box_w(box))
        printf("overflow: \"%s\" is %d px, %s is %d\n", s, w, om_boxes[box].name, om_box_w(box));
    CHECK(w <= om_box_w(box));
}

static int nmarquee;
static void dynamic(int face, const char *s, int box, int mode)
{   /* a dynamic element: fits, or its mode scrolls / fades it inside the box (never cut with "…") */
    int w = om_uf_width(om_face_get(face), s);
    CHECK(strstr(s, "\xE2\x80\xA6") == NULL);
    if (w > om_box_w(box)) {
        CHECK(mode == OM_T_MARQUEE || mode == OM_T_FADE);
        nmarquee++;
    }
}

int main(void)
{
    static const char t60[] = "Super Ultra Mega Shining Panzer Dragoon Saga Holy Ark Deluxe";   /* 60 characters */
    static const char genre31[] = "Shoot 'em up / Vertical Scroll!";                               /* 31 characters */
    char buf[200];
    int16_t bx[OM_NHINTS], lx[OM_NHINTS];
    uint32_t i;
    int x;
    om_lib_entry e;
    om_game g;
    om_clock c;

    CHECK_INT((int)strlen(t60), 60);
    CHECK_INT(om_boxes[OM_B_SCREEN].x1, 319);
    CHECK_INT(om_boxes[OM_B_SCREEN].y1, 223);
    for (i = 0; i < OM_B_COUNT; i++)
        CHECK(om_boxes[i].parent < (int)i);                        /* parents come first: the table is a tree */
    check_state(OM_S_LIST, "list");
    check_state(OM_S_SETTINGS, "settings");
    check_state(OM_S_FILTER, "filter");
    check_state(OM_S_LOADING, "loading");                          /* Task 13c */
    check_state(OM_S_BOOT, "boot");                                /* M6 Task 5 */
    for (i = 0; i < OM_OVS_COUNT; i++)                             /* every safe-area level */
        check_level((uint8_t)i);
    check_boot();
    {   /* VERTAG is the tag's measured extent (menu/ui/vertag.c: right-aligned on the pen's end at
         * OM_SAFE_BOOT, descenders inside it); the interstitial's text box holds its widest line, centered */
        const om_face *f = &om_face_label8;
        int pen = om_uf_width_tracked(f, OM_VERSION_TAG, 0), wl, wc;
        CHECK_INT(OM_BX(VERTAG)->x0, 320 - OM_SAFE_BOOT - pen);
        CHECK_INT(OM_BX(VERTAG)->x1, 319 - OM_SAFE_BOOT);
        CHECK_INT(OM_BX(VERTAG)->y1, 223 - OM_SAFE_BOOT);
        CHECK_INT(OM_BX(VERTAG)->y0, 224 - OM_SAFE_BOOT - om_uf_height(f));
        wl = om_uf_width_tracked(&om_face_label8, "LOADING YOUR LIBRARY", OM_ISTL_LAB_TRACK8);
        /* the counts line at four digits each (interstitial.c raster: "n games  \xC2\xB7  n folders ...") */
        wc = om_uf_width(&om_face_small, "9999 games  \xC2\xB7  9999 folders  \xC2\xB7  9999 covers");
        CHECK(OM_ISTL_CX - (wl >> 1) >= OM_BX(ISTL_TEXT)->x0);
        CHECK(OM_ISTL_CX - (wl >> 1) + wl - 1 <= OM_BX(ISTL_TEXT)->x1);
        CHECK(OM_ISTL_CX - (wc >> 1) >= OM_BX(ISTL_TEXT)->x0);
        CHECK(OM_ISTL_CX - (wc >> 1) + wc - 1 <= OM_BX(ISTL_TEXT)->x1);
        CHECK(OM_ISTL_LAB_MID + om_face_label8.mid - om_face_label8.asc >= OM_BX(ISTL_TEXT)->y0);   /* texture tops */
        CHECK(OM_ISTL_BAR_Y + 2 <= OM_BX(ISTL_TEXT)->y1);                                  /* the bar's last row */
        CHECK(OM_ISTL_CNT_MID + om_face_small.mid - om_face_small.asc + om_uf_height(&om_face_small) - 1 <=
              OM_BX(ISTL_TEXT)->y1);
        CHECK(OM_ISTL_CX - OM_ISTL_RX >= OM_BX(ISTL_ART)->x0 && OM_ISTL_CX + OM_ISTL_RX <= OM_BX(ISTL_ART)->x1);
        CHECK(OM_ISTL_CY - OM_ISTL_LOGO_H / 2 >= OM_BX(ISTL_ART)->y0);
        CHECK(OM_ISTL_CY - OM_ISTL_LOGO_H / 2 + OM_ISTL_LOGO_H - 1 <= OM_BX(ISTL_ART)->y1);
    }
    /* the overlays' one panel (the approved mockup, native 50..269 x 28..197): centered, its highlight inset 4 px,
     * labels and values 4 px inside the highlight; the game panel's rows start right of its 44 x 62 cover */
    CHECK_INT(OM_BX(OVERLAY)->x0 + OM_BX(OVERLAY)->x1, 319);
    CHECK_INT(OM_BX(OV_BAR)->x0 - OM_BX(OVERLAY)->x0, 4); CHECK_INT(OM_BX(OVERLAY)->x1 - OM_BX(OV_BAR)->x1, 4);
    CHECK_INT(OM_BX(OV_LABEL)->x0 - OM_BX(OV_BAR)->x0, 4); CHECK_INT(OM_BX(OV_BAR)->x1 - OM_BX(OV_VALUE)->x1, 4);
    CHECK_INT(OM_BX(OV_GBAR)->x1, OM_BX(OV_BAR)->x1);
    CHECK(OM_BX(OV_GBAR)->x0 > OM_BX(OV_COVER)->x1 + 4);
    CHECK_INT(om_box_w(OM_B_OV_COVER), 44); CHECK_INT(OM_BX(OV_COVER)->y1 - OM_BX(OV_COVER)->y0 + 1, 62);
    /* the game panel's longest label and value pairs fit its row (label 4 px in, value 4 px from the end) */
    CHECK(om_uf_width(&om_face_small, "Reset to defaults") <= OM_BX(OV_GBAR)->x1 - OM_BX(OV_GBAR)->x0 - 8);
    CHECK(om_uf_width(&om_face_small, "Loader") + 4 + om_uf_width(&om_face_small, "Auto (CWX, database)") <=
          OM_BX(OV_GBAR)->x1 - OM_BX(OV_GBAR)->x0 - 8);
    CHECK(om_uf_width(&om_face_small, "Cheats") + 4 + om_uf_width(&om_face_small, "Needs CWX loader \xE2\x80\xBA") <=
          OM_BX(OV_GBAR)->x1 - OM_BX(OV_GBAR)->x0 - 8);
    CHECK(OM_BX(OV_TITLE)->x0 + om_uf_width(&om_face_title, "Launch Options") + 6 + 40 <= OM_BX(OV_TITLE)->x1);
    CHECK(OM_BX(OVERLAY)->pad_x >= 6 && OM_BX(DETAILS)->pad_x >= 6 && OM_BX(LIST)->pad_x >= 6);
    /* the cover shapes: tall 100x156; square (JP, folder art) 100x100 centred in the same column (addendum 2) */
    CHECK_INT(om_box_w(OM_B_COVER_TALL), 100); CHECK_INT(OM_BX(COVER_TALL)->y1 - OM_BX(COVER_TALL)->y0 + 1, 156);
    CHECK_INT(om_box_w(OM_B_COVER_SQ), 100); CHECK_INT(OM_BX(COVER_SQ)->y1 - OM_BX(COVER_SQ)->y0 + 1, 100);
    CHECK_INT(OM_BX(COVER_SQ)->y0 - OM_BX(COVER_TALL)->y0, OM_BX(COVER_TALL)->y1 - OM_BX(COVER_SQ)->y1);
    /* the soft shadow of either shape stays inside SHADOW */
    CHECK(OM_BX(COVER_TALL)->x0 + OM_SHADOW_DX - 2 >= OM_BX(SHADOW)->x0);
    CHECK(OM_BX(COVER_TALL)->x1 + OM_SHADOW_DX + 2 <= OM_BX(SHADOW)->x1);
    CHECK(OM_BX(COVER_TALL)->y1 + OM_SHADOW_DY + 2 <= OM_BX(SHADOW)->y1);
    /* 8 rows of 14 px fill ROWS; the overlay's 9 rows (the whole game panel) fill OV_BAR; the hint line in OV_HINT */
    CHECK_INT(OM_BX(ROWS)->y1 - OM_BX(ROWS)->y0 + 1, 8 * OM_ROW_H);
    CHECK_INT(OM_BX(OV_BAR)->y1 - OM_BX(OV_BAR)->y0 + 1, 9 * OM_OV_ROW_H);
    CHECK_INT(OM_BX(OV_GBAR)->y1 - OM_BX(OV_GBAR)->y0 + 1, 9 * OM_OV_ROW_H);
    CHECK_INT(OM_OV_ROW0_MID - OM_BX(OV_BAR)->y0, 7);
    CHECK(OM_OV_ROW_H >= om_uf_height(&om_face_small));
    CHECK(OM_OV_HINT_MID - 6 >= OM_BX(OV_HINT)->y0 && OM_OV_HINT_MID + 6 <= OM_BX(OV_HINT)->y1);
    CHECK(OM_OV_TITLE_MID + om_face_title.mid - om_face_title.asc >= OM_BX(OV_TITLE)->y0);

    /* every fixed string fits its box, never with an ellipsis */
    for (i = 0; i < om_ui_fixed_count; i++) {
        CHECK_INT(om_ui_fixed[i].mode, OM_T_FIT);
        CHECK(strstr(om_ui_fixed[i].s, "\xE2\x80\xA6") == NULL);
        fits(om_ui_fixed[i].face, om_ui_fixed[i].s, om_ui_fixed[i].box);
    }
    /* the settings rows: the widest label, a 4 px gap and the widest fixed value within the row */
    CHECK(om_uf_width(&om_face_small, "In Game Reset") + 4 + om_uf_width(&om_face_small, "Reduced") <=
          OM_BX(OV_VALUE)->x1 - OM_BX(OV_LABEL)->x0 + 1);
    CHECK(OM_BX(OV_VALUE)->x0 - OM_BX(OV_LABEL)->x1 >= 4);
    x = OM_BX(BRAND)->x0 + om_icon_wordmark.w;                    /* the Aether wordmark icon (D2) */
    CHECK(x - 1 <= OM_BX(BRAND)->x1);
    CHECK(OM_HDR_MID - 6 >= OM_BX(BRAND)->y0 - 2 && OM_HDR_MID - 6 + om_icon_wordmark.h - 1 <= OM_BX(BRAND)->y1 + 2);
    x = om_ui_hint_layout(bx, lx);                                 /* the footer hints end inside the strip */
    CHECK(x - 1 <= OM_BX(HINTS)->x1);
    CHECK_INT(bx[0], OM_BX(HINTS)->x0);
    /* FIT dynamics: every clock value, the largest counts, any two region letters */
    memset(&c, 0, sizeof c);
    c.valid = 1;
    for (c.hour = 0; c.hour < 24; c.hour++)
        for (c.minute = 0; c.minute < 60; c.minute += 59) {
            om_clock_fmt(&c, 1, buf); fits(OM_FACE_TITLE, buf, OM_B_CLOCK);
            om_clock_fmt(&c, 0, buf); fits(OM_FACE_TITLE, buf, OM_B_CLOCK);
        }
    om_count_line(2048, 3077, buf, sizeof buf); fits(OM_FACE_SMALL, buf, OM_B_CRUMB_COUNT);
    om_count_line(0, 3077, buf, sizeof buf); fits(OM_FACE_SMALL, buf, OM_B_CRUMB_COUNT);
    /* flags (addendum 6): the list's flag column and the filter's flag column hold a 12 px flag */
    CHECK(om_box_w(OM_B_ROW_FLAG) >= OM_FLAG_TEX_W);
    CHECK(om_box_w(OM_B_OV_RFLAG) >= OM_FLAG_W);
    CHECK(OM_BX(ROW_FLAG)->x1 <= OM_BX(ROWS)->x1 && OM_BX(ROW_FLAG)->x0 > OM_BX(ROW_STAR)->x1);
    CHECK(OM_OV_RROW0 + 5 * OM_OV_RROW_H - 1 <= OM_BX(OV_RNAME)->y1);
    CHECK_INT(OM_OV_RROW0, OM_BX(OV_BAR)->y0 + OM_OV_ROW_H);       /* right under the Genre row */
    {   /* every area code has a name the details card spells out (the meta line marquees when long) */
        static const char codes[] = "JTUBKAEL";
        for (i = 0; i < 8; i++) CHECK(om_area_name(codes[i])[0] != 0);
    }
    /* MARQUEE / FADE dynamics: worst cases overflow and are flagged, never abbreviated */
    nmarquee = 0;
    dynamic(OM_FACE_ITEM, t60, OM_B_ROW_TITLE, OM_T_FADE);         /* a non-selected row */
    dynamic(OM_FACE_SEL, t60, OM_B_ROW_TITLE, OM_T_MARQUEE);       /* the selected row */
    dynamic(OM_FACE_ITEM, t60, OM_B_ROW_FTITLE, OM_T_FADE);
    dynamic(OM_FACE_TITLE, t60, OM_B_DET_TITLE, OM_T_MARQUEE);
    memset(&e, 0, sizeof e); memset(&g, 0, sizeof g);
    g.year = 1998; g.players = 10; g.region = 0x1F; e.disc = 4; e.disc_total = 4;
    om_meta_line(&e, &g, genre31, NULL, buf, sizeof buf);
    CHECK(strstr(buf, "Latin") == NULL && strstr(buf, "Brazil") != NULL);   /* five areas in words (addendum 6) */
    dynamic(OM_FACE_SMALL, buf, OM_B_DET_META, OM_T_MARQUEE);
    om_crumb("/Shooters/Classic/A Very Long Folder Name For A Breadcrumb", buf, sizeof buf);
    dynamic(OM_FACE_SMALL, buf, OM_B_CRUMB_PATH, OM_T_MARQUEE);
    dynamic(OM_FACE_ITEM, genre31, OM_B_OV_VALUE, OM_T_MARQUEE);   /* a long genre or theme name */
    om_msg_rc(buf, 64, "THEME: ", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", -9);
    dynamic(OM_FACE_SMALL, buf, OM_B_TOAST, OM_T_MARQUEE);
    CHECK_INT(nmarquee, 8);                                        /* every worst case really did overflow */
    {   /* Task 13c, the loading screen (loading_mockup_render.py) */
        static const char *const steps[3] = { "Reading disc image", "Preparing launcher", "Starting game" };
        char l1[200], l2[200];
        int lines;
        /* "NOW LOADING" letter-spaced, and the step in progress with its U+2026 progress mark, fit their boxes */
        x = om_uf_width_tracked(&om_face_label, "NOW LOADING", OM_LD_TRACK8);
        CHECK(x <= om_box_w(OM_B_LD_LABEL));
        CHECK(x > om_uf_width(&om_face_label, "NOW LOADING"));
        CHECK(OM_BX(LD_ULINE)->x1 - OM_BX(LD_ULINE)->x0 + 1 <= x);         /* the accent underline under the label */
        for (i = 0; i < 3; i++) {
            snprintf(buf, sizeof buf, "%s\xE2\x80\xA6", steps[i]);
            fits(OM_FACE_SMALL, buf, OM_B_LD_STEPS);
        }
        CHECK(OM_LD_STEP_MID + 2 * OM_LD_STEP_H + 3 <= OM_BX(LD_DOTS)->y1 + 1);   /* three 6 px dots */
        CHECK(OM_LD_STEP_MID - 3 >= OM_BX(LD_DOTS)->y0);
        CHECK_INT(OM_BX(LD_DOTS)->x1 - OM_BX(LD_DOTS)->x0 + 1, 6);
        x = OM_BX(LD_BRAND)->x0 + om_icon_wordmark_s.w;
        CHECK(x - 1 <= OM_BX(LD_BRAND)->x1);
        CHECK(OM_LD_BRAND_MID - 4 >= OM_BX(LD_BRAND)->y0 && OM_LD_BRAND_MID - 4 + om_icon_wordmark_s.h - 1 <= OM_BX(LD_BRAND)->y1);
        /* the meta lines end at least 8 px before the cover (brief) */
        CHECK(OM_BX(LD_META1)->x1 <= OM_BX(LD_COVER_TALL)->x0 - 9);
        CHECK(OM_BX(LD_META2)->x1 <= OM_BX(LD_COVER_TALL)->x0 - 9);
        CHECK(OM_BX(LD_TITLE)->x1 <= OM_BX(LD_COVER_TALL)->x0 - 9);
        /* two title lines inside LD_TITLE; meta middles inside their boxes */
        CHECK(OM_LD_TITLE_BASE - om_face_hero.asc + 3 >= OM_BX(LD_TITLE)->y0);
        CHECK(OM_LD_TITLE_BASE + OM_LD_TITLE_LH + om_face_hero.desc <= OM_BX(LD_TITLE)->y1 + 1);
        CHECK(OM_LD_META_MID >= OM_BX(LD_META1)->y0 && OM_LD_META_MID <= OM_BX(LD_META1)->y1);
        CHECK(OM_LD_META_MID + OM_LD_META_LH >= OM_BX(LD_META2)->y0);
        CHECK(OM_LD_META_MID + OM_LD_META_LH <= OM_BX(LD_META2)->y1);
        /* the title: at most two lines, the first one whole words that fit, the rest a marquee (never abbreviated) */
        lines = om_title_wrap(&om_face_hero, "Panzer Dragoon Saga", om_box_w(OM_B_LD_TITLE), l1, sizeof l1, l2,
                              sizeof l2);
        CHECK_INT(lines, 2);
        CHECK_STR(l1, "Panzer Dragoon");
        CHECK_STR(l2, "Saga");
        lines = om_title_wrap(&om_face_hero, "Daytona USA", om_box_w(OM_B_LD_TITLE), l1, sizeof l1, l2, sizeof l2);
        CHECK_INT(lines, 1);
        CHECK_STR(l1, "Daytona USA");
        nmarquee = 0;
        lines = om_title_wrap(&om_face_hero, t60, om_box_w(OM_B_LD_TITLE), l1, sizeof l1, l2, sizeof l2);
        CHECK_INT(lines, 2);
        fits(OM_FACE_HERO, l1, OM_B_LD_TITLE);
        dynamic(OM_FACE_HERO, l2, OM_B_LD_TITLE, OM_T_MARQUEE);
        CHECK(strlen(l1) + 1 + strlen(l2) == strlen(t60));          /* nothing dropped */
        lines = om_title_wrap(&om_face_hero, "Supercalifragilisticexpialidocious!", om_box_w(OM_B_LD_TITLE), l1,
                              sizeof l1, l2, sizeof l2);
        CHECK_INT(lines, 1);                                       /* one word wider than the box: one marquee line */
        dynamic(OM_FACE_HERO, l1, OM_B_LD_TITLE, OM_T_MARQUEE);
        e.disc = 1; e.disc_total = 4;
        g.maker = "Team Andromeda"; g.players = 1; g.region = 0x04; g.year = 1998;
        om_load_meta(&e, &g, "RPG", NULL, l1, sizeof l1, l2, sizeof l2);
        CHECK_STR(l1, "Team Andromeda" OM_SEP_DOT "1998" OM_SEP_DOT "RPG");
        CHECK_STR(l2, "Disc 1 of 4" OM_SEP_DOT "USA" OM_SEP_DOT "1 player");
        fits(OM_FACE_SMALL, l1, OM_B_LD_META1);
        fits(OM_FACE_SMALL, l2, OM_B_LD_META2);
        g.maker = "A Very Long Developer Name Corporation"; g.players = 10; g.region = 0x1F;
        om_load_meta(&e, &g, genre31, NULL, l1, sizeof l1, l2, sizeof l2);
        dynamic(OM_FACE_SMALL, l1, OM_B_LD_META1, OM_T_MARQUEE);
        dynamic(OM_FACE_SMALL, l2, OM_B_LD_META2, OM_T_MARQUEE);
        CHECK_INT(nmarquee, 4);
        memset(&g, 0, sizeof g); e.disc_total = 1; e.area = 0;
        om_load_meta(&e, &g, NULL, NULL, l1, sizeof l1, l2, sizeof l2);
        CHECK_STR(l1, "");                                          /* nothing known: empty lines, no stray dots */
        CHECK_STR(l2, "");
    }
    {   /* past the 504 px raster cap a MARQUEE element is fade-cut at its box, never drawn wider */
        static const int boxes[] = { OM_B_ROW_TITLE, OM_B_DET_TITLE, OM_B_DET_META, OM_B_CRUMB_PATH, OM_B_OV_VALUE,
                                     OM_B_TOAST };
        char huge[200];
        int w;
        memset(huge, 'W', sizeof huge - 1); huge[sizeof huge - 1] = 0;
        w = om_uf_width(&om_face_title, huge);
        CHECK(w > 504);
        for (i = 0; i < sizeof boxes / sizeof boxes[0]; i++) {
            CHECK_INT(om_marquee_mode(w, om_box_w(boxes[i]), 504), OM_MQ_FADE);
            CHECK_INT(om_marquee_mode(400, om_box_w(boxes[i]), 504), OM_MQ_SCROLL);
            CHECK_INT(om_marquee_mode(om_box_w(boxes[i]), om_box_w(boxes[i]), 504), OM_MQ_FITS);
        }
    }
    return th_done("test_layout");
}
