/* menu/launcher/ldesc.h -- the launch descriptors (lseq.c step 3). One disc: out.desc in the game's folder, as the
 * official menu does (satiator-menu main.c:159-160). A multi-disc set (hand-off v5, src/discset.h): out1..out<n>.desc
 * in the discs' common parent, one per disc, and out<cur>.desc emulated -- the official menu's multi-disc files
 * (main.c:111-139), which Satiator firmware 161+ cycles when the console's lid is opened and closed. Every stale
 * out<n+1>..out9.desc there is deleted (another set's, in a shared parent such as the card root, would otherwise join
 * the cycle). A disc that does not parse launches the chosen disc alone (out.desc, no swap): *alone is that disc's
 * number, and the working folder becomes the chosen disc's own folder (its track names stay as short as a one-disc
 * launch's). The working folder on entry is the hand-off's dir (step 2). BSD-3-Clause. */
#ifndef OM_LDESC_H
#define OM_LDESC_H
#include "handoff.h"

/* Returns image2desc's result for the disc that is emulated (0 = written), or the s_chdir error of the fallback; name
 * gets the file to emulate and dir the folder it is in (absolute, the working folder on return). */
int om_ldesc_write(const om_handoff *h, char name[12], char dir[256], int *alone);
#endif
