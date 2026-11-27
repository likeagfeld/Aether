"""ART.PAK writer/reader and cover/title encoders (contract/SPEC.md §5.2).
VDP1 limits per VDP1_Manual.txt:2572-2580 (width 8-504 in 8-px units, height 1-255).
RGB555 bit order (bit15 opaque, BGR 5:5:5) is unverified until checked against ST-013-R3 (spec §12)."""
from __future__ import annotations

import struct
from dataclasses import dataclass

from PIL import Image

from . import binfmt as bf

THUMB = {"square": (80, 80), "tall": (64, 100)}
HERO = {"square": (160, 160), "tall": (128, 200)}
_ENT = struct.Struct(">16sBBBBBBHB3sIIIII")
assert _ENT.size == 48
F_THUMB, F_HERO, F_JP, F_SQUARE = 1, 2, 4, 8


def rgb555(r: int, g: int, b: int) -> int:
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def fnv1a32(data: bytes) -> int:
    """FNV-1a-32 (spec §5.2 Folder art): offset basis 0x811C9DC5, prime 0x01000193."""
    h = 0x811C9DC5
    for byte in data:
        h ^= byte
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def folder_art_key(path: str) -> str:
    """Folder-art ART.PAK key for `path` (spec §5.2 Folder art): "F" + 8 uppercase hex
    digits of fnv1a32(path), where path is `/`-separated UTF-8 with no leading/trailing `/`."""
    return "F%08X" % fnv1a32(path.encode("utf-8"))


def layout_for(img: Image.Image) -> str:
    w, h = img.size
    return "square" if w / h >= 0.85 else "tall"


def encode_cover(img: Image.Image, size: tuple[int, int]) -> bytes:
    tw, th = size
    src = img.convert("RGBA")
    scale = min(tw / src.width, th / src.height)
    nw, nh = max(1, round(src.width * scale)), max(1, round(src.height * scale))
    fitted = src.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(fitted, ((tw - nw) // 2, (th - nh) // 2))
    out = bytearray()
    for r, g, b, a in canvas.getdata():
        out += struct.pack(">H", rgb555(r, g, b) if a >= 128 else 0)
    return bytes(out)


@dataclass
class ArtEntry:
    key: str
    disc: int
    thumb: Image.Image | None
    hero: Image.Image | None
    jp_title: tuple | None
    manifest_row: int


def write_artpak(entries, build_date: int) -> bytes:
    ents = sorted(entries, key=lambda e: (bf.key_bytes(e.key), e.disc))
    table_off = bf.HEADER_SIZE
    table_size = 48 * len(ents)
    data_off = bf.align(table_off + table_size)
    blobs = bytearray()
    table = bytearray()

    def put(b: bytes) -> int:
        sec = len(blobs) // bf.SECTOR
        blobs.extend(b)
        blobs.extend(b"\x00" * (bf.align(len(b)) - len(b)))
        return sec

    for e in ents:
        flags, crc_src = 0, b""
        lay = layout_for(e.thumb or e.hero) if (e.thumb or e.hero) else "square"
        tw, th = THUMB[lay] if e.thumb else (0, 0)
        hw, hh = HERO[lay] if e.hero else (0, 0)
        ts = hs = js = 0
        jw, jh = 0, 0
        if e.thumb:
            b = encode_cover(e.thumb, (tw, th)); ts = put(b); crc_src += b; flags |= F_THUMB
        if e.hero:
            b = encode_cover(e.hero, (hw, hh)); hs = put(b); crc_src += b; flags |= F_HERO
        if e.jp_title:
            jw, jh, b = e.jp_title; js = put(b); crc_src += b; flags |= F_JP
        if lay == "square" and (e.thumb or e.hero):
            flags |= F_SQUARE
        table += _ENT.pack(bf.key_bytes(e.key), e.disc, flags, tw, th, hw, hh, jw, jh, b"\x00" * 3,
                           ts, hs, js, e.manifest_row, bf.crc32(crc_src))
    h = bf.Header(magic=bf.MAGIC_ART, count=len(ents), table_off=table_off, table_size=table_size,
                  data_off=data_off, data_size=len(blobs), build_date=build_date,
                  table_crc=bf.crc32(bytes(table)), sections=((0, 0), (0, 0), (0, 0)))
    head = bf.pack_header(h) + bytes(table)
    return head + b"\x00" * (data_off - len(head)) + bytes(blobs)


_FIELDS = ("key", "disc", "flags", "thumb_w", "thumb_h", "hero_w", "hero_h", "jp_w", "jp_h", "_r",
           "thumb_sec", "hero_sec", "jp_sec", "manifest_row", "blob_crc")


def read_artpak(data: bytes) -> list[dict]:
    h = bf.validate_common(data, bf.MAGIC_ART)
    if h.table_size != 48 * h.count:
        raise bf.FormatError("table size != 48 * count")
    out = []
    for i in range(h.count):
        d = dict(zip(_FIELDS, _ENT.unpack_from(data, h.table_off + 48 * i)))
        d["key"] = bf.key_str(d["key"])
        d.pop("_r")
        d["_data_off"] = h.data_off
        out.append(d)
    return out


def get_blob(data: bytes, e: dict, kind: str) -> bytes:
    if kind == "thumb":
        sec, n = e["thumb_sec"], e["thumb_w"] * e["thumb_h"] * 2
    elif kind == "hero":
        sec, n = e["hero_sec"], e["hero_w"] * e["hero_h"] * 2
    else:
        sec, n = e["jp_sec"], e["jp_w"] // 2 * e["jp_h"]
    start = e["_data_off"] + sec * bf.SECTOR
    return data[start:start + n]
