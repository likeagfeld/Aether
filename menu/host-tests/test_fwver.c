/* menu/host-tests/test_fwver.c -- the Satiator firmware string -> a version number, judged
 * against the tested minimum. Never silent: every outcome has a status. BSD-3-Clause. */
#include "th.h"
#include "fwver.h"

int main(void)
{
    int v;
    /* the user's hardware (2026-11-20, JP V-Saturn, diag page 1 photo): "SATIATOR FW 163 2505030818  BOOTLDR 0000" is
     * the old 40-column line "SATIATOR FW " + s_get_fw_version's string + "  BOOTLDR " + 8 hex digits, cut at column
     * 40, so the string itself is exactly "163 2505030818" */
    CHECK_INT(om_fw_parse("163 2505030818"), 163);
    CHECK_INT(om_fw_parse("SATIATOR FW 163 2505030818  BOOTLDR 0000"), 163);
    CHECK_INT(om_fw_parse("163"), 163);
    CHECK_INT(om_fw_parse("v163"), 163);
    CHECK_INT(om_fw_parse("FW163"), 163);
    CHECK_INT(om_fw_parse("2024-01-05 v163"), 163);       /* a 4-digit year is not a version */
    CHECK_INT(om_fw_parse("1.63"), 163);
    CHECK_INT(om_fw_parse("v1.61 build 7"), 161);
    CHECK_INT(om_fw_parse("FAKE"), -1);
    CHECK_INT(om_fw_parse(""), -1);
    CHECK_INT(om_fw_parse("12"), -1);
    CHECK_INT(om_fw_parse("1631"), -1);                   /* four digits: not this form */
    CHECK_INT(om_fw_parse("2505030818"), -1);             /* the build stamp alone is not a version */
    CHECK_INT(om_fw_judge(0, "163 2505030818", OM_FW_MIN_TESTED, &v), OM_FW_OK); CHECK_INT(v, 163);
    CHECK_INT(om_fw_judge(0, "163", OM_FW_MIN_TESTED, &v), OM_FW_OK); CHECK_INT(v, 163);
    CHECK_INT(om_fw_judge(0, "161", OM_FW_MIN_TESTED, &v), OM_FW_OK);  /* the minimum itself is fine */
    CHECK_INT(om_fw_judge(0, "158", OM_FW_MIN_TESTED, &v), OM_FW_OLD); CHECK_INT(v, 158);
    CHECK_INT(om_fw_judge(0, "160 2401010101", OM_FW_MIN_TESTED, &v), OM_FW_OLD); CHECK_INT(v, 160);
    CHECK_INT(om_fw_judge(0, "beta", OM_FW_MIN_TESTED, &v), OM_FW_UNKNOWN); CHECK_INT(v, -1);
    CHECK_INT(om_fw_judge(-1, "FAKE", OM_FW_MIN_TESTED, &v), OM_FW_READ_FAIL); CHECK_INT(v, -1);
    CHECK_INT(OM_FW_MIN_TESTED, 161);
    CHECK_STR(om_fw_word(OM_FW_OK), "OK");
    CHECK_STR(om_fw_word(OM_FW_OLD), "OLD");
    CHECK_STR(om_fw_word(OM_FW_UNKNOWN), "UNREADABLE");
    CHECK_STR(om_fw_word(OM_FW_READ_FAIL), "READ FAILED");
    return th_done("test_fwver");
}
