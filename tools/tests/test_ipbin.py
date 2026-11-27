import struct

import pytest

from omsat import iphdr
from omsat.ipbin import build_sysid, set_ip_size


def _sid():
    return build_sysid(maker="SEGA TP OMSAT", product="OMSAT-S0", version="V0.001", date="20261117",
                       device="CD-1/1", area="JTUE", peripherals="JA", title="VIBEMENU S0", ip_size=0x1000)


def test_sysid_fields_and_binary_tail():
    sid = _sid()
    s = iphdr.parse_system_id(sid)
    assert (s.maker, s.product, s.area, s.peripherals) == ("SEGA TP OMSAT", "OMSAT-S0", "JTUE", "JA")
    assert sid[0xD0:0xE0] == bytes(16)
    assert struct.unpack(">I", sid[0xE0:0xE4])[0] == 0x1000
    assert struct.unpack(">I", sid[0xF0:0xF4])[0] == 0x06004000


def test_area_must_be_jtue():
    with pytest.raises(ValueError, match="JTUE"):
        build_sysid(maker="X", product="X", version="V0.001", date="20261117", device="CD-1/1", area="U",
                    peripherals="J", title="X", ip_size=0x1000)


def test_set_ip_size_pads_and_writes():
    ip = _sid() + b"\x11" * 0xC00                        # 0xD00 total, below the 0x1000 minimum
    out = set_ip_size(ip)
    assert len(out) == 0x1000 and struct.unpack(">I", out[0xE0:0xE4])[0] == 0x1000
    big = _sid() + b"\x22" * 0x1F02                      # 0x2002 -> padded to 0x2004
    out2 = set_ip_size(big)
    assert len(out2) == 0x2004 and struct.unpack(">I", out2[0xE0:0xE4])[0] == 0x2004
    with pytest.raises(ValueError, match="8000"):
        set_ip_size(_sid() + bytes(0x8000))
