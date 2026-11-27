"""User decision: VibeMenu's version is "0.1" and changes ONLY when the user says so. menu/src/version.h is its
one home; an accidental bump fails here."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def menu_version() -> str:
    m = re.search(r'#define\s+VIBEMENU_VERSION\s+"([^"]*)"', (ROOT / "menu/src/version.h").read_text(encoding="utf-8"))
    assert m, "VIBEMENU_VERSION missing from menu/src/version.h"
    return m.group(1)


def test_version_is_0_1():
    assert menu_version() == "0.1"


def test_build_string_and_tag_derive_from_the_one_define():
    s = (ROOT / "menu/src/version.h").read_text(encoding="utf-8")
    assert '#define OM_BUILD_STRING "Aether " VIBEMENU_VERSION " " OM_BUILD_ID' in s
    assert '#define OM_VERSION_TAG "FARKUS " VIBEMENU_VERSION' in s


def test_no_other_hardcoded_version_in_menu_sources():
    """Nothing else in the menu spells a version number of its own (diag.c used a fixed "M3 TASK 16A" tag)."""
    for p in (ROOT / "menu").rglob("*.[ch]"):
        if "spikes" in p.parts or p.name == "version.h":
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        assert "TASK 16A" not in t and "Aether 0." not in t and "VibeMenu 0." not in t, p
