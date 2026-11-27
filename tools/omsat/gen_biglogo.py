"""Generate menu/app/assets/BIGLOGO.BIN: the start-up interstitial's large logo,
152 x 101 -- the Aether mark since the Aether rename (2026-11-25; formerly the Sega Saturn logo). The header logo (gen_logo.py, 40x25) is too small to scale
up to ~150 px, and 152 x 101 RGB555 would cost 30,704 B of HWRAM, so the image is stored indexed and compressed with the
boot animation's codec 2 (one frame against the all-zero reference; tools/omsat/bootanim.py encode_frame, decoded on
the Saturn by menu/src/bootanim.c om_ba_ops). The menu expands it to RGB555 in VDP1 VRAM.

Source: assets/logo/aether_big_master.png (1216 x 810), assets/logo/aether_mark.svg rendered 8x with its glow (a blur
of 1.8 output px at full strength in #A8D4FF: the user asked for a glow that reads on a TV; the pipeline keeps it as four
edge levels, alpha 17..68, with no fringe violation). The 152 x 101 render is finished by logoclean.finalize (soft-edge pixels carry the interior's
color, none lighter than the interior next to it) and logoclean.palettize_big: median cut over the opaque pixels, plus
up to 24 (color, alpha) pairs for the soft edge, n <= 127 entries; an edge pixel never takes a lighter entry.

Layout v2 (big-endian):
    u16 width, u16 height, u16 colors (n), u16 ops length
    n x u16 palette: RGB555 with bit 15 set (HWF-01), for indices 1..n
    n x u8 alpha, for indices 1..n (255 = opaque; between: blended with the backdrop at upload by menu/src/logoblend.c),
        plus one zero byte if n is odd
    the codec-2 ops (ops length bytes), then zero padding to a multiple of 4
Index 0 is transparent: the RGB-mode transparent code 0x0000 (SPdis clear, HWF-02).

indexed / v1_pixels keep the pre-Task 7 rule (1-bit, alpha >= 128, 128 median-cut colors, the original
saturn_logo.png) for the review sheet's "old" row and the gate's own test.

docs/ledger.md, "M6 C2": until M6 the table below was 6,004 B of the menu's HWRAM .rodata. It is now the file
BIGLOGO.BIN on the menu disc (ISO root, like BOOTANIM.BIN and SOUND.BIN), with the 64-byte common header (contract/SPEC.md
§5.1, magic OMSATBLG, entry count 1; §5.19), read at boot into LWRAM (OM_LW_BIGLOGO) before API mode. The table is the
layout below, byte for byte what was embedded before; the build date is fixed so the file is reproducible.

The omsat-build image has no Pillow, so the output is committed and tools/tests/test_biglogo.py checks it is in step
with the PNG. `python -m omsat.gen_biglogo` writes it."""
from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image

from . import binfmt as bf, logoclean
from .bootanim import decode_frame, encode_frame

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "assets" / "logo" / "aether_big_master.png"     # the Aether rename, 2026-11-25
ORIGINAL = ROOT / "assets" / "logo" / "saturn_logo.png"     # the pre-Task 7 rule (indexed, v1_pixels) reads this
OUT = ROOT / "menu" / "app" / "assets" / "BIGLOGO.BIN"
MAGIC = b"OMSATBLG"
DATE = 20261121             # fixed: the committed file must match make() whatever the day
W, H = 152, 101             # W a multiple of 8 (HWF-06); 152:101 is the source's 217:144 within half a pixel
COLORS = 128
HEAD = struct.Struct(">HHHH")


def _rgb555(r: int, g: int, b: int) -> int:
    return 0x8000 | (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3)


def indexed(src: Path = ORIGINAL) -> tuple[bytes, list[int]]:
    """The pre-Task 7 rule (1-bit, alpha >= 128): -> (W*H indices, 0 = transparent; palette for 1..n as RGB555 words)."""
    with Image.open(src) as img:
        im = img.convert("RGBA").resize((W, H), Image.LANCZOS)
    alpha = im.getchannel("A").tobytes()
    rgb = im.convert("RGB").point(lambda v: (v >> 3) << 3)
    q = rgb.quantize(COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:3 * COLORS]
    idx = q.tobytes()
    used = sorted({i for i, a in zip(idx, alpha) if a >= 128})
    remap = {old: new + 1 for new, old in enumerate(used)}
    pix = bytes(remap[i] if a >= 128 else 0 for i, a in zip(idx, alpha))
    words = [_rgb555(pal[3 * o], pal[3 * o + 1], pal[3 * o + 2]) for o in used]
    return pix, words


def body(src: Path = SRC) -> bytes:
    """rendered from the cleaned master and finished by logoclean (finalize, then palettize_big: opaque
    entries by median cut plus up to 24 (color, alpha) edge pairs, n <= COLORS - 1)."""
    with Image.open(src) as img:
        pix, pal, alpha, _shown = logoclean.palettize_big(logoclean.finalize(logoclean.render_size(img, (W, H))), W)
    n = len(pal)
    assert 0 < n <= COLORS - 1
    ops = encode_frame(bytes(W * H), pix, W)
    assert decode_frame(bytes(W * H), ops) == pix
    out = (HEAD.pack(W, H, n, len(ops)) + struct.pack(f">{n}H", *(c | 0x8000 for c in pal)) + bytes(alpha)
           + b"\0" * (n & 1) + ops)
    return out + b"\0" * (-len(out) % 4)


def wrap(table: bytes) -> bytes:
    """BIGLOGO.BIN = the §5.1 header (entry count 1, table = the logo, no data, no sections) + the table."""
    h = bf.Header(magic=MAGIC, count=1, table_off=bf.HEADER_SIZE, table_size=len(table), data_off=0, data_size=0,
                  build_date=DATE, table_crc=bf.crc32(table), sections=((0, 0),) * 3)
    return bf.pack_header(h) + table


def make(src: Path = SRC) -> bytes:
    return wrap(body(src))


def check_biglogo(data: bytes) -> list[str]:
    """omsat.check: the header (validate_common) and what menu/src/biglogo.c refuses: the size, n, and the ops inside
    the table; the ops must decode to a full frame."""
    try:
        h = bf.validate_common(data, MAGIC)
        table = data[h.table_off:h.table_off + h.table_size]
        w, hh, n, olen = HEAD.unpack_from(table)
    except (bf.FormatError, struct.error) as e:
        return [str(e)]
    if h.count != 1:
        return [f"entry count {h.count}; BIGLOGO.BIN holds one logo"]
    if (w, hh) != (W, H):
        return [f"logo is {w} x {hh}; the menu draws {W} x {H}"]
    at = HEAD.size + 3 * n + (n & 1)
    if not 0 < n < 256 or at + olen > len(table):
        return [f"colors {n} / ops length {olen} do not fit the {len(table)} B table"]
    try:
        _decode(table)
    except Exception as e:                       # noqa: BLE001 -- any decode failure is a format error here
        return [f"ops: {e}"]
    return []


def _decode(data: bytes) -> tuple[bytes, tuple[int, ...], bytes]:
    """v2 -> (indices, palette words, alpha bytes)."""
    w, h, n, olen = HEAD.unpack_from(data)
    pal = struct.unpack_from(f">{n}H", data, HEAD.size)
    alpha = data[HEAD.size + 2 * n:HEAD.size + 3 * n]
    at = HEAD.size + 3 * n + (n & 1)
    return decode_frame(bytes(w * h), data[at:at + olen]), pal, alpha


def expand(data: bytes) -> list[int]:
    """The palette colors per pixel (0 = transparent), alpha not applied: what a test compares with the PNG."""
    pix, pal, _ = _decode(data)
    return [pal[p - 1] if p else 0 for p in pix]


def decode(data: bytes) -> list[tuple[int, int]]:
    """v2 -> (RGB555 without bit 15, alpha 0..255) per pixel."""
    pix, pal, alpha = _decode(data)
    return [(pal[p - 1] & 0x7FFF, alpha[p - 1]) if p else (0, 0) for p in pix]


def shown_pixels(data: bytes, back: int = 0) -> list[int]:
    """What the menu uploads over a flat backdrop (RGB555) by the blend rule (menu/src/logoblend.c): alpha 255 -> the
    color | 0x8000, 0 -> 0x0000, else the color blended with the backdrop | 0x8000."""
    return [0 if not a else (c | 0x8000 if a == 255 else logoclean.mix555(c, back, a) | 0x8000)
            for c, a in decode(data)]


def v1_pixels(src: Path = ORIGINAL) -> list[int]:
    """v1's pixels: indexed() with the palette expanded (0 = transparent), what the menu showed before v2."""
    pix, words = indexed(src)
    return [words[p - 1] if p else 0 for p in pix]


if __name__ == "__main__":
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(make())
    print(f"wrote {OUT} ({OUT.stat().st_size} B)")
