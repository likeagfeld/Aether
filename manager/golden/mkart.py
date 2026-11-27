"""manager/golden/mkart.py -- Python-side expects for the Card Manager's cover encoder and JP title renderer (M4 Task 6).
Aether Card Manager, GPL-3.0-or-later. Uses tools/omsat read-only (artpak.encode_cover / layout_for / rgb555,
jpfont.render_jp and its size choice) and the font cardbuild uses, build/fonts/NotoSansJP[wght].ttf (omsat.jpfont).

    python manager/golden/mkart.py      -> manager/golden/expect/art_covers.json, art_titles.json

Covers: input images are generated from formulas both sides implement (gradient(w, h) = make_fixtures._gradient;
solid colors; an RGBA disc on a transparent field). Every case records the output's SHA-256 (the C# encoder reproduces
Pillow's Lanczos, so all are byte-identical); resampled cases also record the whole output (zlib + base64) so a
mismatch names its first differing pixel.
Titles: per title, the px omsat.jpfont.render_jp chose (14..10, or 10 + scaled), the ink right edge PIL measured at
each px, and the 4-bpp bitmap (hex)."""
from __future__ import annotations

import base64
import hashlib
import json
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
from PIL import Image  # noqa: E402

from omsat import artpak, jpfont  # noqa: E402

FONT = REPO / "build/fonts/NotoSansJP[wght].ttf"


def gradient(w: int, h: int) -> Image.Image:          # contract/fixtures/make_fixtures.py _gradient
    img = Image.new("RGB", (w, h))
    img.putdata([((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), 128) for y in range(h) for x in range(w)])
    return img


def solid(w: int, h: int, rgb) -> Image.Image:
    return Image.new("RGB", (w, h), rgb)


def disc(w: int, h: int) -> Image.Image:
    """An opaque disc of radius min(w,h)/2 - 2 centred on a fully transparent field (alpha 0 or 255 only)."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    r = min(w, h) / 2 - 2
    img.putdata([((x * 7) & 255, (y * 5) & 255, 200, 255) if (x - w / 2 + 0.5) ** 2 + (y - h / 2 + 0.5) ** 2 <= r * r
                 else (0, 0, 0, 0) for y in range(h) for x in range(w)])
    return img


COVERS = [
    # name, generator spec, exact?
    ("same_square_gradient", {"kind": "gradient", "w": 160, "h": 160}, True),
    ("same_tall_gradient", {"kind": "gradient", "w": 128, "h": 200}, True),
    ("same_thumb_gradient", {"kind": "gradient", "w": 64, "h": 100}, True),
    ("solid_wide_red", {"kind": "solid", "w": 200, "h": 100, "rgb": [255, 0, 0]}, True),
    ("solid_tall_teal", {"kind": "solid", "w": 90, "h": 300, "rgb": [16, 200, 180]}, True),
    ("solid_square_odd", {"kind": "solid", "w": 333, "h": 301, "rgb": [77, 66, 255]}, True),
    # 320x201 at scale 0.5: the height is 100.5, which round() takes to 100 (half to even), not 101
    ("solid_half_round", {"kind": "solid", "w": 320, "h": 201, "rgb": [200, 100, 50]}, True),
    ("half_round_gradient", {"kind": "gradient", "w": 320, "h": 201}, False),
    ("down_tall_gradient", {"kind": "gradient", "w": 300, "h": 420}, False),
    ("down_square_gradient", {"kind": "gradient", "w": 480, "h": 470}, False),
    ("up_square_gradient", {"kind": "gradient", "w": 40, "h": 40}, False),
    ("disc_rgba", {"kind": "disc", "w": 320, "h": 320}, False),
]


def make(spec) -> Image.Image:
    if spec["kind"] == "gradient":
        return gradient(spec["w"], spec["h"])
    if spec["kind"] == "solid":
        return solid(spec["w"], spec["h"], tuple(spec["rgb"]))
    return disc(spec["w"], spec["h"])


TITLES = ["グランディア", "サクラ大戦", "バーチャファイター2", "ナイツ", "パンツァードラグーン ツヴァイ", "デイトナUSA",
          "真・女神転生 デビルサマナー", "ドラゴンフォース", "SEGA RALLY", "Gun Griffon", "セガラリーチャンピオンシップ",
          "スーパーロボット大戦F完結編", "東京魔人學園外法帖", "悪魔城ドラキュラX 月下の夜想曲",
          "シャイニング・フォースIII シナリオ1 王都の巨神", "あ"]


def covers() -> dict:
    out = []
    for name, spec, exact in COVERS:
        img = make(spec)
        lay = artpak.layout_for(img)
        for part, sizes in (("hero", artpak.HERO), ("thumb", artpak.THUMB)):
            size = sizes[lay]
            if exact and spec["kind"] == "gradient" and (spec["w"], spec["h"]) != size:
                continue                                   # a same-size case is exact only at its own size
            b = artpak.encode_cover(img, size)
            row = {"name": name, "part": part, "spec": spec, "layout": lay, "w": size[0], "h": size[1],
                   "exact": exact, "sha256": hashlib.sha256(b).hexdigest()}
            if not exact:
                row["data_zlib_b64"] = base64.b64encode(zlib.compress(b, 9)).decode("ascii")
            out.append(row)
    return {"source": "omsat.artpak.encode_cover (mkart.py)", "covers": out,
            "layout_cases": {f"{w}x{h}": artpak.layout_for(Image.new("RGB", (w, h)))
                             for w, h in ((85, 100), (84, 100), (100, 100), (64, 100), (1, 2), (200, 100), (17, 20))}}


def titles() -> dict:
    out = []
    for t in TITLES:
        right = {}
        for px in (14, 13, 12, 11, 10):
            right[str(px)] = jpfont._font(FONT, px).getbbox(t)[2]
        chosen = next((px for px in (14, 13, 12, 11, 10) if right[str(px)] <= jpfont.OM_JP_CANVAS_W), None)
        (w, h, data), scaled = jpfont.render_jp(t, FONT)
        out.append({"text": t, "px": chosen, "scaled": scaled, "ink_right": right, "w": w, "h": h, "data": data.hex()})
    return {"source": "omsat.jpfont.render_jp over build/fonts/NotoSansJP[wght].ttf at wght 400 (mkart.py)",
            "font_sha256": hashlib.sha256(FONT.read_bytes()).hexdigest(), "max_w": jpfont.OM_JP_CANVAS_W, "titles": out}


def main() -> int:
    d = REPO / "manager/golden/expect"
    for name, fn in (("art_covers", covers), ("art_titles", titles)):
        p = d / f"{name}.json"
        p.write_text(json.dumps(fn(), indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
