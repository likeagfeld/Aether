/* manager/golden/tidy_main.c -- VibeMenu Card Manager's golden harness for Tidy saves (M6 Task 24b). GPL-3.0-or-later.
 * Runs the launcher's own Tidy, menu/launcher/tidy.c om_tidy (contract/SPEC.md §5.17 Tidy saves), compiled for the host
 * against menu/host-tests/fake_satiator.c, on the card directory FAKE_SD_ROOT names, set up as
 * menu/host-tests/test_pgsbak.c main() and tidy_run() do: om_pgs_arena_split over a 4-aligned arena, the fake card's
 * add-only check on, and fake_tidy_ok around each run (so the fake card itself fails any delete that is not a
 * byte-identical duplicate with a copy left on the card), and its no-path mode on (FR_NO_PATH for a name under a missing
 * folder, as real FatFs answers: M6 Task 24b fix). ops: a string of 'p' (the preview) and 'r' (a run).
 * Prints, per op, its progress lines ("say <line>") and then
 *     tidy run=R files=N bytes=N bups=N bkps=N folders=N slots=N skipped=N rc=N
 * and at the end "caps tcap=N kcap=N" (tidy.c's table sizes here) and "violations=N" (the fake card's count).
 * usage: FAKE_SD_ROOT=<card> tidy <ops> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pgsrun.h"
#include "pgsplan.h"
#include "tidy.h"

extern int fake_addonly, fake_addonly_violations, fake_tidy_ok, fake_r1_on, fake_nopath;
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];

/* tidy.c's table entries, as it declares them (the caps line; the source pin covers a change to tidy.c) */
typedef struct { uint32_t crc, size, gen; char fn[OM_PGS_FNAME]; uint8_t skip, keep, pad[2]; } tent;
typedef struct { uint32_t crc, gen; uint16_t dir; char fn[OM_PGS_FNAME]; uint8_t pad[2]; } kent;

static void say(const char *s) { printf("say %s\n", s); }

int main(int argc, char **argv)
{
    om_pgs_env env;
    om_tidy_res r;
    const char *ops = argc > 1 ? argv[1] : "p";
    if (!getenv("FAKE_SD_ROOT")) { fprintf(stderr, "FAKE_SD_ROOT not set\n"); return 2; }
    memset(&env, 0, sizeof env);
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.say = say;
    fake_addonly = 1;
    fake_nopath = 1;                                  /* a missing parent folder answers FR_NO_PATH, as real FatFs does */
    for (; *ops; ops++) {
        int run = *ops == 'r';
        fake_tidy_ok = run;
        om_tidy(&env, run, &r);
        fake_tidy_ok = 0;
        printf("tidy run=%d files=%u bytes=%u bups=%u bkps=%u folders=%u slots=%u skipped=%u rc=%d\n", run, r.files,
               r.bytes, r.bups, r.bkps, r.folders, r.slots, r.skipped, r.rc);
    }
    printf("caps tcap=%u kcap=%u\n", (unsigned)(OM_PGS_REC / sizeof(tent)),
           (unsigned)((uint32_t)(env.rec - (env.lib + 512u * 32u)) / sizeof(kent)));
    printf("violations=%d\n", fake_addonly_violations);
    return 0;
}
