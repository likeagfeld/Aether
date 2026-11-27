"""LAUNCHER.BIN 4 KB header (menu/src/handoff.h). Big-endian. Stdlib only, because
menu/host-tests/gen_cases.py imports it inside omsat-build, which has no Pillow.
Layout: 0x00 magic "OMSATLNC", 0x08 u32 version 1, 0x0C u32 load address 0x00200000, 0x10 u32 body size,
0x14 u32 CRC-32 of body, 0x18 u32 CRC-32 of bytes 0x00-0x17, zero padding to 0x1000."""
from __future__ import annotations

import struct
import zlib

MAGIC = b"OMSATLNC"
VERSION = 1
LOAD = 0x00200000
HDR = 0x1000
MAX_BODY = 0x0007F000


def make_header(body: bytes) -> bytes:
    if not 0 < len(body) <= MAX_BODY:
        raise ValueError(f"launcher body size {len(body)} outside 1..{MAX_BODY}")
    h = MAGIC + struct.pack(">IIII", VERSION, LOAD, len(body), zlib.crc32(body) & 0xFFFFFFFF)
    h += struct.pack(">I", zlib.crc32(h) & 0xFFFFFFFF)
    return h.ljust(HDR, b"\x00")


def parse_header(hdr: bytes, file_size: int) -> dict:
    if len(hdr) < HDR or hdr[:8] != MAGIC:
        raise ValueError("magic")
    if struct.unpack(">I", hdr[24:28])[0] != zlib.crc32(hdr[:24]) & 0xFFFFFFFF:
        raise ValueError("header CRC")
    version, load, size, crc = struct.unpack(">IIII", hdr[8:24])
    if version != VERSION or load != LOAD:
        raise ValueError("version/load")
    if not 0 < size <= MAX_BODY or file_size != HDR + size:
        raise ValueError("size")
    return {"body_size": size, "body_crc": crc}
