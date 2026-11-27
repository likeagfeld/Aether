"""Task 13r addendum: no British spelling from the swept list appears in the menu's user-visible string
literals. There is no single central string table, so this scans every quoted C string literal in the menu's
own source (menu/app, menu/src, menu/ui, menu/views, menu/launcher -- excluding menu/lib, which is
unmodified upstream/public-domain code, same exclusion the Task 13r grep used). Comments are stripped first,
since a comment can legitimately *mention* one of these words (e.g. documenting a device's own field name)
without it ever reaching the screen.

This is also the proof grep for the task's closing requirement ("a grep showing no user-visible 'OpenMenu'
remains in menu strings" is the sibling check in the report; this file is the British-spelling half)."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = ["menu/app", "menu/src", "menu/ui", "menu/views", "menu/launcher"]

# Case-insensitive, whole-word-ish: the exact list from the task-13r-brief addendum.
BRITISH = re.compile(
    r"favour|colour|\bgrey\b|centre|behaviour|initialis|organis|recognis|licence|cancell|programme|catalogue",
    re.IGNORECASE,
)

# Upstream (MPL-2.0 satiator-menu) strings, carried over verbatim -- quoted source text, not ours to
# Americanise (menu/launcher/UPSTREAM.md tracks these files' exact upstream hashes).
UPSTREAM_ALLOW = {
    "Unrecognised file extension - no dot in filename",
    "Unrecognised file extension '%s'",
    "Unrecognised BIOS version!",
}

STRING_LIT = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def _strip_comments(text: str) -> str:
    # Block comments (DOTALL) then line comments. Good enough for this codebase's C (no string literal here
    # contains "/*", "*/" or "//").
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)
    return text


def _string_literals(path: Path) -> list[str]:
    code = _strip_comments(path.read_text(encoding="utf-8"))
    return [m.group(0)[1:-1] for m in STRING_LIT.finditer(code)]


def _source_files():
    for d in SCAN_DIRS:
        base = ROOT / d
        for ext in ("*.c", "*.h"):
            yield from sorted(base.rglob(ext))


def test_no_british_spelling_in_menu_string_literals():
    hits = []
    for path in _source_files():
        for lit in _string_literals(path):
            if lit in UPSTREAM_ALLOW:
                continue
            if BRITISH.search(lit):
                hits.append((path.relative_to(ROOT).as_posix(), lit))
    assert hits == [], f"British spelling found in menu string literal(s): {hits}"


def test_scan_actually_covers_the_menu_tree():
    """A guard against the scan silently finding nothing because SCAN_DIRS or ROOT is wrong."""
    files = list(_source_files())
    assert len(files) > 50
    total_literals = sum(len(_string_literals(p)) for p in files)
    assert total_literals > 50
