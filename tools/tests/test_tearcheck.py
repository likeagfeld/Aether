"""tools/omsat/tearcheck.py (Task 13c round 3): a whole loop frame passes, a capture made of two frames' rows fails."""
import numpy as np
from PIL import Image

from omsat import tearcheck


def _capture(path, native):
    img = Image.new("RGB", (1062, 749))
    img.paste(Image.fromarray(np.uint8(native)).resize((960, 672), Image.NEAREST), (51, 50))
    img.save(path)


def test_whole_and_torn(tmp_path):
    rng = np.random.default_rng(3)
    prev = tmp_path / "prev"
    prev.mkdir()
    frames = [rng.integers(40, 220, (224, 320, 3)) for _ in range(3)]
    for i, f in enumerate(frames):
        Image.fromarray(np.uint8(f)).save(prev / f"lbp_{i:04d}.png")
    _capture(tmp_path / "whole.png", frames[1])
    torn = frames[0].copy()
    torn[110:] = frames[2][110:]
    _capture(tmp_path / "torn.png", torn)
    assert tearcheck.main(["--preview", str(prev), str(tmp_path / "whole.png")]) == 0
    assert tearcheck.main(["--preview", str(prev), str(tmp_path / "torn.png")]) == 1
