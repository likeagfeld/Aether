"""Generate the small Aether logos (the Aether rename, 2026-11-25; formerly the Sega Saturn logo):

- menu/app/assets/logo.bin: the menu header's logo (it replaces openMenu's Dreamcast swirl), 40 x 25 at
  (10,8) (2026-11-25; was 32 x 22, where the mark lost its circle: 25 rows are the header's 8..32 above the hairline,
  the width a multiple of 8 for HWF-06 HVsize).
- menu/app/assets/footlogo.bin: the loading screen's footer logo, 22 x 15 (menu/src/layout.h LD_LOGO),
  stored 24 wide (HWF-06) with two transparent columns on the right.

Sources: assets/logo/aether_hdr_master.png (40 x 25, 1:1) is a small-size redraw of the mark for the header: one
thicker stroke (circle 2.3 px, A 2.1 px, ring 1.9 px), a full circle, an A with a crossbar, and the ring tilted -17
degrees crossing in front of the circle's lower half with a 1 px gap cut either side (its back half hidden behind the
circle), drawn 16x and box-reduced, the mark's own top-to-bottom gradient. aether_foot_master.png (176 x 120) is the
mark (assets/logo/aether_mark_small.svg, the single-line version for small sizes) rendered 8x at its own size with a
1.4 output px line. Each is reduced (the header's at its own size: unchanged) and finished with logoclean.finalize:
soft-edge pixels carry the interior's color and none is lighter than the interior next to it (the no-light-fringe
gate, tools/tests/test_logo.py).

logo.bin, format v2 (2,500 B):
    2,000 B: 40 x 25 RGB555 big-endian words, row-major (bit 15 set on every visible pixel, 0x0000 = transparent)
    500 B: 4-bit alpha, two pixels a byte, high nibble = left pixel (15 = opaque, 0 = transparent; alpha = nibble x 17)
The menu (menu/ui/logo.c, menu/src/logoblend.c) blends an edge pixel's color with the backdrop at upload.

footlogo.bin (720 B): 24 x 15 RGB555 big-endian words, already blended with the loading screen's backdrop (the mean
of LOADBG.BIN under the footer over all its frames, logoclean.loading_mean: a flat dark scrim, within one RGB555 step),
bit 15 set on every visible pixel, 0x0000 transparent. The menu uploads it as is (menu/ui/logo.c).

The omsat-build image has no Pillow, so the outputs are committed and tests check they are in step with the PNG.
`python -m omsat.gen_logo` writes both. v1_pixels keeps the pre-Task 7 rule (1-bit, alpha >= 128, the original
saturn_logo.png) for the review sheet's "old" row and the gate's own test."""
from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image

from . import logoclean
from .artpak import encode_cover

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "assets" / "logo" / "aether_hdr_master.png"
FOOT_SRC = ROOT / "assets" / "logo" / "aether_foot_master.png"
ORIGINAL = ROOT / "assets" / "logo" / "saturn_logo.png"
OUT = ROOT / "menu" / "app" / "assets" / "logo.bin"
FOOT_OUT = ROOT / "menu" / "app" / "assets" / "footlogo.bin"
W, H = 40, 25
FW, FH, FW_STORED = 22, 15, 24


def v1_pixels(src: Path = ORIGINAL) -> list[int]:
    """The pre-Task 7 asset's RGB555 words (0 = transparent): 1-bit, alpha >= 128, from the original PNG."""
    with Image.open(src) as img:
        return list(struct.unpack(f">{W * H}H", encode_cover(img, (W, H))))


def pixels(src: Path = SRC) -> list[tuple[int, int]]:
    """(RGB555, alpha) per pixel of the 40 x 25 header logo, as stored."""
    with Image.open(src) as img:
        return logoclean.finalize(logoclean.render_size(img, (W, H)))


def make(src: Path = SRC) -> bytes:
    px = pixels(src)
    words = [c | 0x8000 if a else 0 for c, a in px]
    nib = [round(a / 17) for _c, a in px]
    alpha = bytes(nib[i] << 4 | nib[i + 1] for i in range(0, W * H, 2))
    return struct.pack(f">{W * H}H", *words) + alpha


def decode(data: bytes) -> list[tuple[int, int]]:
    """logo.bin v2 -> (RGB555 without bit 15, alpha 0..255) per pixel."""
    assert len(data) == W * H * 2 + W * H // 2
    words = struct.unpack_from(f">{W * H}H", data)
    return [(w & 0x7FFF, ((data[W * H * 2 + i // 2] >> (0 if i & 1 else 4)) & 15) * 17) for i, w in enumerate(words)]


def shown_pixels(data: bytes, back: int = 0) -> list[int]:
    """What the menu uploads over a flat backdrop (RGB555) by the blend rule (menu/src/logoblend.c): alpha 255 -> the
    color | 0x8000, 0 -> 0x0000, else the color blended with the backdrop | 0x8000."""
    return [0 if not a else (c | 0x8000 if a == 255 else logoclean.mix555(c, back, a) | 0x8000)
            for c, a in decode(data)]


def make_footer(src: Path = FOOT_SRC) -> bytes:
    with Image.open(src) as img:
        px = logoclean.finalize(logoclean.render_size(img, (FW, FH)))
    back = logoclean.loading_mean()
    words = []
    for y in range(FH):
        row = px[y * FW:(y + 1) * FW]
        words += [0 if not a else (c if a == 255 else logoclean.mix555(c, back, a)) | 0x8000 for c, a in row]
        words += [0] * (FW_STORED - FW)
    return struct.pack(f">{FW_STORED * FH}H", *words)


if __name__ == "__main__":
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(make())
    FOOT_OUT.write_bytes(make_footer())
    print(f"wrote {OUT} ({OUT.stat().st_size} B), {FOOT_OUT} ({FOOT_OUT.stat().st_size} B)")
