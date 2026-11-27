/* manager/golden/imp_main.c -- VibeMenu Card Manager's golden harness for the save import (M4 Task 1b). GPL-3.0-or-later.
 * Runs the console's own import, menu/launcher/pgsbak.c om_pgs_import (contract/SPEC.md §5.17 Import), compiled for the
 * host against menu/host-tests/fake_satiator.c, on the card directory FAKE_SD_ROOT names. The environment is set up as
 * menu/host-tests/test_pgsbak.c main() does (om_pgs_arena_split over a 4-aligned arena). Prints one line per run:
 *     counts kai=N rings=N present=N skipped=N conflicts=N older=N cart=N rc=N   (cart: M6 Task 24 fix round 2)
 * usage: FAKE_SD_ROOT=<card> imp [runs]          exit 0, or 1 when a run's rc is not 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pgsrun.h"
#include "pgsbak.h"

extern int fake_nopath;
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];

int main(int argc, char **argv)
{
    om_pgs_env env;
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int runs = argc > 1 ? atoi(argv[1]) : 1, i, bad = 0;
    if (!getenv("FAKE_SD_ROOT")) { fprintf(stderr, "FAKE_SD_ROOT not set\n"); return 2; }
    memset(&env, 0, sizeof env);
    fake_nopath = 1;                                  /* FR_NO_PATH under a missing folder, as real FatFs (24b fix) */
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    for (i = 0; i < runs; i++) {
        memset(&r, 0, sizeof r);
        om_pgs_import(&env, &r, note);
        printf("counts kai=%u rings=%u present=%u skipped=%u conflicts=%u older=%u cart=%u rc=%d\n", r.kai, r.rings,
               r.present, r.skipped, r.conflicts, r.older, r.cart, r.rc);
        if (r.rc) bad = 1;
    }
    return bad;
}
