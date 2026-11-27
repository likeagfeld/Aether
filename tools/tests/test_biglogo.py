"""The interstitial's large logo: 152x101, indexed, codec-2 ops, format v2; committed file in
step with the cleaned master, n <= 127, soft edges and no light fringe. The file is BIGLOGO.BIN on
the menu disc, the 64-byte common header (magic OMSATBLG, SPEC §5.19) + that table, byte for byte."""
import re
import struct
from pathlib import Path

import pytest

from omsat import binfmt as bf, check

from PIL import Image

from omsat import gen_biglogo, logoclean
from omsat.gen_biglogo import COLORS, H, HEAD, MAGIC, OUT, W, body, check_biglogo, expand, make

BACKS = (0, logoclean._rgb555(logoclean.THEMES["saturn"][1]))      # black; the saturn theme's darkest blue


def test_transparent_and_black(tmp_path):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for x in range(W):                                  # rows big enough to survive the resize unchanged
        for y in range(40, 60):
            img.putpixel((x, y), (0, 0, 0, 255))        # opaque black must not collide with the transparent code
    p = tmp_path / "t.png"
    img.save(p)
    px = expand(body(p))
    assert px[0] == 0                                   # transparent corner
    assert px[50 * W + 70] == 0x8000                    # opaque black: bit 15 set (HWF-01)


def test_committed_biglogo_matches_png():
    f = Path(OUT).read_bytes()
    assert f == make()
    h = bf.validate_common(f, MAGIC)                     # the common header, then the table
    assert (h.count, h.table_off, h.table_size, h.data_size) == (1, 64, len(f) - 64, 0)
    data = f[64:]
    assert data == body()
    w, h, n, olen = HEAD.unpack_from(data)
    assert (w, h) == (W, H) and W % 8 == 0
    assert 0 < n <= COLORS - 1                          # n <= 127
    assert len(data) == (HEAD.size + 3 * n + (n & 1) + olen + 3) // 4 * 4
    assert len(data) % 4 == 0
    lay = (Path(OUT).parents[2] / "src" / "omlayout.h").read_text()   # whole sectors fit OM_LW_BIGLOGO
    cap = int(re.search(r"OM_LW_BIGLOGO_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    assert (len(f) + 2047) // 2048 * 2048 <= cap
    px = expand(data)
    assert any(px) and not all(px)
    assert all(p == 0 or p & 0x8000 for p in px)


def test_committed_biglogo_has_soft_edges_and_no_light_fringe():
    """the edge alpha uses more than 2 levels, and over black or the darkest blue no edge pixel is lighter
    than the opaque interior within 3 px by more than 8 luma."""
    px = gen_biglogo.decode(Path(OUT).read_bytes()[64:])
    assert len({a for _c, a in px}) > 2
    for back in BACKS:
        bad, checked, _ = logoclean.fringe_violations(px, W, H, back)
        assert bad == [] and checked > 1000


def test_check_biglogo_and_its_mutations():
    """omsat.check validates BIGLOGO.BIN (the menu refuses the same cases: menu/src/biglogo.c)."""
    f = Path(OUT).read_bytes()
    assert check_biglogo(f) == [] and check.check_bytes(f) == []
    bad = bytearray(f); bad[64 + 100] ^= 1
    assert any("CRC" in e for e in check_biglogo(bytes(bad)))
    t = bytearray(f[64:]); t[1] ^= 1                    # width 152 -> 153
    assert check_biglogo(gen_biglogo.wrap(bytes(t)))
    t = bytearray(f[64:]); struct.pack_into(">H", t, 6, len(t))   # ops length past the table
    assert check_biglogo(gen_biglogo.wrap(bytes(t)))
    assert check.limit_errors(f) == []
    assert check.limit_errors(f + b"\0" * (check.BLG_MAX_FILE - len(f) + 1))


def test_blend_over_black_crc_matches_the_c_host_test():
    """The menu's decode + blend over black (menu/src/biglogo.c, host test test_biglogo.c) must give exactly what the
    blend rule gives here (gen_biglogo.shown_pixels): one CRC-32 over the big-endian words, pinned in both files."""
    words = gen_biglogo.shown_pixels(Path(OUT).read_bytes()[64:], 0)
    crc = bf.crc32(struct.pack(f">{len(words)}H", *words))
    src = (Path(OUT).parents[2] / "host-tests" / "test_biglogo.c").read_text()
    pinned = int(re.search(r"#define BLACK_CRC\s+0x([0-9A-Fa-f]{8})u", src).group(1), 16)
    assert crc == pinned
