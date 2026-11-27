/* menu/host-tests/test_setupid.c -- the console setup file's detectors and text (menu/src/setupid.c) over fake
 * readings: SMPC status/area/RTC and peripheral IDs, the cartridge ID byte, A-bus CS0 headers, VDP2 TVSTAT, BUP_Stat
 * results. BSD-3-Clause. */
#include "th.h"
#include "setupid.h"
#include "settings.h"

static char s_out[OM_SU_CAP];

static const char *text(const om_su_raw *r) { om_su_text(r, s_out, sizeof s_out); return s_out; }

static int has(const char *t, const char *line)
{   /* the whole line, "\n"-bounded */
    size_t n = strlen(line);
    const char *p = t;
    while ((p = strstr(p, line)) != NULL) {
        if ((p == t || p[-1] == '\n') && p[n] == '\n') return 1;
        p += n;
    }
    printf("  missing line \"%s\" in:\n%s", line, t);
    return 0;
}

static const uint8_t k_bios[16] = { 'B','T','R','_','1','.','0','1','9','1','9','4','1','2','2','8' };   /* JP 1.01 dump */

static void base(om_su_raw *r, uint8_t *cs0)
{
    static const uint8_t rtc[7] = { 0x20, 0x26, 0x3B, 0x25, 0x14, 0x03, 0x05 };   /* Wed 2026-11-25 14:03:05 */
    memset(r, 0, sizeof *r);
    memset(cs0, 0xFF, 0x80);
    r->aether = "0.1 abc1234 2026-11-25";
    r->fw = "163 2505030818";
    r->bios = k_bios;
    r->cs0 = cs0;
    r->area = 1;
    r->cart_id = 0xFF;
    r->smpc_cond = 0xC0;                  /* STE set, RESD (reset disabled) */
    memcpy(r->rtc, rtc, 7);
    r->nport[0] = 1; r->port[0][0] = 0x02;
    r->bram.rc = 0; r->bram.total = 512; r->bram.free = 509; r->bram.kb = 32;
    r->cart_bram.rc = 1;
    r->games = 689;
    r->loader = OM_LDR_AUTO;
}

int main(void)
{
    om_su_raw r;
    uint8_t cs0[0x80];
    char tmp[6];
    const char *t;

    /* cartridge ID byte (ST-TECH-47 Table 2) */
    CHECK_STR(om_su_cart(0x5A), "RAM1MB");
    CHECK_STR(om_su_cart(0x5C), "RAM4MB");
    CHECK_STR(om_su_cart(0xFF), "NONE");
    CHECK_STR(om_su_cart(0x21), "OTHER");
    CHECK_STR(om_su_cart(0x00), "OTHER");

    /* peripheral IDs: HWF-77's measured pads, SL_DEF.H's names, an unknown one */
    CHECK_STR(om_su_dev(0x02, tmp), "PAD");
    CHECK_STR(om_su_dev(0x16, tmp), "3DPAD_ANALOG");
    CHECK_STR(om_su_dev(0x15, tmp), "ANALOG");
    CHECK_STR(om_su_dev(0x13, tmp), "RACER");
    CHECK_STR(om_su_dev(0x23, tmp), "MOUSE");
    CHECK_STR(om_su_dev(0x25, tmp), "GUN");
    CHECK_STR(om_su_dev(0x34, tmp), "KEYBOARD");
    CHECK_STR(om_su_dev(0xE1, tmp), "MD3PAD");
    CHECK_STR(om_su_dev(0xE2, tmp), "MD6PAD");
    CHECK_STR(om_su_dev(0xE3, tmp), "MDMOUSE");
    CHECK_STR(om_su_dev(0xFF, tmp), "NONE");
    CHECK_STR(om_su_dev(0x4A, tmp), "ID_4A");

    /* SMPC area codes (SMPC_Manual.txt OREG9) */
    CHECK_INT(om_su_area(1), 'J');  CHECK_INT(om_su_area(2), 'T');  CHECK_INT(om_su_area(4), 'U');
    CHECK_INT(om_su_area(5), 'B');  CHECK_INT(om_su_area(6), 'K');  CHECK_INT(om_su_area(0xA), 'A');
    CHECK_INT(om_su_area(0xC), 'E'); CHECK_INT(om_su_area(0xD), 'L');
    CHECK_INT(om_su_area(0), 0); CHECK_INT(om_su_area(3), 0); CHECK_INT(om_su_area(0xF), 0);

    /* RTC: STE clear = never set since a cold reset; a bad BCD field */
    {
        uint8_t ok[7] = { 0x20, 0x26, 0x3B, 0x25, 0x14, 0x03, 0x05 }, bad[7];
        CHECK_STR(om_su_rtc(0x80, ok), "OK");
        CHECK_STR(om_su_rtc(0x40, ok), "UNSET");
        memcpy(bad, ok, 7); bad[5] = 0x6A; CHECK_STR(om_su_rtc(0x80, bad), "BAD");   /* minute 6A */
        memcpy(bad, ok, 7); bad[2] = 0x0D; CHECK_STR(om_su_rtc(0x80, bad), "BAD");   /* month 13 */
        memcpy(bad, ok, 7); bad[3] = 0x00; CHECK_STR(om_su_rtc(0x80, bad), "BAD");   /* day 0 */
        memcpy(bad, ok, 7); bad[4] = 0x24; CHECK_STR(om_su_rtc(0x80, bad), "BAD");   /* hour 24 */
    }

    /* the whole file: a Japanese console, one pad, no cartridge */
    base(&r, cs0);
    t = text(&r);
    CHECK(!strncmp(t, "# ", 2));
    CHECK(has(t, "FORMAT=1"));
    CHECK(has(t, "AETHER=0.1 abc1234 2026-11-25"));
    CHECK(has(t, "DATE=2026-11-25 14:03:05"));
    CHECK(has(t, "SATIATOR_FW=163"));
    CHECK(has(t, "SATIATOR_FW_RAW=163 2505030818"));
    CHECK(has(t, "BIOS=BTR_1.0191941228"));
    CHECK(has(t, "AREA=J"));
    CHECK(has(t, "AREA_CODE=1"));
    CHECK(has(t, "VIDEO=NTSC60"));
    CHECK(has(t, "CART=NONE"));
    CHECK(has(t, "CART_ID=FF"));
    CHECK(has(t, "CART_BRAM=NONE"));
    CHECK(has(t, "CART_ROM=NONE"));
    CHECK(has(t, "NETLINK=UNKNOWN"));
    CHECK(has(t, "PORT1=PAD"));
    CHECK(has(t, "PORT2=NONE"));
    CHECK(has(t, "RTC=OK"));
    CHECK(has(t, "BRAM=509/512 32KB"));
    CHECK(has(t, "GAMES=689"));
    CHECK(has(t, "LOADER=AUTO"));
    CHECK(strlen(t) < 600u);
    CHECK(t[strlen(t) - 1] == '\n');

    /* a European console on a 4 MB cart with a backup cart's figures, a 3D pad (analog) and a multitap */
    base(&r, cs0);
    r.area = 0xC; r.tvstat_pal = 1; r.cart_id = 0x5C;
    r.cart_bram.rc = 0; r.cart_bram.total = 8128; r.cart_bram.free = 8000; r.cart_bram.kb = 4096;
    r.port[0][0] = 0x16;
    r.nport[1] = 3; r.port[1][0] = 0x02; r.port[1][1] = 0xFF; r.port[1][2] = 0x13;
    r.loader = OM_LDR_JHL;
    t = text(&r);
    CHECK(has(t, "AREA=E")); CHECK(has(t, "AREA_CODE=12")); CHECK(has(t, "VIDEO=PAL50"));
    CHECK(has(t, "CART=RAM4MB")); CHECK(has(t, "CART_ID=5C"));
    CHECK(has(t, "CART_BRAM=8000/8128 4096KB"));
    CHECK(has(t, "PORT1=3DPAD_ANALOG"));
    CHECK(has(t, "PORT2=MULTITAP:PAD,NONE,RACER"));
    CHECK(has(t, "LOADER=JHL"));
    r.nport[1] = 9;                                   /* never more than the 6 entries kept */
    memset(r.port[1], 0x02, OM_SU_TAPS);
    CHECK(has(text(&r), "PORT2=MULTITAP:PAD,PAD,PAD,PAD,PAD,PAD"));
    r.loader = OM_LDR_CWX;
    CHECK(has(text(&r), "LOADER=CWX"));

    /* the unknowns: firmware unread, BIOS unread, reserved area, RTC unset, BUP not run, unformatted BRAM */
    base(&r, cs0);
    r.fw = NULL; r.bios = NULL; r.cs0 = NULL; r.area = 3; r.smpc_cond = 0x40;
    r.bram.rc = -1; r.cart_bram.rc = -1;
    t = text(&r);
    CHECK(has(t, "SATIATOR_FW=UNKNOWN")); CHECK(has(t, "SATIATOR_FW_RAW=UNKNOWN")); CHECK(has(t, "BIOS=UNKNOWN"));
    CHECK(has(t, "AREA=UNKNOWN")); CHECK(has(t, "AREA_CODE=3")); CHECK(has(t, "DATE=UNKNOWN")); CHECK(has(t, "RTC=UNSET"));
    CHECK(has(t, "BRAM=UNKNOWN")); CHECK(has(t, "CART_BRAM=UNKNOWN")); CHECK(has(t, "CART_ROM=UNKNOWN"));
    r.bram.rc = 2; r.fw = "FAKE";
    t = text(&r);
    CHECK(has(t, "BRAM=UNFORMATTED")); CHECK(has(t, "SATIATOR_FW=UNKNOWN")); CHECK(has(t, "SATIATOR_FW_RAW=FAKE"));
    r.fw = "16\x01\n3";                               /* never a control character or a line break in a value */
    CHECK(has(text(&r), "SATIATOR_FW_RAW=16??3"));

    /* boot cartridge headers in CS0 (ST-040-R4 §4.2 layout) */
    base(&r, cs0);
    memset(cs0, ' ', 0x80);
    memcpy(cs0, "SEGA SEGASATURN SEGA TP KAI     PSKAI_6545V6.545", 48);
    CHECK(has(text(&r), "CART_ROM=PSKAI PSKAI_6545 V6.545"));
    memset(cs0, ' ', 0x80);
    memcpy(cs0, "SEGA SEGASATURN SEGA TP T-00    T-1234    V1.000", 48);
    memcpy(cs0 + 0x60, "SOME ROM CARTRIDGE", 18);
    CHECK(has(text(&r), "CART_ROM=ROM T-1234 V1.000 SOME ROM CARTRIDGE"));
    memcpy(cs0, "SEGA SEGASATURN!", 16);              /* one byte off: not a header */
    CHECK(has(text(&r), "CART_ROM=NONE"));

    /* an RTC set but invalid writes no date */
    base(&r, cs0);
    r.rtc[6] = 0x99;
    t = text(&r);
    CHECK(has(t, "RTC=BAD")); CHECK(has(t, "DATE=UNKNOWN"));

    /* a tiny buffer is never overrun and stays terminated */
    {
        char small[40];
        memset(small, 'X', sizeof small);
        base(&r, cs0);
        CHECK(om_su_text(&r, small, 32) == 31u);
        CHECK(small[31] == 0 && small[32] == 'X');
    }

    /* om_su_same: DATE= is not part of the setup; every other line is */
    {
        static char a[OM_SU_CAP], b[OM_SU_CAP];
        uint32_t na, nb;
        base(&r, cs0);
        na = om_su_text(&r, a, sizeof a);
        r.rtc[6] = 0x59;                               /* a later boot, same setup */
        nb = om_su_text(&r, b, sizeof b);
        CHECK(strcmp(a, b) != 0);
        CHECK(om_su_same(a, na, b, nb));
        CHECK(om_su_same(a, na, a, na));
        r.smpc_cond = 0x40;                            /* the clock lost: DATE=UNKNOWN and RTC=UNSET -- RTC differs */
        nb = om_su_text(&r, b, sizeof b);
        CHECK(!om_su_same(a, na, b, nb));
        base(&r, cs0);
        r.bram.free = 508;                             /* a save written since */
        nb = om_su_text(&r, b, sizeof b);
        CHECK(!om_su_same(a, na, b, nb));
        base(&r, cs0);
        r.nport[1] = 1; r.port[1][0] = 0x02;           /* a second pad */
        nb = om_su_text(&r, b, sizeof b);
        CHECK(!om_su_same(a, na, b, nb));
        CHECK(om_su_same(a, na, a, na - 1));           /* only the last '\n' missing: the same lines */
        CHECK(!om_su_same(a, na, a, na - 2));          /* a cut file: its last value one byte short */
        CHECK(!om_su_same(a, na, "", 0));
        CHECK(om_su_same("", 0, "", 0));
        CHECK(om_su_same("DATE=1\nA=1\n", 11, "A=1\nDATE=2\n", 11));
        CHECK(om_su_same("A=1", 3, "A=1\n", 4));       /* a missing last newline is the same line */
        CHECK(!om_su_same("A=1\n", 4, "A=1\nB=2\n", 8));
        CHECK(!om_su_same("A=1\n", 4, "A=2\n", 4));
    }

    printf("test_setupid: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
