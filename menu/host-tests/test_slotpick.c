/* menu/host-tests/test_slotpick.c -- per-game saves' defaults and slot UX (menu/src/slotpick.c and
 * loader.c): om_saves_want (global On by default, the per-game Saves row over it, never for a save tool), the save-tool
 * match at launch (save_tools.json's product numbers and title substrings, kept in sync by
 * tools/tests/test_save_tools.py), the slot picker's keys, its first slot, its status line, and ACTIVE.TXT's first two
 * fields (which game's saves the console holds, for the details card). BSD-3-Clause. */
#include "th.h"
#include "loader.h"
#include "overlay.h"
#include "pergame.h"
#include "slotpick.h"

int main(void)
{
    uint8_t slot;
    char gid[OM_GID_MAX], line[64];
    int i;

    /* --- om_saves_want: on when global on and the game not set Off, or the game set On; never for a save tool ---- */
    CHECK_INT(om_saves_want(1, OM_PG_DEFAULT, 0), 1);
    CHECK_INT(om_saves_want(0, OM_PG_DEFAULT, 0), 0);
    CHECK_INT(om_saves_want(0, OM_PG_ON, 0), 1);
    CHECK_INT(om_saves_want(1, OM_PG_OFF, 0), 0);
    CHECK_INT(om_saves_want(1, OM_PG_ON, 1), 0);
    CHECK_INT(om_saves_want(1, OM_PG_DEFAULT, 1), 0);
    CHECK_INT(om_saves_want(1, 77, 0), 1);                       /* a hand-edited byte reads as Default */
    CHECK_INT(om_saves_want(0, 77, 0), 0);

    /* --- the save tools (save_tools.json): product numbers exactly, title substrings case-insensitively -------- */
    CHECK_INT(om_savetool_ip("SGC", "JO ENGINE"), 1);
    CHECK_INT(om_savetool_ip("SATAKORE", ""), 1);
    CHECK_INT(om_savetool_ip("T-060680", ""), 1);
    CHECK_INT(om_savetool_ip("610-6276", ""), 1);
    CHECK_INT(om_savetool_ip("T-042G", "SATURN SAVE DATA MANAGER v1.2"), 1);
    CHECK_INT(om_savetool_ip("X-1", "Released by The Rockin'-B for SEGA Saturn"), 1);
    CHECK_INT(om_savetool_ip("X-1", "SEGA SATURN     FDD OPERATOR"), 1);
    CHECK_INT(om_savetool_ip("SGC2", "JO ENGINE"), 0);           /* products compare exactly */
    CHECK_INT(om_savetool_ip("GS-9031", "GUARDIAN HEROES"), 0);
    CHECK_INT(om_savetool_ip(NULL, NULL), 0);
    CHECK_INT(OM_CF_SAVETOOL, 0x80);                             /* GAMES.DB compat bit 7 (SPEC §5.3) */

    /* --- the picker: Left/Right walk 0..9 and wrap, A launches, B cancels, the rest does nothing ----------------- */
    slot = 0;
    CHECK_INT(om_slot_key(&slot, OM_K_LEFT), 0); CHECK_INT(slot, 9);
    CHECK_INT(om_slot_key(&slot, OM_K_RIGHT), 0); CHECK_INT(slot, 0);
    for (i = 0; i < 3; i++) om_slot_key(&slot, OM_K_RIGHT);
    CHECK_INT(slot, 3);
    CHECK_INT(om_slot_key(&slot, OM_K_UP | OM_K_DOWN), 0); CHECK_INT(slot, 3);
    CHECK_INT(om_slot_key(&slot, OM_K_A), OM_SP_LAUNCH); CHECK_INT(slot, 3);
    CHECK_INT(om_slot_key(&slot, OM_K_B), OM_SP_CANCEL);
    CHECK_INT(om_slot_key(&slot, OM_K_START), OM_SP_LAUNCH);    /* Start: launch, as A does */
    slot = 12;
    om_slot_key(&slot, OM_K_RIGHT);
    CHECK(slot <= 9);

    /* --- ACTIVE.TXT's first two fields; the picker's first slot is the one the console holds for this game ------ */
    CHECK_INT(om_active_parse("GS-9031@U 3 0000A001 1234ABCD\n", 30, gid, &slot), 0);
    CHECK_STR(gid, "GS-9031@U"); CHECK_INT(slot, 3);
    CHECK_INT(om_active_parse("GS-9031@U 3 0000A001 1234ABCD", 29, gid, &slot), 0);   /* no newline */
    CHECK(om_active_parse("GS-9031@U x 0000A001\n", 21, gid, &slot) != 0);
    CHECK(om_active_parse("bad id 3\n", 9, gid, &slot) != 0);
    CHECK(om_active_parse("", 0, gid, &slot) != 0);
    CHECK_STR(gid, "");
    CHECK_INT(om_slot_first("GS-9031@U", "GS-9031@U", 4), 4);
    CHECK_INT(om_slot_first("GS-9031@U", "GS-9125@U", 4), 0);
    CHECK_INT(om_slot_first("GS-9031@U", "", 4), 0);

    /* --- the status line, under the slot (34 columns at most) ---------------------------------------------------- */
    om_slot_line(line, sizeof line, 0, 0, 0);  CHECK_STR(line, "SLOT 0: EMPTY");
    om_slot_line(line, sizeof line, 3, 1, 0);  CHECK_STR(line, "SLOT 3: 1 SAVE");
    om_slot_line(line, sizeof line, 3, 12, 0); CHECK_STR(line, "SLOT 3: 12 SAVES");
    om_slot_line(line, sizeof line, 9, 2, 1);  CHECK_STR(line, "SLOT 9: 2 SAVES, IN THE CONSOLE");
    om_slot_line(line, sizeof line, 9, -1, 0); CHECK_STR(line, "SLOT 9: CARD UNREADABLE");
    om_slot_line(line, 8, 9, 2, 1);            CHECK_INT((int)strlen(line), 7);
    CHECK((int)strlen("SLOT 9: 255 SAVES, IN THE CONSOLE") <= 34);

    /* --- the details card's line for the game whose saves the console holds ------------------------------------ */
    om_saves_held_line(line, sizeof line, 0);  CHECK_STR(line, "Saves in console: slot 0");
    om_saves_held_line(line, sizeof line, 7);  CHECK_STR(line, "Saves in console: slot 7");
    return th_done("test_slotpick");
}
