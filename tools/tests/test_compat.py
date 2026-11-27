"""compat rows land in GAMES.DB bytes 0x2E-0x3F, the record CRC/header stay valid, an absent controller
file is a no-op, and Golden Axe - The Duel gets its 3D-pad note."""
import json
import struct
import pytest
from pathlib import Path

from omsat import binfmt as bf
from omsat import compat, gamesdb
from omsat.check import check_bytes

ROOT = Path(__file__).resolve().parents[2]
CTRL = ROOT / "tools" / "data" / "compat" / "controller_compat.json"


def _db(keys):
    recs = [gamesdb.GameRecord(key=k, title_en=k, region_flags=4) for k in sorted(keys)]
    return gamesdb.write_gamesdb(recs, [], {}, 20261120)


def test_golden_axe_note(tmp_path):
    lj = tmp_path / "l.json"
    lj.write_text(json.dumps({"source": "https://ppcenter.webou.net/pskai/compat/", "fetched": "2026-11-20",
                              "games": [{"product": "MK-81307", "region": "U", "title": "t", "loader": "jhl",
                                         "note": "n"}]}))
    rows = compat.load(lj, CTRL)
    data, n = compat.fill_compat(_db(["MK-81045", "MK-81307", "GS-9001"]), rows)
    assert n == 2 and check_bytes(data) == []
    recs = {r.key: r for r in gamesdb.read_gamesdb(data)[0]}
    assert recs["MK-81045"].compat_flags & 0x01 and "standard pad" in recs["MK-81045"].compat_note.lower()
    assert recs["MK-81307"].loader_hint == 1
    assert recs["GS-9001"].loader_hint == 0 and recs["GS-9001"].compat_note == ""


def test_absent_controller_file_is_noop(tmp_path):
    lj = tmp_path / "l.json"
    lj.write_text(json.dumps({"source": "s", "fetched": "x", "games": []}))
    data = _db(["MK-81045"])
    assert compat.fill_compat(data, compat.load(lj, tmp_path / "missing.json")) == (data, 0)


def test_only_confident_rows(tmp_path):
    rows = compat.load_controller([{"product_codes": ["X-1"], "title": "t", "peripheral": "p", "symptom": "s",
                                    "warning_text": "w", "confidence": "rumor", "sources": []}])
    assert rows == {}


def test_multiple_reports_kept_single_report_dropped_and_named():
    """Review fix round 1 (Important 2): the real dataset's confidence vocabulary is `confirmed` / `multiple
    reports` / `single report`, not `confirmed` / `likely`. Shinobi Legions ("multiple reports", two independent
    corroborating sources) must be kept; Street Fighter Alpha 2 ("single report", one inconsistent source) must be
    dropped, and named in the report list, same as an unrecognized-peripheral row would be."""
    data = json.loads(CTRL.read_text(encoding="utf-8"))
    notes: list[str] = []
    rows = compat.load_controller(data, report=notes)
    assert "T-2301H" in rows and rows["T-2301H"].flags & 0x02       # Shinobi Legions: kept, standard-pad bit set
    assert "standard pad" in rows["T-2301H"].note.lower()
    assert "T-1213H" not in rows                                     # Street Fighter Alpha 2: dropped
    assert any("Street Fighter Alpha 2" in n and "single report" in n for n in notes)


def test_igr_bad_sets_noigr_compat_flag(tmp_path):
    """a loader JSON row's "igr": "bad" sets OM_CF_NOIGR (0x20, gamesdb.h) -- menu/src/loader.c's
    om_igr_want then forces the in-game reset off for that key regardless of the global or per-game setting. No
    titles are known-bad yet; this is only the plumbing, exercised with a synthetic row."""
    lj = tmp_path / "l.json"
    lj.write_text(json.dumps({"source": "s", "fetched": "x",
                              "games": [{"product": "MK-81045", "region": "U", "title": "t", "loader": "jhl",
                                         "igr": "bad", "note": "n"},
                                        {"product": "GS-9001", "region": "U", "title": "ok", "loader": "jhl"}]}))
    rows = compat.load(lj, None)
    assert rows["MK-81045"].flags & compat.CF_NOIGR
    assert not (rows["GS-9001"].flags & compat.CF_NOIGR)          # no "igr" key: unaffected
    data, n = compat.fill_compat(_db(["MK-81045", "GS-9001"]), rows)
    assert n == 2 and check_bytes(data) == []
    recs = {r.key: r for r in gamesdb.read_gamesdb(data)[0]}
    assert recs["MK-81045"].compat_flags & compat.CF_NOIGR
    assert not (recs["GS-9001"].compat_flags & compat.CF_NOIGR)


def test_corrupt_note_offset_is_caught_by_the_python_checker():
    """brief Step 5: corrupting the new note offset (0x30) into an out-of-pool value must turn the checker RED.
    menu/host-tests/test_gamesdb.c proves the C reader rejects the same corruption with OM_E_FIELD."""
    data, n = compat.fill_compat(_db(["MK-81045"]),
                                 {"MK-81045": compat.CompatRow(hint=1, flags=3, note="n", regions=0)})
    assert n == 1
    h = bf.unpack_header(data)
    m = bytearray(data)
    struct.pack_into(">I", m, h.table_off + 0x30, 0x7FFFFFFF)         # note offset far beyond the pool
    struct.pack_into(">I", m, 0x24, bf.crc32(bytes(m[h.table_off:h.table_off + h.table_size])))
    assert check_bytes(bytes(m)) != []


LOADER = ROOT / "tools" / "data" / "compat" / "loader_compat.json"
SAVE_TOOLS = ROOT / "tools" / "data" / "compat" / "save_tools.json"
GAMESDB = ROOT / "build" / "db" / "GAMES.DB"


def test_compat_rows_land_on_the_key_the_matcher_gives_the_disc():
    """2026-11-24 hardware (Panzer Dragoon Saga black-screens under Auto): the compat data says product "MK-81307" is
    JHL-only, but GAMES.DB's keys are "81307" (US) and "MK81307-50" (PAL) -- the record the §6 matcher resolves that
    disc to. Each row must land there, by its own area: the US rows on 81307, the E rows on MK81307-50."""
    db = gamesdb.write_gamesdb([gamesdb.GameRecord(key=k, title_en="Panzer Dragoon Saga", region_flags=f)
                                for k, f in (("81307", 4), ("MK81307-50", 8), ("GS-9076", 1))], [], {}, 20261124)
    data, n = compat.fill_compat(db, compat.load(LOADER, None))
    recs = {r.key: r for r in gamesdb.read_gamesdb(data)[0]}
    assert recs["GS-9076"].loader_hint == 1
    assert recs["81307"].loader_hint == 1 and recs["81307"].compat_regions & 4
    assert recs["MK81307-50"].loader_hint == 1 and recs["MK81307-50"].compat_regions & 8


@pytest.mark.skipif(not GAMESDB.is_file(), reason="build/db/GAMES.DB not built on this machine")
def test_every_compat_product_lands_on_a_gamesdb_record():
    """Every product of loader_compat.json, controller_compat.json and save_tools.json resolves, through the same §6
    matcher a disc goes through, to a GAMES.DB record -- except the ones named here, which have no record at all."""
    recs, _, aliases = gamesdb.read_gamesdb(GAMESDB.read_bytes())
    from omsat.match import Matcher
    m = Matcher([r.key for r in recs], aliases)
    products = {(g["product"], g.get("area") or "") for g in json.loads(LOADER.read_text(encoding="utf-8"))["games"]}
    products |= {(p, "") for row in json.loads(CTRL.read_text(encoding="utf-8")) for p in row.get("product_codes", [])}
    products |= {(p, "") for t in json.loads(SAVE_TOOLS.read_text(encoding="utf-8"))["tools"]
                 for p in t.get("product_codes", [])}
    missing = sorted({p for p, a in products if not compat.resolve(m, p, a)})
    assert missing == sorted(NOT_IN_GAMESDB), missing
    data, n = compat.fill_compat(GAMESDB.read_bytes(), compat.load(LOADER, CTRL, save_tools_json=SAVE_TOOLS))
    hinted = {r.key for r in gamesdb.read_gamesdb(data)[0] if r.loader_hint}
    for p, a in products:
        for k in compat.resolve(m, p, a):
            if any(g["product"] == p for g in json.loads(LOADER.read_text(encoding="utf-8"))["games"]):
                assert k in hinted, (p, a, k)


# Measured 2026-11-24 against build/db/GAMES.DB after Task 18e (2,112 records: the sheet, the libretro DAT, the disc
# aliases of tools/data/gamesdb/disc_aliases.json and the save-tool records): the §6 matcher gives these product codes
# no key. All four are placeholders many homebrew discs share (Kai matches them by IP CRC, not by code), so a GAMES.DB
# record for any of them would put one title on unrelated discs. (Before Task 18e: 17 of 91 had no key.)
NOT_IN_GAMESDB = {"T-99901G", "T-81", "_UNKNOWN", "MINESWPR"}


FIX = ROOT / "contract" / "fixtures" / "compat_cases.json"


def _fixture_fill(tmp_path):
    from omsat import iphdr
    from omsat.libidx import LibEntry
    from omsat.match import Matcher
    spec = json.loads(FIX.read_text(encoding="utf-8"))
    loader = tmp_path / "loader.json"
    loader.write_text(json.dumps({"games": spec["games"]}), encoding="utf-8")
    db = gamesdb.write_gamesdb([gamesdb.GameRecord(key=k, title_en=k) for k in spec["keys"]], [], {}, 20261125)
    m = Matcher(spec["keys"], {})
    ents, sids = [], {}
    for c in spec["cases"]:
        s = iphdr.parse_system_id(bytes.fromhex(c["raw_hex"]))
        ents.append(LibEntry(path=c["path"], key=m.match(s.product, s.area).key or "",
                             area_flags=gamesdb.region_flags_from(s.area), sets=c.get("sets", 0)))
        sids[c["path"]] = s
    report: list[str] = []
    out, _ = compat.fill_compat(db, compat.load(loader, None), compat.discs_of(ents, sids), report)
    return spec, ents, sids, {r.key: r for r in gamesdb.read_gamesdb(out)[0]}, report


def test_per_disc_rules_match_the_shared_fixture(tmp_path):
    """2026-11-25 hardware: the user's Mortal Kombat II (USA), region-patched to area J, black-screened under Auto
    (CWX) -- Kai's T-8103H row is area U. contract/fixtures/compat_cases.json (also run by the Card Manager's
    CompatTests): Kai's IP CRC first, then product + area, then the product alone; our override rows ahead of all."""
    from omsat import iphdr
    spec, ents, sids, recs, report = _fixture_fill(tmp_path)
    for c, e in zip(spec["cases"], ents):
        r = recs[e.key]
        hint = r.loader_hint if r.loader_hint and (not r.compat_regions or r.compat_regions & e.area_flags) else 0
        assert (e.key, f"{iphdr.kai_ip_crc(sids[c['path']].raw):08X}", hint) == \
            (c["expect_key"], c["expect_crc"], c["expect_hint"]), c["path"]
    assert report == spec["expect_report"]


def test_mortal_kombat_ii_area_j_is_kais_crc():
    """The real header in the fixture: T-8103H, area J, and Kai's CRC 78A37A80 (the loader_compat.json U row's)."""
    from omsat import iphdr
    c = json.loads(FIX.read_text(encoding="utf-8"))["cases"][0]
    s = iphdr.parse_system_id(bytes.fromhex(c["raw_hex"]))
    assert (s.product, s.area, iphdr.kai_ip_crc(s.raw)) == ("T-8103H", "J", 0x78A37A80)
    row = [g for g in json.loads(LOADER.read_text(encoding="utf-8"))["games"] if g["product"] == "T-8103H"]
    assert [(g["area"], g["ip_crc"], g["loader"]) for g in row] == [("U", "78A37A80", "jhl")]


def test_same_product_keeps_pal_variants_apart():
    assert compat.same_product("T-8103H", "T-8103H  ")
    assert compat.same_product("MK-81307", "81307")
    assert not compat.same_product("T-8103H", "T-8103H-50")
    assert not compat.same_product("T-8103H", "T-8107G")


def test_override_source_is_the_neutral_name():
    games = json.loads(LOADER.read_text(encoding="utf-8"))["games"]
    own = [g for g in games if "source" in g]
    assert own and all(g["source"].startswith(compat.OVERRIDE_SOURCE) for g in own)
    assert "VibeMenu" not in LOADER.read_text(encoding="utf-8")
