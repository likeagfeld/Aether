"""Synthetic coverpack tests: no network, no real images (small PIL images generated here)."""
import io
import zipfile

import pytest
from PIL import Image

from omsat import coverpack as cp
from omsat.artsrc import RemoteArt
from omsat.gamesdb import GameRecord, write_gamesdb


def _png(size=(8, 8), color=(200, 10, 10), alpha=255):
    img = Image.new("RGBA", size, (*color, alpha))
    return img


# --------------------------------------------------------------------------- encode_cover

def test_encode_cover_downscales_and_never_upscales():
    big = Image.new("RGB", (1024, 512), (10, 20, 30))
    data, fmt, w, h = cp.encode_cover(big, long_side=512)
    assert (w, h) == (512, 256) and fmt == "jpeg"
    small = Image.new("RGB", (64, 32), (1, 2, 3))
    data, fmt, w, h = cp.encode_cover(small, long_side=512)
    assert (w, h) == (64, 32)          # not upscaled


def test_encode_cover_picks_jpeg_when_opaque_png_when_alpha():
    opaque = _png(alpha=255)
    data, fmt, _, _ = cp.encode_cover(opaque)
    assert fmt == "jpeg"
    Image.open(io.BytesIO(data)).load()

    transparent = _png(alpha=0)
    data, fmt, _, _ = cp.encode_cover(transparent)
    assert fmt == "png"
    out = Image.open(io.BytesIO(data)).convert("RGBA")
    assert out.getchannel("A").getextrema() == (0, 0)


# --------------------------------------------------------------------------- pack write/read

def _entry(code, source, color=(1, 2, 3), size=(4, 4)):
    data, fmt, w, h = cp.encode_cover(Image.new("RGB", size, color))
    return cp.CoverEntry(code, source, data, fmt, w, h)


def test_roundtrip_index_and_blobs():
    entries = [_entry("T-8113H", "coverproject", (255, 0, 0)),
               _entry("T-8113H", "libretro", (0, 255, 0)),
               _entry("GS-9001", "retrohead2d", (0, 0, 255))]
    pak = cp.write_coverpack(entries, 20261123)
    assert pak[:8] == cp.MAGIC

    index = cp.read_coverpack(pak)
    assert index["build_date"] == 20261123
    assert sorted(index["codes"]) == ["GS-9001", "T-8113H"]
    assert {e["source"] for e in index["codes"]["T-8113H"]} == {"coverproject", "libretro"}

    cover = cp.read_cover(pak, index, "GS-9001", "retrohead2d")
    assert Image.open(io.BytesIO(cover)).size == (4, 4)
    with pytest.raises(KeyError):
        cp.read_cover(pak, index, "GS-9001", "coverproject")


def test_index_entry_has_offset_size_dims_and_crc():
    pak = cp.write_coverpack([_entry("A-0001", "coverproject")], 20261123)
    index = cp.read_coverpack(pak)
    e = index["codes"]["A-0001"][0]
    for field in ("source", "format", "offset", "size", "width", "height", "crc32"):
        assert field in e
    assert e["size"] > 0 and e["width"] == 4 and e["height"] == 4


def test_check_coverpack_clean_pack_has_no_problems():
    pak = cp.write_coverpack([_entry("A-0001", "coverproject"), _entry("A-0002", "libretro")], 20261123)
    assert cp.check_coverpack(pak) == []


def test_check_coverpack_catches_a_flipped_byte_mutation():
    pak = bytearray(cp.write_coverpack([_entry("A-0001", "coverproject")], 20261123))
    index = cp.read_coverpack(bytes(pak))
    e = index["codes"]["A-0001"][0]
    mutate_at = index["data_off"] + e["offset"]
    pak[mutate_at] ^= 0xFF               # flip a bit inside the blob
    problems = cp.check_coverpack(bytes(pak))
    assert problems and "CRC" in problems[0]


def test_check_coverpack_catches_a_truncated_file():
    pak = cp.write_coverpack([_entry("A-0001", "coverproject")], 20261123)
    problems = cp.check_coverpack(pak[:-3])
    assert problems and "range" in problems[0]


def test_check_coverpack_catches_a_bad_magic():
    pak = bytearray(cp.write_coverpack([_entry("A-0001", "coverproject")], 20261123))
    pak[0:1] = b"X"
    problems = cp.check_coverpack(bytes(pak))
    assert problems and "magic" in problems[0]


def test_stats_counts_by_source_and_coverage():
    entries = [_entry("A-0001", "coverproject"), _entry("A-0001", "libretro"), _entry("A-0002", "retrohead3d")]
    index = cp.read_coverpack(cp.write_coverpack(entries, 20261123))
    s = cp.stats(index)
    assert s["codes"] == 2 and s["codes_with_cover"] == 2
    assert s["by_source"] == {"coverproject": 1, "retrohead2d": 0, "retrohead3d": 1, "libretro": 1}


# --------------------------------------------------------------------------- build() end to end, offline

def _fake_gamesdb():
    recs = [
        GameRecord(key="T-0001", title_en="Gex", region_flags=1 << 2),          # "U" -> USA
        GameRecord(key="T-0002", title_en="Grandia", region_flags=1 << 0),      # "J" -> Japan
        GameRecord(key="T-0003", title_en="Nothing Here", region_flags=1 << 2),
    ]
    return write_gamesdb(recs, [], {}, 20261123)


def _fake_zip(tmp_path):
    z = tmp_path / "covers.zip"
    with zipfile.ZipFile(z, "w") as zf:
        buf = io.BytesIO()
        Image.new("RGB", (100, 140), (50, 60, 70)).save(buf, "JPEG")
        zf.writestr("US/Gex (USA).jpg", buf.getvalue())
    return z


def _fake_fetch_factory():
    """A RemoteArt fetch() stand-in: no network, just a few canned URLs (the retrohead2d/3d and
    libretro hits for this test), everything else is a 404 (None)."""
    png2d = io.BytesIO()
    Image.new("RGB", (64, 100), (10, 10, 10)).save(png2d, "PNG")
    png3d = io.BytesIO()
    Image.new("RGBA", (64, 100), (10, 10, 10, 128)).save(png3d, "PNG")   # a 3D render with real alpha
    libretro_png = io.BytesIO()
    Image.new("RGB", (100, 140), (1, 1, 1)).save(libretro_png, "PNG")

    hits = {
        "https://files-ds-scene.net/retrohead/satiator/box/2d/T-0001.TGA": png2d.getvalue(),
        "https://files-ds-scene.net/retrohead/satiator/box/3d/T-0001.TGA": png3d.getvalue(),
        "https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Saturn/master/Named_Boxarts/"
        "Grandia%20%28Japan%29.png": libretro_png.getvalue(),
    }

    def fetch(url):
        return hits.get(url)
    return fetch


def test_build_end_to_end_keeps_every_source_and_flags_the_miss(tmp_path):
    remote = RemoteArt(tmp_path / "cache", download=True, fetch=_fake_fetch_factory())
    rep = cp.build(_fake_gamesdb(), _fake_zip(tmp_path), tmp_path / "out", remote=remote, workers=2)

    assert rep["total_codes"] == 3
    assert rep["codes_with_any_cover"] == 2        # T-0003 ("Nothing Here") has no cover anywhere
    assert rep["codes_with_no_cover"] == ["T-0003"]
    assert rep["by_source"]["coverproject"] == 1   # T-0001 matched by title in the zip
    assert rep["by_source"]["retrohead2d"] == 1
    assert rep["by_source"]["retrohead3d"] == 1
    assert rep["by_source"]["libretro"] == 1       # T-0002 (Grandia, Japan)
    assert rep["errors"] == []
    assert (tmp_path / "out" / "COVERS.PAK").is_file()
    assert (tmp_path / "out" / "coverage.json").is_file()

    pak = (tmp_path / "out" / "COVERS.PAK").read_bytes()
    assert rep["pack_size"] == len(pak)
    assert cp.check_coverpack(pak) == []
    index = cp.read_coverpack(pak)
    t1_sources = {e["source"] for e in index["codes"]["T-0001"]}
    assert t1_sources == {"coverproject", "retrohead2d", "retrohead3d"}   # 3 of 4 sources for one code

    # the 3D render kept its alpha (PNG); the 2D front was fully opaque (JPEG)
    e2d = next(e for e in index["codes"]["T-0001"] if e["source"] == "retrohead2d")
    e3d = next(e for e in index["codes"]["T-0001"] if e["source"] == "retrohead3d")
    assert e2d["format"] == "jpeg" and e3d["format"] == "png"


def test_build_is_offline_on_rerun_with_the_same_cache(tmp_path):
    calls = {"n": 0}
    real_fetch = _fake_fetch_factory()

    def counting_fetch(url):
        calls["n"] += 1
        return real_fetch(url)

    cache = tmp_path / "cache"
    remote1 = RemoteArt(cache, download=True, fetch=counting_fetch)
    cp.build(_fake_gamesdb(), _fake_zip(tmp_path), tmp_path / "out1", remote=remote1, workers=2)
    first_calls = calls["n"]
    assert first_calls > 0

    remote2 = RemoteArt(cache, download=True, fetch=counting_fetch)
    cp.build(_fake_gamesdb(), _fake_zip(tmp_path), tmp_path / "out2", remote=remote2, workers=2)
    assert calls["n"] == first_calls          # every lookup served from the cache, nothing re-fetched


# --------------------------------------------------------------------------- Task 18e: names, region fallback

def _zip_with(tmp_path, names):
    z = tmp_path / "cp.zip"
    with zipfile.ZipFile(z, "w") as zf:
        for i, n in enumerate(names):
            b = io.BytesIO()
            Image.new("RGB", (8, 8), (i * 40 % 256, 0, 0)).save(b, "JPEG")
            zf.writestr(n, b.getvalue())
    return z


def _dat(*games):
    return "".join(f'game (\n\tname "{n}"\n\tregion "{r}"\n\tserial "{s}"\n)\n' for n, r, s in games)


def test_finder_uses_the_dat_name_before_the_title(tmp_path):
    from omsat.coverzip import CoverZip
    recs = [GameRecord("T-15909H", "The Horde", region_flags=4)]       # the sheet's title; the zip has Redump's
    with CoverZip(_zip_with(tmp_path, ["US/Horde, The (USA).jpg"])) as cz:
        assert cp.CoverFinder(recs, {}, cz, None, None).zip_member(recs[0]) is None
        f = cp.CoverFinder(recs, {}, cz, None, _dat(("Horde, The (USA)", "USA", "T-15909H")))
        assert f.zip_member(recs[0]) == "US/Horde, The (USA).jpg"


def test_region_fallback_order_us_then_jp_then_eu(tmp_path):
    from omsat.coverzip import CoverZip
    recs = [GameRecord("T-1G-50", "Game", region_flags=8),                 # E only, no EU cover
            GameRecord("T-1G", "Game", region_flags=1),                    # J
            GameRecord("T-1H", "Game", region_flags=4)]                    # U
    z = _zip_with(tmp_path, ["JAPAN/Game (Japan).png", "US/Game (USA).jpg"])
    with CoverZip(z) as cz:
        f = cp.CoverFinder(recs, {}, cz, None, None)
        img, src, source, letter, from_key = f.fallback(recs[0])
        assert (src, source, letter, from_key) == ("US/Game (USA).jpg", "coverproject", "U", "T-1G-50")
        assert f.fallback(recs[2])[3] == "J"                                # a US game falls back to JP first
    with CoverZip(_zip_with(tmp_path, ["EU/Game (Europe).jpg"])) as cz:
        assert cp.CoverFinder(recs, {}, cz, None, None).fallback(recs[1])[3] == "E"
    with CoverZip(_zip_with(tmp_path, ["EU/Other (Europe).jpg"])) as cz:
        assert cp.CoverFinder(recs, {}, cz, None, None).fallback(recs[1]) is None


def test_fallback_entries_are_marked_and_blobs_shared():
    a = _entry("T-1G", "coverproject", (9, 9, 9))
    b = cp.CoverEntry("T-1G-50", "coverproject", a.data, a.format, a.width, a.height, "J", "T-1G")
    pak = cp.write_coverpack([a, b], 20261124)
    index = cp.read_coverpack(pak)
    e = index["codes"]["T-1G-50"][0]
    assert e["fallback_region"] == "J" and e["fallback_from"] == "T-1G"
    assert "fallback_region" not in index["codes"]["T-1G"][0]
    assert e["offset"] == index["codes"]["T-1G"][0]["offset"]               # one copy of the bytes
    assert len(pak) - index["data_off"] == len(a.data) and cp.check_coverpack(pak) == []
