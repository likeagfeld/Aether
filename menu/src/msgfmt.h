/* menu/src/msgfmt.h -- build a bounded-width "prefix + reason + rc" status/error line, the suffix
 * (" rc <n>") first, so the rc code is NEVER truncated -- only the reason text is ever shortened to
 * make room. Fixes coordinator fix round 1, Task 15, point 1: menu/ui/art.c's finish() and
 * menu/views/folders.c's launch_selected() each built a line by appending the reason, then " rc ",
 * then the number, with each append capped only by the line's remaining width -- a long-enough
 * reason filled all 40 columns and the rc suffix was silently dropped. Portable, no Saturn/SGL
 * dependency: host-tested directly (menu/host-tests/test_msgfmt.c). BSD-3-Clause. */
#ifndef OM_MSGFMT_H
#define OM_MSGFMT_H
#include <stdint.h>
/* Writes into out[0..cap-1], always NUL-terminated within cap bytes. Builds "prefix" + "reason" +
 * " rc " + rc, truncating "reason" (never the rc suffix) to fit; in the pathological case where
 * "prefix" + the rc suffix alone would not fit in cap-1 bytes, "reason" is dropped entirely and, as
 * a last resort, "prefix" itself is shortened -- the rc suffix is still the last thing to be cut. */
void om_msg_rc(char *out, uint32_t cap, const char *prefix, const char *reason, int rc);
#endif
