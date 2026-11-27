/* menu/app/setup.h -- the console setup file /aether/DIAG.TXT (menu/src/setupid.h has the format). Detected once a menu
 * boot when the menu is idle, by POPUP.BIN (menu/pop/psetup.c, through popup.c om_pop_setup: the code runs in LWRAM),
 * and written only when it differs from the file on the card. BSD-3-Clause. */
#ifndef OM_SETUP_H
#define OM_SETUP_H
#include <stdint.h>

/* setupw.c (host-tested): text is compared with OM_SU_PATH (read into buf: 4-aligned, cap >= the file + 4) and
 * written through om_sd_replace only when the setup differs (om_su_same: DATE= aside). 0 = unchanged, 1 = written,
 * < 0 = the read (other than "absent" or "too big") or the write failed; nothing is written after a failed read. */
int om_su_save(const char *text, uint32_t len, uint8_t *buf, uint32_t cap);
#endif
