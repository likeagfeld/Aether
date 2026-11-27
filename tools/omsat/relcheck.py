"""`python -m omsat.relcheck OUT_DIR [--allow-dirty]` -- release hygiene for the --app build.

Reads OUT_DIR/menu.map and the program image the ISO carries (menu/app/cd/0.BIN next to OUT_DIR) and fails when:
  * any developer or fake-build wording is in the image's printable strings (DEV_TEXT below);
  * any fake-only object or symbol is linked (FAKE_MAP below; menu/app/fake/*, menu/src/fakefs.c, the fake card blob);
  * the diagnostics build string is missing or is not "Aether <VIBEMENU_VERSION> <sha> <yyyy-mm-dd>"
    (menu/src/version.h), or -- without --allow-dirty -- it says -dirty.
Prints one PASS line with the build string, or every failure."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEV_TEXT = re.compile(r"(?i)fake|simulated|\btask ?\d|boot frame|chant event|fakescript|\bdebug\b|\btodo\b|spike|"
                      r"not for a saturn|\bm3 task")
FAKE_MAP = re.compile(r"fakesat|fakefs|om_fakecard|/fake/|fake_[a-z]")


def version() -> str:
    m = re.search(r'#define\s+VIBEMENU_VERSION\s+"([^"]*)"', (ROOT / "menu/src/version.h").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("relcheck: VIBEMENU_VERSION missing from menu/src/version.h")
    return m.group(1)


def strings(blob: bytes, n: int = 5) -> list[str]:
    return [m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{%d,}" % n, blob)]


def check(image: bytes, map_text: str, ver: str, allow_dirty: bool = False) -> tuple[list[str], str | None]:
    errs = []
    ss = strings(image)
    for s in ss:
        if DEV_TEXT.search(s):
            errs.append(f"developer text in the image: {s!r}")
    for line in map_text.splitlines():
        if FAKE_MAP.search(line) and "Discarded" not in line:
            errs.append(f"fake-only code in the map: {line.strip()[:120]}")
            break
    pat = re.compile(r"Aether " + re.escape(ver) + r" ([0-9a-f]{7,12})(-dirty)? (\d{4}-\d{2}-\d{2})$")
    build = [s for s in ss if pat.fullmatch(s)]
    if len(build) != 1:
        errs.append(f"build string 'Aether {ver} <sha> <date>' found {len(build)} times")
        return errs, None
    if pat.fullmatch(build[0]).group(2) and not allow_dirty:
        errs.append(f"build string {build[0]!r}: built from a tree with uncommitted changes")
    return errs, build[0]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default=str(ROOT / "menu/app/out"))
    ap.add_argument("--allow-dirty", action="store_true")
    a = ap.parse_args(argv)
    out = Path(a.out)
    image = (out.parent / "cd" / "0.BIN").read_bytes()
    map_text = (out / "menu.map").read_text(encoding="utf-8", errors="replace")
    # the map's "Discarded input sections" block lists what --gc-sections dropped: only the kept part is checked
    kept = map_text.split("Memory Configuration", 1)[-1]
    errs, build = check(image, kept, version(), a.allow_dirty)
    for e in errs:
        print("relcheck FAIL:", e)
    if errs:
        return 1
    print(f"relcheck: PASS ({len(strings(image))} strings, no developer text, no fake-only code; build '{build}')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
