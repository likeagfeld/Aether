"""M2 deferred (a): the console's load limits for the three card files (contract/SPEC.md §5.9).
Each limit is re-derived from the menu's C source, so a drift between C and the checker goes RED."""
import re
from pathlib import Path

from omsat import artpak as ap, check
from omsat.gamesdb import GameRecord, write_gamesdb
from omsat.libidx import Folder, LibEntry, write_libidx

ROOT = Path(__file__).resolve().parents[2]


def _c(path: str) -> str:
    return (ROOT / path).read_text()


def test_limits_match_the_c_source():
    lay = _c("menu/src/omlayout.h")
    gdb_cap = int(re.search(r"OM_LW_GAMESDB_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    art_cap = int(re.search(r"OM_LW_ARTTAB_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    kb = int(re.search(r"#define OM_LIBBUF \((\d+)u \* 1024u\)", _c("menu/app/main.c")).group(1))
    assert "if (size + 4 > cap)" in _c("menu/app/sd.c")              # sd.c:65 read slack
    assert "need + 4 > b->arttab_cap" in _c("menu/app/card.c")      # card.c:37
    assert check.GDB_MAX_FILE == gdb_cap - 4 == 245_756               # 0x3C000 cap (VRTILES.BIN above it)
    assert check.LIB_MAX_FILE == kb * 1024 - 4 == 212_988
    assert check.ART_MAX_HEAD == art_cap - 4 == 122_876              # 0x1E000 cap (BIGLOGO.BIN above it)
    assert check.ART_MAX_ENTRIES == (check.ART_MAX_HEAD - 64) // 48 == 2_558
    blg = int(re.search(r"OM_LW_BIGLOGO_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    assert int(re.search(r"OM_LW_BIGLOGO\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16) == 0x240000 + art_cap
    assert check.BLG_MAX_FILE == blg == 8_192


def test_gamesdb_at_and_over_the_limit():
    base = write_gamesdb([GameRecord("A-1", "A")], ["G"], {}, 20261118)
    at = base + b"\0" * (check.GDB_MAX_FILE - len(base))
    assert check.limit_errors(at) == []
    over = at + b"\0"
    errs = check.limit_errors(over)
    assert errs and "245757" in errs[0] and "245756" in errs[0]
    assert check.check_bytes(over), "check_bytes must surface the limit error"


def test_libidx_at_and_over_the_limit():
    base = write_libidx([LibEntry("a.cue", "A-1")], [Folder("", 0xFFFF)], 7, 20261118)
    at = base + b"\0" * (check.LIB_MAX_FILE - len(base))
    assert check.limit_errors(at) == []
    assert check.limit_errors(at + b"\0")


def _art(n: int) -> bytes:
    return ap.write_artpak([ap.ArtEntry(f"K{i:05d}", 0, None, None, None, i) for i in range(n)], 20261118)


def test_artpak_entry_limit():
    assert check.limit_errors(_art(check.ART_MAX_ENTRIES)) == []
    errs = check.limit_errors(_art(check.ART_MAX_ENTRIES + 1))
    assert errs and "2559" in errs[0] and "2558" in errs[0]
