"""the clean-logo pipeline is deterministic on its Lanczos path, keeps alpha in 16 levels, and
nothing replaces the shipped asset without --apply."""
from pathlib import Path

from PIL import Image

from omsat import logoclean

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "assets" / "logo" / "saturn_logo.png"


def test_lanczos_path_is_deterministic():
    a, m1 = logoclean.upscale(Image.open(SRC), None)
    b, m2 = logoclean.upscale(Image.open(SRC), None)
    assert m1 == m2 == "lanczos" and a.size == (217 * 4, 144 * 4) and a.tobytes() == b.tobytes()


def test_render_sizes_and_alpha_levels():
    big, hdr = logoclean.render(logoclean.clean_matte(logoclean.upscale(Image.open(SRC), None)[0]))
    assert big.size == (152, 101) and hdr.size == logoclean.HDR == (40, 25)
    for im in (big, hdr):
        assert {a for *_, a in im.getdata()} <= {17 * k for k in range(16)}


def test_review_does_not_touch_assets(tmp_path):
    before = (ROOT / "menu" / "app" / "assets" / "BIGLOGO.BIN").read_bytes()
    assert logoclean.main(["--review", "--out", str(tmp_path), "--no-fetch"]) == 0
    assert (tmp_path / "sheet.png").is_file() and "lanczos" in (tmp_path / "REVIEW.txt").read_text()
    assert (ROOT / "menu" / "app" / "assets" / "BIGLOGO.BIN").read_bytes() == before


# --- review round 2 (user: "It still has white flecks around the edges"): the no-light-fringe gate ----------------
# An edge pixel (alpha > 0, within 3 px of transparency), blended by Task 6's rule over a dark backdrop, may not be
# lighter than the opaque pixels within 3 px that lie at least as deep in the shape by more than FRINGE_TOL = 8 luma
# (Rec.601, 0..255; about one RGB555 step).

import pytest  # noqa: E402

BLACK, BLUE = 0, logoclean._rgb555(logoclean.THEMES["saturn"][1])     # the saturn theme's darkest blue


def _rgb(r, g, b):
    return (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3)


def test_gate_tolerance_is_stated():
    assert logoclean.FRINGE_TOL == 8 and logoclean.FRINGE_RADIUS == 3


def test_gate_white_tube_end_passes():
    """A white tube (legitimately white) with a soft white end: white next to white is not a fringe."""
    w, h = 16, 9
    px = [(0, 0)] * (w * h)
    for y in range(2, 7):
        for x in range(2, 13):
            px[y * w + x] = (_rgb(250, 250, 250), 255)
        px[y * w + 13] = (_rgb(250, 250, 250), 136)          # the soft end, its color bled from the tube
    for back in (BLACK, BLUE):
        bad, checked, _ = logoclean.fringe_violations(px, w, h, back)
        assert bad == [] and checked > 20


def test_gate_white_speck_next_to_blue_fails():
    """A blue disc's rim with one white speck (opaque, and a soft one): both are fringes."""
    w, h = 16, 16
    px = [(0, 0)] * (w * h)
    for y in range(h):
        for x in range(w):
            if (x - 8) ** 2 + (y - 8) ** 2 <= 36:
                px[y * w + x] = (_rgb(0, 150, 255), 255)
    px[8 * w + 2] = (_rgb(255, 255, 255), 255)               # opaque speck on the left rim
    px[2 * w + 8] = (_rgb(255, 255, 255), 170)               # soft speck on the top rim
    for back in (BLACK, BLUE):
        bad, _, _ = logoclean.fringe_violations(px, w, h, back)
        assert {(x, y) for x, y, _ in bad} >= {(2, 8), (8, 2)}


def test_gate_catches_the_shipped_assets():
    """The gate sees what the user saw: the pre-Task 7 1-bit big logo (old_assets: the original art, white matte in its
    edge colors) fails it."""
    w, h, px = logoclean.old_assets()["big"]
    assert len(logoclean.fringe_violations(px, w, h, BLACK)[0]) > 0


@pytest.fixture(scope="module")
def built():
    return logoclean.build(None)                             # the Lanczos path: deterministic, no tool needed


def test_final_assets_pass_the_gate(built):
    """The final assets as stored (big logo palette-reduced) have no light fringe over black or the darkest blue."""
    _master, method, assets, _path, _dec = built
    assert method == "lanczos"
    total, lines = logoclean.gate(assets)
    assert total == 0, lines
    assert assets["big_n"] <= 127 and len(assets["big_edge_alpha"]) > 2
    w, h, px = assets["foot"]
    assert (w, h) == (22, 15)


def test_decontamination_keeps_the_interior(built):
    """Only the geometric edge band changes: every opaque source pixel more than BAND px from transparency keeps its
    exact color (the logo's own whites included)."""
    _m, _me, _a, path, dec = built
    src = Image.open(SRC).convert("RGBA")
    w, h = src.size
    sp, dp = list(src.getdata()), list(dec.getdata())
    dist, _ = logoclean._bfs(w, h, [i for i, p in enumerate(sp) if p[3] < 128])
    interior = [i for i in range(w * h) if sp[i][3] >= 128 and dist[i] > logoclean.BAND]
    assert interior and all(dp[i] == sp[i] for i in interior)
    assert all(path[i] == 0 for i in interior)
