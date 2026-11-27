/* menu/launcher/lorbpage.h -- the orbiting logo's pages (lorbpage.c, .lui: never after launch step 9): its art and the
 * step-9 stash (lorb.h), and the save import's progress and result. BSD-3-Clause. */
#ifndef OM_LORBPAGE_H
#define OM_LORBPAGE_H
#include <stdint.h>
#include "pgsbak.h"

/* the eye frame now shown on NBG1 (VRAM and CRAM), averaged over the scene's area: what the logo and text blend into */
uint16_t om_orbp_bg(int compact);
/* the logo blended over bg into NBG0's hidden columns and halved into the stash, with three faces and the CRC; 0 = done */
int om_orbp_stash(uint16_t bg);
/* the import: the full scene over the eye frame on screen (0, or no logo: stay on the text screen), its progress
 * (canceling: B was held), its result with the counts (note: the stop's reason; first_skip: the first file skipped) */
int om_orbp_import_begin(void);
void om_orbp_import_prog(const om_pgs_prog *p, int canceling);
void om_orbp_import_result(const om_pgs_imp *r, const char *note, const char *first_skip);
#endif
