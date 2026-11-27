"""HWF gate: every HWF-nn tag under menu/ must resolve to a settled row of docs/decisions/m2-hw-facts.md.
`python -m omsat.hwf [--root DIR]`. Exit 1 on any unresolved, pending or unknown tag."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Strict form: exactly "HWF-" + two digits, word-bounded on both sides, so
# "HWF-123" or "HWF-1" cannot silently be read as a valid 2-digit tag.
_TAG = re.compile(r"\bHWF-(\d{2})\b")
# Broad near-miss detector (case-insensitive, optional dash, any digit run):
# catches typo'd forms like "HWF-123", "HWF-1", "hwf-01" so they surface as a
# gate failure instead of being silently invisible to _TAG.
_NEARMISS = re.compile(r"(?i)\bhwf-?\d+\b")
_ROW = re.compile(r"^\|\s*(HWF-\d\d)\s*\|(.*)\|\s*([A-Z-]+)\s*\|\s*$")
SETTLED = {"CONFIRMED", "CORRECTED", "UNVERIFIED-ACCEPTED"}


def rows(doc: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for n, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
        m = _ROW.match(line)
        if m:
            tag = m.group(1)
            if tag in out:
                raise ValueError(
                    f"{tag}: duplicate row in {doc.name} (second occurrence at line {n}) -- "
                    "last-wins dict insertion would let this row silently shadow the earlier one"
                )
            out[tag] = m.group(3)
    return out


def tags(menu: Path) -> dict[str, list[str]]:
    found: dict[str, list[str]] = {}
    for p in sorted(menu.rglob("*")):
        if p.suffix.lower() not in (".c", ".h", ".ld") or "out" in p.parts:
            continue
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in _TAG.finditer(line):
                found.setdefault("HWF-" + m.group(1), []).append(f"{p.relative_to(menu.parent)}:{n}")
    return found


def near_misses(menu: Path) -> list[str]:
    """Locations where an HWF-like token does not match the strict HWF-nn form."""
    out: list[str] = []
    for p in sorted(menu.rglob("*")):
        if p.suffix.lower() not in (".c", ".h", ".ld") or "out" in p.parts:
            continue
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in _NEARMISS.finditer(line):
                if not _TAG.fullmatch(m.group(0)):
                    out.append(
                        f"{p.relative_to(menu.parent)}:{n}: {m.group(0)!r} is not a valid HWF-nn tag "
                        "(expected exact form HWF-NN, e.g. HWF-07)"
                    )
    return out


def check(root: Path) -> list[str]:
    doc = root / "docs" / "decisions" / "m2-hw-facts.md"
    if not doc.exists():
        return ["docs/decisions/m2-hw-facts.md not found in this checkout -- this gate needs a local "
                "checkout that still has docs/decisions/ (it is kept on disk, just not tracked)"]
    try:
        status = rows(doc)
    except ValueError as e:
        return [str(e)]
    found = tags(root / "menu")
    misses = near_misses(root / "menu")
    if not found and not misses:
        return ["no HWF tags found under menu/ (0 of 0) -- gate cannot pass vacuously"]
    errs = list(misses)
    for tag, where in sorted(found.items()):
        st = status.get(tag)
        if st is None:
            errs.append(f"{tag}: used at {where[0]} (+{len(where) - 1} more) but has no row in m2-hw-facts.md")
        elif st not in SETTLED:
            errs.append(f"{tag}: status {st} is not settled (used at {where[0]})")
    return errs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(ROOT))
    a = ap.parse_args(argv)
    errs = check(Path(a.root))
    found = tags(Path(a.root) / "menu")
    for e in errs:
        print("FAIL", e)
    print(f"hwf: {sum(len(v) for v in found.values())} tag uses, {len(found)} distinct, {len(errs)} errors")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
