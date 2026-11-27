"""The About popup's logo (menu/pop/plogo.c): the committed file in step with the Card Manager's About logo, the tile's
size what pabout.h says, every level used once at least and the corners transparent."""
import re
from pathlib import Path

from omsat import gen_aboutlogo as g

PABOUT = Path(g.ROOT) / "menu" / "pop" / "pabout.h"


def test_committed_file_is_current():
    assert g.OUT.read_text() == g.make(), "run `python -m omsat.gen_aboutlogo` (tools/) and commit menu/pop/plogo.c"


def test_size_matches_pabout_h():
    h = PABOUT.read_text()
    assert int(re.search(r"#define OM_PA_LOGO_W\s+(\d+)", h).group(1)) == g.W
    assert int(re.search(r"#define OM_PA_LOGO_H\s+(\d+)", h).group(1)) == g.H
    assert g.W % 8 == 0


def test_levels_and_corners():
    lut, px = g.indexed()
    assert len(lut) == 16 and lut[0] == 0 and all(v & 0x8000 for v in lut[1:])
    assert px[0] == px[g.W - 1] == px[(g.H - 1) * g.W] == px[-1] == 0
    assert 0 not in px[g.W * (g.H // 2):g.W * (g.H // 2) + 1] + px[g.W * g.RADIUS + g.RADIUS:g.W * g.RADIUS + g.W - g.RADIUS]
    assert len(set(px)) >= 12                       # the glow's levels, not a flat two-tone
