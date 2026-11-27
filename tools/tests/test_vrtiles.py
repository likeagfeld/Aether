"""The virtual root folders' art tiles (final M3 fix round; M6 C3 moved them out of HWRAM): the committed
/aether/VRTILES.BIN is in step with the generator, carries the §5.1 common header (OMSATVRT), fits its LWRAM region,
and is no longer linked into the menu image."""
import os
import re
from pathlib import Path

from omsat import binfmt as bf, check
from omsat.gen_vrtiles import DATE, HEAD, MAGIC, NAMES, OUT, H, W, body, expand, make

ROOT = Path(__file__).resolve().parents[2]


def test_committed_vrtiles_match_generator():
    data = OUT.read_bytes()
    assert OUT.name == "VRTILES.BIN"
    assert data == make()
    h = bf.validate_common(data, MAGIC)
    tiles = body()
    assert (h.count, h.table_off, h.table_size, h.data_off, h.data_size) == (len(NAMES), 64, len(tiles), 0, 0)
    assert h.build_date == DATE and h.sections == ((0, 0), (0, 0), (0, 0))
    assert data[64:] == tiles
    w, hh, n, z = HEAD.unpack_from(tiles)
    assert (w, hh, n, z) == (W, H, len(NAMES), 0) and W * H == 25600     # the square hero (omlayout.h)
    assert len(tiles) % 4 == 0
    assert check.check_bytes(data) == []


def test_tiles_use_roles_and_differ():
    data = body()
    tiles = [expand(data, i) for i in range(len(NAMES))]
    for t in tiles:
        assert all(r <= 4 and 0 <= lv <= 255 for r, lv in t)
        assert t[0] == (0, 0)                                           # the corner is the base
    assert tiles[0][84 * W + 70] == (3, 255)                            # Favorites: gold star
    assert len({tuple(t) for t in tiles}) == len(NAMES)


def test_vrtiles_fit_their_lwram_region():
    lay = (ROOT / "menu" / "src" / "omlayout.h").read_text()
    base = int(re.search(r"OM_LW_VRTILES\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    cap = int(re.search(r"OM_LW_VRTILES_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    gdb = int(re.search(r"OM_LW_GAMESDB\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    gdb_cap = int(re.search(r"OM_LW_GAMESDB_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    art = int(re.search(r"OM_LW_ARTTAB\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    assert gdb + gdb_cap == base == 0x0023C000 and base + cap == art      # between GAMES.DB and the ART.PAK table
    assert check.VRT_MAX_FILE == cap - 4                                # sd.c's 4 B read slack
    assert len(OUT.read_bytes()) <= check.VRT_MAX_FILE
    assert check.limit_errors(OUT.read_bytes()) == []
    over = OUT.read_bytes() + b"\0" * (check.VRT_MAX_FILE + 1 - len(OUT.read_bytes()))
    assert check.limit_errors(over)


def test_vrtiles_are_not_linked_into_hwram():
    """the 13,876 B of tiles left the menu's .rodata (sbuild's embed_blob); the menu reads the card file."""
    sb = (ROOT / "tools" / "sbuild.py").read_text(encoding="utf-8")
    assert not re.search(r"embed_blob\([^)]*vrtiles", sb, re.I)
    assert "vrtiles.bin" not in os.listdir(ROOT / "menu" / "app" / "assets")   # case-sensitive (NTFS is not)
    art = (ROOT / "menu" / "ui" / "art.c").read_text(encoding="utf-8")
    assert "om_vrtiles" not in art
