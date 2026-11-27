/* menu/app/fake/fakepad.c -- see fakepad.h. Frame 0 = the Folders view's first om_pad_poll. Each press is held for 3
 * frames, i.e. one om_pad_poll edge and no auto-repeat (OM_PAD_REPEAT_DELAY is 18). Steps are 240 frames apart
 * (~4 s at 59.94 Hz, HWF-11) so a screenshot can land on each. The tour is frozen here: later tasks implement what
 * the keys do, they do not edit this table. BSD-3-Clause. */
/* DEVIATION (brief): fakepad.h only pulls in "sgl.h", whose SL_DEF.H defines Uint8/Uint16/Uint32 as plain typedefs,
 * not <stdint.h> -- so this file's own uint16_t/uint32_t locals need the explicit include (menu/ui/art.c gets it
 * transitively through card.h; this file has no such chain). */
#include <stdint.h>
#include "fakepad.h"
#include "fakesat.h"
#include "vdp2theme.h"

typedef struct { uint16_t frame; Uint16 keys; const char *label; } step;

static const step k_tour[] = {
    {   60, 0,          "01 ROOT VIEW" },
    {  300, PER_DGT_TA, "02 FAVORITES" },
    {  540, PER_DGT_TB, "" }, {  560, PER_DGT_KD, "" },
    {  580, PER_DGT_TA, "03 RECENTLY PLAYED" },
    {  820, PER_DGT_TB, "" }, {  840, PER_DGT_KD, "" },
    {  860, PER_DGT_TA, "04 A-Z LETTERS" },
    { 1100, PER_DGT_TA, "05 A-Z FIRST LETTER" },
    { 1340, PER_DGT_TB, "" }, { 1360, PER_DGT_TB, "" }, { 1380, PER_DGT_KD, "" },
    { 1400, PER_DGT_TA, "06 GENRES" },
    { 1640, PER_DGT_TB, "" }, { 1660, PER_DGT_KD, "" },
    { 1680, PER_DGT_TA, "07 REGIONS" },
    { 1920, PER_DGT_TB, "" }, { 1940, PER_DGT_KD, "" },
    { 1960, PER_DGT_TA, "08 CARD FOLDER" },
    { 2200, PER_DGT_TY, "09 SORT GENRE" },
    { 2440, PER_DGT_TY, "10 SORT REGION" },
    { 2680, PER_DGT_TY, "" }, { 2700, PER_DGT_TY, "11 SORT A-Z" },
    { 2940, PER_DGT_TX, "12 FAVORITE TOGGLED" },
    { 3180, PER_DGT_TZ, "13 FILTER OPEN" },
    { 3200, PER_DGT_KR, "" },
    { 3420, PER_DGT_TZ, "14 FILTER GENRE ON" },
    { 3660, PER_DGT_TZ, "" }, { 3680, PER_DGT_KL, "" },
    { 3700, PER_DGT_TZ, "15 FILTER CLEARED" },
    { 3940, PER_DGT_ST, "16 SETTINGS" },
    { 4180, PER_DGT_KR, "17 THEME AETHER" },
    { 4420, PER_DGT_KR, "18 THEME TEST BG" },
    { 4660, PER_DGT_TB, "19 SETTINGS SAVED" },
    { 4900, PER_DGT_TA, "20 LAUNCH FAIL PATH" },
    { 5140, 0,          "21 END" },
};

/* Review-13a I6: a second, selectable script (FAKECARD.BIN built with `python -m omsat.fakecard --scroll`), so held
 * scrolling is measured. The tour above stays frozen. Rows are {first frame, keys, label, frames held}: at the root
 * (31 items, the longest list on the fake card) hold Down for 4 s, Up for 4 s, Down again, then launch twice with an
 * input between (the repeated-error check). Auto-repeat (menu/ui/pad.c) turns a hold into a step every 4 frames. */
typedef struct { uint16_t frame, hold; Uint16 keys; const char *label; } hstep;
static const hstep k_scroll[] = {
    {   60,   1, 0,          "S1 ROOT" },
    {  120, 240, PER_DGT_KD, "S2 HOLD DOWN" },
    {  420, 240, PER_DGT_KU, "S3 HOLD UP" },
    {  720, 240, PER_DGT_KD, "S4 HOLD DOWN" },
    { 1020,   3, PER_DGT_TA, "S5 LAUNCH" },              /* a game: the launch fails on the fake card (rc shown) */
    { 1140,   3, PER_DGT_TB, "S6 DISMISS" },             /* any input dismisses the rc error */
    { 1200,   3, PER_DGT_TA, "S7 LAUNCH AGAIN" },        /* the same failure must show again */
    { 1320,   1, 0,          "S8 END" },
};

/* a third script (FAKECARD.BIN built with `python -m omsat.fakecard --loading [--launcher-stub]`): open
 * Favorites and launch its third game (the card dresses it as the mockup's). With the LAUNCHER.BIN stub the loading
 * screen runs to "Starting game...", the scream hook and the fade to black, then the fake build returns ("would hand
 * off now"); L4 launches it again and L5 holds A+B+C+Start during the loading screen (cancel; the menu must not then
 * return to the official menu while the buttons are still held). Without the stub, L3 fails at "Preparing launcher"
 * (rc shown, the background loops) and L4's press returns to the menu. Frames count om_pad_poll calls: the menu's
 * fades do not poll, the loading screen polls every frame. */
static const hstep k_loading[] = {
    {   60,   1, 0,                                              "L1 ROOT" },
    {  120,   3, PER_DGT_TA,                                     "L2 FAVORITES" },
    {  200,   3, PER_DGT_KD,                                     "" },
    {  215,   3, PER_DGT_KD,                                     "" },   /* the mockup's game, third by title */
    {  240,   3, PER_DGT_TA,                                     "L3 LAUNCH" },
    {  600,   3, PER_DGT_TA,                                     "L4 LAUNCH AGAIN" },
    {  630,  60, PER_DGT_TA | PER_DGT_TB | PER_DGT_TC | PER_DGT_ST, "L5 CANCEL HELD" },
    {  900,   1, 0,                                              "L6 END" },
};

/* two more scripts (FAKECARD.BIN built with `python -m omsat.fakecard --diag page1` / `page2` / `dismiss`): the fake
 * pad's scripted tour never presses the diag combo (hold L+R, then Start -- the frozen tour's own frame table is
 * owned by the review-13a plan, not this task), so the brief asks for a fake-only, opt-in way to reach the
 * screen for a capture, without touching k_tour. D2/D3 reproduce the real combo exactly as a human would: a
 * window with only L+R held (D2, no Start yet) immediately followed by a window with L+R+Start held together
 * (D3 -- Start's OWN first frame of being held is a fresh om_pad_poll edge while L+R are already-held, exactly
 * menu/views/folders.c's `held & (TL|TR) == (TL|TR) && (p & ST)` check).
 *
 * MEASURED this session: a frame-numbered schedule for the REST of the sequence (page 2, dismiss) is not safe to
 * predict a capture time against. om_diag_run's own SD measurements block for ~1.5-2 s of real time with no
 * om_pad_poll call at all (so this script's own frame counter does not advance then either) -- but the bigger
 * effect is that Mednafen does not appear to throttle an unchanging diag page to 60 fps the way it throttles the
 * boot animation and the Folders view's own motion: a capture at t=26 s showed "D2 HOLD L+R" (frame 120-139) and
 * one at t=28 s already showed page 2 (frame >= 900) -- over 760 ticks in at most 2 s of real time, several
 * hundred x realtime, against a measured ~1x pace for the frame 60-120 span just before it. A fixed frame
 * therefore does not correspond to a fixed wall-clock second once nothing on screen is changing.
 *
 * So each script does the combo, then (DIAG2 only) one L/R press right after it, then goes idle FOREVER --
 * no further scripted input, ever. Whichever page is on screen when the idling starts stays there for the rest
 * of the run, so a capture's -Times can be any time comfortably after the combo (empirically, after the Folders
 * view's own startup -- see the capture script/report for the measured window) without having to land inside a
 * narrow, speed-dependent frame range. Two scripts, not one with a scripted page flip, because this fully
 * sidesteps needing to predict how long page 1 stays on screen before a scripted flip would fire. */
static const hstep k_diag[] = {
    {   60,   1, 0,                                              "D1 ROOT" },
    {  120,  20, PER_DGT_TL | PER_DGT_TR,                         "D2 HOLD L+R" },
    {  140,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "D3 DIAG COMBO (STAYS ON PAGE 1)" },
};
static const hstep k_diag2[] = {
    {   60,   1, 0,                                              "E1 ROOT" },
    {  120,  20, PER_DGT_TL | PER_DGT_TR,                         "E2 HOLD L+R" },
    {  140,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "E3 DIAG COMBO" },
    /* MEASURED this session (emulator capture): without a real release in between, R is never seen as a FRESH
     * press here -- it has been held continuously since E2 (first as part of L+R, then as part of the combo),
     * so om_pad_poll's edge detection correctly treats "R alone" right after "L+R+Start" as the same ongoing
     * hold, not a new press, and the scripted page flip never fires. A gap with nothing held lets R go through
     * a real release before this script presses it again. */
    {  145,  10, 0,                                               "" },
    {  160,   5, PER_DGT_TR,                                     "E4 DIAG PAGE 2 (STAYS HERE)" },
};
/* FAKESCRIPT.DIAG3: the combo, then (after the same real-release gap as DIAG2) B dismisses the screen, back to
 * the Folders view -- idle there forever. Exercises the exit path (om_pad_prime, om_text_clear, the fake HUD's
 * s_diag_reset, s_vb_restart, the s_ph[] clear) that DIAG/DIAG2 never reach on their own. */
static const hstep k_diag3[] = {
    {   60,   1, 0,                                              "G1 ROOT" },
    {  120,  20, PER_DGT_TL | PER_DGT_TR,                         "G2 HOLD L+R" },
    {  140,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "G3 DIAG COMBO" },
    {  145,  10, 0,                                               "" },
    {  160,   5, PER_DGT_TB,                                     "G4 DISMISS (BACK TO FOLDERS)" },
};
/* FAKESCRIPT.DIAG4 (2026-11-19 hardware flicker): held scrolling as k_scroll's S2/S3, then DIAG2's combo and R: page 2
 * shows the NBG2/VDP1 counters (vdp2theme.h om_v2_stat) of that scrolling; idle there forever. */
static const hstep k_diag4[] = {
    {   60,   1, 0,                                              "H1 ROOT" },
    {  120, 240, PER_DGT_KD,                                     "H2 HOLD DOWN" },
    {  420, 240, PER_DGT_KU,                                     "H3 HOLD UP" },
    {  720,  20, PER_DGT_TL | PER_DGT_TR,                         "H4 HOLD L+R" },
    {  740,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "H5 DIAG COMBO" },
    {  745,  10, 0,                                               "" },
    {  760,   5, PER_DGT_TR,                                     "H6 DIAG PAGE 2 (STAYS HERE)" },
};

/* Settings scrolled to Safe area, overscan cycled, Loader cycled, Start. M6 Task 11 (M3.5 Task 5's
 * steps, renumbered): on to the Controls page, Play set to B (Back becomes A), closed and saved; at the root the
 * physical B now opens Favorites and A comes back; then Settings again, Controls, "Reset all" with the D-pad alone,
 * closed and saved. */
static const hstep k_settings[] = {
    {   60,   1, 0,           "P1 ROOT" },
    {  120,   3, PER_DGT_ST,  "P2 SETTINGS" },
    {  200,   3, PER_DGT_KD,  "" }, {  215,   3, PER_DGT_KD,  "" }, {  230,   3, PER_DGT_KD,  "" },
    {  245,   3, PER_DGT_KD,  "" }, {  260,   3, PER_DGT_KD,  "P3 SAFE AREA ROW" },
    {  380,   3, PER_DGT_KR,  "P4 SAFE 16" },
    {  500,   3, PER_DGT_KR,  "P5 SAFE 24" },
    {  620,   3, PER_DGT_KR,  "P6 SAFE OFF" },
    {  740,   3, PER_DGT_KR,  "P7 SAFE 8" },
    {  760,   3, PER_DGT_KD,  "P8 LOADER ROW" },
    {  880,   3, PER_DGT_KR,  "P9 LOADER CWX" },
    { 1015,   3, PER_DGT_KD,  "P10 SOFT RESET ROW" },          /* M6 Task 20a (2026-11-23: no Per-game row) */
    { 1120,   3, PER_DGT_KR,  "P11 SOFT RESET OFF" },
    { 1240,   3, PER_DGT_KD,  "P11B SAVES ROW" },             /* Saves (per-game saves), On */
    { 1285,   3, PER_DGT_KD,  "P11C MANAGE SAVES ROW" },
    { 1308,   3, PER_DGT_KD,  "" }, { 1315,   3, PER_DGT_KD,  "" },   /* over Start on Last Game and Screensaver */
    { 1330,   3, PER_DGT_KD,  "P12 CONTROLS ROW" },
    { 1390,   3, PER_DGT_KR,  "P13 CONTROLS PAGE" },
    { 1510,   3, PER_DGT_KR,  "P14 PLAY ON B" },
    { 1630,   3, PER_DGT_ST,  "P15 CLOSED PLAY=B" },
    { 1750,   3, PER_DGT_TB,  "P16 B OPENS FAVORITES" },
    { 1870,   3, PER_DGT_TA,  "P17 A GOES BACK" },
    { 1990,   3, PER_DGT_ST,  "P18 SETTINGS AGAIN" },
    { 2070,   3, PER_DGT_KD,  "" }, { 2085,   3, PER_DGT_KD,  "" }, { 2100,   3, PER_DGT_KD,  "" },
    { 2115,   3, PER_DGT_KD,  "" }, { 2130,   3, PER_DGT_KD,  "" }, { 2145,   3, PER_DGT_KD,  "" },
    { 2160,   3, PER_DGT_KD,  "" }, { 2175,   3, PER_DGT_KD,  "" }, { 2190,   3, PER_DGT_KD,  "" },
    { 2205,   3, PER_DGT_KD,  "" }, { 2220,   3, PER_DGT_KD,  "" },
    { 2250,   3, PER_DGT_KD,  "P19 CONTROLS ROW AGAIN" },     /* 12: + Saves, + Manage / Import Saves */
    { 2325,   3, PER_DGT_KR,  "P20 CONTROLS PAGE" },
    { 2405,   3, PER_DGT_KD,  "" }, { 2420,   3, PER_DGT_KD,  "" }, { 2435,   3, PER_DGT_KD,  "" },
    { 2450,   3, PER_DGT_KD,  "" }, { 2465,   3, PER_DGT_KD,  "" }, { 2480,   3, PER_DGT_KD,  "" },
    { 2510,   3, PER_DGT_KD,  "P21 RESET ROW" },               /* 7 actions (no Info row since 2026-11-23) */
    { 2630,   3, PER_DGT_KR,  "P22 RESET DONE" },
    { 2750,   3, PER_DGT_ST,  "P23 CLOSED SAVED" },
    { 2870,   1, 0,           "P24 END" },
};

/* VRTILES.BIN off HWRAM: the five virtual root folders' tiles, one at a time, 240 frames apart, so each one
 * can be captured at rest and pixel-compared between builds (FAKECARD.BIN built with `--tiles`) */
static const hstep k_tiles[] = {
    {   60,   1, 0,           "T1 FAVORITES TILE" },
    {  300,   3, PER_DGT_KD,  "T2 RECENTLY PLAYED TILE" },
    {  540,   3, PER_DGT_KD,  "T3 A-Z TILE" },
    {  780,   3, PER_DGT_KD,  "T4 GENRE TILE" },
    { 1020,   3, PER_DGT_KD,  "T5 REGION TILE" },
    { 1260,   1, 0,           "T6 END" },
};

/* --diag page3, the third diagnostics page (the BIGLOGO line), after the same release gaps as DIAG2 */
static const hstep k_diag5[] = {
    {   60,   1, 0,                                              "H1 ROOT" },
    {  120,  20, PER_DGT_TL | PER_DGT_TR,                         "H2 HOLD L+R" },
    {  140,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "H3 DIAG COMBO" },
    {  145,  10, 0,                                               "" },
    {  160,   5, PER_DGT_TR,                                     "H4 DIAG PAGE 2" },
    {  165,  10, 0,                                               "" },
    {  180,   5, PER_DGT_TR,                                     "H5 DIAG PAGE 3 (STAYS HERE)" },
};

/* the screensaver (FAKESCRIPT.IDLE, with saver=1 in SETTINGS.INI): leave the pad ~83 s, then one A that
 * must wake the saver and do nothing else (the cursor stays on Favorites at the root) */
static const hstep k_idle[] = {
    {   60,   1, 0,           "I1 ROOT" },
    {  120,   1, 0,           "I2 IDLE" },
    { 5000,   3, PER_DGT_TA,  "I3 WAKE" },
    { 5200,   1, 0,           "I4 END" },
};

/* FAKESCRIPT.LOADER, `python -m omsat.fakecard --loader-script`: the loading screen's meta line names the
 * loader. Favorites, launch the third game (no compat row: "CWX (auto)"); after the stub launch returns, Up to the second
 * (GAMES.DB hint JHL: "JHL (database)") and launch it; idle after. Timing as k_loading (its L3/L4 rows). */
static const hstep k_loader[] = {
    {   60,   1, 0,           "K1 ROOT" },
    {  120,   3, PER_DGT_TA,  "K2 FAVORITES" },
    {  200,   3, PER_DGT_KD,  "" },
    {  215,   3, PER_DGT_KD,  "" },
    {  240,   3, PER_DGT_TA,  "K3 LAUNCH: CWX (AUTO)" },
    {  600,   3, PER_DGT_KU,  "K4 UP" },
    {  640,   3, PER_DGT_TA,  "K5 LAUNCH: JHL (DATABASE)" },
    { 1000,   1, 0,           "K6 END" },
};

/* FAKESCRIPT.GAME, `python -m omsat.fakecard --game-script --diag lines`: Favorites; A on its first game
 * (alpha_racer.iso, an IP stand-in with product MK-81045, area U): the panel opens on Launch (user decision
 * 2026-11-23, row 0); Down, off Launch, onto Loader: "Auto (CWX)"; Right: "CWX"; Down, Right: Saves On; B saves
 * (PERGAME.INI: the two seeded sections + this one = 3); A: the same game's panel again (Launch); Down x8 to Reset
 * to defaults (the last row: a ninth Down would wrap to Launch); A ("Press A again"), A: reset, the panel closes (2
 * sections: the seed). Each save shows the file's length and CRC-32
 * on NBG0 row 23 (folders.c pg_save). */
static const hstep k_game[] = {
    {   60,   1, 0,           "G1 ROOT" },
    {  120,   3, PER_DGT_TA,  "G2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "G3 PANEL (LAUNCH HIGHLIGHTED)" },
    {  300,   3, PER_DGT_KD,  "G3B OFF LAUNCH, ONTO LOADER" },
    {  360,   3, PER_DGT_KR,  "G4 LOADER CWX" },
    {  480,   3, PER_DGT_KD,  "" }, {  495,   3, PER_DGT_KR,  "G5 SAVES ON" },
    {  615,   3, PER_DGT_TB,  "G6 CLOSED, SAVED" },
    {  735,   3, PER_DGT_TA,  "G7 PANEL AGAIN (LAUNCH)" },
    {  855,   3, PER_DGT_KD,  "" }, {  870,   3, PER_DGT_KD,  "" }, {  885,   3, PER_DGT_KD,  "" },
    {  900,   3, PER_DGT_KD,  "" }, {  915,   3, PER_DGT_KD,  "" }, {  930,   3, PER_DGT_KD,  "" },
    {  945,   3, PER_DGT_KD,  "" },
    {  960,   3, PER_DGT_KD,  "G8 RESET ROW (OM_OGR_RESET = 8)" },
    { 1080,   3, PER_DGT_TA,  "G9 PRESS A AGAIN" },
    { 1200,   3, PER_DGT_TA,  "G10 FORGOTTEN, CLOSED" },
    { 1320,   1, 0,           "G11 END" },
};

/* M6 Task 18, the eye-animation preload (FAKESCRIPT.PGLOAD, `--pgload-script`: the --loading card, its stub, and an IP
 * stand-in for the dressed game): as k_loading's first launch (L3), but the game's panel is open for 4 s first (A,
 * then B; 2026-11-23: A opens it, C no longer does; the launch is then A, A: the panel again, its Launch row): LOADBG.BIN's HWRAM part is read meanwhile, so the loading screen's background plays from its first frame.
 * A comes 5 frames after the last move, before the details card has read the disc's IP (review I1: the panel opens the
 * image itself while a neighbor's cover prefetch may still hold its file; the panel prints the fake SD layer's most
 * files open at once, FDMAX). This script's frames are V-blanks, not loop iterations (om_fakepad_tick),
 * so a panel that drops frames while it reads still dwells 260 V-blanks (4.33 s) */
#define PGLOAD_VB_FROM 222u   /* the A press (220, held 3) has been seen: loop iterations before, V-blanks after */
static const hstep k_pgload[] = {
    {   60,   1, 0,           "Q1 ROOT" },
    {  120,   3, PER_DGT_TA,  "Q2 FAVORITES" },
    {  200,   3, PER_DGT_KD,  "" },
    {  215,   3, PER_DGT_KD,  "" },
    {  220,   3, PER_DGT_TA,  "Q3 PANEL (READ AHEAD)" },   /* 5 frames after the move: review I1's race */
    {  480,   3, PER_DGT_TB,  "Q4 CLOSED" },          /* 3 frames, as every script: the panel's reads latch it */
    {  500,   3, PER_DGT_TA,  "Q5 PANEL AGAIN" },
    {  540,   3, PER_DGT_TA,  "Q5B LAUNCH" },
    {  900,   1, 0,           "Q6 END" },
};

/* the same launch after a 15 s static panel (900 V-blanks), with the panel's
 * iterations forced to 8 V-blanks and a 9 V-blank stall every 50th (fakecard --pglong-script; folders.c and
 * loading.c read the marker): the music's refill must keep up, MUSR on the panel's close says PASS or FAIL */
static const hstep k_pglong[] = {
    {   60,   1, 0,           "R1 ROOT" },
    {  120,   3, PER_DGT_TA,  "R2 FAVORITES" },
    {  200,   3, PER_DGT_KD,  "" },
    {  215,   3, PER_DGT_KD,  "" },
    {  220,   3, PER_DGT_TA,  "R3 PANEL 15 S (SLOW)" },
    {  320,   3, PER_DGT_KD,  "" },                    /* 8 taps of 3 frames while the panel */
    {  420,   3, PER_DGT_KU,  "" },                    /* reads (8 V-blank iterations): PANEL ... P must count 9 */
    {  520,   3, PER_DGT_KD,  "" },                    /* (8 + the B), none lost */
    {  620,   3, PER_DGT_KU,  "" },
    {  720,   3, PER_DGT_KD,  "" },
    {  820,   3, PER_DGT_KU,  "" },
    {  920,   3, PER_DGT_KD,  "" },
    { 1020,   3, PER_DGT_KU,  "" },
    { 1120,   3, PER_DGT_TB,  "R4 CLOSED" },
    { 1140,   3, PER_DGT_TA,  "R5 PANEL AGAIN" },
    { 1180,   3, PER_DGT_TA,  "R5B LAUNCH" },
    { 1540,   1, 0,           "R6 END" },
};

/* FAKECARD.BIN built with `python -m omsat.fakecard --pad-script [--pad3d]`: Favorites' first row is
 * game 0, whose compat bytes fakecard.py's --pad-script sets to Golden Axe: The Duel's (0x03, PADWARN_NOTE); A opens
 * the panel on Launch (user decision 2026-11-23), A again starts it. With --pad3d (om_fakepad_port1_id) the
 * pre-launch warning should show (it fires inside launch_selected, only once Launch is confirmed); without it the
 * launch proceeds straight away, as a standard pad does on hardware. M6 Task 19 fix round 1 (review-task-19.md Step
 * 4 gap: the brief's "games without a note are pixel-identical" capture-diff): W2B/W2C dip down to row 1 (Cosmic
 * Quest, no compat row) and back, so one script also captures a non-flagged game's details card next to the
 * flagged one, without a second FAKECARD.BIN variant. */
static const hstep k_padwarn[] = {
    {   60,   1, 0,          "W1 ROOT" },
    {  120,   3, PER_DGT_TA, "W2 FAVORITES (ALPHA RACER, FLAGGED)" },
    {  180,   3, PER_DGT_KD, "W2B DOWN (COSMIC QUEST, NO NOTE)" },
    {  300,   3, PER_DGT_KU, "W2C UP (BACK TO ALPHA RACER)" },
    {  420,   3, PER_DGT_TA, "W3 PANEL (LAUNCH HIGHLIGHTED)" },
    {  540,   3, PER_DGT_TA, "W3B LAUNCH (WARNING IF --pad3d)" },
    {  660,   1, 0,          "W4 END" },
};

/* Settings > About (FAKESCRIPT.ABOUT, `python -m omsat.fakecard --about-script`): Start, Down x13 (THEME=0 to
 * ABOUT=13, the last row: a 14th would wrap to Theme), A opens its popup (2026-11-27), Down scrolls a line, Right a
 * page, B returns to Settings (About row still selected). Captures: the Settings row scrolled into view (">",
 * OM_OVR_ABOUT), the popup at its top, scrolled, a page on, and the return. */
static const hstep k_about[] = {
    {   60,   1, 0,           "AB1 ROOT" },
    {  120,   3, PER_DGT_ST,  "AB2 SETTINGS" },
    {  200,   3, PER_DGT_KD,  "" }, {  215,   3, PER_DGT_KD,  "" }, {  230,   3, PER_DGT_KD,  "" },
    {  245,   3, PER_DGT_KD,  "" }, {  260,   3, PER_DGT_KD,  "" }, {  275,   3, PER_DGT_KD,  "" },
    {  290,   3, PER_DGT_KD,  "" }, {  305,   3, PER_DGT_KD,  "" }, {  320,   3, PER_DGT_KD,  "" },
    {  335,   3, PER_DGT_KD,  "" }, {  350,   3, PER_DGT_KD,  "" }, {  365,   3, PER_DGT_KD,  "" },
    {  410,   3, PER_DGT_KD,  "AB3 ABOUT ROW" },
    {  530,   3, PER_DGT_TA,  "AB4 ABOUT PAGE" },
    {  770,   3, PER_DGT_KD,  "AB5 SCROLLED A LINE" },
    {  890,   3, PER_DGT_KR,  "AB6 A PAGE ON" },
    { 1010,   3, PER_DGT_TB,  "AB7 BACK TO SETTINGS" },
    { 1130,   1, 0,           "AB8 END" },
};

/* The launch panel (FAKESCRIPT.LAUNCH, `python -m omsat.fakecard --launch-script`): Favorites, A on its first game
 * (user decision 2026-11-23: opens the panel on Launch, row 0, instead of launching at once -- needs the same
 * IP.BIN stand-in as --game-script, so game 0 has a product id to key PERGAME.INI by), then A again: the double-tap
 * that starts it with the settings shown (the fake build stops at "would hand off now"). Captures: the panel open
 * on Launch, and the launch result. */
static const hstep k_launch[] = {
    {   60,   1, 0,           "LA1 ROOT" },
    {  120,   3, PER_DGT_TA,  "LA2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "LA3 PANEL (LAUNCH HIGHLIGHTED)" },
    {  390,   3, PER_DGT_TA,  "LA4 LAUNCH (DOUBLE-TAP)" },
    {  600,   1, 0,           "LA5 END" },
};

/* 2026-11-25 (FAKESCRIPT.TOOL, `fakecard --tool-script`, the --launch-script card plus the LAUNCHER.BIN stub): the
 * launch panel's tools hand off through the loading screen, as a launch does (no NBG0 text): Favorites, A on its first
 * game (the panel), Down x5 to Cheats, A (the eye with "CHEATS"; the fake build stops at "would hand off now"), Up to
 * Manage saves, A (the same with "MANAGE SAVES") */
static const hstep k_tool[] = {
    {   60,   1, 0,           "T1 ROOT" },
    {  120,   3, PER_DGT_TA,  "T2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "T3 PANEL" },
    {  300,   3, PER_DGT_KD,  "" }, { 315, 3, PER_DGT_KD, "" }, { 330, 3, PER_DGT_KD, "" }, { 345, 3, PER_DGT_KD, "" },
    {  360,   3, PER_DGT_KD,  "T4 CHEATS ROW" },
    {  480,   3, PER_DGT_TA,  "T5 CHEATS (LOADING)" },
    {  900,   3, PER_DGT_KU,  "T6 MANAGE SAVES ROW" },
    { 1020,   3, PER_DGT_TA,  "T7 MANAGE SAVES (LOADING)" },
    { 1500,   1, 0,           "T8 END" },
};

/* 2026-11-25, the return from a tool (fakecard --return-script cheats|saves|settings, with FAKESCRIPT.RETURN): the
 * hand-off as above, then the fake build arms the return block, carries it in sound RAM and starts the menu again from
 * its first instruction (folders.c load_run); that start skips the intro and reopens the list and the panel, and this script stays idle (g_om_ret_ok) */
static const hstep k_retch[] = {
    {   60,   1, 0,           "R1 ROOT" },
    {  120,   3, PER_DGT_TA,  "R2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "R3 PANEL" },
    {  300,   3, PER_DGT_KD,  "" }, { 315, 3, PER_DGT_KD, "" }, { 330, 3, PER_DGT_KD, "" }, { 345, 3, PER_DGT_KD, "" },
    {  360,   3, PER_DGT_KD,  "R4 CHEATS ROW" },
    {  480,   3, PER_DGT_TA,  "R5 CHEATS (HAND-OFF)" },
};
static const hstep k_retsv[] = {
    {   60,   1, 0,           "R1 ROOT" },
    {  120,   3, PER_DGT_TA,  "R2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "R3 PANEL" },
    {  300,   3, PER_DGT_KD,  "" }, { 315, 3, PER_DGT_KD, "" }, { 330, 3, PER_DGT_KD, "" },
    {  345,   3, PER_DGT_KD,  "R4 MANAGE SAVES ROW" },
    {  480,   3, PER_DGT_TA,  "R5 MANAGE SAVES (HAND-OFF)" },
};
static const hstep k_retset[] = {
    {   60,   1, 0,           "R1 ROOT" },
    {  120,   3, PER_DGT_ST,  "R2 SETTINGS" },
    {  200,   3, PER_DGT_KD,  "" }, {  215,   3, PER_DGT_KD,  "" }, {  230,   3, PER_DGT_KD,  "" },
    {  245,   3, PER_DGT_KD,  "" }, {  260,   3, PER_DGT_KD,  "" }, {  275,   3, PER_DGT_KD,  "" },
    {  290,   3, PER_DGT_KD,  "" }, {  305,   3, PER_DGT_KD,  "" },
    {  320,   3, PER_DGT_KD,  "R3 MANAGE SAVES ROW" },
    {  440,   3, PER_DGT_TA,  "R4 MANAGE SAVES (HAND-OFF)" },
};

/* 2026-11-25, the in-menu popups (fakecard --popup-script cheats|saves|settings): the launch panel's Cheats and Manage
 * saves rows, and Settings > Manage saves, open over the dimmed list (popup.h) -- each page captured, then B back to
 * the panel or Settings with no hand-off. Cheats: a toggle, the codes page, B saves and closes. Manage saves: the three
 * tabs, a card set, a save copied into the console (a real BUP write on the emulator's BRAM), the console tab with it,
 * its options and Delete's first press. Settings: the game list, one game's card sets, back. */
static const hstep k_popch[] = {
    {   60,   1, 0,           "P1 ROOT" },
    {  120,   3, PER_DGT_TA,  "P2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "P3 PANEL" },
    {  300,   3, PER_DGT_KD,  "" }, { 315, 3, PER_DGT_KD, "" }, { 330, 3, PER_DGT_KD, "" }, { 345, 3, PER_DGT_KD, "" },
    {  360,   3, PER_DGT_KD,  "P4 CHEATS ROW" },
    {  480,   3, PER_DGT_TA,  "P5 CHEATS POPUP" },
    {  600,   3, PER_DGT_KD,  "P6 SECOND CHEAT" },
    {  660,   3, PER_DGT_TA,  "P7 TOGGLED ON" },
    {  720,   3, PER_DGT_KU,  "P8 FIRST CHEAT" },
    {  780,   3, PER_DGT_TY,  "P9 ITS CODES" },
    {  900,   3, PER_DGT_TB,  "P10 THE LIST AGAIN" },
    {  960,   3, PER_DGT_TB,  "P11 SAVED, THE PANEL" },
    { 1140,   1, 0,           "P12 END" },
};
static const hstep k_popsv[] = {
    {   60,   1, 0,           "S1 ROOT" },
    {  120,   3, PER_DGT_TA,  "S2 FAVORITES" },
    {  240,   3, PER_DGT_TA,  "S3 PANEL" },
    {  300,   3, PER_DGT_KD,  "" }, { 315, 3, PER_DGT_KD, "" }, { 330, 3, PER_DGT_KD, "" },
    {  345,   3, PER_DGT_KD,  "S4 MANAGE SAVES ROW" },
    {  480,   3, PER_DGT_TA,  "S5 CONSOLE TAB" },
    {  600,   3, PER_DGT_KR,  "S6 CARTRIDGE TAB" },
    {  660,   3, PER_DGT_KR,  "S7 CARD TAB: SETS" },
    {  760,   3, PER_DGT_TA,  "S8 A SET" },
    {  840,   3, PER_DGT_TA,  "S9 ITS OPTIONS" },
    {  920,   3, PER_DGT_TA,  "S10 COPIED TO THE CONSOLE" },
    { 1040,   3, PER_DGT_KR,  "S11 CONSOLE TAB" },
    { 1120,   3, PER_DGT_TA,  "S12 CONSOLE OPTIONS" },
    { 1180,   3, PER_DGT_KU,  "S13 DELETE ROW" },
    { 1240,   3, PER_DGT_TA,  "S14 DELETE ASKS TWICE" },
    { 1340,   3, PER_DGT_TB,  "" },
    { 1380,   3, PER_DGT_TB,  "" },
    { 1420,   3, PER_DGT_TB,  "S15 THE PANEL" },
    { 1560,   1, 0,           "S16 END" },
};
static const hstep k_popset[] = {
    {   60,   1, 0,           "Q1 ROOT" },
    {  120,   3, PER_DGT_ST,  "Q2 SETTINGS" },
    {  200,   3, PER_DGT_KD,  "" }, {  215,   3, PER_DGT_KD,  "" }, {  230,   3, PER_DGT_KD,  "" },
    {  245,   3, PER_DGT_KD,  "" }, {  260,   3, PER_DGT_KD,  "" }, {  275,   3, PER_DGT_KD,  "" },
    {  290,   3, PER_DGT_KD,  "" }, {  305,   3, PER_DGT_KD,  "" },
    {  320,   3, PER_DGT_KD,  "Q3 MANAGE SAVES ROW" },
    {  440,   3, PER_DGT_TA,  "Q4 THE GAMES" },
    {  540,   3, PER_DGT_KD,  "Q5 SECOND GAME" },
    {  600,   3, PER_DGT_TA,  "Q6 ITS CARD SETS" },
    {  720,   3, PER_DGT_TB,  "Q7 THE GAMES AGAIN" },
    {  820,   3, PER_DGT_TB,  "Q8 SETTINGS AGAIN" },
    {  960,   1, 0,           "Q9 END" },
};

/* hardware report on 3c64908 (A, B, A, B ... froze the menu): FAKESCRIPT.PGCYCLE (`fakecard --pgcycle-script`, the
 * --pgload-script card): Favorites' first game (the dressed one, with an IP stand-in), then PGCYCLE_N cycles of A (the
 * panel opens; after 15 quiet iterations its read-ahead runs) and, 30 loop iterations later, B (it closes), one every PGCYCLE_LEN iterations (om_fakepad_tick generates
 * them); the label counts the cycles begun */
#define PGCYCLE_FROM 260u
#define PGCYCLE_LEN  40u
#define PGCYCLE_N    200u
static const hstep k_pgcycle[] = {
    {   60,   1, 0,           "Y1 ROOT" },
    {  120,   3, PER_DGT_TA,  "Y2 FAVORITES" },
    {  215,   1, 0,           "Y3 CYCLES DONE" },          /* the first favorite: the dressed game, with its IP */
};

static uint32_t s_frame;
/* --diag page4 (the --sdtest disc), the SD stress page: R three times, then A arms and A starts the run */
static const hstep k_diag6[] = {
    {   60,   1, 0,                                              "J1 ROOT" },
    {  120,  20, PER_DGT_TL | PER_DGT_TR,                         "J2 HOLD L+R" },
    {  140,   5, PER_DGT_TL | PER_DGT_TR | PER_DGT_ST,            "J3 DIAG COMBO" },
    {  145,  10, 0,                                               "" },
    {  160,   5, PER_DGT_TR,                                     "J4 DIAG PAGE 2" },
    {  165,  10, 0,                                               "" },
    {  180,   5, PER_DGT_TR,                                     "J5 DIAG PAGE 3" },
    {  185,  10, 0,                                               "" },
    {  200,   5, PER_DGT_TR,                                     "J6 DIAG PAGE 4" },
    {  205,  10, 0,                                               "" },
    {  220,   5, PER_DGT_TA,                                     "J7 ARM" },
    {  225,  10, 0,                                               "" },
    {  240,   5, PER_DGT_TA,                                     "J8 RUN (STAYS HERE)" },
};
static Uint16 s_held;
static const char *s_label = "";
static int s_script = -1;
static uint8_t s_vb_mode;    /* a PGLOAD/PGLONG script past its C press: s_frame = V-blanks - s_vb_base */
static uint32_t s_vb_base;    /* 0 tour, 1 held scroll, 2 loading (13c), 3 and up: a k_steps[] row (s_script - 3) */

/* The step scripts, one marker file each (tools/omsat/fakecard.py), first match wins (M6 C2: one table instead of a
 * ternary chain per script) */
#define NSTEPS(a) (uint32_t)(sizeof a / sizeof a[0])
static const struct { const char *marker; const hstep *k; uint32_t n; } k_steps[] = {
    { "aether/FAKESCRIPT.TILES",    k_tiles,    NSTEPS(k_tiles) },      /* M6 C3 */
    { "aether/FAKESCRIPT.SETTINGS", k_settings, NSTEPS(k_settings) },   /* M6 Task 4 */
    { "aether/FAKESCRIPT.DIAG2",    k_diag2,    NSTEPS(k_diag2) },      /* 16a: page 2 */
    { "aether/FAKESCRIPT.DIAG3",    k_diag3,    NSTEPS(k_diag3) },      /* 16a: dismiss */
    { "aether/FAKESCRIPT.DIAG4",    k_diag4,    NSTEPS(k_diag4) },
    { "aether/FAKESCRIPT.DIAG5",    k_diag5,    NSTEPS(k_diag5) },      /* page 3 */
    { "aether/FAKESCRIPT.DIAG6",    k_diag6,    NSTEPS(k_diag6) },      /* page 4 (--sdtest) */
    { "aether/FAKESCRIPT.DIAG1",    k_diag,     NSTEPS(k_diag) },       /* --diag page1 */
    { "aether/FAKESCRIPT.IDLE",     k_idle,     NSTEPS(k_idle) },       /* M6 Task 14 */
    { "aether/FAKESCRIPT.LOADER",   k_loader,   NSTEPS(k_loader) },     /* M6 Task 17 */
    { "aether/FAKESCRIPT.GAME",     k_game,     NSTEPS(k_game) },       /* M6 Task 18 */
    { "aether/FAKESCRIPT.PGCYCLE",  k_pgcycle,  NSTEPS(k_pgcycle) },    /* A/B panel cycles, before PGLOAD */
    { "aether/FAKESCRIPT.PGLONG",   k_pglong,   NSTEPS(k_pglong) },     /* M6 Task 18b fix round, before PGLOAD */
    { "aether/FAKESCRIPT.PGLOAD",   k_pgload,   NSTEPS(k_pgload) },     /* M6 Task 18 */
    { "aether/FAKESCRIPT.PADWARN",  k_padwarn,  NSTEPS(k_padwarn) },    /* M6 Task 19 */
    { "aether/FAKESCRIPT.ABOUT",    k_about,    NSTEPS(k_about) },     /* Settings > About */
    { "aether/FAKESCRIPT.LAUNCH",   k_launch,   NSTEPS(k_launch) },    /* the launch panel, A then A (double-tap) */
    { "aether/FAKESCRIPT.TOOL",     k_tool,     NSTEPS(k_tool) },      /* the panel's tools via the loading screen */
    { "aether/FAKESCRIPT.RETCH",    k_retch,    NSTEPS(k_retch) },     /* Cheats, then the return (RETURN marker) */
    { "aether/FAKESCRIPT.RETSV",    k_retsv,    NSTEPS(k_retsv) },     /* Manage saves, then the return */
    { "aether/FAKESCRIPT.RETSET",   k_retset,   NSTEPS(k_retset) },    /* Settings > Manage saves, then the return */
    { "aether/FAKESCRIPT.POPCH",    k_popch,    NSTEPS(k_popch) },     /* the in-menu popups (popup.h) */
    { "aether/FAKESCRIPT.POPSV",    k_popsv,    NSTEPS(k_popsv) },
    { "aether/FAKESCRIPT.POPSET",   k_popset,   NSTEPS(k_popset) },
};

/* 2026-11-25 freeze hunt (fakecard --soak cheats|settings): FAKESCRIPT.SOAK carries the script as data, so a soak costs
 * almost no HWRAM: rows of { u16 keys, u8 frames held, u8 frames to the next row }, a row of 0 frames ending them;
 * they repeat for as long as the emulator runs, each round starting and ending at the list's root. 1: the soak runs
 * (s_held set), 0: none on this card. om_fakepad_soak.rows counts the rows played (a savestate reads it by symbol) */
struct { const uint8_t *d, *r; uint32_t at, rows; } om_fakepad_soak;
static __attribute__((noinline, optimize("Os"))) int soak(void)
{
    typeof(om_fakepad_soak) *k = &om_fakepad_soak;
    if (!k->r) {
        if (k->d || !(k->d = om_fakesat_data("aether/FAKESCRIPT.SOAK"))) return 0;   /* looked once */
        k->r = k->d;
    }
    if (k->at == k->r[3]) { k->at = 0; k->rows++; if (!(k->r += 4)[3]) k->r = k->d; }
    s_held = k->at++ < k->r[2] ? (Uint16)(k->r[0] << 8 | k->r[1]) : 0;
    return 1;
}

extern int g_om_ret_ok;      /* main.c: this start is a tool's return -- the script already ran before the reset */

/* -Os: fake-only, once a frame, and the fake build sits at the 64 KB floor (the soak above paid for itself here) */
__attribute__((optimize("Os"))) void om_fakepad_tick(void)
{
    uint32_t i;
    if (g_om_ret_ok) { s_held = 0; s_label = "RETURNED"; return; }
    if (s_script < 0 && soak()) return;
    if (s_script < 0) {          /* the order of the M6 Task 4 / C3 chain: TILES, SETTINGS, then SCROLL, LOADING, the rest */
        for (i = 0; i < NSTEPS(k_steps) && s_script < 0; i++) {
            if (i == 2 && om_fakesat_has("aether/FAKESCRIPT.SCROLL")) s_script = 1;
            else if (i == 2 && om_fakesat_has("aether/FAKESCRIPT.LOADING")) s_script = 2;
            else if (om_fakesat_has(k_steps[i].marker)) s_script = 3 + (int)i;
        }
        if (s_script < 0) s_script = 0;
    }
    s_held = 0;
    if (s_script >= 3) {
        const hstep *k = k_steps[s_script - 3].k;
        uint32_t n = k_steps[s_script - 3].n;
        if ((k == k_pgload || k == k_pglong) && s_frame >= PGLOAD_VB_FROM) {   /* from the panel on, */
            uint32_t vb = om_v2_vblanks();                  /* V-blank time, so frame drops keep the panel's dwell */
            if (!s_vb_mode) { s_vb_mode = 1; s_vb_base = vb - s_frame; }
            s_frame = vb - s_vb_base;
        }
        for (i = 0; i < n; i++) {
            if (s_frame >= k[i].frame && s_frame < (uint32_t)k[i].frame + k[i].hold)
                s_held = k[i].keys;
            if (s_frame >= k[i].frame)
                s_label = k[i].label;
        }
        if (k == k_pgcycle && s_frame >= PGCYCLE_FROM && (s_frame - PGCYCLE_FROM) / PGCYCLE_LEN < PGCYCLE_N) {
            uint32_t ph = (s_frame - PGCYCLE_FROM) % PGCYCLE_LEN;
            static char labs[2][8];                     /* two buffers: folders.c redraws on a new pointer */
            uint32_t c = (s_frame - PGCYCLE_FROM) / PGCYCLE_LEN + 1u;
            char *lab = labs[c & 1u];
            if (ph < 3u) s_held = PER_DGT_TA;
            else if (ph >= 30u && ph < 33u) s_held = PER_DGT_TB;
            lab[0] = 'C'; lab[1] = (char)('0' + c / 100u); lab[2] = (char)('0' + c / 10u % 10u);
            lab[3] = (char)('0' + c % 10u); lab[4] = 0;
            s_label = lab;
        }
        if (!s_vb_mode) s_frame++;
        return;
    }
    if (s_script) {
        const hstep *k = s_script == 1 ? k_scroll : k_loading;
        uint32_t n = s_script == 1 ? sizeof k_scroll / sizeof k_scroll[0] : sizeof k_loading / sizeof k_loading[0];
        for (i = 0; i < n; i++) {
            /* without the LAUNCHER.BIN stub the launch fails and L4 returns to the menu: the cancel step is skipped
             * there, since A+B+C+Start in the menu is the return to the official menu (HWF-21) */
            if (s_script == 2 && (k[i].keys & PER_DGT_TC) && !om_fakesat_has("aether/LAUNCHER.BIN"))
                continue;
            if (s_frame >= k[i].frame && s_frame < (uint32_t)k[i].frame + k[i].hold)
                s_held = k[i].keys;
            if (s_frame >= k[i].frame)
                s_label = k[i].label;
        }
        s_frame++;
        return;
    }
    for (i = 0; i < sizeof k_tour / sizeof k_tour[0]; i++) {
        if (s_frame >= k_tour[i].frame && s_frame < k_tour[i].frame + 3u)
            s_held = k_tour[i].keys;
        if (s_frame >= k_tour[i].frame && k_tour[i].label[0])
            s_label = k_tour[i].label;
    }
    s_frame++;
}

Uint16 om_fakepad_held(void) { return s_held; }

Uint16 om_fakepad_peek(void)
{   /* the keys at this V-blank, between two polls (a V-blank-timed script only) */
    const hstep *k;
    uint32_t i, n, f;
    Uint16 h = 0;
    if (!s_vb_mode) return s_held;
    k = k_steps[s_script - 3].k;
    n = k_steps[s_script - 3].n;
    f = om_v2_vblanks() - s_vb_base;
    for (i = 0; i < n; i++)
        if (f >= k[i].frame && f < (uint32_t)k[i].frame + k[i].hold)
            h = k[i].keys;
    return h;
}
const char *om_fakepad_label(void) { return s_label; }

/* a software-only 3D-pad id for the pre-launch warning's fake capture -- Mednafen has no way to present
 * the Sega 3D Control Pad itself, so FAKESCRIPT.PAD3D stands in for it. Without that marker, port 1 reads whatever
 * Mednafen's own SMPC emulation reports (normally the standard pad, 0x02). */
Uint8 om_fakepad_port1_id(void)
{
    return om_fakesat_has("aether/FAKESCRIPT.PAD3D") ? (Uint8)0x16 : (Per_Connect1 ? Smpc_Peripheral[0].id : (Uint8)0xFF);
}
