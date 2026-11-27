"""PSKai v6.545's cheat database, converted once (contract/SPEC.md §5.15).

tools/data/cheats/kai_cheats.txt is the only copy of the data in the repo: the dataset in the public v6.545 release's
SatCom_src_20240114.7z (SHA-256 28675b8d...; 122 games, 428 cheats, 502 codes, CodeGen generation date 2016/08/02;
the user's choice until universal cheats work -- the 447-game 2024 tables convert and pack the same way, by path).
Nothing reads the upstream files. Set OMSAT_KAI_SRC to a directory holding them (cheats_full_data_{defs.h,ram.c,rom.c})
to re-check the conversion."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest

from omsat import cheatpak as cp, kaicheats as kc

ROOT = Path(__file__).resolve().parents[2]
GAMESDB_PATH = ROOT / "build" / "db" / "GAMES.DB"
# sha256 of the converted file: a change to it is a change to the cheat data, and must be deliberate
SOURCE_SHA256 = "1b7def83e435091370cac28130aab845c1b64b8d54d5d8b2517c05f9aef6842b"


@pytest.fixture(scope="module")
def db():
    return kc.load_db()


def test_counts(db):
    assert len(db.games) == 122
    assert len(db.cheats) == 428
    assert len(db.codes) == 502


def test_source_is_pinned_and_round_trips(db):
    text = kc.SOURCE_PATH.read_text(encoding="ascii")
    assert hashlib.sha256(text.encode("ascii")).hexdigest() == SOURCE_SHA256
    assert kc.dump_text(db, "2016/08/02") == text                # the text format loses nothing it carries


@pytest.mark.skipif(not os.environ.get("OMSAT_KAI_SRC"), reason="OMSAT_KAI_SRC (PSKai's CodeGen tables) not set")
def test_source_matches_upstream_tables(db):
    """The conversion check: the committed text equals PSKai's own tables, field for field."""
    assert kc.load_c_tables(Path(os.environ["OMSAT_KAI_SRC"])) == db


def test_parse_text_is_strict():
    with pytest.raises(ValueError):
        kc.parse_text("[game]\nname=x\nbogus=1\n")
    with pytest.raises(ValueError):
        kc.parse_text("[game]\nname=x\ncode=06000000:0001:01\n")      # a code before any cheat
    with pytest.raises(ValueError):
        kc.parse_text("name=x\n")                                      # a key before any [game]


def test_every_game_cheats_and_codes_are_contiguous_and_in_range(db):
    for g in db.games:
        assert 0 <= g.first_cheat
        assert g.first_cheat + g.num_cheats <= len(db.cheats)
        for c in db.cheats_for(g):
            assert 0 <= c.first_code
            assert c.first_code + c.num_codes <= len(db.codes)


def test_per_game_fits_om_ch_file(db):
    """Every game fits menu/src/cheatpak.h's OM_CH_MAX_CHEATS/OM_CH_MAX_CODES (64/256), header rows included."""
    for g in db.games:
        cheats = db.cheats_for(g)
        assert kc.fits_om_ch_file(cheats, sum(c.num_codes for c in cheats)), g.name


def test_greyout_rows_are_exactly_the_codeless_ones(db):
    assert all(bool(c.flags & kc.CT_PARAM_BF_GREYOUT) == (c.num_codes == 0) for c in db.cheats)


def test_command_bytes_match_task2_research(db):
    """docs/research/m6-research.md §6: Kai's in-RAM handler acts on 0x00/0x01/0x03/0x0D."""
    from collections import Counter
    assert Counter(c.cmd for c in db.codes) == {0x01: 357, 0x03: 83, 0x00: 52, 0x0D: 10}   # S4 §2's counts


# --- the committed CHEATS.PAK ---------------------------------------------------------------------------------

def test_committed_pak_is_built_from_the_committed_source():
    """tools/data/cheats/CHEATS.PAK == pack(kai_cheats.txt, kai_keymap.json), on a fresh clone too (no GAMES.DB)."""
    data = kc.PAK_PATH.read_bytes()
    assert data == cp.build_from_source()
    assert cp.check_pak(data) == []
    assert len(data) <= cp.MAX_FILE


def test_committed_pak_holds_every_matched_product(db):
    km = json.loads(kc.KEYMAP_PATH.read_text(encoding="utf-8"))
    recs = cp.read_pak(kc.PAK_PATH.read_bytes())
    by_name = {g.name: g for g in db.games}
    want = {}
    for row in km["games"]:
        if row["how"] != "unmatched":
            for k in row["products"]:
                want[k] = row
    assert set(recs) == set(want)
    for k, row in want.items():
        g, r = by_name[row["kai_name"]], recs[k]
        assert (r.master_addr, r.master_value, r.location, r.cache4) == (g.master_addr, g.master_opc,
                                                                          g.cfg_location, g.cache_4_capacity), k
        assert bool(r.flags & cp.G_JHL) == row["jhl_only"], k
        kai = db.cheats_for(g)
        assert [c.name for c in r.cheats] == [c.name[:30] for c in kai], k
        assert [c.codes for c in r.cheats] == [[(x.addr, x.value, x.cmd) for x in db.codes_for(c)] for c in kai], k
        assert [bool(c.flags & cp.C_ON) for c in r.cheats] == [bool(c.flags & kc.CT_PARAM_BF_DEFAULT_ON) for c in kai]


def test_nothing_reads_the_removed_upstream_files():
    """Decision (b): PSKai's upstream files are no longer in the repo, and no tracked file points at their old home."""
    out = subprocess.run(["git", "grep", "-l", "third_party" + "/pskai_cheats", "--", "tools", "menu", "contract",
                          "README.md", "CREDITS.md"], cwd=ROOT, capture_output=True, text=True)
    assert out.returncode == 1 and out.stdout == "", out.stdout
    assert not (ROOT / "third_party" / "pskai_cheats").exists()


# --- the keymap (needs build/db/GAMES.DB) ---------------------------------------------------------------------

@pytest.mark.skipif(not GAMESDB_PATH.is_file(), reason="build/db/GAMES.DB not built on this machine")
def test_keymap_covers_every_game(db):
    km = kc.build_keymap(db, GAMESDB_PATH.read_bytes(), kc.load_loader_compat())
    assert len(km["games"]) == 122
    assert {row["kai_name"] for row in km["games"]} == {g.name for g in db.games}
    collision_losers = {row["kai_name"] for row in km["games"] if row["how"] != "unmatched" and not row["products"]}
    for name in collision_losers:
        assert any(name in {c["kai_name"] for c in coll["claims"]} and coll["resolved"] != name
                   for coll in km["collisions"].values()), name
    for row in km["games"]:
        if row["how"] == "unmatched":
            assert row["products"] == []
        elif row["kai_name"] not in collision_losers:
            assert row["products"], row


@pytest.mark.skipif(not GAMESDB_PATH.is_file(), reason="build/db/GAMES.DB not built on this machine")
def test_committed_keymap_is_up_to_date(db):
    """Re-running `map` from the current GAMES.DB must reproduce the committed, hand-reviewed keymap."""
    fresh = kc.build_keymap(db, GAMESDB_PATH.read_bytes(), kc.load_loader_compat())
    assert fresh == json.loads(kc.KEYMAP_PATH.read_text(encoding="utf-8"))


# --- spot checks, made by hand against PSKai's 2016 C tables (cheats_full_data_ram.c / _rom.c) ------------------

def test_spot_check_astal_jap(db):
    """The first record of _ct_games_data: "ASTAL - JAP", 060B6C12:C305, 2 cheats, location 060FF000, 500/4."""
    g = db.games[0]
    assert g.name == "ASTAL - JAP"
    assert (g.master_addr, g.master_opc, g.num_cheats, g.cfg_location, g.cache_4_capacity, g.first_cheat) ==         (0x060B6C12, 0xC305, 2, 0x060FF000, 125, 0)
    assert [(c.name, c.num_codes) for c in db.cheats_for(g)] == [("UNLIMITED LIVES", 1), ("UNLIMITED ENERGY", 1)]
    c0 = db.codes[0]
    assert (c0.addr, c0.value, c0.cmd) == (0x060DBCAA, 0x0003, 0x03)


def test_spot_check_shining_wisdom_usa(db):
    """SPEC §5.15's old example game: 06000914:C305, location 06002800, 156/4, cheats at 411; codes 484 and 485."""
    g = next(x for x in db.games if x.name == "SHINING WISDOM - USA")
    assert (g.master_addr, g.master_opc, g.cfg_location, g.cache_4_capacity, g.first_cheat) ==         (0x06000914, 0xC305, 0x06002800, 39, 411)
    cheats = db.cheats_for(g)
    assert [c.name for c in cheats] == ["INFINITE MONEY", "HAVE MONKEY SUIT"]
    assert [(c.addr, c.value, c.cmd) for c in db.codes_for(cheats[0]) + db.codes_for(cheats[1])] ==         [(0x06006114, 0x2500, 0x01), (0x06006142, 0x0001, 0x03)]


# --- product-key collisions never put one game's codes on another --------------------------------------------

def _synthetic_collision_db(name_a="Widget Deluxe", name_b="WIDGET DELUXE"):
    game_a = kc.KaiGame(name_a, [0x11111111], 0x06000914, 0xC305, 1, 0x06002800, 0, 39, 0)
    game_b = kc.KaiGame(name_b, [0x22222222], 0x06000914, 0xC305, 1, 0x06002800, 0, 39, 1)
    db = kc.KaiDB([game_a, game_b], [kc.KaiCheat("Cheat A", 0, 1, 0), kc.KaiCheat("Cheat B", 1, 1, 0)],
                  [kc.KaiCode(0x06010000, 0x0001, 0x01), kc.KaiCode(0x06020000, 0x0002, 0x01)])
    from omsat.gamesdb import GameRecord, write_gamesdb
    gamesdb = write_gamesdb([GameRecord("T-99999G", "Widget Deluxe", region_flags=0)], ["Misc"], {}, 20261120)
    return db, gamesdb


def test_collision_is_detected_and_never_silently_packed():
    """Two Kai games with the same normalized title both match T-99999G: a tie, so neither keeps it."""
    db, gamesdb = _synthetic_collision_db()
    km = kc.build_keymap(db, gamesdb)
    assert {c["kai_name"] for c in km["collisions"]["T-99999G"]["claims"]} == {"Widget Deluxe", "WIDGET DELUXE"}
    assert km["collisions"]["T-99999G"]["resolved"] is None
    assert all("T-99999G" not in row["products"] for row in km["games"])
    assert "T-99999G" not in cp.build_records(db, km)


def test_collision_resolves_to_the_higher_ranked_claim():
    db, gamesdb = _synthetic_collision_db(name_a="Widget Deluxe", name_b="Widget Deluxe Plus")
    km = kc.build_keymap(db, gamesdb)
    assert km["games"][0]["how"] == "exact" and km["games"][1]["how"].startswith("fuzzy:")
    assert km["collisions"]["T-99999G"]["resolved"] == "Widget Deluxe"
    recs = cp.build_records(db, km)
    assert [c.name for c in recs["T-99999G"].cheats] == ["Cheat A"]       # the winner's codes, never the loser's


def test_build_records_fails_loud_on_an_unresolved_collision():
    """Defense in depth: a keymap that still gives one product to two games is refused, not first-wins."""
    db, _ = _synthetic_collision_db()
    km = {"games": [{"kai_name": "Widget Deluxe", "products": ["T-99999G"], "how": "exact", "jhl_only": False},
                    {"kai_name": "WIDGET DELUXE", "products": ["T-99999G"], "how": "exact", "jhl_only": False}]}
    with pytest.raises(Exception, match="T-99999G"):
        cp.build_records(db, km)
