/* menu/src/layout.c -- see layout.h. BSD-3-Clause. */
#include "layout.h"

#define OM_B_NONE_ (-1)
#define OM_BOX_ROW(n, x0, y0, x1, y1, par, kind, px, py, dp, st) \
    { #n, x0, y0, x1, y1, (int8_t)OM_B_##par, kind, px, py, dp, st },
const om_box om_boxes[OM_B_COUNT] = { OM_BOXES(OM_BOX_ROW) };

#define P(a, b) { OM_B_##a, OM_B_##b }
const om_box_pair om_box_layered[] = {
    P(ROW_TITLE, ROW_ICON),      /* a game row has a title, a folder row an icon: never both */
    P(ROW_FTITLE, ROW_TITLE),    /* a folder row's name uses the star/region columns games keep */
    P(ROW_FTITLE, ROW_STAR),
    P(ROW_FTITLE, ROW_FLAG),
    P(ROW_FTITLE, ROW_ICON),
    P(BAR, ROWS),                /* the cursor bar sits under the selected row */
    P(BAR, SCROLL),              /* the mockup's scrollbar runs at the bar's right end */
    P(COVER_TALL, COVER_SQ),     /* one cover shape at a time */
    P(SHADOW, COVER),            /* the drop shadow under the card */
    P(OV_BAR, OV_LABEL),         /* the overlay's highlight under its row */
    P(OVERLAY, HEADER),          /* the overlays' panel lies over the dimmed menu */
    P(OVERLAY, CRUMB),
    P(OVERLAY, LIST),
    P(OVERLAY, DETAILS),
    P(OV_BAR, OV_GBAR),          /* the game panel's rows start right of its cover */
    P(OV_BAR, OV_COVER),
    P(OV_GBAR, OV_LABEL),
    P(OV_GBAR, OV_VALUE),
    P(OV_LABEL, OV_COVER),       /* Settings' labels use the column the game panel gives its cover */
    P(OV_BAR, OV_VALUE),
    P(OV_BAR, OV_RFLAG),         /* filter: the region toggles use the rows below Genre (addendum 6) */
    P(OV_BAR, OV_RNAME),
    P(OV_LABEL, OV_RFLAG),
    P(OV_LABEL, OV_RNAME),
    P(OV_VALUE, OV_RNAME),
    P(HINTS, TOAST),             /* the toast slides up over the hints and replaces them (addendum 1) */
    P(LD_HINT, LD_STATUS),       /* loading: a named error or a background notice replaces the cancel hint */
    P(LD_COVER_TALL, LD_COVER_SQ),   /* one cover shape at a time */
    P(LD_SHADOW, LD_COVER_TALL), /* the drop shadow under the cover */
    P(LD_SHADOW, LD_COVER_SQ),
    P(LD_REFL, LD_COVER_TALL),   /* the reflection starts under the tilted cover's lower edge */
    P(LD_REFL, LD_SHADOW),
    /* Task 1's wider version tag (menu/ui/vertag.c: "FARKUS 0.1" needs 48 px, not "v0.1"'s 17) moved VERTAG's x0 to
     * 248, a 7x2 px bounding-box corner into ISTL_TEXT's (248-254, 187-188) -- never ink: the counts line's lowest
     * row is 178 (OM_ISTL_CNT_MID + mid - asc + height - 1, test_layout's own check) and the progress bar's right
     * column is 210 (OM_ISTL_BAR_X1), both short of the corner on every axis that matters. */
    P(VERTAG, ISTL_TEXT),
};
const uint32_t om_box_layered_count = sizeof om_box_layered / sizeof om_box_layered[0];

#define F(str, face, box, mode) { str, OM_FACE_##face, OM_B_##box, OM_T_##mode }
const om_ui_text om_ui_fixed[] = {
    F("Games", SMALL, CRUMB_PATH, FIT),
    F("empty", SMALL, CRUMB_COUNT, FIT),
    F("NO ART", SMALL, COVER_SQ, FIT),
    F("Settings", TITLE, OV_TITLE, FIT),
    F("Filter", TITLE, OV_TITLE, FIT),
    F("Launch Options", TITLE, OV_TITLE, FIT),
    /* the approved wording (the motion preview): "Sounds" and "Animations"; the overlay's
     * labels and values are in the 9 px small face so "Animations" + "Reduced" fit the 90 px inside the panel */
    F("Theme", SMALL, OV_LABEL, FIT),
    F("Clock", SMALL, OV_LABEL, FIT),
    F("Music", SMALL, OV_LABEL, FIT),
    F("Sounds", SMALL, OV_LABEL, FIT),
    F("Animations", SMALL, OV_LABEL, FIT),
    F("Genre", SMALL, OV_LABEL, FIT),
    /* the overscan (Safe area) and Loader rows */
    F("Overscan", SMALL, OV_LABEL, FIT),
    F("Loader", SMALL, OV_LABEL, FIT),
    /* the Last game / Saver / Controls rows, and the Controls page (om_act_label + "Reset all") */
    F("Start on Last Game", SMALL, OV_BAR, FIT),   /* past OV_LABEL: its value is short (test_ovpages) */
    F("Screensaver", SMALL, OV_LABEL, FIT),
    F("Controls", SMALL, OV_LABEL, FIT),
    F("Play", SMALL, OV_LABEL, FIT),
    F("Back", SMALL, OV_LABEL, FIT),
    F("Favorite", SMALL, OV_LABEL, FIT),
    F("Sort", SMALL, OV_LABEL, FIT),
    F("Filter", SMALL, OV_LABEL, FIT),
    F("Page up", SMALL, OV_LABEL, FIT),
    F("Page down", SMALL, OV_LABEL, FIT),
    F("Reset all", SMALL, OV_LABEL, FIT),
    /* the game panel's own rows and values; "Press A again" is the armed Reset row's label (that row has no value) */
    F("Launch", SMALL, OV_LABEL, FIT),
    F("Saves Per Game", SMALL, OV_LABEL, FIT),
    F("Slot", SMALL, OV_LABEL, FIT),
    F("Manage saves", SMALL, OV_LABEL, FIT),
    F("Cheats", SMALL, OV_LABEL, FIT),
    F("In Game Reset", SMALL, OV_LABEL, FIT),
    F("Manage / Import Saves", SMALL, OV_BAR, FIT),   /* the same */
    F("About", SMALL, OV_LABEL, FIT),
    F("Pad", SMALL, OV_LABEL, FIT),              /* the static controller-compat row; its default value
                                                   * (no compat note) is "Default" (k_pg3[0]), already in this table */
    F("Reset to defaults", SMALL, OV_LABEL, FIT),
    F("Press A again", SMALL, OV_LABEL, FIT),
    F("Default \xE2\x80\xBA", SMALL, OV_VALUE, FIT),          /* Cheats: its value, then the arrow that opens it */
    F("Needs CWX loader \xE2\x80\xBA", SMALL, OV_VALUE, FIT),
    F("Default", SMALL, OV_VALUE, FIT),
    F("Ask", SMALL, OV_VALUE, FIT),
    F("Controls", TITLE, OV_TITLE, FIT),
    F("Select", SMALL, OV_VALUE, FIT),
    F("1 min", SMALL, OV_VALUE, FIT),
    F("3 min", SMALL, OV_VALUE, FIT),
    F("5 min", SMALL, OV_VALUE, FIT),
    F("\xE2\x80\xBA", SMALL, OV_VALUE, FIT),    /* a row that opens a page or a hand-off */
    F("Done", SMALL, OV_VALUE, FIT),
    F("W", SMALL, OV_VALUE, FIT),              /* the widest single button letter stands for A B C X Y Z L R */
    F("24h", SMALL, OV_VALUE, FIT),
    F("12h", SMALL, OV_VALUE, FIT),
    F("On", SMALL, OV_VALUE, FIT),
    F("Off", SMALL, OV_VALUE, FIT),
    F("Vol 10", SMALL, OV_VALUE, FIT),          /* the Music row's volume (the widest value) */
    F("Full", SMALL, OV_VALUE, FIT),
    F("Reduced", SMALL, OV_VALUE, FIT),
    F("All", SMALL, OV_VALUE, FIT),
    F("Saturn", SMALL, OV_VALUE, FIT),
    F("8 px", SMALL, OV_VALUE, FIT),
    F("16 px", SMALL, OV_VALUE, FIT),
    F("24 px", SMALL, OV_VALUE, FIT),
    F("Auto", SMALL, OV_VALUE, FIT),
    F("CWX", SMALL, OV_VALUE, FIT),
    F("JHL", SMALL, OV_VALUE, FIT),
    F("Japan", SMALL, OV_RNAME, FIT),           /* the filter's region toggles, flag + name (addendum 6) */
    F("Asia", SMALL, OV_RNAME, FIT),
    F("USA", SMALL, OV_RNAME, FIT),
    F("Europe", SMALL, OV_RNAME, FIT),
    F("Brazil", SMALL, OV_RNAME, FIT),
    F("L/R or A: change \xC2\xB7 B/Start: close, save", SMALL, OV_HINT, FIT),   /* each hint pair is one line */
    F("L/R move \xC2\xB7 A toggle \xC2\xB7 Z/B: close, apply", SMALL, OV_HINT, FIT),
    /* Task 13c, the loading screen. The step in progress is followed by U+2026 (the mockup's "Starting game..."): a
     * progress mark, not a cut -- test_layout checks every step fits with it. */
    F("NOW LOADING", LABEL, LD_LABEL, FIT),     /* drawn letter-spaced: test_layout measures it tracked too */
    F("Reading disc image", SMALL, LD_STEPS, FIT),
    F("Preparing launcher", SMALL, LD_STEPS, FIT),
    F("Starting game", SMALL, LD_STEPS, FIT),
    F("Hold A+B+C+Start to cancel", SMALL, LD_HINT, FIT),
    F("NO ART", SMALL, LD_COVER_SQ, FIT),
};
const uint32_t om_ui_fixed_count = sizeof om_ui_fixed / sizeof om_ui_fixed[0];

const char *const om_ui_hint_label[OM_NHINTS] = { "Play", "Back", "Fav", "Sort", "Filter", "Settings" };
const uint8_t om_ui_hint_act[OM_NHINTS] = { OM_ACT_OK, OM_ACT_BACK, OM_ACT_FAV, OM_ACT_SORT, OM_ACT_FILTER,
                                            OM_ACT_COUNT };

void om_ovhint_text(char *out, uint8_t which, const om_remap *m)
{   /* '\1' = the mapped OK, '\2' = Back, '\3' = Filter; every other byte as is */
    static const char *const k_tpl[4] = { "L/R or \1: change", "\xC2\xB7 \2/Start: close, save",
                                          "L/R move \xC2\xB7 \1 toggle", "\xC2\xB7 \3/\2: close, apply" };
    static const uint8_t k_act[4] = { 0, OM_ACT_OK, OM_ACT_BACK, OM_ACT_FILTER };
    const char *t = k_tpl[which < 4 ? which : 0];
    uint32_t o = 0;
    for (; *t && o + 1 < OM_OVHINT_MAX; t++)
        out[o++] = (*t >= 1 && *t <= 3) ? om_btn_letter(m->btn[k_act[(uint8_t)*t]]) : *t;
    out[o] = 0;
}

int om_box_w(int box) { return om_boxes[box].x1 - om_boxes[box].x0 + 1; }

int om_ui_hint_layout(int16_t badge_x[OM_NHINTS], int16_t label_x[OM_NHINTS])
{   /* mockup: badge (wpx + 1) wide, wpx 11 or 17 (ST); label 3 px after it; 9 px to the next badge */
    int i, x = om_boxes[OM_B_HINTS].x0;
    for (i = 0; i < OM_NHINTS; i++) {
        int wpx = i == 5 ? 17 : 11;
        badge_x[i] = (int16_t)x;
        x += wpx + 3;
        label_x[i] = (int16_t)x;
        x += om_uf_width(&om_face_small, om_ui_hint_label[i]);
        if (i + 1 < OM_NHINTS)
            x += 9;
    }
    return x;
}
