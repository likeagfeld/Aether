import io
import json
import zipfile
from pathlib import Path

from PIL import Image

from omsat import artpak, cardbuild
from omsat.check import check_bytes
from omsat.iphdr import make_system_id
from omsat.libidx import read_libidx

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"


def _iso(p: Path, product: str, device: str, area: str):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(make_system_id(product=product, device=device, area=area, title="T") + b"\0" * 2048)


def _card(tmp_path: Path) -> tuple[Path, Path]:
    card = tmp_path / "card"
    _iso(card / "Racing" / "Daytona USA" / "daytona.iso", "GS-9001", "CD-1/1", "JTUE")
    Image.new("RGB", (80, 80), (0, 0, 255)).save(card / "Racing" / "Daytona USA" / "BOX.TGA")
    _iso(card / "RPG" / "Grandia" / "Grandia (Japan) (Disc 1) (4M).iso", "T-4507G", "CD-1/2", "J")
    _iso(card / "RPG" / "Grandia" / "Grandia (Japan) (Disc 2) (4M).iso", "T-4507G", "CD-2/2", "J")
    Image.new("RGB", (100, 100), (0, 255, 0)).save(card / "RPG" / "FOLDER.PNG")
    (card / "Racing" / "Other").mkdir()
    (card / "menu.bin").write_bytes(b"official")
    z = tmp_path / "cp.zip"
    with zipfile.ZipFile(z, "w") as zf:
        b = io.BytesIO()
        Image.new("RGB", (850, 850), (255, 0, 0)).save(b, "PNG")
        zf.writestr("JAPAN/Grandia (Japan) (Disc 1).png", b.getvalue())
    return card, z


def test_build_end_to_end(tmp_path, monkeypatch):
    card, z = _card(tmp_path)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (Path("fake.ttf"), "fake"))
    monkeypatch.setattr(cardbuild, "render_jp", lambda text, font: ((16, 16, bytes(range(128))), False))
    out = tmp_path / "out"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {"T-4507G": "グランディア"}, z, None, False, out, 20261117)
    for n in ("LIBRARY.IDX", "GAMES.DB", "ART.PAK"):
        assert check_bytes((out / n).read_bytes()) == [], n
    assert (out / "menu.bin").exists() is False and (card / "menu.bin").read_bytes() == b"official"
    ents = {(e["key"], e["disc"]): e for e in artpak.read_artpak((out / "ART.PAK").read_bytes())}
    assert ents[("GS-9001", 0)]["flags"] & artpak.F_HERO                  # BOX.TGA
    assert ents[("T-4507G", 1)]["flags"] & artpak.F_HERO                  # zip master, disc 1
    assert ents[("T-4507G", 0)]["flags"] & artpak.F_JP                    # JP bitmap
    assert ents[(artpak.folder_art_key("RPG"), 0)]["flags"] & artpak.F_HERO
    assert rep["covers"] == {"zip": 1, "box_tga": 1, "none": 1, "box_tga_unreadable": 0, "denominator": "3 matched images"}
    assert rep["jp"]["rendered"] == 1 and rep["jp"]["denominator"] == "1 matched keys with a JP title"
    assert rep["folder_art"] == {"found": 1, "denominator": "4 folders below root"}
    lib, folders, fp = read_libidx((out / "LIBRARY.IDX").read_bytes())
    assert [f.path for f in folders] == ["", "RPG", "Racing", "RPG/Grandia", "Racing/Other"]   # 2 discs: a folder
    assert json.loads((out / "report.json").read_text())["covers"]["zip"] == 1
    rows = (out / "manifest.csv").read_text().splitlines()
    assert rows[0] == "id,key,disc,source,source_file,transform" and len(rows) == 1 + 4


def test_font_unavailable_skips_jp_and_says_so(tmp_path, monkeypatch):
    card, z = _card(tmp_path)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "download failed: offline"))
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {"T-4507G": "x"}, None, None, True,
                          tmp_path / "o2", 20261117)
    assert rep["jp"] == {"status": "SKIPPED", "reason": "download failed: offline", "denominator": "1 matched keys with a JP title"}
    ents = artpak.read_artpak((tmp_path / "o2" / "ART.PAK").read_bytes())
    assert not any(e["flags"] & artpak.F_JP for e in ents)


def test_corrupt_box_tga_still_prefers_a_zip_master(tmp_path, monkeypatch):
    """The card's own BOX.TGA is tried first (SAROO deploy); a corrupt one is counted by name and the Cover Project
    zip's master for the same game still gives the cover."""
    card, z = _card(tmp_path)
    (card / "Racing" / "Daytona USA" / "BOX.TGA").write_bytes(b"not an image, corrupted")
    with zipfile.ZipFile(z, "a") as zf:
        b = io.BytesIO()
        Image.new("RGB", (400, 400), (0, 0, 255)).save(b, "JPEG")
        zf.writestr("US/Daytona USA.jpg", b.getvalue())
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o_corrupt_zip"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, out, 20261117)
    assert rep["covers"]["box_tga_unreadable"] == 1            # tried first, unreadable, then the zip
    assert rep["covers"]["zip"] == 2                            # Daytona (zip) + Grandia (zip)
    ents = {(e["key"], e["disc"]): e for e in artpak.read_artpak((out / "ART.PAK").read_bytes())}
    assert ents[("GS-9001", 0)]["flags"] & artpak.F_HERO


def test_corrupt_box_tga_with_no_zip_hit_falls_back_to_no_art_with_named_reason(tmp_path, monkeypatch):
    """When the zip has nothing for a game and its BOX.TGA is corrupt, the hero art falls back to
    none and the reason is counted by name (box_tga_unreadable), not silently dropped."""
    card, z = _card(tmp_path)
    (card / "Racing" / "Daytona USA" / "BOX.TGA").write_bytes(b"not an image, corrupted")
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o_corrupt_none"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, None, None, False, out, 20261117)
    assert rep["covers"]["box_tga_unreadable"] == 1
    assert rep["covers"]["none"] == 3                           # Daytona (corrupt box) + Grandia disc 1 and disc 2 (no box, no zip)
    ents = {(e["key"], e["disc"]): e for e in artpak.read_artpak((out / "ART.PAK").read_bytes())}
    assert ("GS-9001", 0) not in ents


def test_unmatched_game_gets_no_art_entry_and_is_counted(tmp_path, monkeypatch):
    """A disc whose product code matches nothing in GAMES.DB is scanned (key == "") but never
    reaches the art pipeline, and the scan report says so by name (unmatched)."""
    card, z = _card(tmp_path)
    _iso(card / "Unknown" / "Mystery Game" / "mystery.iso", "ZZ-0000", "CD-1/1", "U")
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o_unmatched"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, out, 20261117)
    assert rep["scan"]["unmatched"] == 1
    ents = artpak.read_artpak((out / "ART.PAK").read_bytes())
    assert not any(e["key"] == "" for e in ents)


def _fallback_build(tmp_path, monkeypatch, fetch, **kw):
    from omsat.artsrc import RemoteArt
    card, z = _card(tmp_path)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "skipped in this test"))
    remote = RemoteArt(tmp_path / "cache", download=True, fetch=fetch)
    out = tmp_path / "out"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, out, 20261119,
                          remote=remote, **kw)
    return rep, out, remote


def test_fallback_retrohead_then_cache_is_offline(tmp_path, monkeypatch):
    """the image with no zip cover and no BOX.TGA (Grandia disc 2) takes retrohead's 2d/<product>.TGA; a second
    run with downloads off is served from the cache, hits and misses alike."""
    calls = []

    def fetch(url):
        calls.append(url)
        if url.endswith("/2d/T-4507G.TGA"):
            b = io.BytesIO()
            Image.new("RGB", (64, 100), (9, 9, 9)).save(b, "TGA")
            return b.getvalue()
        return None
    rep, out, remote = _fallback_build(tmp_path, monkeypatch, fetch)
    assert rep["covers"]["retrohead"] == 1 and rep["covers"]["none"] == 0
    assert [t["source"] for t in rep["per_title"] if t["source"] == "retrohead"] == ["retrohead"]
    from omsat.artsrc import RemoteArt
    off = RemoteArt(tmp_path / "cache", download=False, fetch=lambda u: (_ for _ in ()).throw(AssertionError(u)))
    img, url = off.retrohead("T-4507G")
    assert img is not None and url.endswith("T-4507G.TGA")


def test_fallback_title_card_is_flagged_and_folder_art_injected(tmp_path, monkeypatch):
    img = tmp_path / "folder.png"
    Image.new("RGB", (64, 64), (1, 2, 3)).save(img)
    rep, out, _ = _fallback_build(tmp_path, monkeypatch, lambda url: None, title_cards=True,
                                  folder_art={"Racing": img, "RPG": img})
    assert rep["covers"]["generated"] == 1 and rep["covers"]["none"] == 0
    assert any("FLAGGED" in t["source"] for t in rep["per_title"])
    # Racing had no FOLDER.PNG: injected; RPG's own FOLDER.PNG still wins
    assert rep["folder_art"]["injected"] == 1 and rep["folder_art"]["found"] == 2
    ents = {e["key"] for e in artpak.read_artpak((out / "ART.PAK").read_bytes())}
    assert artpak.folder_art_key("Racing") in ents


def test_art_prefer_honored_over_normal_order(tmp_path, monkeypatch):
    """--art-prefer GS-9001=libretro: Daytona already has a BOX.TGA (normally used), but the preferred
    source is tried first and, on a hit, used instead of it."""
    def fetch(url):
        if url.endswith("Daytona%20USA%20%28USA%29.png"):
            b = io.BytesIO()
            Image.new("RGB", (512, 862), (1, 2, 3)).save(b, "PNG")
            return b.getvalue()
        return None
    rep, out, remote = _fallback_build(tmp_path, monkeypatch, fetch, art_prefer={"GS-9001": "libretro"})
    assert rep["covers"]["libretro"] == 1 and rep["covers"]["box_tga"] == 0
    assert [t["source"] for t in rep["per_title"] if t["key"] == "GS-9001"] == ["libretro"]
    ents = {e["key"]: e for e in artpak.read_artpak((out / "ART.PAK").read_bytes())}
    assert ents["GS-9001"]["flags"] & artpak.F_HERO


def test_art_prefer_falls_back_when_preferred_source_missing(tmp_path, monkeypatch):
    """--art-prefer GS-9001=libretro: when libretro has nothing for the key, it falls back to the normal
    order (here, the BOX.TGA Daytona already has)."""
    rep, out, remote = _fallback_build(tmp_path, monkeypatch, lambda url: None, art_prefer={"GS-9001": "libretro"})
    assert rep["covers"]["libretro"] == 0 and rep["covers"]["box_tga"] == 1
    assert [t["source"] for t in rep["per_title"] if t["key"] == "GS-9001"] == ["BOX.TGA"]


def test_retrohead_reject_list_falls_through():
    from omsat.artsrc import RETROHEAD_REJECT, RemoteArt, retrohead_names
    assert retrohead_names("81070") == ["81070", "MK-81070"] and retrohead_names("T-4507G") == ["T-4507G"]
    assert "MK-81070" in RETROHEAD_REJECT
    b = io.BytesIO()
    Image.new("RGB", (64, 100)).save(b, "TGA")
    r = RemoteArt(Path("unused"), download=True, fetch=lambda u: None)
    r.get = lambda url: b.getvalue() if url.endswith("MK-81070.TGA") else None
    img, why = r.retrohead("81070")
    assert img is None and "REJECTED" in why


def test_ipbin_year_fills_card_gamesdb(tmp_path, monkeypatch):
    """2026-11-20 hardware report: the source GAMES.DB has no years, the details card showed the IP.BIN year and the
    Year sort ignored it. cardbuild writes each card game's IP.BIN date (earliest disc) into the card's GAMES.DB."""
    from omsat.gamesdb import read_gamesdb, write_gamesdb
    card = tmp_path / "card"
    for disc, date in ((1, "19971117"), (2, "19971108")):           # two discs of one key: the earlier date wins
        p = card / "Games" / "Grandia" / f"Grandia (Disc {disc}).iso"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(make_system_id(product="T-4507G", device=f"CD-{disc}/2", area="U", title="T",
                                     release_date=date) + b"\0" * 2048)
    p = card / "Games" / "Daytona USA" / "daytona.iso"
    p.parent.mkdir(parents=True)
    p.write_bytes(make_system_id(product="GS-9001", device="CD-1/1", area="JTUE", title="T", release_date="19950401")
                  + b"\0" * 2048)
    src = (FIX / "tiny.games.db").read_bytes()
    recs, genres, aliases = read_gamesdb(src)
    assert {r.key for r in recs} >= {"T-4507G", "GS-9001"}
    for r in recs:                                                   # GS-9001 already has a year: kept
        r.year, r.date = (1994, 0) if r.key == "GS-9001" else (0, 0)
    src = write_gamesdb(recs, genres, aliases, 20261117)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (Path("fake.ttf"), "fake"))
    monkeypatch.setattr(cardbuild, "render_jp", lambda text, font: ((16, 16, bytes(range(128))), False))
    out = tmp_path / "out"
    # compat=False: this test is about the IP.BIN year fill only (M6 Task 16's compat rows are tested in
    # test_compat.py and would otherwise patch the fixture's real-world product codes here too).
    rep = cardbuild.build(card, src, {}, None, None, False, out, 20261117, compat=False)
    data = (out / "GAMES.DB").read_bytes()
    assert check_bytes(data) == []
    got = {r.key: r for r in read_gamesdb(data)[0]}
    assert (got["T-4507G"].year, got["T-4507G"].date) == (1997, 19971108)
    assert (got["GS-9001"].year, got["GS-9001"].date) == (1994, 0)
    before = {r.key: r for r in recs}
    for k, r in got.items():                                        # every other record unchanged
        if k != "T-4507G":
            assert r == before[k]
    assert rep["years_from_ipbin"] == 1
    # nothing to fill: the source bytes pass through untouched
    rep2 = cardbuild.build(card, data, {}, None, None, False, tmp_path / "out2", 20261117, compat=False)
    assert (tmp_path / "out2" / "GAMES.DB").read_bytes() == data and rep2["years_from_ipbin"] == 0


def test_jp_title_too_tall_is_dropped_and_reported(tmp_path, monkeypatch):
    """SPEC §5.2: a title whose ink is taller than the bitmap even at 10 px gets no JP bitmap,
    never a clipped one, and the report names it with a warning"""
    from omsat.jpfont import TooTall
    card, z = _card(tmp_path)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (Path("fake.ttf"), "fake"))

    def tall(text, font):
        raise TooTall("over 16 rows even at 10 px")
    monkeypatch.setattr(cardbuild, "render_jp", tall)
    out = tmp_path / "out"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {"T-4507G": "グランディア"}, z, None, False, out,
                          20261117)
    assert rep["jp"]["rendered"] == 0 and rep["jp"]["too_tall"] == ["T-4507G"] and "10 px" in rep["jp"]["warning"]
    ents = artpak.read_artpak((out / "ART.PAK").read_bytes())
    assert not any(e["flags"] & artpak.F_JP for e in ents)
    assert check_bytes((out / "ART.PAK").read_bytes()) == []


def _useredit_build(tmp_path, monkeypatch, useredit: str | None):
    from omsat.gamesdb import read_gamesdb
    card, _ = _card(tmp_path)
    if useredit is not None:
        (card / "aether").mkdir()
        (card / "aether" / "USEREDIT.INI").write_text(useredit, encoding="utf-8")
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (Path("fake.ttf"), "fake"))
    monkeypatch.setattr(cardbuild, "render_jp", lambda text, font: ((16, 16, bytes(range(128))), False))
    out = tmp_path / "out"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, None, None, False, out, 20261123, compat=False)
    data = (out / "GAMES.DB").read_bytes()
    assert check_bytes(data) == []
    recs, genres, _ = read_gamesdb(data)
    return rep, {r.key: r for r in recs}, genres


def test_useredit_applied_and_flags_the_user_title(tmp_path, monkeypatch):
    """2026-11-23 user request (Card Manager parity, SPEC §5.3 0x35 bit0): cardbuild applies the card's
    /aether/USEREDIT.INI (title, genre, year, region, players) onto GAMES.DB, and flags exactly the records whose
    title the user set, so the menu shows that title instead of the folder or file name."""
    rep, got, genres = _useredit_build(tmp_path, monkeypatch, "# comment\r\n[GS-9001]\r\ntitle=Daytona USA (My Copy)\r\n"
                                       "genre=arcade racing\r\nyear=1996\r\nregion=UE\r\nplayers=2\r\n"
                                       "[T-4507G]\ngenre=rpg\n[NOT-ON-DB]\ntitle=Nobody\n")
    d, g = got["GS-9001"], got["T-4507G"]
    assert (d.title_en, d.title_rom, d.user_flags) == ("Daytona USA (My Copy)", "Daytona USA (My Copy)", 0x01)
    assert genres[d.genre] == "arcade racing" and d.year == 1996 and d.region_flags == 0b1100 and d.players == 2
    assert g.user_flags == 0 and g.title_en == "Grandia" and genres[g.genre] == "RPG"      # genre matched, any case
    assert all(r.user_flags == 0 for k, r in got.items() if k != "GS-9001")
    assert rep["useredit"] == {"records": 2, "titles": 1, "not_in_db": ["NOT-ON-DB"]}


def test_no_useredit_leaves_gamesdb_unflagged(tmp_path, monkeypatch):
    rep, got, _ = _useredit_build(tmp_path, monkeypatch, None)
    assert all(r.user_flags == 0 for r in got.values()) and got["GS-9001"].title_en == "Daytona USA"
    assert rep["useredit"] == {"records": 0, "titles": 0, "not_in_db": []}


def test_own_box_tga_beats_a_zip_master(tmp_path, monkeypatch):
    """SAROO deploy: a readable BOX.TGA beside the image wins over the Cover Project zip."""
    card, z = _card(tmp_path)
    with zipfile.ZipFile(z, "a") as zf:
        b = io.BytesIO()
        Image.new("RGB", (400, 400), (255, 255, 0)).save(b, "JPEG")
        zf.writestr("US/Daytona USA.jpg", b.getvalue())
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, tmp_path / "o", 20261117)
    assert rep["covers"]["box_tga"] == 1 and rep["covers"]["zip"] == 1 and rep["covers"]["box_tga_unreadable"] == 0


def test_box_tga_is_folder_art(tmp_path, monkeypatch):
    """Satiator Rings' model (2026-11-23): a folder's BOX.TGA is its art, after FOLDER.PNG/JPG/TGA -- with or without
    loose images of its own (they fall back to it as their cover too)."""
    card, _ = _card(tmp_path)
    Image.new("RGB", (60, 60), (9, 9, 9)).save(card / "Racing" / "BOX.TGA")              # Racing: no image of its own
    _iso(card / "Loose" / "game.iso", "GS-7103", "CD-1/1", "J")                          # Loose: has an image
    (card / "Loose" / "Sub").mkdir()                                                     # (a subfolder: a folder row)
    Image.new("RGB", (60, 60), (9, 9, 9)).save(card / "Loose" / "BOX.TGA")
    (card / "RPG" / "FOLDER.PNG").unlink()
    Image.new("RGB", (60, 60), (9, 9, 9)).save(card / "RPG" / "BOX.TGA")                 # RPG: only a game folder below
    Image.new("RGB", (60, 60), (1, 1, 1)).save(card / "RPG" / "FOLDER.TGA")              # ...and FOLDER.TGA wins
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o"
    rep = cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, None, None, False, out, 20261117)
    rows = {r.split(",")[1]: r for r in (out / "manifest.csv").read_text().splitlines()[1:]}
    assert rows[artpak.folder_art_key("Racing")].endswith(",user,Racing/BOX.TGA,fit+letterbox RGB555")
    assert rows[artpak.folder_art_key("RPG")].endswith(",user,RPG/FOLDER.TGA,fit+letterbox RGB555")
    assert rows[artpak.folder_art_key("Loose")].endswith(",user,Loose/BOX.TGA,fit+letterbox RGB555")
    assert rep["folder_art"]["found"] == 3


def _zip_with(tmp_path: Path, *stems: str) -> Path:
    z = tmp_path / "cp2.zip"
    with zipfile.ZipFile(z, "w") as zf:
        for s in stems:
            b = io.BytesIO()
            Image.new("RGB", (400, 400), (255, 255, 0)).save(b, "JPEG")
            zf.writestr(f"US/{s}.jpg", b.getvalue())
    return z


def test_a_folders_only_loose_game_takes_its_box_tga_first(tmp_path, monkeypatch):
    """2026-11-24 (user's card: four games lost their own cover): a folder holding exactly ONE loose game gives that
    game its BOX.TGA at the top priority -- even with subfolders beside it (Andretti Racing with a nested game; the
    XBAND folders' _glue) -- so the Cover Project zip no longer wins over the user's own art."""
    card = tmp_path / "card"
    _iso(card / "Racing" / "Andretti Racing" / "andretti.iso", "T-8113H", "CD-1/1", "U")
    Image.new("RGB", (60, 60), (1, 2, 3)).save(card / "Racing" / "Andretti Racing" / "BOX.TGA")
    _iso(card / "Racing" / "Andretti Racing" / "Nested Game" / "nested.iso", "GS-7103", "CD-1/1", "J")
    _iso(card / "XBAND" / "Keriotosse" / "keriotosse.iso", "GS-9001", "CD-1/1", "JTUE")
    Image.new("RGB", (60, 60), (4, 5, 6)).save(card / "XBAND" / "Keriotosse" / "BOX.TGA")
    (card / "XBAND" / "Keriotosse" / "_glue").mkdir()
    (card / "XBAND" / "Keriotosse" / "_glue" / "patch.bin").write_bytes(b"x")
    z = _zip_with(tmp_path, "Andretti Racing", "Keriotosse")
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o"
    cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, out, 20261117, cheats=False)
    rows = {r.split(",")[1]: r for r in (out / "manifest.csv").read_text().splitlines()[1:] if ",0," in r}
    assert rows["T-8113H"].endswith(",user,Racing/Andretti Racing/BOX.TGA,fit+letterbox RGB555"), rows["T-8113H"]
    assert rows["GS-9001"].endswith(",user,XBAND/Keriotosse/BOX.TGA,fit+letterbox RGB555"), rows["GS-9001"]


def test_a_box_tga_shared_by_several_loose_games_stays_a_fallback(tmp_path, monkeypatch):
    card = tmp_path / "card"
    _iso(card / "Shared" / "a.iso", "T-8113H", "CD-1/1", "U")
    _iso(card / "Shared" / "b.iso", "GS-9001", "CD-1/1", "JTUE")
    Image.new("RGB", (60, 60), (7, 8, 9)).save(card / "Shared" / "BOX.TGA")
    z = _zip_with(tmp_path, "Shared")
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "no font needed"))
    out = tmp_path / "o"
    cardbuild.build(card, (FIX / "tiny.games.db").read_bytes(), {}, z, None, False, out, 20261117, cheats=False)
    rows = {r.split(",")[1]: r for r in (out / "manifest.csv").read_text().splitlines()[1:] if ",0," in r}
    assert ",coverproject,US/Shared.jpg," in rows["T-8113H"] and ",coverproject,US/Shared.jpg," in rows["GS-9001"]


def test_region_fallback_comes_after_the_exact_sources_and_before_a_title_card(tmp_path, monkeypatch):
    """user-approved: a PAL game with no PAL cover shows the same game's US cover, marked; never a title
    card while a real cover of the game exists."""
    from omsat.gamesdb import GameRecord, write_gamesdb
    card = tmp_path / "card"
    _iso(card / "Games" / "Game" / "game.iso", "T-1H-50", "CD-1/1", "E")
    db = write_gamesdb([GameRecord("T-1H-50", "Game", region_flags=8), GameRecord("T-1H", "Game", region_flags=4)],
                       [], {}, 20261124)
    monkeypatch.setattr(cardbuild, "ensure_font", lambda path, download: (None, "skipped in this test"))
    rep = cardbuild.build(card, db, {}, _zip_with(tmp_path, "Game (USA)"), None, False, tmp_path / "out", 20261124,
                          title_cards=True, cheats=False)
    assert rep["covers"]["region_fallback"] == 1 and rep["covers"]["generated"] == 0
    assert [t["source"] for t in rep["per_title"]] == ["region_fallback (fallback_region U, from T-1H-50)"]
    rows = (tmp_path / "out" / "manifest.csv").read_text().splitlines()
    assert "fallback_region U" in rows[1]
