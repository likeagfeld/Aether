"""The header logo and the loading screen's footer logo: committed files in step with the
cleaned master, format v2, and no light fringe (the gate, tools/omsat/logoclean.py fringe_violations)."""
import struct
from pathlib import Path

from PIL import Image

from omsat import gen_logo, logoclean
from omsat.gen_logo import FH, FOOT_OUT, FW, FW_STORED, H, OUT, W, make

BACKS = (0, logoclean._rgb555(logoclean.THEMES["saturn"][1]))      # black; the saturn theme's darkest blue


def test_alpha_nibbles_and_black(tmp_path):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for y in range(6, 16):
        for x in range(4, 28):
            img.putpixel((x, y), (0, 0, 0, 255))     # opaque black must not collide with the transparent code
    for y in range(6, 16):
        img.putpixel((28, y), (0, 0, 0, 128))        # a soft edge: 128 -> nibble round(136 / 17) = 8
    p = tmp_path / "t.png"
    img.save(p)
    data = make(p)
    assert len(data) == W * H * 2 + W * H // 2 == 2500
    px = struct.unpack_from(f">{W * H}H", data)
    assert px[10 * W + 10] == 0x8000 and px[0] == 0
    assert gen_logo.decode(data)[10 * W + 28][1] == 136
    assert W % 8 == 0


def test_committed_logo_matches_png():
    data = Path(OUT).read_bytes()
    assert data == make()
    px = struct.unpack_from(f">{W * H}H", data)
    assert any(px) and not all(px)                  # some logo, some transparent background
    assert all(p == 0 or p & 0x8000 for p in px)


def test_committed_logo_has_soft_edges_and_no_light_fringe():
    """the edge uses more than 2 alpha levels, and over black or the darkest blue no edge pixel is lighter
    than the opaque interior within 3 px by more than 8 luma."""
    px = gen_logo.decode(Path(OUT).read_bytes())
    assert len({a for _c, a in px}) > 2
    for back in BACKS:
        bad, checked, _ = logoclean.fringe_violations(px, W, H, back)
        assert bad == [] and checked > 100


def test_committed_footer_matches_png_and_has_no_light_fringe():
    data = Path(FOOT_OUT).read_bytes()
    assert data == gen_logo.make_footer()
    assert len(data) == FW_STORED * FH * 2 == 720 and FW_STORED % 8 == 0
    words = struct.unpack(f">{FW_STORED * FH}H", data)
    assert all(w == 0 or w & 0x8000 for w in words)
    assert all(words[y * FW_STORED + x] == 0 for y in range(FH) for x in range(FW, FW_STORED))   # the padding
    px = [((w & 0x7FFF), 255) if w else (0, 0) for y in range(FH) for w in words[y * FW_STORED:y * FW_STORED + FW]]
    bad, checked, _ = logoclean.fringe_violations(px, FW, FH, logoclean.loading_mean())
    assert bad == [] and checked > 50
