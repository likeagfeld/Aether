"""The UI font atlas: the committed menu/src/uifont_data.c is in step with tools/omsat/uifont.py and the
size gate holds. Regenerating needs Noto Sans JP (build/fonts/, fetched on demand); without it the comparison is
skipped and says why, but the parse/size checks still run on the committed file."""
import re

import pytest

from omsat import uifont


def test_committed_atlas_matches_generator():
    path, why = uifont.ensure_font(download=False)
    if not path:
        pytest.skip(f"Noto Sans JP not available: {why}")
    text, n = uifont.build(path)
    assert uifont.OUT.read_text(encoding="utf-8") == text, "run `cd tools && python -m omsat.uifont`"
    assert n <= uifont.MAX_BYTES


def test_committed_atlas_shape_and_size():
    src = uifont.OUT.read_text(encoding="utf-8")
    n = int(re.search(r"om_uifont_bytes = (\d+)u", src).group(1))
    assert 10_000 < n <= uifont.MAX_BYTES
    m = uifont.load_metrics()
    assert set(m) == {name for name, _px, _w in uifont.FACES}
    assert all(len(g) == len(uifont.CHARSET) == 98 for g in m.values())


def test_measure_is_proportional():
    m = uifont.load_metrics()
    assert uifont.measure(m["item"], "W") > uifont.measure(m["item"], "i")
    assert 60 <= uifont.measure(m["brand"], "VibeMenu") <= 80
    assert uifont.measure(m["small"], "") == 0
