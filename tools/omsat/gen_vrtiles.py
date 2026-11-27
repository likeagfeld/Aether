"""Generate menu/app/assets/VRTILES.BIN (/aether/VRTILES.BIN on the card): built-in art tiles for the five virtual root folders (Favorites, Recently
Played, A-Z, Genre, Region), so the cover panel never shows NO ART for them (final M3 fix round, user request).

Each tile is 160 x 160, the square hero size (menu/src/omlayout.h OM_HERO_PIXELS; shown 100 x 100 like folder art).
The tiles are drawn in *theme roles*, not fixed colors: every palette entry is (role, level), and the menu maps it to
RGB555 at upload time as mix(BASE, role color, level) with the current theme's colors (menu/src/vrtile.c), so a tile
follows the theme accent:
    role 1 accent (theme cursor), 2 highlight (theme text_hi), 3 gold (fixed), 4 white (fixed); index 0 = BASE.
Pixels are anti-aliased (4x supersampled, box-filtered); a pixel keeps the role of its topmost layer and its level
is that layer's coverage times its level (the edge blends toward BASE, which the tile's faint glow sits close to).
Levels are quantized to 16 steps. Indices are stored with the boot animation's codec 2 against an all-zero frame
(tools/omsat/bootanim.py encode_frame; menu/src/bootanim.c om_ba_ops), like biglogo.bin.

M6 C3 (docs/ledger.md, M6 memory plan): the tiles were 13,876 B of the menu's HWRAM .rodata; they are now a card file
with the 64-byte common header (contract/SPEC.md §5.1, magic OMSATVRT; §5.18), which the menu reads with GAMES.DB into
LWRAM (OM_LW_VRTILES) and validates (magic, version, CRC-32 of the table, entry count = tile count). The table is the
tile block below, byte for byte what was embedded before; the build date is fixed so the file is reproducible.

Table layout (big-endian):
    u16 width, u16 height, u16 tile count, u16 0
    per tile: u16 colors (n), u16 ops length, n x u16 palette (role << 8 | level) for indices 1..n, the ops,
              zero padding to a multiple of 4
The "A-Z" lettering uses Noto Sans JP (SIL OFL 1.1, build/fonts/, credited in CREDITS.md), as the UI font does.
The omsat-build image has no Pillow, so the output is committed and tools/tests/test_vrtiles.py checks it is in step.
`python -m omsat.gen_vrtiles` writes it."""
from __future__ import annotations

import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from . import binfmt as bf
from .bootanim import decode_frame, encode_frame
from .jpfont import ensure_font

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "menu" / "app" / "assets" / "VRTILES.BIN"
MAGIC = b"OMSATVRT"
DATE = 20261120             # fixed: the committed file must match make() whatever the day
W = H = 160
SS = 4                      # supersampling
ACCENT, HI, GOLD, WHITE = 1, 2, 3, 4
NAMES = ("favorites", "recent", "az", "genre", "region")   # browse.h OM_VR_* order
HEAD = struct.Struct(">HHHH")
THEAD = struct.Struct(">HH")
LEVELS = 16


class Tile:
    def __init__(self) -> None:
        self.role = [ACCENT] * (W * H)
        self.level = [0.0] * (W * H)
        # faint accent glow toward the centre, and a thin accent frame inset 6 px
        for y in range(H):
            for x in range(W):
                d = math.hypot(x - 79.5, y - 79.5) / 113.0
                self.level[y * W + x] = max(0.0, 0.14 * (1.0 - d))
        self.layer(ACCENT, 0.55, lambda d: d.rounded_rectangle(
            (6 * SS, 6 * SS, (W - 7) * SS, (H - 7) * SS), radius=10 * SS, outline=255, width=2 * SS))

    def layer(self, role: int, level: float, draw) -> None:
        im = Image.new("L", (W * SS, H * SS), 0)
        draw(ImageDraw.Draw(im))
        cov = im.resize((W, H), Image.BOX).tobytes()
        for i, c in enumerate(cov):
            if c:
                a = c / 255.0
                if self.role[i] == role:
                    self.level[i] = self.level[i] * (1.0 - a) + level * a
                else:
                    self.role[i] = role
                    self.level[i] = level * a        # the edge blends toward BASE

    def indexed(self) -> tuple[bytes, list[int]]:
        keys = []
        for r, lv in zip(self.role, self.level):
            q = min(LEVELS - 1, int(lv * (LEVELS - 1) + 0.5))
            keys.append((r, q * 255 // (LEVELS - 1)) if q else None)
        used = sorted({k for k in keys if k})
        idx = {k: n + 1 for n, k in enumerate(used)}
        return bytes(idx[k] if k else 0 for k in keys), [r << 8 | lv for r, lv in used]


def _s(v: float) -> float:
    return v * SS


def star(t: Tile) -> None:
    cx, cy, ro, ri = 80, 84, 54, 22
    pts = []
    for k in range(10):
        r = ro if k % 2 == 0 else ri
        a = -math.pi / 2 + k * math.pi / 5
        pts.append((_s(cx + r * math.cos(a)), _s(cy + r * math.sin(a))))
    t.layer(GOLD, 1.0, lambda d: d.polygon(pts, fill=255))
    sh = [pts[0], pts[1], pts[2], pts[3], pts[4], (_s(cx), _s(cy))]   # the right-hand facets a shade deeper
    t.layer(GOLD, 0.8, lambda d: d.polygon(sh, fill=255))


def clock(t: Tile) -> None:
    cx, cy, r = 80, 80, 50
    t.layer(HI, 0.9, lambda d: d.ellipse((_s(cx - r), _s(cy - r), _s(cx + r), _s(cy + r)), outline=255, width=7 * SS))
    for k in range(12):
        a = k * math.pi / 6
        r0, r1 = (r - 16, r - 9) if k % 3 == 0 else (r - 13, r - 9)
        p0 = (_s(cx + r0 * math.sin(a)), _s(cy - r0 * math.cos(a)))
        p1 = (_s(cx + r1 * math.sin(a)), _s(cy - r1 * math.cos(a)))
        t.layer(WHITE, 0.7, lambda d, p0=p0, p1=p1: d.line((p0, p1), fill=255, width=3 * SS))
    t.layer(ACCENT, 1.0, lambda d: d.line(((_s(cx), _s(cy)), (_s(cx - 22), _s(cy - 13))), fill=255, width=7 * SS))
    t.layer(WHITE, 0.95, lambda d: d.line(((_s(cx), _s(cy)), (_s(cx + 4), _s(cy - 36))), fill=255, width=5 * SS))
    t.layer(GOLD, 1.0, lambda d: d.ellipse((_s(cx - 6), _s(cy - 6), _s(cx + 6), _s(cy + 6)), fill=255))


def az(t: Tile) -> None:
    path, why = ensure_font()
    if path is None:
        raise SystemExit(f"gen_vrtiles: {why}")
    font = ImageFont.truetype(str(path), 58 * SS)
    font.set_variation_by_axes([800])
    text = "A–Z"
    def draw(d):
        x0, y0, x1, y1 = d.textbbox((0, 0), text, font=font)
        d.text(((W * SS - (x1 - x0)) / 2 - x0, _s(74) - (y1 - y0) / 2 - y0), text, font=font, fill=255)
    t.layer(WHITE, 0.95, draw)
    t.layer(ACCENT, 1.0, lambda d: d.rounded_rectangle((_s(42), _s(112), _s(118), _s(119)), radius=_s(3), fill=255))


def genre(t: Tile) -> None:
    c = [(52, 52), (108, 52), (52, 108), (108, 108)]
    r = 20
    t.layer(ACCENT, 1.0, lambda d: d.ellipse((_s(c[0][0] - r), _s(c[0][1] - r), _s(c[0][0] + r), _s(c[0][1] + r)),
                                             fill=255))
    tri = [(_s(c[1][0]), _s(c[1][1] - r - 1)), (_s(c[1][0] + r + 2), _s(c[1][1] + r - 2)),
           (_s(c[1][0] - r - 2), _s(c[1][1] + r - 2))]
    t.layer(GOLD, 1.0, lambda d: d.polygon(tri, fill=255))
    t.layer(HI, 1.0, lambda d: d.rounded_rectangle((_s(c[2][0] - r + 2), _s(c[2][1] - r + 2), _s(c[2][0] + r - 2),
                                                    _s(c[2][1] + r - 2)), radius=_s(4), fill=255))
    dia = [(_s(c[3][0]), _s(c[3][1] - r)), (_s(c[3][0] + r), _s(c[3][1])), (_s(c[3][0]), _s(c[3][1] + r)),
           (_s(c[3][0] - r), _s(c[3][1]))]
    t.layer(WHITE, 0.9, lambda d: d.polygon(dia, fill=255))


def globe(t: Tile) -> None:
    cx, cy, r = 80, 80, 50
    box = (_s(cx - r), _s(cy - r), _s(cx + r), _s(cy + r))
    t.layer(ACCENT, 0.45, lambda d: d.ellipse(box, fill=255))
    for rx in (17, 36):
        t.layer(WHITE, 0.7, lambda d, rx=rx: d.ellipse((_s(cx - rx), _s(cy - r), _s(cx + rx), _s(cy + r)),
                                                       outline=255, width=3 * SS))
    for dy in (-25, 0, 25):
        hw = math.sqrt(r * r - dy * dy) - 2
        t.layer(WHITE, 0.7, lambda d, dy=dy, hw=hw: d.line(((_s(cx - hw), _s(cy + dy)), (_s(cx + hw), _s(cy + dy))),
                                                           fill=255, width=3 * SS))
    t.layer(WHITE, 0.95, lambda d: d.ellipse(box, outline=255, width=5 * SS))


DRAW = (star, clock, az, genre, globe)


def tiles() -> list[tuple[bytes, list[int]]]:
    out = []
    for fn in DRAW:
        t = Tile()
        fn(t)
        out.append(t.indexed())
    return out


def body() -> bytes:
    """The tile block (the file's table): what menu/src/vrtile.c om_vrtile_decode reads."""
    out = HEAD.pack(W, H, len(DRAW), 0)
    for pix, pal in tiles():
        assert 0 < len(pal) < 256
        ops = encode_frame(bytes(W * H), pix, W)
        assert decode_frame(bytes(W * H), ops) == pix
        blk = THEAD.pack(len(pal), len(ops)) + struct.pack(f">{len(pal)}H", *pal) + ops
        out += blk + b"\0" * (-len(blk) % 4)
    return out


def wrap(table: bytes) -> bytes:
    """The card file: the §5.1 header (entry count = tiles, table = the tile block, no data, no sections) + the table."""
    h = bf.Header(magic=MAGIC, count=HEAD.unpack_from(table)[2], table_off=bf.HEADER_SIZE, table_size=len(table),
                  data_off=0, data_size=0, build_date=DATE, table_crc=bf.crc32(table), sections=((0, 0),) * 3)
    return bf.pack_header(h) + table


def check_vrtiles(data: bytes) -> list[str]:
    """omsat.check: the header (validate_common), the tile size and count, and every tile decodes."""
    try:
        h = bf.validate_common(data, MAGIC)
        table = data[h.table_off:h.table_off + h.table_size]
        w, hh, n, _ = HEAD.unpack_from(table)
    except (bf.FormatError, struct.error) as e:
        return [str(e)]
    if (w, hh) != (W, H):
        return [f"tiles are {w} x {hh}; the menu draws {W} x {H}"]
    if n != h.count:
        return [f"entry count {h.count} != {n} tiles in the table"]
    for i in range(n):
        try:
            expand(table, i)
        except Exception as e:                   # noqa: BLE001 -- any decode failure is a format error here
            return [f"tile {i}: {e}"]
    return []


def make() -> bytes:
    return wrap(body())


def expand(data: bytes, i: int) -> list[tuple[int, int]]:
    """Tile i as (role, level) per pixel ((0, 0) = BASE): what the menu maps through the theme."""
    w, h, n, _ = HEAD.unpack_from(data)
    off = HEAD.size
    for k in range(n):
        nc, olen = THEAD.unpack_from(data, off)
        pal = struct.unpack_from(f">{nc}H", data, off + THEAD.size)
        ops = data[off + THEAD.size + 2 * nc:off + THEAD.size + 2 * nc + olen]
        if k == i:
            pix = decode_frame(bytes(w * h), ops)
            return [(pal[p - 1] >> 8, pal[p - 1] & 0xFF) if p else (0, 0) for p in pix]
        blk = THEAD.size + 2 * nc + olen
        off += blk + (-blk % 4)
    raise IndexError(i)


def preview(data: bytes, path: Path) -> None:
    """A PNG of the five tiles in the aether theme's colors (review aid only)."""
    roles = {0: (0x20, 0x20, 0x30), ACCENT: (0x3C, 0x8C, 0xF0), HI: (0x9C, 0xD2, 0xFF), GOLD: (0xF5, 0xC5, 0x42),
             WHITE: (0xF0, 0xF0, 0xF5)}
    img = Image.new("RGB", (W * 5 + 40, H + 8), (10, 10, 16))
    for i in range(5):
        for k, (r, lv) in enumerate(expand(data, i)):
            b, c = roles[0], roles[r] if r else roles[0]
            px = tuple(b[j] + (c[j] - b[j]) * lv // 255 for j in range(3))
            img.putpixel((4 + i * (W + 8) + k % W, 4 + k // W), px)
    img.save(path)


if __name__ == "__main__":
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(make())
    print(f"wrote {OUT} ({OUT.stat().st_size} B)")
