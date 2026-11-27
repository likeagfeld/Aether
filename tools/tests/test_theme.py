"""contract/SPEC.md §5.7 BG.BIN and §5.5 THEME.INI: converter, reader, checker (with its mutation demo)."""
import struct

import pytest
from PIL import Image

from omsat import theme
from omsat.check import check_bytes


def _halves() -> Image.Image:
    img = Image.new("RGB", (320, 224), (255, 0, 0))
    img.paste((0, 0, 255), (160, 0, 320, 224))
    return img


def test_encode_layout_and_colours():
    data = theme.encode_bg(_halves(), 20261118)
    assert len(data) == theme.FILE_SIZE == 72256
    pal, pix = theme.decode_bg(data)
    assert len(pal) == 256 and len(pix) == 320 * 224
    assert pal[0] == 0 and min(pix) >= 1                         # index 0 is never emitted
    assert pal[pix[0]] == 0x001F                                  # red: R in bits 4-0 (HWF-30)
    assert pal[pix[319]] == 0x7C00                                # blue: B in bits 14-10
    assert all(c & 0x8000 == 0 for c in pal)
    assert check_bytes(data) == []


def test_letterbox_never_stretches():
    pal, pix = theme.decode_bg(theme.encode_bg(Image.new("RGB", (100, 100), (255, 255, 255)), 1))
    assert pal[pix[0]] == 0                                       # top-left corner is the black bar
    assert pal[pix[112 * 320 + 160]] == 0x7FFF                    # centre is the image


def test_checker_mutation_demo():
    good = theme.encode_bg(_halves(), 20261118)
    crc = bytearray(good); crc[64 + 512 + 5] ^= 1
    assert check_bytes(bytes(crc)), "pixel flip (CRC) not caught"
    short = good[:-1]
    assert check_bytes(short), "truncation not caught"
    zero = bytearray(good); zero[64 + 512] = 0
    zero[0x24:0x28] = struct.pack(">I", theme.bf.crc32(bytes(zero[64:])))
    assert any("index 0" in e for e in check_bytes(bytes(zero))), "index 0 not caught"
    b15 = bytearray(good); b15[64 + 2] |= 0x80
    b15[0x24:0x28] = struct.pack(">I", theme.bf.crc32(bytes(b15[64:])))
    assert any("bit15" in e for e in check_bytes(bytes(b15))), "palette bit15 not caught"


def test_theme_ini_round_trip():
    text = theme.theme_ini("Neon", "Me", text="FFFFFF", panel="000080", panel_alpha=40)
    kv = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    assert kv["name"] == "Neon" and kv["panel"] == "000080" and kv["panel_alpha"] == "40" and kv["bg"] == "BG.BIN"
    with pytest.raises(ValueError):
        theme.theme_ini("x", panel="red")
    with pytest.raises(ValueError):
        theme.theme_ini("x", panel_alpha=101)
