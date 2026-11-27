import pytest
from omsat import iphdr
from omsat.binfmt import FormatError

SID = iphdr.make_system_id(maker="SEGA ENTERPRISES", product="GS-9001", version="V1.000",
                           release_date="19941122", device="CD-1/2", area="JTUE",
                           peripherals="JA", title="TEST GAME")


def test_make_and_parse_roundtrip():
    s = iphdr.parse_system_id(SID)
    assert len(SID) == 256
    assert s.product == "GS-9001" and s.version == "V1.000" and s.area == "JTUE"
    assert s.device == "CD-1/2" and s.title == "TEST GAME" and s.peripherals == "JA"
    assert iphdr.disc_n_m(s.device) == (1, 2)


def test_field_offsets_per_st040():
    assert SID[0x00:0x10] == b"SEGA SEGASATURN "
    assert SID[0x20:0x2A] == b"GS-9001   "
    assert SID[0x2A:0x30] == b"V1.000"
    assert SID[0x38:0x40] == b"CD-1/2  "
    assert SID[0x60:0x69] == b"TEST GAME"


def test_disc_n_m_defaults():
    assert iphdr.disc_n_m("") == (1, 1)
    assert iphdr.disc_n_m("CD-2/3") == (2, 3)


def test_bad_hardware_id():
    with pytest.raises(FormatError):
        iphdr.parse_system_id(b"\x00" * 256)


def _raw_sector(mode: int, payload: bytes) -> bytes:
    sync = b"\x00" + b"\xff" * 10 + b"\x00"
    hdr = b"\x00\x02\x00" + bytes([mode])
    sub = b"\x00" * 8 if mode == 2 else b""
    body = sub + payload
    return (sync + hdr + body).ljust(2352, b"\x00")


def test_read_iso_bin_mode1_mode2_and_cue(tmp_path):
    (tmp_path / "g.iso").write_bytes(SID + b"\x00" * 1792)
    (tmp_path / "m1.bin").write_bytes(_raw_sector(1, SID))
    (tmp_path / "m2.bin").write_bytes(_raw_sector(2, SID))
    (tmp_path / "g.cue").write_text('FILE "m2.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    for name in ("g.iso", "m1.bin", "m2.bin", "g.cue"):
        assert iphdr.read_system_id(tmp_path / name).product == "GS-9001"
