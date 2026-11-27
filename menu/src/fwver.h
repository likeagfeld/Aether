/* menu/src/fwver.h -- the Satiator firmware string (s_get_fw_version: an opaque ASCII
 * string, menu/lib/satiator/satiator.c) -> a version number, judged against the oldest firmware Aether has run on in
 * a hardware session. Pure C, host-tested (menu/host-tests/test_fwver.c). BSD-3-Clause. */
#ifndef OM_FWVER_H
#define OM_FWVER_H

#define OM_FW_MIN_TESTED 161          /* M3.5 decision 1: the oldest firmware Aether has run on in a hardware session (HWF-54) */
enum { OM_FW_OK, OM_FW_OLD, OM_FW_UNKNOWN, OM_FW_READ_FAIL };
/* The first maximal run of exactly three decimal digits ("163 2505030818" -> 163, "v163", "FW163"; a four-digit year
 * or the ten-digit build stamp is skipped); else the dotted form d.dd ("1.63" -> 163); else -1. */
int om_fw_parse(const char *s);
/* read_rc < 0: READ_FAIL; unparsable: UNKNOWN; below min: OLD; else OK. *ver = the parsed number, or -1. */
int om_fw_judge(int read_rc, const char *s, int min, int *ver);
const char *om_fw_word(int status);   /* "OK" "OLD" "UNREADABLE" "READ FAILED" (the diagnostics line) */
#endif
