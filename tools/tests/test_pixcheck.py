"""Task 13's panel-transparency gate instrument: mean colour of one box in two screenshots."""
from PIL import Image

from omsat import pixcheck


def _png(tmp_path, name, colour):
    p = tmp_path / name
    Image.new("RGB", (64, 48), colour).save(p)
    return p


def test_differ_and_same(tmp_path):
    red, blend, red2 = _png(tmp_path, "a.png", (255, 0, 0)), _png(tmp_path, "b.png", (128, 128, 0)), _png(tmp_path, "c.png", (250, 2, 0))
    box = ["--box", "10", "10", "20", "20"]
    assert pixcheck.main([str(red), str(blend), *box, "--min-delta", "40"]) == 0
    assert pixcheck.main([str(red), str(red2), *box, "--min-delta", "40"]) == 1      # the RED leg: no difference
    assert pixcheck.main([str(red), str(red2), *box, "--max-delta", "10"]) == 0
