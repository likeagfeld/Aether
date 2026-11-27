/* menu/launcher/lsaves.h -- the save manager's screens (OM_HO_SAVEMGR): the operations are svmgr.h's. In .lui.
 * BSD-3-Clause. */
#ifndef OM_LSAVES_H
#define OM_LSAVES_H
#include "handoff.h"
#include "lseq.h"
/* the manager for h->gid (that game's saves) or, with none, every save; then a console reset to Aether */
int om_run_saves(const om_handoff *h, om_lerr *e);
#endif
