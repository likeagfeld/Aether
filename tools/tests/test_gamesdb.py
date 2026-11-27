import pytest
from omsat import binfmt as bf
from omsat.gamesdb import (GameRecord, ascii_fold, periph_flags_from, read_gamesdb,
                           region_flags_from, write_gamesdb)

RECS = [GameRecord(key="T-8113H", title_en="Alien Trilogy", genre=1, region_flags=region_flags_from("U"),
                   players=1, peripherals=periph_flags_from("J"), year=1996, date=19960101),
        GameRecord(key="GS-9001", title_en="Daytona USA", title_rom="Daytona USA", maker="SEGA",
                   genre=0, region_flags=region_flags_from("JTUE"), players=1)]
GENRES = ["Racing", "First Person Shooter"]


def test_roundtrip_sorted_by_key():
    data = write_gamesdb(RECS, GENRES, {"610-6342": "GS-9001"}, 20261117)
    recs, genres, aliases = read_gamesdb(data)
    assert [r.key for r in recs] == ["GS-9001", "T-8113H"]
    assert recs[0].title_en == "Daytona USA" and recs[0].maker == "SEGA"
    assert recs[1].year == 1996 and recs[1].genre == 1
    assert genres == GENRES and aliases == {"610-6342": "GS-9001"}


def test_layout_record_is_64_bytes_and_big_endian():
    data = write_gamesdb(RECS, GENRES, {}, 20261117)
    h = bf.validate_common(data, bf.MAGIC_GDB)
    assert h.count == 2 and h.table_size == 128
    rec0 = data[h.table_off:h.table_off + 64]
    assert rec0[0:16] == bf.key_bytes("GS-9001")
    assert rec0[0x11] == 0                                    # genre id
    assert rec0[0x12] == 0b1111                               # J T U E
    assert data[h.data_off] == 0                              # pool byte 0 is NUL


def test_flags():
    assert region_flags_from("JTUE") == 0b1111
    assert region_flags_from("E") == 0b1000
    assert periph_flags_from("JAM") == 0b111


def test_ascii_fold():
    assert ascii_fold("Pokémon") == "Pokemon"
    assert ascii_fold("森高") == "??"


def test_rejects_duplicate_keys():
    with pytest.raises(ValueError, match="duplicate"):
        write_gamesdb([RECS[0], RECS[0]], GENRES, {}, 20261117)


def test_compat_fields_round_trip():
    """SPEC §5.3 amendment, bytes 0x2E-0x3F: loader hint, compat flags, note and region mask survive
    a write/read round trip; a record without them reads back all-zero/empty."""
    recs = [GameRecord(key="T-8113H", title_en="Alien Trilogy", loader_hint=1, compat_flags=0x03,
                       compat_note="Use a standard pad", compat_regions=0x04),
            GameRecord(key="GS-9001", title_en="Daytona USA")]
    data = write_gamesdb(recs, [], {}, 20261117)
    got = {r.key: r for r in read_gamesdb(data)[0]}
    assert got["T-8113H"].loader_hint == 1 and got["T-8113H"].compat_flags == 0x03
    assert got["T-8113H"].compat_note == "Use a standard pad" and got["T-8113H"].compat_regions == 0x04
    assert got["GS-9001"].loader_hint == 0 and got["GS-9001"].compat_flags == 0
    assert got["GS-9001"].compat_note == "" and got["GS-9001"].compat_regions == 0


def test_user_title_flag_round_trip():
    """2026-11-23 (SPEC §5.3 byte 0x35 bit0): a record whose title_en is the user's own USEREDIT.INI title carries
    the flag through a write/read round trip; every other record, and every older file, reads 0."""
    recs = [GameRecord(key="T-8113H", title_en="My Alien", user_flags=0x01), GameRecord(key="GS-9001", title_en="D")]
    data = write_gamesdb(recs, [], {}, 20261123)
    h = bf.validate_common(data, bf.MAGIC_GDB)
    assert data[h.table_off + 64 + 0x35] == 0x01 and data[h.table_off + 0x35] == 0   # sorted: GS-9001 first
    got = {r.key: r for r in read_gamesdb(data)[0]}
    assert got["T-8113H"].user_flags == 0x01 and got["GS-9001"].user_flags == 0
