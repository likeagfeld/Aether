"""Theme tools (contract/SPEC.md §5.5 THEME.INI, §5.7 BG.BIN). PNG/JPG -> BG.BIN (320x224, 8 bpp, 256-color RGB555
palette for VDP2 CRAM mode 0, HWF-30), the THEME.INI writer, and a reader. Aspect is preserved with black bars (spec
§8.3: never stretch). Writers emit palette indices 1..255 only; index 0 is reserved.
`python -m omsat.theme IMAGE --out DIR --name NAME [--author A] [--text RRGGBB] [--text-hi RRGGBB] [--panel RRGGBB]
 [--cursor RRGGBB] [--panel-alpha 0-100]`, or `--demo` instead of IMAGE; copy DIR to /aether/THEMES/NAME/."""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageOps

from . import binfmt as bf

MAGIC = b"OMSATBGI"
W, H = 320, 224
PAL_OFF, PAL_SIZE = 64, 512
PIX_OFF, PIX_SIZE = PAL_OFF + PAL_SIZE, W * H
FILE_SIZE = PIX_OFF + PIX_SIZE
_HEX = re.compile(r"^#?[0-9A-Fa-f]{6}$")


def rgb555(r: int, g: int, b: int) -> int:
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def encode_bg(img: Image.Image, build_date: int) -> bytes:
    fit = ImageOps.contain(img.convert("RGB"), (W, H), Image.LANCZOS)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(fit, ((W - fit.width) // 2, (H - fit.height) // 2))
    q = canvas.quantize(colors=255, method=0, dither=0)          # median cut, no dither: deterministic
    flat = (q.getpalette() or [])[:255 * 3]
    flat += [0] * (255 * 3 - len(flat))
    palette = [0] + [rgb555(*flat[3 * i:3 * i + 3]) for i in range(255)]
    pixels = bytes(i + 1 for i in q.tobytes())
    table = b"".join(struct.pack(">H", c) for c in palette) + pixels
    h = bf.Header(magic=MAGIC, count=1, table_off=PAL_OFF, table_size=len(table), data_off=0, data_size=0,
                  build_date=build_date, table_crc=bf.crc32(table),
                  sections=((PAL_OFF, PAL_SIZE), (PIX_OFF, PIX_SIZE), (0, 0)))
    return bf.pack_header(h) + table


def decode_bg(data: bytes) -> tuple[list[int], bytes]:
    h = bf.validate_common(data, MAGIC)
    if len(data) != FILE_SIZE:
        raise bf.FormatError(f"BG.BIN is {len(data)} B, expected {FILE_SIZE}")
    if (h.count, h.table_off, h.table_size) != (1, PAL_OFF, PAL_SIZE + PIX_SIZE):
        raise bf.FormatError("BG.BIN header count/table fields wrong")
    pal = list(struct.unpack_from(">256H", data, PAL_OFF))
    return pal, data[PIX_OFF:PIX_OFF + PIX_SIZE]


def check_bg(data: bytes) -> list[str]:
    pal, pix = decode_bg(data)
    errs = []
    if any(c & 0x8000 for c in pal):
        errs.append("palette entry with bit15 set (VDP2 CRAM mode 0 is 15-bit, HWF-30)")
    if 0 in pix:
        errs.append("pixel uses palette index 0 (reserved; writers use 1-255)")
    return errs


def _hex(v: str) -> str:
    if not _HEX.match(v):
        raise ValueError(f"color {v!r} is not RRGGBB hex")
    return v.lstrip("#").upper()


def theme_ini(name: str, author: str = "", text: str = "E8EEF8", text_hi: str = "78C8FF", panel: str = "080C1A",
              cursor: str = "409CFF", panel_alpha: int = 55, bg: str = "BG.BIN") -> str:
    if not 0 <= int(panel_alpha) <= 100:
        raise ValueError("panel_alpha is an opacity percentage, 0-100")
    return (f"name={name}\nauthor={author}\ntext={_hex(text)}\ntext_hi={_hex(text_hi)}\npanel={_hex(panel)}\n"
            f"panel_alpha={int(panel_alpha)}\ncursor={_hex(cursor)}\nbg={bg}\n")


def demo_image() -> Image.Image:
    img = Image.new("RGB", (W, H))
    img.putdata([((x * 255) // (W - 1), 64 + (y * 128) // (H - 1), 255 - (x * 255) // (W - 1)) if (x // 20 + y // 20) % 2
                 else (20, 30, 60) for y in range(H) for x in range(W)])
    return img


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?")
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--author", default="")
    for k in ("text", "text-hi", "panel", "cursor"):
        ap.add_argument(f"--{k}")
    ap.add_argument("--panel-alpha", type=int, default=70)
    ap.add_argument("--date", type=int, default=20261118)
    a = ap.parse_args(argv)
    if not a.demo and not a.image:
        ap.error("give IMAGE or --demo")
    img = demo_image() if a.demo else Image.open(a.image)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cols = {k.replace("-", "_"): getattr(a, k.replace("-", "_")) for k in ("text", "text-hi", "panel", "cursor")}
    (out / "BG.BIN").write_bytes(encode_bg(img, a.date))
    (out / "THEME.INI").write_text(theme_ini(a.name, a.author, panel_alpha=a.panel_alpha,
                                             **{k: v for k, v in cols.items() if v}),
                                    encoding="utf-8", newline="\n")
    print(f"theme {a.name}: {out / 'THEME.INI'}, {out / 'BG.BIN'} ({FILE_SIZE} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
