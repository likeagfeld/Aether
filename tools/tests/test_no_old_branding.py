"""Task 13r closing requirement: a grep proving no user-visible "OpenMenu", "Satiator" (as a brand), or
"VibeSat" remains in the menu's strings. Reuses the same string-literal extraction as
test_no_british_spelling.py (menu/{app,src,ui,views,launcher}, menu/lib excluded -- unmodified upstream).

"Satiator" alone is the ODE hardware's own name ("Satiator not found", "SATIATOR: NO RESPONSE") and stays --
only the old product-name combination ("OpenMenu Satiator") or the bare old brand ("OpenMenu") is a hit.
"VibeSat" was a same-day intermediate rename (2026-11-18: OpenMenu Satiator -> VibeSat -> VibeMenu) and must
leave no trace anywhere.
"""
from __future__ import annotations

import re
from pathlib import Path

from test_no_british_spelling import _source_files, _string_literals

OLD_BRAND = re.compile(r"openmenu\s*satiator|(?<![a-z])openmenu(?![a-z])", re.IGNORECASE)
VIBESAT = re.compile(r"vibesat", re.IGNORECASE)

# Task 13r: "openmenu" survives as a *comparison* string in the hidden-folder-name set (menu/src/libidx.c's
# om_is_hidden_name), never drawn on screen -- a dev card still carrying the old /openmenu/ folder must not
# list it as a game folder. Not a user-visible brand string.
NEVER_DRAWN_ALLOW = {"openmenu"}

# The About page's credits (the upstream openMenu among them, as CREDITS.md names it) live in menu/pop/pabout.c,
# outside the scanned directories; a credit drawn on purpose that the scan does reach goes here.
CREDIT_ALLOW: set[str] = set()


def test_no_old_brand_strings_in_menu_string_literals():
    hits = []
    for path in _source_files():
        for lit in _string_literals(path):
            if lit in NEVER_DRAWN_ALLOW or lit in CREDIT_ALLOW:
                continue
            if OLD_BRAND.search(lit) or VIBESAT.search(lit):
                hits.append((path.relative_to(path.anchor), lit))
    assert hits == [], f"old-brand string(s) found in menu string literal(s): {hits}"


def test_no_vibesat_anywhere_in_the_repo():
    """The brand was renamed twice (OpenMenu Satiator -> VibeSat -> VibeMenu); only VibeMenu ships."""
    import subprocess

    from test_no_british_spelling import ROOT

    self_path = Path(__file__).resolve().relative_to(ROOT).as_posix()   # this file defines the word itself
    out = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
    ).stdout.decode().split("\0")
    hits = []
    for rel in out:
        if not rel or rel == self_path:
            continue
        p = ROOT / rel
        try:
            text = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if VIBESAT.search(text):
            hits.append(rel)
    assert hits == [], f"'VibeSat' found in tracked file(s): {hits}"


# The Aether rename (2026-11-25): VibeMenu -> Aether, /vibemenu -> /aether. "vibemenu" survives only where it must:
# the pre-rename folder's name (hidden, detected: carddir.h OM_CARD_OLD_NAME), the theme id SETTINGS.INI may still hold
# (themes.c), the console-BRAM marker and the slot stamp (pgsplan.h: stored on consoles and cards, never renamed), the
# BRAM marker record's comment, and the banner that names the old folder to the user.
VIBEMENU = re.compile(r"vibe\s*menu", re.IGNORECASE)
VIBEMENU_ALLOW = {"vibemenu", "VIBEMENU_PG", "VIBEMENU.TXT", "VIBEMENU",
                  "Old /vibemenu folder: saves off. Card Manager > Update card"}


def test_no_vibemenu_in_menu_string_literals():
    hits = []
    for path in _source_files():
        for lit in _string_literals(path):
            if VIBEMENU.search(lit) and lit not in VIBEMENU_ALLOW:
                hits.append((path.relative_to(path.anchor), lit))
    assert hits == [], f"pre-rename name in menu string literal(s): {hits}"
