#!/bin/sh
# manager/golden/run_golden.sh <sha> -- regenerate manager/golden/expect/<case>.json from the console's own save import
# (menu/launcher/pgsbak.c at <sha>, host-compiled) and manager/golden/sources.txt, the SHA-256 of every source of it at
# <sha> (the C# test fails when the working tree's copy differs: review-1 I-1). The console's Tidy saves is gone in 0.1
# (52e13a6 removed launcher/tidy.c and r1.c): manager/golden/expect/tidy_<case>.json stay as generated at 014189c, its
# last commit, by this script's earlier version (git show 014189c:manager/golden/run_golden.sh).
# Aether Card Manager, GPL-3.0-or-later. Run from the repo root.
# The sources are taken from the commit (git archive), never from the working tree, and everything is built and run in
# manager/golden/out/ (gitignored) and the container's /tmp: build/host-tests (the menu's host tests) is never touched.
set -eu
SHA=${1:?usage: sh manager/golden/run_golden.sh <sha>}
FULL=$(git rev-parse --short=7 "$SHA")
# the import's sources: pgsbak.c and everything its import path includes or calls (pgsrun.c for the arena split the
# harness uses), and the host fake whose listing and FatFs semantics the golden relies on. Line endings are hashed as
# LF (the repo's form), so a CRLF checkout does not trip the check.
SOURCES="menu/launcher/pgsbak.c menu/launcher/pgsbak.h menu/launcher/pgsrun.c menu/launcher/pgsrun.h menu/launcher/bram.h
menu/src/pgsplan.c menu/src/pgsplan.h menu/src/bupfile.c menu/src/bupfile.h menu/src/sdwrite.c menu/src/sdwrite.h
menu/src/gameid.c menu/src/gameid.h menu/src/om_fmt.c menu/src/om_fmt.h menu/lib/satiator/satiator.h
menu/host-tests/fake_satiator.c menu/src/savegen.c menu/src/savegen.h"
{
    echo "# manager/golden/sources.txt -- SHA-256 (LF line endings) of the console import's sources at the pin."
    echo "# Written by run_golden.sh; checked by SaveImporterGoldenTests against the working tree."
    echo "pinned $FULL"
    for f in $SOURCES; do
        printf '%s %s\n' "$(git show "$FULL:$f" | sed 's/\r$//' | sha256sum | cut -d' ' -f1)" "$f"
    done
} > manager/golden/sources.txt
rm -rf manager/golden/out
mkdir -p manager/golden/out/src
git archive "$FULL" menu contract tools/omsat | tar -x -C manager/golden/out/src
# the compile list is menu/host-tests/Makefile's EXTRA_test_pgsbak (plus every menu/src/*.c, which includes sdwrite.c)
git show "$FULL:menu/host-tests/Makefile" | grep '^EXTRA_test_pgsbak' > manager/golden/out/makefile_line.txt
MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/work" -w /work omsat-build sh -eu -c '
S=manager/golden/out/src
gcc -std=gnu99 -O1 -w -I$S/menu/src -I$S/menu/lib/satiator -I$S/menu/launcher -I$S/menu/app -I$S/menu/host-tests \
    -Wl,--wrap=opendir,--wrap=readdir,--wrap=closedir -o /tmp/imp \
    manager/golden/imp_main.c manager/golden/sortdir.c $S/menu/src/*.c \
    $S/menu/launcher/pgsrun.c $S/menu/launcher/pgsbak.c $S/menu/launcher/smpcwait.c \
    $S/menu/host-tests/fake_bram.c $S/menu/host-tests/fake_satiator.c
for c in manager/golden/cases/*.json; do
    n=$(basename "$c" .json)
    runs=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get(\"runs\",1))" "$c")
    rm -rf /tmp/card_$n
    python3 manager/golden/realize.py "$c" /tmp/card_$n
    FAKE_SD_ROOT=/tmp/card_$n /tmp/imp $runs > /tmp/counts_$n.txt
    set --
    while read -r line; do set -- "$@" --counts "$line"; done < /tmp/counts_$n.txt
    python3 manager/golden/realize.py --manifest /tmp/card_$n --sha '"$FULL"' "$@" > manager/golden/expect/$n.json
    echo "$n: $(cat /tmp/counts_$n.txt | tr "\n" " ")"
done'
