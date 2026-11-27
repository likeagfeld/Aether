/* menu/host-tests/test_msgfmt.c -- coordinator fix round 1, Task 15, point 1. */
#include "th.h"
#include "msgfmt.h"

/* Reproduces the OLD, buggy line-building that menu/ui/art.c's finish() and menu/views/folders.c's
 * launch_selected() used before this fix: append the reason, then " rc ", then the number, each
 * capped only by the line's remaining width -- so a long-enough reason eats the whole line and the
 * rc never gets appended at all. Neither call site contains this code any more (both now call
 * om_msg_rc); kept here only to demonstrate the bug the new helper fixes. */
static void old_buggy(char *out, uint32_t cap, const char *prefix, const char *reason, int rc)
{
    char t[12], num[12];
    unsigned u = rc < 0 ? 0u - (unsigned)rc : (unsigned)rc;
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (rc < 0) num[i++] = '-';
    while (n) num[i++] = t[--n];
    num[i] = 0;
    strcpy(out, prefix);
    strncat(out, reason, cap - 1 - strlen(out));
    strncat(out, " rc ", cap - 1 - strlen(out));
    strncat(out, num, cap - 1 - strlen(out));
}

int main(void)
{
    char out[41];
    /* The coordinator's exact worst case: menu/ui/art.c's BOX.TGA prefix plus the longest
     * om_tga_strerror() reason. Measured this session: strlen("ART: BOX.TGA ") == 13,
     * strlen("TGA bit depth not supported") == 27 -- together exactly 40, so the OLD code's
     * appends of " rc " and the number both have zero room left and vanish silently. */
    const char *prefix = "ART: BOX.TGA ";
    const char *reason = "TGA bit depth not supported";

    /* RED, demonstrated against the old approach (not against current code, which no longer
     * contains it -- see the comment on old_buggy() above): the rc is dropped entirely. */
    old_buggy(out, sizeof out, prefix, reason, -120);
    CHECK_INT((int)strlen(out), 40);
    CHECK(strstr(out, "-120") == NULL);                    /* the bug: the rc is gone */
    printf("test_msgfmt: old buggy formatter drops rc -120 for the worst-case reason (as expected)\n");

    /* GREEN: om_msg_rc keeps the rc fully visible by truncating only the reason. The line is
     * exactly full (13 prefix + 19 truncated reason + 8 suffix = 40), and the rc is intact. */
    om_msg_rc(out, sizeof out, prefix, reason, -120);
    CHECK_INT((int)strlen(out), 40);
    CHECK(strstr(out, " rc -120") != NULL);

    /* The longest rc the coordinator named: a 4-digit negative. Suffix " rc -9999" is 9 chars, one
     * more than " rc -120", so one less byte of reason survives (18 instead of 19) -- the rc is
     * still fully intact and the line is still exactly full. */
    om_msg_rc(out, sizeof out, prefix, reason, -9999);
    CHECK_INT((int)strlen(out), 40);
    CHECK(strstr(out, " rc -9999") != NULL);

    /* Short prefix + short reason: nothing is truncated, exact text survives untouched. */
    om_msg_rc(out, sizeof out, "ART: ", "ART.PAK CRC", 0);
    CHECK_STR(out, "ART: ART.PAK CRC rc 0");

    /* Zero-length reason still gets the rc appended correctly. */
    om_msg_rc(out, sizeof out, "X: ", "", -1);
    CHECK_STR(out, "X:  rc -1");

    /* folders.c's own prefix ("LAUNCH FAILED: LAUNCHER.BIN ", 28 chars, measured this session with
     * `printf '%s' ... | wc -c`) + a long om_strerror() text + the longest rc: still never drops
     * the rc. */
    om_msg_rc(out, sizeof out, "LAUNCH FAILED: LAUNCHER.BIN ", "some very long error description text", -9999);
    CHECK_INT((int)strlen(out), 40);
    CHECK(strstr(out, " rc -9999") != NULL);

    /* cap boundary: cap==1 yields an empty, NUL-terminated string, no overrun. */
    out[0] = 'Z';
    om_msg_rc(out, 1, "anything", "anything", -1);
    CHECK_INT(out[0], 0);

    /* Pathological: prefix alone already fills the whole line, longer than cap permits. The rc
     * suffix still gets priority over everything else -- the reason is dropped first, then the
     * prefix itself is cut down to "0", but " rc -5" survives intact. */
    om_msg_rc(out, 8, "0123456789", "reason", -5);
    CHECK_INT((int)strlen(out), 7);
    CHECK(strstr(out, " rc -5") != NULL);

    return th_done("test_msgfmt");
}
