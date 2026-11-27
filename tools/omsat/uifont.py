"""Generate menu/src/uifont_data.c: the menu's proportional anti-aliased UI faces and icons.

Glyphs are rendered from Noto Sans JP (Google, SIL OFL 1.1; build/fonts/, fetched on demand by omsat.jpfont.ensure_font,
never committed) at the sizes and weights of the approved mockup (.superpowers/sdd/2026-11-18-m3-library-themes-music/
mockup_render.py): item 11 px Regular, sel 11 px SemiBold, small 9 px Regular, title 12 px SemiBold, brand 13 px
SemiBold; Task 13c's loading screen adds label 9 px SemiBold and hero 17 px SemiBold (loading_mockup_render.py);
Task 13d's start-up interstitial adds label8 8 px SemiBold (boot2menu_mockup_render.py).
The brief's 13 px Regular and 8 px SemiBold faces were unused and dropped; the badges render
their letters at 8 px SemiBold here. Spaces advance at least 3 px. Charset: ASCII 0x20-0x7E, then
U+203A, U+00B7, U+2026 (indices 95, 96, 97).

Each glyph is 4 bpp coverage (0 = none .. 15 = full; the menu maps 0 to the VDP1 transparent code), packed two pixels a
byte, high nibble = left pixel (HWF-05), row stride (w + 1) // 2. Metrics are kerning-free: xoff/yoff of the ink box from
the pen at the baseline, and the advance in 1/8 px so the menu's pen position does not drift from rounding.

Icons (vector-drawn here, like the mockup script): the folder, the 5-point favourite star (gold (255,214,90) is applied
by the menu's LUT), and the rounded button badges A, B, X, Y, Z and ST (plus C, L and R since M6 Task 11, for the
remappable buttons). A badge pixel is an index into a two-ramp LUT:
1..8 = footer background -> badge fill (edge coverage), 8..15 = fill -> letter (letter coverage).

The omsat-build container has neither Pillow nor FreeType, so like logo.bin the output is generated on the PC and
committed, and tools/tests/test_uifont.py checks it is in step with this script. `python -m omsat.uifont` writes it.
The committed file is a rendering of the font, not the font: the OFL allows embedding it; CREDITS.md credits it."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from .jpfont import ensure_font

# The Aether wordmark (2026-11-25, user decision D2): "AETHER" in Montserrat Light (Julieta Ulanovsky et al., SIL OFL
# 1.1; build/fonts/, fetched on demand like Noto, never committed), letter-spaced like the logo artwork (0.27 x the cap
# height), rendered here as 4 bpp icons so the menu fades and colours it like its other text.
BRAND_URL = "https://github.com/google/fonts/raw/main/ofl/montserrat/Montserrat%5Bwght%5D.ttf"
BRAND_OFL_URL = "https://github.com/google/fonts/raw/main/ofl/montserrat/OFL.txt"
BRAND_FONT = Path(__file__).resolve().parents[2] / "build" / "fonts" / "Montserrat[wght].ttf"
BRAND_SHA256 = "0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7"
WORDMARKS = (("wordmark", 11, 300), ("wordmark_s", 7, 400))   # (icon, cap height px, weight): header; loading footer


def ensure_brand_font(download: bool = True) -> tuple[Path | None, str]:
    """Montserrat at build/fonts/Montserrat[wght].ttf, fetched once when missing; its SHA-256 is pinned so a changed
    upstream file cannot silently change the committed atlas."""
    import hashlib
    from .jpfont import _fetch
    p = BRAND_FONT
    if not p.exists():
        if not download:
            return None, f"font missing at {p} (download disabled)"
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(_fetch(BRAND_URL))
            (p.parent / "OFL-Montserrat.txt").write_bytes(_fetch(BRAND_OFL_URL))
        except Exception as e:  # network, HTTP or filesystem: reported, never raised
            return None, f"download failed: {type(e).__name__}: {e}"
    if hashlib.sha256(p.read_bytes()).hexdigest() != BRAND_SHA256:
        return None, f"{p} is not the pinned Montserrat (sha256 differs)"
    return p, "ok"


def icon_wordmark(path: Path, cap: int, wght: int):
    """"AETHER" at `cap` px cap height, tracked 0.27 x cap, rendered 8x and box-filtered (coverage, 16 levels)."""
    S = 8
    font = ImageFont.truetype(str(path), 100)
    font.set_variation_by_axes([wght])
    bb = font.getbbox("H")
    size = round(100 * cap * S / (bb[3] - bb[1]))
    font = ImageFont.truetype(str(path), size)
    font.set_variation_by_axes([wght])
    track = 0.27 * cap * S
    adv = [font.getlength(c) for c in "AETHER"]
    W = int(sum(adv) + track * 5 + 4 * S)
    H = (cap + 2) * S
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    x = 1.0 * S
    top = font.getbbox("H")[1]
    for c, a in zip("AETHER", adv):
        d.text((x, S - top), c, font=font, fill=255)
        x += a + track
    x1 = img.getbbox()[2]
    w = -(-x1 // S) + 1
    img = img.crop((0, 0, w * S, H)).resize((w, cap + 2), Image.BOX)
    return w, cap + 2, [_q(v) for v in img.tobytes()]

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "menu" / "src" / "uifont_data.c"
MAX_BYTES = 40_000   # gate: the rodata of uifont_data.c (also checked by sbuild on the compiled object)

FACES = (                       # (C name, px, weight) -- mockup roles, task-13a-brief.md deliverable 1
    ("item", 11, 400),
    ("sel", 11, 600),
    ("small", 9, 400),
    ("title", 12, 600),
    ("brand", 13, 600),
    ("label", 9, 600),          # the loading screen's "NOW LOADING" (letter-spaced) and its footer wordmark
    ("hero", 17, 600),          # the loading screen's game title (mockup 17 px SemiBold)
    ("label8", 8, 600),         # the start-up interstitial's "LOADING YOUR LIBRARY" (letter-spaced)
)   # brand2 (13 Regular) and btn (8 SemiBold) were unused -- the badges are pre-rendered icons
# a face whose font ascent + descent would exceed the 20-row raster (menu/ui/uitext.c RAS_MAX_H) keeps only
# its ink's extent: hero's Noto metrics are 20 + 5 rows, its ink 14 + 5.
TIGHT = {"hero"}
# a face used for one fixed upper-case label keeps only those glyphs' bitmaps (every other glyph keeps its
# metrics and advance but draws nothing), to stay inside the task's HWRAM budget.
# plus "v0.1" (and every digit) for the startup screen's version tag, menu/ui/vertag.c.
SUBSET = {"label8": set(" ABCDEFGHIJKLMNOPQRSTUVWXYZ") | set("v.0123456789")}
MIN_SPACE_ADV8 = 24     # Noto's space is 0.224 em, 2 px at 9-11 px; words ran together. At least 3 px.
EXTRA = ("›", "·", "…")
CHARSET = [chr(c) for c in range(0x20, 0x7F)] + list(EXTRA)

BADGES = (("A", 11), ("B", 11), ("X", 11), ("Y", 11), ("Z", 11), ("ST", 17),   # mockup: wpx, box = wpx + 1 wide
          ("C", 11), ("L", 11), ("R", 11))   # the remappable buttons (Controls page)
BADGE_H = 11


def _face(path: Path, px: int, wght: int):
    f = ImageFont.truetype(str(path), px)
    f.set_variation_by_axes([wght])
    return f


def _pack(cov: list[int], w: int, h: int) -> bytes:
    """cov: row-major 0..15 values -> 4 bpp, high nibble = left pixel, stride (w + 1) // 2."""
    out = bytearray()
    for y in range(h):
        row = cov[y * w:(y + 1) * w] + [0]
        for x in range(0, w, 2):
            out.append((row[x] << 4) | row[x + 1])
    return bytes(out)


def _q(v: int) -> int:
    return v >> 4                      # 8-bit coverage -> 16 levels, as the mockup's (v // 16)


def render_glyph(font, ch: str):
    """-> (w, h, xoff, yoff, adv8, packed bytes). Ink box relative to the pen at the baseline (anchor 'ls')."""
    adv8 = round(font.getlength(ch) * 8)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    if x1 <= x0 or y1 <= y0:
        return 0, 0, 0, 0, adv8, b""
    pad = 4
    w, h = x1 - x0, y1 - y0
    img = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
    ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
    bb = img.getbbox()
    if not bb:
        return 0, 0, 0, 0, adv8, b""
    img = img.crop(bb)
    cov = [_q(v) for v in img.tobytes()]
    w, h = img.size
    # trim rows/columns that quantised to 0
    rows = [y for y in range(h) if any(cov[y * w:(y + 1) * w])]
    cols = [x for x in range(w) if any(cov[y * w + x] for y in range(h))]
    if not rows:
        return 0, 0, 0, 0, adv8, b""
    ty0, ty1, tx0, tx1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
    cov = [cov[y * w + x] for y in range(ty0, ty1) for x in range(tx0, tx1)]
    xoff = bb[0] - pad + x0 + tx0
    yoff = bb[1] - pad + y0 + ty0
    w, h = tx1 - tx0, ty1 - ty0
    return w, h, xoff, yoff, adv8, _pack(cov, w, h)


def build_face(path: Path, px: int, wght: int, tight: bool = False, subset: set | None = None) -> dict:
    font = _face(path, px, wght)
    asc, desc = font.getmetrics()
    glyphs, bits = [], bytearray()
    for ch in CHARSET:
        w, h, xo, yo, adv8, data = render_glyph(font, ch)
        if subset is not None and ch not in subset:
            w, h, xo, yo, data = 0, 0, 0, 0, b""             # metrics-only: advances, no bitmap
        if ch == " ":
            adv8 = max(adv8, MIN_SPACE_ADV8)
        assert 0 <= adv8 < 256 and -128 <= xo < 128 and -128 <= yo < 128 and w < 256 and h < 256
        glyphs.append((w, h, xo, yo, adv8, len(bits)))
        bits += data
    assert len(bits) < 65536
    if tight:
        asc = max(-yo for w, h, xo, yo, adv8, off in glyphs if w)
        desc = max(h + yo for w, h, xo, yo, adv8, off in glyphs if w)
    # Pillow's 'm' (middle) anchor: halfway between ascender and descender -> baseline = middle + mid
    return {"asc": asc, "desc": desc, "mid": round((asc - desc) / 2), "glyphs": glyphs, "bits": bytes(bits)}


def _supersample(draw_fn, w: int, h: int, s: int = 4) -> list[int]:
    big = Image.new("L", (w * s, h * s), 0)
    draw_fn(ImageDraw.Draw(big), s)
    return [_q(v) for v in big.resize((w, h), Image.BOX).tobytes()]


def icon_folder():
    """Mockup: rectangle (16,y+4)-(24,y+10) and tab (16,y+3)-(19,y+4), inclusive -> 9 x 8 from (16, y+3)."""
    w, h = 9, 8
    cov = [0] * (w * h)
    for y in range(1, 8):
        for x in range(9):
            cov[y * w + x] = 15
    for x in range(4):
        cov[x] = 15
    return w, h, cov


def icon_star():
    """Mockup: 10-point polygon, radii 3.6 / 1.5, centred on (156, y+7) -> an 8 x 8 cell centred on (4, 4)."""
    w = h = 8
    cx = cy = 4.0

    def draw(d, s):
        pts = [((cx + (3.6 if k % 2 == 0 else 1.5) * math.sin(k * math.pi / 5)) * s,
                (cy - (3.6 if k % 2 == 0 else 1.5) * math.cos(k * math.pi / 5)) * s) for k in range(10)]
        d.polygon(pts, fill=255)
    return w, h, _supersample(draw, w, h)


def icon_badge(path: Path, label: str, wpx: int):
    """Mockup: rounded_rectangle((x,207,x+wpx,217), radius 5), letter at its centre in the btn face, anchor 'mm'."""
    w, h = wpx + 1, BADGE_H
    fill = _supersample(lambda d, s: d.rounded_rectangle((0, 0, (wpx + 1) * s - 1, h * s - 1), radius=5 * s,
                                                           fill=255), w, h)
    font = _face(path, 8, 600)
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((wpx / 2 + 0.5, 5), label, font=font, fill=255, anchor="mm")
    let = [_q(v) for v in img.tobytes()]
    cov = []
    for f, lt in zip(fill, let):
        if lt and f:
            cov.append(8 + round(lt * 7 / 15))          # 8 = fill .. 15 = letter
        elif f:
            cov.append(max(1, round(f * 8 / 15)))       # 1 .. 8 = background -> fill
        else:
            cov.append(0)
    return w, h, cov


# --- region flags (controller addendum 6) ------------------------------------------------------------------------------
# 12x8 flags (1 px dark outline) in the right 12 columns of a 16x8 4 bpp sprite, each with its own 16-entry palette:
# index 0 transparent, 1..15 the flag's colours (quantised after an 8x supersampled draw). Order = IP.BIN's area-code
# order (J T U B K A E L, ST-040-R4); T, A and L have no single country: a neutral globe, tinted per area (no letter --
# a 6 px letter is not legible; recorded in the Task 13a report).
FLAGS = "JTUBKAEL"
FLAG_W, FLAG_H, FLAG_TEX_W = 12, 8, 16
OUTLINE = (16, 20, 30)


def _flag_rgb(code: str) -> Image.Image:
    S = 8
    W, H = FLAG_W * S, FLAG_H * S
    img = Image.new("RGB", (W, H), OUTLINE)
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = S, S, W - S - 1, H - S - 1               # inside the 1 px outline
    iw, ih = x1 - x0 + 1, y1 - y0 + 1
    cx, cy = x0 + iw / 2, y0 + ih / 2

    def disc(r, fill):
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)

    if code == "J":
        d.rectangle((x0, y0, x1, y1), fill=(245, 245, 245))
        disc(ih * 0.30, (200, 16, 46))
    elif code == "U":
        for i in range(7):                                       # 7 stripes over 6 inner px, red first
            ya = y0 + ih * i / 7
            d.rectangle((x0, ya, x1, y0 + ih * (i + 1) / 7), fill=(191, 10, 48) if i % 2 == 0 else (245, 245, 245))
        d.rectangle((x0, y0, x0 + iw * 0.45, y0 + ih * 4 / 7), fill=(10, 49, 97))
    elif code == "E":
        d.rectangle((x0, y0, x1, y1), fill=(0, 51, 153))
        for k in range(12):
            a = k * math.pi / 6
            px, py = cx + ih * 0.32 * math.cos(a), cy + ih * 0.32 * math.sin(a)
            d.ellipse((px - S * 0.45, py - S * 0.45, px + S * 0.45, py + S * 0.45), fill=(255, 204, 0))
    elif code == "B":
        d.rectangle((x0, y0, x1, y1), fill=(0, 156, 59))
        d.polygon([(cx, y0 + S * 0.6), (x1 - S * 0.6, cy), (cx, y1 - S * 0.6), (x0 + S * 0.6, cy)], fill=(255, 223, 0))
        disc(ih * 0.22, (0, 39, 118))
    elif code == "K":
        d.rectangle((x0, y0, x1, y1), fill=(245, 245, 245))
        r = ih * 0.30
        d.pieslice((cx - r, cy - r, cx + r, cy + r), 180, 360, fill=(205, 46, 58))
        d.pieslice((cx - r, cy - r, cx + r, cy + r), 0, 180, fill=(0, 71, 160))
    else:                                                        # T, A, L: a neutral globe, tinted per area
        bg, land = {"T": ((72, 92, 120), (150, 176, 205)), "A": ((96, 80, 120), (176, 160, 205)),
                    "L": ((72, 104, 92), (150, 196, 176))}[code]
        d.rectangle((x0, y0, x1, y1), fill=bg)
        r = ih * 0.42
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=land, width=S)
        d.line((cx, cy - r, cx, cy + r), fill=land, width=S // 2)
        d.line((cx - r, cy, cx + r, cy), fill=land, width=S // 2)
    return img.resize((FLAG_W, FLAG_H), Image.BOX)


def flag(code: str):
    """-> (packed 16x8 4 bpp bits, 15 RGB palette entries) with the flag in columns 4..15."""
    q = _flag_rgb(code).quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:45]
    pal += [0] * (45 - len(pal))
    idx = list(q.tobytes())
    cov = []
    for y in range(FLAG_H):
        cov += [0] * (FLAG_TEX_W - FLAG_W) + [idx[y * FLAG_W + x] + 1 for x in range(FLAG_W)]
    return _pack(cov, FLAG_TEX_W, FLAG_H), [tuple(pal[3 * i:3 * i + 3]) for i in range(15)]


def _rgb555(c) -> int:
    return 0x8000 | ((c[2] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[0] >> 3)


def build(path: Path, brand: Path | None = None) -> tuple[str, int]:
    if brand is None:
        brand, why = ensure_brand_font(download=False)
        if brand is None:
            raise FileNotFoundError(f"Montserrat for the wordmark: {why}")
    faces = [(name, build_face(path, px, wght, name in TIGHT, SUBSET.get(name))) for name, px, wght in FACES]
    icons = [("folder", *icon_folder()), ("star", *icon_star())]
    icons += [(f"badge_{lab.lower()}", *icon_badge(path, lab, wpx)) for lab, wpx in BADGES]
    icons += [(name, *icon_wordmark(brand, cap, wght)) for name, cap, wght in WORDMARKS]
    nbytes = 0
    L = ["/* menu/src/uifont_data.c -- GENERATED by tools/omsat/uifont.py from Noto Sans JP (Google, SIL OFL 1.1; see",
         " * CREDITS.md). Do not edit: run `cd tools && python -m omsat.uifont`. tools/tests/test_uifont.py checks it is in",
         " * step with the generator. 4 bpp coverage, high nibble = left pixel (HWF-05). */",
         '#include "uifont.h"', ""]
    for name, f in faces:
        L.append(f"static const uint8_t k_bits_{name}[{max(1, len(f['bits']))}] = {{")
        L += _hex_rows(f["bits"] or b"\0")
        L.append("};")
        L.append(f"static const om_glyph k_gl_{name}[OM_UF_NGLYPH] = {{")
        for i, (w, h, xo, yo, adv8, off) in enumerate(f["glyphs"]):
            L.append(f"    {{ {w}, {h}, {xo}, {yo}, {adv8}, 0, {off} }},   /* {_cname(CHARSET[i])} */")
        L.append("};")
        L.append(f"const om_face om_face_{name} = {{ {f['asc']}, {f['desc']}, {f['mid']}, 0, k_gl_{name}, "
                 f"k_bits_{name} }};")
        L.append("")
        nbytes += len(f["bits"]) + 8 * len(CHARSET) + 12
    for name, w, h, cov in icons:
        data = _pack(cov, w, h)
        L.append(f"static const uint8_t k_icon_{name}[{len(data)}] = {{")
        L += _hex_rows(data)
        L.append("};")
        L.append(f"const om_icon om_icon_{name} = {{ {w}, {h}, k_icon_{name} }};")
        L.append("")
        nbytes += len(data) + 8
    for code in FLAGS:
        bits, pal = flag(code)
        L.append(f"static const uint8_t k_flag_{code.lower()}[{len(bits)}] = {{")
        L += _hex_rows(bits)
        L.append("};")
    L.append("const om_flag om_flags[OM_NFLAGS] = {")
    for code in FLAGS:
        _bits, pal = flag(code)
        words = ", ".join(f"0x{_rgb555(c):04x}" for c in pal)
        L.append(f"    {{ '{code}', k_flag_{code.lower()}, {{ {words} }} }},")
    L.append("};")
    L.append("")
    nbytes += len(FLAGS) * (64 + 32 + 4)
    L.append(f"const uint32_t om_uifont_bytes = {nbytes}u;   /* atlas + metrics + icons, gated <= {MAX_BYTES} */")
    return "\n".join(L) + "\n", nbytes


def _cname(ch: str) -> str:
    o = ord(ch)
    if ch in "*/\\" or o > 0x7E:
        return f"U+{o:04X}"
    return f"'{ch}'"


def _hex_rows(data: bytes) -> list[str]:
    return ["    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]


def main() -> int:
    path, why = ensure_font()
    if not path:
        print(f"uifont: Noto Sans JP unavailable ({why})")
        return 1
    brand, why = ensure_brand_font()
    if not brand:
        print(f"uifont: Montserrat unavailable ({why})")
        return 1
    text, n = build(path, brand)
    if n > MAX_BYTES:
        print(f"uifont: {n} B > {MAX_BYTES} B gate")
        return 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({n} B of atlas, metrics and icons)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


# --- metrics of the committed atlas, for host-side "fits its box" gates (tools/tests/test_msg_width.py) -------------
def load_metrics(path: Path = OUT) -> dict[str, list[tuple[int, int, int, int, int]]]:
    """Parse the committed uifont_data.c -> {face: [(w, h, xoff, yoff, adv8), ...]} in CHARSET order."""
    import re
    faces: dict[str, list] = {}
    cur = None
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"static const om_glyph k_gl_(\w+)\[", line)
        if m:
            cur = faces.setdefault(m.group(1), [])
            continue
        m = re.match(r"\s*\{ (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), 0, \d+ \},", line)
        if cur is not None and m:
            cur.append(tuple(int(v) for v in m.groups()))
        elif line.startswith("};"):
            cur = None
    return faces


def measure(glyphs, s: str) -> int:
    """Width in px exactly as menu/src/uifont.c extent(): pen in 1/8 px, ink extent of each glyph."""
    pen8 = ink = 0
    for ch in s:
        i = CHARSET.index(ch) if ch in CHARSET else CHARSET.index("?")
        w, _h, xo, _yo, adv8 = glyphs[i]
        x = ((pen8 + 4) >> 3) + xo + w
        if w and x > ink:
            ink = x
        pen8 += adv8
    return max(ink, (pen8 + 7) >> 3)
