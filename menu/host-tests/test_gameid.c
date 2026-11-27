/* menu/host-tests/test_gameid.c -- product + area symbols -> the PERGAME.INI id and the SAVES folder name
 * (SPEC §5.14; Kai's rule, docs/research/m6-research.md §1.2). BSD-3-Clause. */
#include <string.h>
#include "th.h"
#include "gameid.h"

static const char *ini(const char *prod, const char *area)
{
    static char a[OM_GID_MAX];
    memset(a, '#', sizeof a);
    if (om_gameid_ini((const uint8_t *)prod, (const uint8_t *)area, a) != 0)
        return "(error)";
    return a;
}

static const char *dir(const char *id)
{
    static char d[OM_GID_MAX + 1];
    memset(d, '#', sizeof d);
    if (om_gameid_dir(id, d) != 0)
        return "(error)";
    return d;
}

int main(void)
{
    char a[OM_GID_MAX], d[OM_GID_MAX + 1];
    CHECK_INT(om_gameid_ini((const uint8_t *)"MK-81045  ", (const uint8_t *)"U         ", a), 0);
    CHECK_STR(a, "MK-81045@U");
    CHECK_INT(om_gameid_dir(a, d), 0); CHECK_STR(d, "MK-81045[U]");
    /* Kai's settings readme example: Scorcher (USA) -> /SAVES/BRAM/MK-81214[UT] */
    CHECK_STR(ini("MK-81214  ", "UT        "), "MK-81214@UT");
    CHECK_STR(dir("MK-81214@UT"), "MK-81214[UT]");
    /* every area letter in field order (not the first of J T U E B); lower-case product upper-cased */
    CHECK_STR(ini("T-4507g   ", "JTUBKAEL  "), "T-4507G@JTUBKAEL");
    CHECK_STR(dir("T-4507G@JTUBKAEL"), "T-4507G[JTUBKAEL]");
    CHECK_STR(ini("GS-9001   ", "EU        "), "GS-9001@EU");             /* field order kept: E before U */
    /* the product stops at the first byte <= ' ' (Kai: Linkle Liver Story is "GS-9055 V1") */
    CHECK_STR(ini("GS-9055 V1", "J         "), "GS-9055@J");
    CHECK_STR(ini("GS 9041/A ", "U         "), "GS@U");
    CHECK_STR(ini("T-1\0\0\0\0\0\0\0", "J         "), "T-1@J");
    /* bytes outside 0-9 A-Z _ - become '_' ('.' too); '_' is kept */
    CHECK_STR(ini("T-1.2/3_X ", "J         "), "T-1_2_3_X@J");
    CHECK_STR(ini("AB\x80" "C      ", "J         "), "AB_C@J");
    /* area: leading spaces skipped (Kai: Dark Savior (Europe) "      E "), non-letters and lower case ignored,
     * a NUL ends the field, at most 8 letters */
    CHECK_STR(ini("T-1       ", "      E   "), "T-1@E");
    CHECK_STR(ini("T-1       ", "  U       "), "T-1@U");
    CHECK_STR(ini("T-1       ", "1jE-      "), "T-1@E");
    CHECK_STR(ini("T-1       ", "J\0U       "), "T-1@J");
    CHECK_STR(ini("T-1       ", "JTUBKAELJT"), "T-1@JTUBKAEL");
    /* no letters at all: "<product>@", and the folder is the product alone (Kai writes no brackets) */
    CHECK_STR(ini("MK81045-50", "          "), "MK81045-50@");
    CHECK_STR(dir("MK81045-50@"), "MK81045-50");
    /* a full 10-byte product and 8 letters fit both buffers */
    CHECK_STR(ini("ABCDEFGHIJ", "JTUBKAEL  "), "ABCDEFGHIJ@JTUBKAEL");
    CHECK_STR(dir("ABCDEFGHIJ@JTUBKAEL"), "ABCDEFGHIJ[JTUBKAEL]");
    CHECK_INT(strlen("ABCDEFGHIJ@JTUBKAEL") + 1, OM_GID_MAX);
    CHECK_INT(strlen("ABCDEFGHIJ[JTUBKAEL]") + 1, OM_GID_MAX + 1);
    /* multi-disc: the id ignores device info, so every disc of a set with one product number shares one id */
    {
        uint32_t n1;
        char b[OM_GID_MAX];
        uint8_t *s1 = th_load(FIX("sysid_gs9001.bin"), &n1), *s2 = th_dup(s1, n1);
        memcpy(s2 + 0x38, "CD-2/2  ", 8);
        CHECK(n1 >= 0x50);
        CHECK_INT(om_gameid_ini(s1 + 0x20, s1 + 0x40, a), 0); CHECK_STR(a, "GS-9001@JTUE");
        CHECK_INT(om_gameid_ini(s2 + 0x20, s2 + 0x40, b), 0); CHECK_STR(b, a);
        free(s1); free(s2);
    }
    /* errors: an empty product (Kai would use "_UNKNOWN"; we refuse, so unknown games never share one entry) */
    CHECK(om_gameid_ini((const uint8_t *)"          ", (const uint8_t *)"J         ", a) < 0);
    CHECK(om_gameid_ini((const uint8_t *)" T-1      ", (const uint8_t *)"J         ", a) < 0);
    CHECK(om_gameid_dir("NOAT", d) < 0);
    CHECK(om_gameid_dir("@U", d) < 0);
    CHECK(om_gameid_dir("A@1", d) < 0);
    CHECK(om_gameid_dir("A@B@C", d) < 0);
    CHECK(om_gameid_dir("a@U", d) < 0);
    CHECK(om_gameid_dir("ABCDEFGHIJK@U", d) < 0);
    CHECK(om_gameid_dir("A@JTUBKAELJ", d) < 0);
    CHECK_INT(om_gameid_check("MK-81214@UT"), 0);
    CHECK_INT(om_gameid_check("MK_1@"), 0);
    CHECK(om_gameid_check("MK.1@U") < 0);
    CHECK(om_gameid_check("") < 0);
    return th_done("test_gameid");
}
