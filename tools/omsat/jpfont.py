"""Noto Sans JP (Google, SIL Open Font License 1.1) for JP-title bitmaps (spec §5.2, §8.1 TitleRenderer).
Downloaded on demand to build/fonts/ together with its OFL.txt. If it is unavailable, the builder skips JP bitmaps
and leaves ART.PAK flag bit2 clear, and the report says why.

revised: over-long JP titles are scaled horizontally, never cropped (spec §8.1
"wrap or scale"), but the scale target is the menu's actual JP display panel width, not the contract's
absolute field cap. menu/src/omlayout.h defines OM_JP_CANVAS_W = 160 (plan Q10: "the menu's JP panel is
160 px wide"), and menu/src/artpak.c:123 rejects jp_w > OM_JP_CANVAS_W on the real console; 320 is only
spec §5.2's field maximum (the u16 width byte's absolute ceiling), never the console's actual display
width. OM_JP_CANVAS_W below is the single Python-side source of truth for this number, so this file and
omlayout.h cannot silently drift apart."""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

NOTO_URL = "https://github.com/google/fonts/raw/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
OFL_URL = "https://github.com/google/fonts/raw/main/ofl/notosansjp/OFL.txt"
DEFAULT = Path(__file__).resolve().parents[2] / "build" / "fonts" / "NotoSansJP[wght].ttf"

OM_JP_CANVAS_W = 160   # menu/src/omlayout.h OM_JP_CANVAS_W -- keep these two in sync by hand


def _fetch(url: str) -> bytes:
    import urllib.request
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def usable(path: Path) -> bool:
    try:
        return ImageFont.truetype(str(path), 14).getbbox("日本語")[2] > 0
    except Exception:
        return False


def ensure_font(path=None, download=True, fetch=_fetch, default: Path = DEFAULT):
    p = Path(path) if path else default
    if p.exists() and usable(p):
        return p, "cached"
    if path:
        return None, f"font not usable at {p}"
    if not download:
        return None, f"font missing at {p} (download disabled)"
    try:
        data = fetch(NOTO_URL)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        (p.parent / "OFL.txt").write_bytes(fetch(OFL_URL))
    except Exception as e:  # network, HTTP or filesystem: reported, never raised
        return None, f"download failed: {type(e).__name__}: {e}"
    return (p, "downloaded") if usable(p) else (None, "downloaded file is not a usable font")


def _font(path: Path, px: int):
    f = ImageFont.truetype(str(path), px)
    try:
        f.set_variation_by_axes([400])   # Regular weight on the variable font, when supported
    except Exception:
        pass
    return f


def _pack_4bpp(img) -> bytes:
    px = list(img.getdata())
    data = bytearray()
    for i in range(0, len(px), 2):
        data.append(((px[i] >> 4) << 4) | (px[i + 1] >> 4))
    return bytes(data)


# the vertical layout. The bitmap is drawn at DET_TITLE's y0 - 1 = 170 (menu/views/folders.c draw_details),
# 16 rows, so rows 0..15 land on 170..185 and DET_META starts at 186 (menu/src/layout.h): the bitmap cannot grow, the
# glyphs must fit it. Pillow's default "la" anchor put the baseline at the font's ascent -- 17 at 14 px, 16 at 13 px
# (measured) -- i.e. at or below the bitmap's last row, and cut off the bottom of most glyphs (537 of the 1,270 titles of
# build/db/jp_titles.json lost ink, measured 2026-11-22). Now each size draws with the "ls" (left, baseline) anchor at a
# fixed baseline row from this table, chosen so a kanji/kana title's ink is centred on row 8 (the box's text middle,
# OM_DET_TITLE_MID 178); measured over the same 1,270 titles, every one then fits at every size except a few with
# full-width descenders, which _place() lifts by the minimum (at most 1 row at 14 px).
# The exact rule (contract/SPEC.md §5.2 "Vertical layout"; the Card Manager mirrors it): the baseline row is
# BASELINE[px] + floor((height - 16) / 2); the ink rows [top, bot) are taken from a render with that baseline on a canvas
# with _PAD spare rows above and below; bot - top > height skips the size; otherwise the bitmap row r is canvas row
# r - dy, with dy = -top when top < 0, height - bot when bot > height, else 0.
BASELINE = {14: 13, 13: 13, 12: 13, 11: 12, 10: 12}
_PAD = 32          # canvas margin above and below the bitmap: ink is located, never lost, before it is placed


def _place(text: str, f, px: int, width: int, height: int):
    """The text at size px on a canvas `width` wide, baseline at BASELINE[px] (+ (height - 16) // 2). Returns
    (height-row image, ink right edge); None when the text has no ink; False when its ink is taller than `height`
    (the caller tries a smaller size). Ink that overhangs the top or bottom is shifted in by the minimum, never cut."""
    tall = Image.new("L", (width, height + 2 * _PAD))
    ImageDraw.Draw(tall).text((0, _PAD + BASELINE[px] + (height - 16) // 2), text, fill=255, font=f, anchor="ls")
    bb = tall.getbbox()
    if not bb:
        return None
    top, bot = bb[1] - _PAD, bb[3] - _PAD
    if bot - top > height:
        return False
    dy = -top if top < 0 else height - bot if bot > height else 0
    return tall.crop((0, _PAD - dy, width, _PAD - dy + height)), bb[2]


class TooTall(ValueError):
    """The title's ink is taller than the bitmap even at 10 px. Its JP bitmap is dropped, never clipped or
    squashed, and the caller reports it (cardbuild's jp.too_tall); the menu then draws the title as text."""


def render_jp(text: str, font_path: Path, max_w: int = OM_JP_CANVAS_W, height: int = 16):
    """Largest size 14..10 px whose ink fits max_w (default OM_JP_CANVAS_W = 160, the menu's actual
    JP panel width -- see the module docstring; spec §5.2's 320 is only the field's absolute cap) and is at
    most `height` rows tall, placed by _place (M6 Task 18d: no ink is ever clipped vertically).
    Ruling M2-R6 (revised): if the title still does not fit at 10px, it is never cropped -- the
    full, uncropped greyscale render is resampled horizontally to exactly max_w with LANCZOS before
    4bpp packing, so every glyph survives, just narrower. Returns ((w, h, 4bpp bytes) | None, scaled),
    where `scaled` means "this title did not fit and was resampled to max_w" (never "cropped").
    Raises TooTall when the ink is taller than `height` rows at every size that was tried, down to 10 px."""
    for px in (14, 13, 12, 11, 10):
        f = _font(font_path, px)
        if f.getbbox(text)[2] > max_w:
            continue
        r = _place(text, f, px, 1024, height)
        if r is None:
            return None, False
        if r is False:
            continue
        img, right = r
        w = min(max_w, (right + 7) // 8 * 8)
        return (w, height, _pack_4bpp(img.crop((0, 0, w, height)))), False

    f = _font(font_path, 10)
    bw = max(1, f.getbbox(text)[2])
    r = _place(text, f, 10, bw, height)
    if r is None:
        return None, False
    if r is False:
        raise TooTall(f"JP title ink is over {height} rows even at 10 px")
    img, right = r
    ink = img.crop((0, 0, right, height))          # trims only blank margin past the ink, never content
    scaled_img = ink.resize((max_w, height), Image.LANCZOS)
    return (max_w, height, _pack_4bpp(scaled_img)), True
