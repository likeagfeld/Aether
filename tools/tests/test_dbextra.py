"""omsat.dbextra: the libretro DAT merge, disc aliases, save-tool records and the same-disc rule.
Synthetic DAT text: codes and titles only, shaped like libretro-database metadat/redump/Sega - Saturn.dat."""
import pytest

from omsat import dbextra
from omsat.gamesdb import GameRecord
from omsat.match import Matcher


def _game(name, region, serial, size=1000, crc="00000001"):
    return (f'game (\n\tname "{name}"\n\tregion "{region}"\n\tserial "{serial}"\n'
            f'\trom ( name "{name} (Track 1).bin" size {size} crc {crc} md5 0 sha1 0 serial "{serial}" )\n)\n')


DAT = "".join([
    _game("Croc - Legend of the Gobbos (Europe)", "Europe", "T-5029H-50", crc="0000000A"),
    _game("Christmas Nights into Dreams... (USA)", "USA", "81067", crc="0000000B"),
    _game("Horde, The (USA)", "USA", "T-15909H", crc="0000000C"),
    _game("3x3 Eyes (Japan) (Disc 2)", "Japan", "T-21301G-1", crc="0000000D"),
    _game("D (Germany) (Disc 1)", "Germany", "T-8106H-50-G-0", crc="0000000E"),
    _game("Typo (Europe)", "Europe", "T-25414-H-50", crc="0000000F"),
    _game("Rally (USA)", "USA", "80012", size=31347456, crc="E7A06577"),
    _game("Rally (USA)", "USA", "81207", size=31347456, crc="E7A06577"),
    _game("Twin (Japan)", "Japan", "T-20115G", size=5, crc="00000005"),
    _game("Twin (Japan)", "Japan", "T-20118G", size=5, crc="00000005"),
    'game (\n\tname "No Serial (Japan)"\n\tregion "Japan"\n)\n',
])


def _sheet():
    keys = {"T-5029H-18": ("Croc - Legend of the Gobbos", 8), "T-15909H": ("The Horde", 4),
            "T-21301G": ("3x3 Eyes", 1), "T-25414H-50": ("Typo", 8), "80012": ("Sega Rally Championship", 4),
            "81207": ("Sega Rally Championship", 4), "T-20115G": ("Black/Matrix", 1), "T-20118G": ("Black/Matrix", 1),
            "T-8106H": ("D", 4), "81205": ("Cyber Speedway", 4)}
    return {k: GameRecord(key=k, title_en=t, region_flags=f) for k, (t, f) in keys.items()}


def test_parse_dat_strips_disc_index_and_maps_areas():
    games, rep = dbextra.parse_dat(DAT)
    by = {g.name: g for g in games}
    assert by["3x3 Eyes (Japan) (Disc 2)"].serial == "T-21301G"
    assert by["D (Germany) (Disc 1)"].serial == "T-8106H-50" and by["D (Germany) (Disc 1)"].area == "E"
    assert by["Rally (USA)"].track1 == (31347456, "E7A06577")
    assert rep == {"games_parsed": 11, "no_serial": 1, "disc_index_stripped": 2}
    assert dbextra.dat_area("Taiwan / Hong Kong") == "T" and dbextra.dat_area("USA, Brazil") == "BU"
    assert dbextra.dat_title("Horde, The (USA)") == "The Horde"
    assert dbextra.dat_title("Lost World, The - Jurassic Park (USA) [!]") == "The Lost World - Jurassic Park"


def test_merge_dat_sheet_priority():
    recs, aliases = _sheet(), {}
    games, _ = dbextra.parse_dat(DAT)
    rep = dbextra.merge_dat(recs, aliases, games)
    assert recs["T-5029H-50"].title_en == "Croc - Legend of the Gobbos"   # the PAL code: not the sheet's -18 sibling
    assert recs["81067"].region_flags == 4
    assert "T-25414-H-50" not in recs                                    # the sheet's T-25414H-50, typo'd
    assert recs["T-15909H"].title_en == "The Horde"                      # the sheet's record is untouched
    assert recs["T-8106H-50"].region_flags == 8           # the PAL code next to the sheet's US T-8106H
    assert rep["dat_records_added"] == 3


def test_same_disc_folds_only_with_a_measured_key():
    recs, aliases, jp = _sheet(), {}, {"80012": "x"}
    games, _ = dbextra.parse_dat(DAT)
    folded, left = dbextra.collapse_same_disc(recs, aliases, games, {"81207": "measured"}, jp)
    assert folded == [["81207", "80012"]] and "80012" not in recs and aliases["80012"] == "81207" and not jp
    assert left == [["T-20115G", "T-20118G"]] and "T-20118G" in recs     # no IP measurement: keys never move
    m = Matcher(list(recs), aliases)
    assert m.match("MK-81207", "U").key == "81207" and m.match("80012", "U").key == "81207"


def test_disc_aliases_and_save_tools():
    recs, aliases = _sheet(), {}
    assert dbextra.apply_disc_aliases(recs, aliases, [{"product": "MK-81204", "key": "81205"}]) == []
    assert Matcher(list(recs), aliases).match("MK-81204V1", "UT").key == "81205"
    errs = dbextra.apply_disc_aliases(recs, aliases, [{"product": "81205", "key": "81205"},
                                                      {"product": "X-1", "key": "NOPE"}])
    assert len(errs) == 2
    added = dbextra.add_save_tools(recs, aliases, [{"name": "Save Game Copier", "product_codes": ["SGC"]},
                                                   {"name": "Known", "product_codes": ["T-15909H"]}])
    assert added == ["SGC"] and recs["SGC"].title_en == "Save Game Copier"


def test_shipped_disc_aliases_are_well_formed():
    d = dbextra.load_json(dbextra.DISC_ALIASES)
    rows = d["aliases"]
    assert len({r["product"] for r in rows}) == len(rows)
    for r in rows:
        assert set(r) == {"product", "key", "title", "evidence"} and r["evidence"].split()[0].rstrip(";") in ("measured", "kai")
    assert set(d["same_disc_keep"]) == {"81207"}


def test_fetch_dat_rejects_a_wrong_hash(tmp_path, monkeypatch):
    p = tmp_path / "x.dat"
    p.write_text("not the pinned file")
    import urllib.request

    class R:
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self): return b"also not it"
    monkeypatch.setattr(urllib.request, "urlopen", lambda *a, **k: R())
    with pytest.raises(ValueError):
        dbextra.fetch_dat(p)


def test_dat_title_keeps_what_names_a_different_disc():
    """A demo or sample disc must not take the full game's title: the cheat keymap matches by title."""
    assert dbextra.dat_title("Panzer Dragoon (Japan) (Demo 2)") == "Panzer Dragoon (Demo 2)"
    assert dbextra.dat_title("Virtua Fighter 2 (Japan) (Mihonban)") == "Virtua Fighter 2 (Mihonban)"
    assert dbextra.dat_title("Sakura Taisen (Japan) (Disc 1) (8M, 13M)") == "Sakura Taisen"
    assert dbextra.dat_title("Atlantis - The Lost Tales (Europe) (En,De,Es) (Disc 1)") == "Atlantis - The Lost Tales"
    assert dbextra.dat_title("Sega Rally Championship (Europe) (Made in USA)") == "Sega Rally Championship"
    assert dbextra.dat_title("Sanguozhi IV (Taiwan, Hong Kong)") == "Sanguozhi IV"
