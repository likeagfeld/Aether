"""Common 64-byte header shared by ART.PAK, GAMES.DB and LIBRARY.IDX (contract/SPEC.md §5.1). Big-endian."""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

MAGIC_ART = b"OMSATART"
MAGIC_GDB = b"OMSATGDB"
MAGIC_LIB = b"OMSATLIB"
HEADER_SIZE = 64
VERSION = 1
SECTOR = 2048

_HDR = struct.Struct(">8sHH7I6I")
assert _HDR.size == HEADER_SIZE


class FormatError(Exception):
    """A file violates contract/SPEC.md."""


@dataclass(frozen=True)
class Header:
    magic: bytes
    count: int
    table_off: int
    table_size: int
    data_off: int
    data_size: int
    build_date: int
    table_crc: int
    sections: tuple
    version: int = VERSION
    header_size: int = HEADER_SIZE


def crc32(b: bytes) -> int:
    return zlib.crc32(b) & 0xFFFFFFFF


def pack_header(h: Header) -> bytes:
    flat = [v for pair in h.sections for v in pair]
    return _HDR.pack(h.magic, h.version, h.header_size, h.count, h.table_off, h.table_size,
                     h.data_off, h.data_size, h.build_date, h.table_crc, *flat)


def unpack_header(b: bytes) -> Header:
    if len(b) < HEADER_SIZE:
        raise FormatError("range: file shorter than header")
    f = _HDR.unpack_from(b, 0)
    sections = ((f[10], f[11]), (f[12], f[13]), (f[14], f[15]))
    return Header(magic=f[0], version=f[1], header_size=f[2], count=f[3], table_off=f[4],
                  table_size=f[5], data_off=f[6], data_size=f[7], build_date=f[8],
                  table_crc=f[9], sections=sections)


def align(n: int, a: int = SECTOR) -> int:
    return (n + a - 1) // a * a


def key_bytes(key: str) -> bytes:
    raw = key.encode("ascii")
    if len(raw) > 16:
        raise ValueError(f"key longer than 16 bytes: {key!r}")
    return raw.ljust(16, b"\x00")


def key_str(b: bytes) -> str:
    return b.rstrip(b"\x00").decode("ascii")


def _in_range(off: int, size: int, total: int) -> bool:
    return 0 <= off and 0 <= size and off + size <= total


def validate_common(data: bytes, magic: bytes) -> Header:
    h = unpack_header(data)
    if h.magic != magic:
        raise FormatError(f"magic: expected {magic!r}, got {h.magic!r}")
    if h.version != VERSION:
        raise FormatError(f"version: unsupported {h.version}")
    if h.header_size != HEADER_SIZE:
        raise FormatError(f"header size: {h.header_size}")
    if not _in_range(h.table_off, h.table_size, len(data)):
        raise FormatError("range: table beyond end of file")
    if not _in_range(h.data_off, h.data_size, len(data)):
        raise FormatError("range: data beyond end of file")
    for i, (off, size) in enumerate(h.sections):
        if size and not _in_range(off, size, len(data)):
            raise FormatError(f"range: section {i} beyond end of file")
    if crc32(data[h.table_off:h.table_off + h.table_size]) != h.table_crc:
        raise FormatError("CRC: table CRC mismatch")
    return h
