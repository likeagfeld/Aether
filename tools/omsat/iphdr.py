"""Saturn System ID (IP.BIN first 256 bytes) reader. Field layout per ST-040-R4 §4.2;
raw-sector offsets per contract/SPEC.md v1 clarification 4."""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from .binfmt import FormatError, crc32

HW_ID = b"SEGA SEGASATURN "
_SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
# (offset, length) per ST-040-R4 §4.2
_F = {"maker": (0x10, 16), "product": (0x20, 10), "version": (0x2A, 6), "release_date": (0x30, 8),
      "device": (0x38, 8), "area": (0x40, 10), "peripherals": (0x50, 16), "title": (0x60, 112)}


@dataclass(frozen=True)
class SystemId:
    maker: str
    product: str
    version: str
    release_date: str
    device: str
    area: str
    peripherals: str
    title: str
    raw: bytes


def _txt(b: bytes) -> str:
    return b.decode("ascii", errors="replace").rstrip(" \x00")


def parse_system_id(b: bytes) -> SystemId:
    if len(b) < 256 or b[:16] != HW_ID:
        raise FormatError("not a Saturn System ID (hardware identifier mismatch)")
    vals = {name: _txt(b[o:o + n]) for name, (o, n) in _F.items()}
    return SystemId(raw=bytes(b[:256]), **vals)


def make_system_id(**fields: str) -> bytes:
    buf = bytearray(b" " * 0xD0 + b"\x00" * 0x30)
    buf[0:16] = HW_ID
    for name, (o, n) in _F.items():
        v = fields.get(name, "").encode("ascii")
        if len(v) > n:
            raise ValueError(f"{name} longer than {n}")
        buf[o:o + n] = v.ljust(n, b" ")
    return bytes(buf)


# PSKai v6.545 ip_header.c:39-69 ip_header_get_crc: its Save Data Manager (title CRC, BackupMngr/app_title.h) is given
# a fixed value; every other IP is a CRC-32 (sc_common.c crc32_update, the zlib one) over bytes 0x10-0x3F then
# 0x50-0xFF, skipping the hardware id and the area field, so a region-patched disc keeps its CRC
KAI_SDM_TITLE_CRC, KAI_SDM_IP_CRC = 0x157E3779, 0xCAFE0100


def kai_ip_crc(raw: bytes) -> int:
    """The IP CRC Pseudo Saturn Kai matches its compat and cheat tables by (`ip_crc` in loader_compat.json)."""
    if crc32(raw[0x60:0x78]) == KAI_SDM_TITLE_CRC:
        return KAI_SDM_IP_CRC
    return crc32(raw[0x10:0x40] + raw[0x50:0x100])


def disc_n_m(device: str) -> tuple[int, int]:
    m = re.search(r"CD-(\d+)/(\d+)", device.upper())
    return (int(m.group(1)), int(m.group(2))) if m else (1, 1)


def _from_image(data: bytes) -> SystemId:
    if data[:12] == _SYNC:
        mode = data[15]
        off = 16 if mode == 1 else 24
        return parse_system_id(data[off:off + 256])
    return parse_system_id(data[:256])


def _first_track_file(cue: Path) -> Path:
    for line in cue.read_text(encoding="latin-1").splitlines():
        m = re.match(r'\s*FILE\s+"([^"]+)"', line, re.I) or re.match(r"\s*FILE\s+(\S+)", line, re.I)
        if m:
            return cue.parent / m.group(1)
    raise FormatError(f"no FILE line in {cue}")


def read_system_id(path) -> SystemId:
    p = Path(path)
    if p.suffix.lower() == ".cue":
        p = _first_track_file(p)
    with p.open("rb") as fh:
        return _from_image(fh.read(2352))
