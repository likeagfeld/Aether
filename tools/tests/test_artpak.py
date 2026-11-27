from PIL import Image
from omsat import artpak as ap
from omsat import binfmt as bf


def test_rgb555_bit_layout():
    # bit15 = opaque, B in 14-10, G in 9-5, R in 4-0 (to be confirmed against ST-013-R3 before M2, spec §12)
    assert ap.rgb555(255, 0, 0) == 0x801F
    assert ap.rgb555(0, 255, 0) == 0x83E0
    assert ap.rgb555(0, 0, 255) == 0xFC00
    assert ap.rgb555(0, 0, 0) == 0x8000


def test_layout_for():
    assert ap.layout_for(Image.new("RGB", (850, 850))) == "square"
    assert ap.layout_for(Image.new("RGB", (1513, 2365))) == "tall"


def test_encode_cover_letterboxes_with_transparent_bars():
    img = Image.new("RGB", (200, 100), (255, 0, 0))      # 2:1 into 80x80 -> 80x40 band, bars above and below
    px = ap.encode_cover(img, (80, 80))
    assert len(px) == 80 * 80 * 2
    top = int.from_bytes(px[0:2], "big")
    mid = int.from_bytes(px[(40 * 80 + 40) * 2:(40 * 80 + 40) * 2 + 2], "big")
    assert top == 0x0000 and mid == 0x801F


def test_fnv1a32_vectors():
    # FNV-1a-32, offset basis 0x811C9DC5, prime 0x01000193 (spec §5.2 Folder art)
    assert ap.fnv1a32(b"") == 0x811C9DC5
    assert ap.fnv1a32(b"a") == 0xE40C292C          # standard FNV-1a-32 test vector


def test_folder_art_key_sample_path():
    # computed this session: fnv1a32(b"RPG") == 0xD6B34428
    assert ap.folder_art_key("RPG") == "FD6B34428"


def test_pack_roundtrip_and_alignment():
    sq = Image.new("RGB", (100, 100), (0, 0, 255))
    tall = Image.new("RGB", (64, 100), (0, 255, 0))
    e = [ap.ArtEntry("T-8113H", 0, tall, tall, None, 2), ap.ArtEntry("GS-9001", 0, sq, sq, (16, 16, bytes(128)), 1)]
    data = ap.write_artpak(e, 20261117)
    h = bf.validate_common(data, bf.MAGIC_ART)
    assert h.data_off % 2048 == 0 and h.table_size == 48 * 2
    ents = ap.read_artpak(data)
    assert [x["key"] for x in ents] == ["GS-9001", "T-8113H"]
    g = ents[0]
    assert (g["thumb_w"], g["thumb_h"], g["hero_w"], g["hero_h"]) == (80, 80, 160, 160)
    assert g["flags"] & 0b1111 == 0b1111                  # thumb, hero, jp, square
    assert len(ap.get_blob(data, g, "thumb")) == 12800
    assert len(ap.get_blob(data, g, "hero")) == 51200
    assert len(ap.get_blob(data, g, "jp")) == 128
    t = ents[1]
    assert (t["thumb_w"], t["thumb_h"]) == (64, 100) and not t["flags"] & 0b1100
