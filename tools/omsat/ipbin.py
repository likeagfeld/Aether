# tools/omsat/ipbin.py
"""Build our own IP.BIN System ID (ST-040-R4 §4.2). The IP is linked from this 256-byte SYSID plus SEGA's
SYS_SEC / SYS_ARE{J,T,U,E} / SYS_INIT objects (ST-040-R4 Appendix smpsys.lnk), used unmodified (§4.4, §4.5)."""
from __future__ import annotations

import struct

from .iphdr import make_system_id


def build_sysid(*, maker: str, product: str, version: str, date: str, device: str, area: str,
                peripherals: str, title: str, ip_size: int, first_read: int = 0x06004000) -> bytes:
    if area != "JTUE":
        raise ValueError("area must be JTUE: all four SEGA area-code objects are linked (ST-040-R4 §4.5)")
    sid = bytearray(make_system_id(maker=maker, product=product, version=version, release_date=date,
                                   device=device, area=area, peripherals=peripherals, title=title))
    struct.pack_into(">IIII", sid, 0xE0, ip_size, 0, 0, 0)        # IP size, reserved, Stack-M 0, Stack-S 0 (defaults)
    struct.pack_into(">IIII", sid, 0xF0, first_read, 0, 0, 0)     # 1st Read address; size ignored for CDs (§4.2)
    return bytes(sid)


def set_ip_size(ip: bytes) -> bytes:
    size = max(0x1000, (len(ip) + 3) // 4 * 4)
    if size > 0x8000:
        raise ValueError("IP larger than 8000H (ST-040-R4 §4.2 IP SIZE range)")
    out = bytearray(ip.ljust(size, b"\x00"))
    struct.pack_into(">I", out, 0xE0, size)
    return bytes(out)
