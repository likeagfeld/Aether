import zipfile

from PIL import Image

from omsat.coverzip import CoverZip, norm_stem


def _zip(tmp_path):
    z = tmp_path / "cp.zip"
    with zipfile.ZipFile(z, "w") as zf:
        for name in ("JAPAN/Grandia (Japan) (Disc 1).png", "US/Gex (USA).jpg", "EU/Gex (Europe).jpg", "US/Gex.jpg",
                     "JAPAN/Dup (Japan).png", "US/Dup (Japan).jpg", "US/Two (Brazil).jpg", "EU/Two (Brazil).png",
                     "Rings Covers/x.tga"):
            img = Image.new("RGB", (8, 8), (200, 10, 10))
            import io
            b = io.BytesIO()
            img.save(b, "PNG" if name.endswith(".png") else "JPEG")
            zf.writestr(name, b.getvalue())
    return z


def test_norm_stem():
    assert norm_stem("JAPAN/Grandia (Japan) (Disc 1).png") == "grandia (japan) (disc 1)"
    assert norm_stem("Grandia (Japan) (Disc 1) (4M) [T-En by X v0.9]") == "grandia (japan) (disc 1) (4m)"


def test_find_drops_trailing_groups_but_keeps_region(tmp_path):
    cz = CoverZip(_zip(tmp_path))
    assert cz.indexed == 8
    assert cz.find("Grandia (Japan) (Disc 1) (4M) [T-En by TrekkiesUnite118 v0.9.3 RC]") == "JAPAN/Grandia (Japan) (Disc 1).png"
    assert cz.disc_of("JAPAN/Grandia (Japan) (Disc 1).png") == 1
    assert cz.find("Gex (USA)") == "US/Gex (USA).jpg"
    assert cz.find("Gex (Brazil)") is None            # the region group is never dropped, so no fall back to "Gex"
    assert cz.find("Dup (Japan)") == "JAPAN/Dup (Japan).png" and cz.resolved == 1   # the name's own region dir
    assert cz.find("Two (Brazil)") is None and cz.ambiguous == 1                      # no region dir, bytes differ
    assert cz.image("US/Gex (USA).jpg").size == (8, 8)


def test_region_dir_need_not_be_at_the_zip_root(tmp_path):
    """Regression for a real-archive bug: the actual Rings Cover Project zip wraps every region
    directory in a top-level "SEGA SATURN RINGS Cover Project/" folder, so the region name is
    the member's parent directory rather than its first path component."""
    z = tmp_path / "wrapped.zip"
    with zipfile.ZipFile(z, "w") as zf:
        img = Image.new("RGB", (8, 8), (10, 20, 30))
        import io
        b = io.BytesIO()
        img.save(b, "JPEG")
        zf.writestr("SEGA SATURN RINGS Cover Project/US/Nested (USA).jpg", b.getvalue())
    cz = CoverZip(z)
    assert cz.indexed == 1
    assert cz.find("Nested (USA)") == "SEGA SATURN RINGS Cover Project/US/Nested (USA).jpg"


def test_close_releases_the_underlying_zip_handle(tmp_path):
    """CoverZip must close its ZipFile, directly or as a context manager,
    rather than leaking the open file handle on a 1.6 GB archive."""
    z = _zip(tmp_path)
    cz = CoverZip(z)
    assert cz.zf.fp is not None
    cz.close()
    assert cz.zf.fp is None

    with CoverZip(z) as cz2:
        assert cz2.find("Gex (USA)") == "US/Gex (USA).jpg"
        assert cz2.zf.fp is not None
    assert cz2.zf.fp is None
