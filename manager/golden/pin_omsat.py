"""manager/golden/pin_omsat.py -- pin the tools/omsat sources the Card Manager's behavior goldens come from (final review
I-4), as sources.txt pins the console's. Aether Card Manager, GPL-3.0-or-later. Run from the repo root:

    python manager/golden/pin_omsat.py <sha>

Writes manager/golden/omsat_sources.txt (each source's SHA-256 at <sha>, LF line endings, taken from the commit, never
the working tree) and stamps "omsat_pin": "<sha>" into every expect those sources produced. Run it after regenerating
those expects (mkcases.py --bup-corpus/--gamesdb-fill/--artpak/--match-real, mkart.py, mkinstall.py, mkbuild.py) at
<sha>. SaveImporterGoldenTests.Omsat_sources_are_still_the_pinned_ones fails while the working tree's copy differs."""
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
# what each generator imports and the C# side ports (measured: the modules each one loads), plus check.py, which the
# pipeline test runs live. cardbuild also loads modules for features the Card Manager does not port (title cards,
# cheats seeding, themes, sound): not pinned (README).
SOURCES = ["artpak", "artsrc", "binfmt", "bupfile", "cardbuild", "cardscan", "check", "compat", "coverzip", "discsets", "gamesdb",
           "install", "iphdr", "jpfont", "keys", "libidx", "lnchhdr", "match"]
EXPECTS = ["art_covers", "art_titles", "artpak", "build", "bup_corpus", "gamesdb_fill", "match_real",
           "install_fresh", "install_replace", "install_twice"]


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__)
        return 2
    sha = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short=7", argv[0]], capture_output=True, text=True,
                         check=True).stdout.strip()
    lines = ["# manager/golden/omsat_sources.txt -- SHA-256 (LF line endings) of the tools/omsat sources the behavior",
             "# goldens come from (pin_omsat.py); checked by SaveImporterGoldenTests against the working tree.",
             f"pinned {sha}"]
    for m in SOURCES:
        b = subprocess.run(["git", "-C", str(REPO), "show", f"{sha}:tools/omsat/{m}.py"], capture_output=True, check=True).stdout
        h = hashlib.sha256(b.replace(b"\r\n", b"\n")).hexdigest()
        lines.append(f"{h} tools/omsat/{m}.py")
    (REPO / "manager/golden/omsat_sources.txt").write_text("\n".join(lines) + "\n", newline="\n")
    for e in EXPECTS:
        p = REPO / f"manager/golden/expect/{e}.json"
        t = p.read_text(encoding="utf-8")
        t = re.sub(r'\n? *"omsat_pin": "[0-9a-f]*",', "", t, count=1)
        t = t.replace("{", '{\n "omsat_pin": "' + sha + '",', 1)
        p.write_text(t, encoding="utf-8", newline="\n")
    print(f"pinned {sha}: {len(SOURCES)} sources, {len(EXPECTS)} expects stamped")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
