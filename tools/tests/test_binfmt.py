import pytest
from omsat import binfmt as bf


def _hdr(**kw):
    base = dict(magic=bf.MAGIC_GDB, count=2, table_off=64, table_size=128, data_off=192,
                data_size=10, build_date=20261117, table_crc=0, sections=((0, 0), (0, 0), (0, 0)))
    base.update(kw)
    return bf.Header(**base)


def test_header_is_64_bytes_big_endian():
    raw = bf.pack_header(_hdr())
    assert len(raw) == 64
    assert raw[0:8] == b"OMSATGDB"
    assert raw[8:10] == b"\x00\x01"          # version 1, big-endian
    assert raw[10:12] == b"\x00\x40"         # header size 64
    assert raw[12:16] == b"\x00\x00\x00\x02"  # count


def test_header_roundtrip():
    h = _hdr(sections=((1, 2), (3, 4), (5, 6)))
    assert bf.unpack_header(bf.pack_header(h)) == h


def test_align():
    assert bf.align(0) == 0
    assert bf.align(1) == 2048
    assert bf.align(2048) == 2048
    assert bf.align(12800) == 14336


def test_key_bytes_pads_and_rejects():
    assert bf.key_bytes("GS-9001") == b"GS-9001" + b"\x00" * 9
    assert bf.key_str(bf.key_bytes("GS-9001")) == "GS-9001"
    with pytest.raises(ValueError):
        bf.key_bytes("X" * 17)
    with pytest.raises(ValueError):
        bf.key_bytes("Ä")


def test_validate_common_accepts_good_and_rejects_bad():
    table = b"\x11" * 128
    h = _hdr(table_crc=bf.crc32(table))
    data = bf.pack_header(h) + table + b"\x00" * 10
    assert bf.validate_common(data, bf.MAGIC_GDB).count == 2
    with pytest.raises(bf.FormatError, match="magic"):
        bf.validate_common(data, bf.MAGIC_ART)
    bad_crc = bytearray(data); bad_crc[64] ^= 1
    with pytest.raises(bf.FormatError, match="CRC"):
        bf.validate_common(bytes(bad_crc), bf.MAGIC_GDB)
    with pytest.raises(bf.FormatError, match="range"):
        bf.validate_common(data[:100], bf.MAGIC_GDB)
    bad_ver = bytearray(data); bad_ver[9] = 2
    with pytest.raises(bf.FormatError, match="version"):
        bf.validate_common(bytes(bad_ver), bf.MAGIC_GDB)
