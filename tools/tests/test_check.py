import struct
from PIL import Image
from omsat import artpak as ap, binfmt as bf
from omsat.check import check_bytes
from omsat.gamesdb import GameRecord, write_gamesdb
from omsat.libidx import Folder, LibEntry, write_libidx


def _good():
    img = Image.new("RGB", (64, 100), (1, 2, 3))
    return {
        "art": ap.write_artpak([ap.ArtEntry("A-1", 0, img, img, None, 0), ap.ArtEntry("B-2", 0, img, None, None, 1)], 20261117),
        "gdb": write_gamesdb([GameRecord("A-1", "A"), GameRecord("B-2", "B", genre=0)], ["G"], {"Z-9": "A-1"}, 20261117),
        "lib": write_libidx([LibEntry("a.cue", "A-1"), LibEntry("b.iso", "B-2", image_type=2)], [Folder("", 0xFFFF)], 7, 20261117),
    }


def _recrc(data: bytearray) -> bytes:
    h = bf.unpack_header(bytes(data))
    crc = bf.crc32(bytes(data[h.table_off:h.table_off + h.table_size]))
    struct.pack_into(">I", data, 0x24, crc)
    return bytes(data)


def test_good_files_pass():
    for name, data in _good().items():
        assert check_bytes(data) == [], name


def test_mutations_are_caught():
    """Mutation demo: every corruption class must turn the checker RED. If any assertion here fails, the checker is broken."""
    for name, data in _good().items():
        h = bf.unpack_header(data)
        width = {"art": 48, "gdb": 64, "lib": 32}[name]
        flipped = bytearray(data); flipped[h.table_off + 3] ^= 0x40
        assert check_bytes(bytes(flipped)), f"{name}: CRC flip not caught"
        swapped = bytearray(data)
        a = swapped[h.table_off:h.table_off + width]
        b = swapped[h.table_off + width:h.table_off + 2 * width]
        swapped[h.table_off:h.table_off + 2 * width] = b + a
        if name != "lib":                                   # LIBRARY.IDX is folder-ordered, not key-sorted
            assert check_bytes(_recrc(swapped)), f"{name}: unsorted table not caught"
        assert check_bytes(data[:len(data) - 1] if name != "lib" else data[:h.table_off + 10]), f"{name}: truncation not caught"
        bad_magic = b"XXXXXXXX" + data[8:]
        assert check_bytes(bad_magic), f"{name}: magic not caught"


def test_art_bad_width_caught():
    data = bytearray(_good()["art"])
    h = bf.unpack_header(bytes(data))
    data[h.table_off + 0x12] = 81                          # thumb width not a multiple of 8
    assert any("multiple of 8" in m for m in check_bytes(_recrc(data)))


def test_art_bad_height_caught():
    """ART.PAK VDP1 height limit: entry 0x15 (hero height) is a u8, so the only reachable
    violation of the spec's 1-255 range is 0 (255 is the field's own max)."""
    data = bytearray(_good()["art"])
    h = bf.unpack_header(bytes(data))
    data[h.table_off + 0x15] = 0                           # hero height 0, below the VDP1 1-255 limit
    assert any("VDP1 limits" in m for m in check_bytes(_recrc(data)))


def test_art_folder_art_rules_caught():
    """Folder-art key rule (spec §5.2 Folder art): a key matching ^F[0-9A-F]{8}$ must have
    disc == 0, the hero bit set, and neither the thumb nor the JP-title bit."""
    img = Image.new("RGB", (160, 160), (4, 5, 6))
    key = ap.folder_art_key("RPG")
    good = ap.write_artpak([ap.ArtEntry(key, 0, None, img, None, 0)], 20261117)
    assert check_bytes(good) == []                         # must not false-positive on a compliant entry
    h = bf.unpack_header(good)

    bad_disc = bytearray(good)
    bad_disc[h.table_off + 0x10] = 1                        # disc 1, not 0
    assert any("disc == 0" in m for m in check_bytes(_recrc(bad_disc)))

    bad_hero = bytearray(good)
    bad_hero[h.table_off + 0x11] &= ~ap.F_HERO              # clear the hero bit
    assert any("must carry the hero bit" in m for m in check_bytes(_recrc(bad_hero)))

    bad_thumb = bytearray(good)
    bad_thumb[h.table_off + 0x11] |= ap.F_THUMB             # add the thumb bit
    assert any("must not carry a thumb" in m for m in check_bytes(_recrc(bad_thumb)))

    bad_jp = bytearray(good)
    bad_jp[h.table_off + 0x11] |= ap.F_JP                   # add the JP-title bit
    assert any("must not carry a JP-title bitmap" in m for m in check_bytes(_recrc(bad_jp)))


def test_gdb_genre_out_of_range_caught():
    data = bytearray(_good()["gdb"])
    h = bf.unpack_header(bytes(data))
    data[h.table_off + 0x11] = 5                           # genre id 5, but only 1 genre ("G") exists
    assert any("genre id" in m for m in check_bytes(_recrc(data)))


def test_gdb_alias_unknown_key_caught():
    data = bytearray(_good()["gdb"])
    h = bf.unpack_header(bytes(data))
    (_, _), (alias_off, alias_size), _ = h.sections
    assert alias_size == 32                                # one alias row: "Z-9" -> "A-1"
    data[alias_off + 16] = ord("X")                        # canonical key byte 0: "A-1" -> "X-1" (unknown)
    assert any("unknown key" in m for m in check_bytes(bytes(data)))  # alias section isn't under table_crc


def test_lib_bad_image_type_caught():
    data = bytearray(_good()["lib"])
    h = bf.unpack_header(bytes(data))
    data[h.table_off + 0x13] = 5                           # image type 5, neither 1 (cue) nor 2 (iso)
    assert any("image type" in m for m in check_bytes(_recrc(data)))


def test_lib_folder_id_out_of_range_caught():
    data = bytearray(_good()["lib"])
    h = bf.unpack_header(bytes(data))
    struct.pack_into(">H", data, h.table_off + 0x18, 5)   # folder id 5, but only 1 folder exists
    assert any("folder id" in m for m in check_bytes(_recrc(data)))
