import struct
import zlib

import pytest

from omsat.lnchhdr import HDR, LOAD, make_header, parse_header


def test_round_trip():
    body = bytes(range(100))
    h = make_header(body)
    assert len(h) == HDR == 0x1000
    assert h[:8] == b"OMSATLNC"
    assert struct.unpack(">IIII", h[8:24]) == (1, LOAD, 100, zlib.crc32(body))
    assert struct.unpack(">I", h[24:28])[0] == zlib.crc32(h[:24])
    assert parse_header(h, HDR + 100)["body_size"] == 100


def test_rejects_wrong_size_and_crc():
    h = bytearray(make_header(b"abc"))
    with pytest.raises(ValueError):
        parse_header(bytes(h), HDR + 4)
    h[0x13] ^= 1
    with pytest.raises(ValueError):
        parse_header(bytes(h), HDR + 3)
    with pytest.raises(ValueError):
        make_header(b"")
