/* menu/app/fwcheck.c -- see fwcheck.h. BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "satiator.h"
#include "fatal.h"
#include "fwcheck.h"
#include "fwver.h"
#include "msgfmt.h"
#include "pad.h"
#include "safearea.h"
#include "text.h"
#include "v1.h"

static char s_fw[33];
static int s_fw_rc = -1;

void om_fw_capture(void)
{   /* one c_info command (satiator.c s_get_fw_version: an opaque string, at most buflen - 1 B), as diag.c reads it */
    s_fw_rc = s_get_fw_version(s_fw, (int)sizeof s_fw);
    if (s_fw_rc < 0) s_fw[0] = 0;
}

int om_fw_status(void)
{
    int v;
    return om_fw_judge(s_fw_rc, s_fw, OM_FW_MIN_TESTED, &v);
}

const char *om_fw_string(void) { return s_fw; }
int om_fw_rc(void) { return s_fw_rc; }

static void notice(int v)
{   /* M3.5 decision 2: NBG0 text at the safe-area inset, 34 columns at most a line. The interstitial's VDP1 parts are
     * not drawn meanwhile (an empty list each frame); no music plays yet. Physical A (an error-class screen, fixed). */
    static const char *const k_lines[] = {
        "SATIATOR FIRMWARE TOO OLD", "", "Aether may not work correctly.", "Update it: wiki.satiator.net",
        "or set fw_check=off in", "aether/SETTINGS.INI", "", "A: continue anyway", "A+B+C+START: reset console" };
    char line[41], n[12];
    unsigned i;
    om_text_set_inset(om_safe_margin(g_om_safe.level) / 8);
    om_text_clear();
    for (i = 0; i < sizeof k_lines / sizeof k_lines[0]; i++)
        om_text_put(0, 8 + (int)i, k_lines[i], 34);
    strcpy(line, "Found: ");
    strcat(line, om_itoa(n, v));
    strcat(line, "   Tested from: ");
    strcat(line, om_itoa(n, OM_FW_MIN_TESTED));
    om_text_put(0, 9, line, 34);
    om_pad_prime();                                            /* a button held from the boot is not a press */
    for (;;) {
        Uint16 p = om_pad_poll(), held = om_pad_held();
        if (om_pad_soft_reset(held)) om_return_official();     /* HWF-58: SYSRES */
        if ((p & PER_DGT_TA) && !(held & (PER_DGT_TB | PER_DGT_TC | PER_DGT_ST))) break;
        om_v1_begin();
        slSynch();
    }
    {   /* the continuing A must not reach the menu: the interstitial buffers presses by its own edge detection
         * (interstitial.c poll_input), so a still-held A would open or launch the first item (measured: Mednafen,
         * the fake tour's 3-frame A). Wait for every button up, bounded (300 V-blanks); A+B+C+Start still resets */
        uint32_t k;
        for (k = 0; k < 300u && om_pad_held(); k++) {
            (void)om_pad_poll();                               /* the pad's per-frame read (the fake pad advances here) */
            if (om_pad_soft_reset(om_pad_held())) om_return_official();
            om_v1_begin();
            slSynch();
        }
    }
    om_text_clear();
    om_text_set_inset(0);
    om_pad_prime();                                            /* the continuing A is not the menu's first press */
}

void om_fw_decide(int fw_check, char *msg, uint32_t cap)
{
    int v, st = om_fw_judge(s_fw_rc, s_fw, OM_FW_MIN_TESTED, &v);
    char n[12];
    msg[0] = 0;
    if (st == OM_FW_READ_FAIL) {
        om_msg_rc(msg, cap, "FW VERSION: ", "READ FAILED", s_fw_rc);
    } else if (st == OM_FW_UNKNOWN) {
        uint32_t i = 0, j, end = cap > 41u ? 40u : cap - 1u;  /* 40 columns (HWF-24): the raw string cut to fit */
        static const char k_pre[] = "FW VERSION UNREADABLE: ";
        for (j = 0; k_pre[j] && i < end; j++) msg[i++] = k_pre[j];
        for (j = 0; s_fw[j] && i < end; j++) msg[i++] = s_fw[j];
        msg[i] = 0;
    } else if (st == OM_FW_OLD && !fw_check) {
        strcpy(msg, "FW ");
        strcat(msg, om_itoa(n, v));
        strcat(msg, " < ");    /* "FW 158 < 161: fw_check=off": the plan's longer text overflows the info pill (Mednafen) */
        strcat(msg, om_itoa(n, OM_FW_MIN_TESTED));
        strcat(msg, ": fw_check=off");
    } else if (st == OM_FW_OLD) {
        notice(v);
    }
}
