import json
from pathlib import Path

from omsat import libidx
from omsat.cardscan import FsSource, TreeSource, scan
from omsat.gamesdb import read_gamesdb
from omsat.iphdr import make_system_id

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"


def _db():
    recs, _, aliases = read_gamesdb((FIX / "tiny.games.db").read_bytes())
    return [r.key for r in recs], aliases


def test_hidden_names():
    assert libidx.is_hidden_name("OpenMenu", True)
    assert libidx.is_hidden_name("autoboot.iso.bak-1", True)
    assert libidx.is_hidden_name(".Trashes", False)
    assert libidx.is_hidden_name("System Volume Information", False)
    assert not libidx.is_hidden_name("openmenu", False)
    assert not libidx.is_hidden_name("Racing", True)


def test_hidden_names_task_24c_kai_saves():
    """Kai's /SAVES/BRAM/... tree is hidden at root, as satiator-saves."""
    assert libidx.is_hidden_name("SAVES", True)
    assert libidx.is_hidden_name("saves", True)
    assert libidx.is_hidden_name("satiator-saves", True)
    assert not libidx.is_hidden_name("SAVES", False)
    assert not libidx.is_hidden_name("Saved Games", True)


def test_hidden_names_task_13r_vibemenu_and_openmenu():
    """The current folder name and the project's old folder name are both hidden at root, so a dev
    card still carrying an old /openmenu/ folder never lists it as a game folder."""
    assert libidx.is_hidden_name("aether", True)            # the Aether rename: the folder now, and before
    assert libidx.is_hidden_name("AETHER", True)
    assert not libidx.is_hidden_name("aether", False)
    assert libidx.is_hidden_name("vibemenu", True)
    assert libidx.is_hidden_name("VibeMenu", True)
    assert libidx.is_hidden_name("openmenu", True)
    assert libidx.is_hidden_name("OpenMenu", True)
    assert not libidx.is_hidden_name("vibemenu", False)   # clarification 7: root only


def test_fingerprint_cases_fixture():
    spec = json.loads((FIX / "fingerprint_cases.json").read_text(encoding="utf-8"))
    assert len(spec["cases"]) >= 3
    for c in spec["cases"]:
        assert libidx.card_fingerprint(c["names"]) == c["expect"], c


def test_scan_tree_matches_golden():
    t = json.loads((FIX / "scan_tree.json").read_text(encoding="utf-8"))
    keys, aliases = _db()
    r = scan(TreeSource(t["tree"], t["sysids"], t["cues"]), keys, aliases, t["build_date"])
    assert r.data == (FIX / "scan_tree.library.idx").read_bytes()
    assert [f.path for f in r.folders] == [
        "", "Deep", "Empty", "Homebrew", "RPG", "Racing",
        "Deep/L2", "RPG/Grandia", "Deep/L2/L3", "Deep/L2/L3/L4", "Deep/L2/L3/L4/L5",
        "Deep/L2/L3/L4/L5/L6", "Deep/L2/L3/L4/L5/L6/L7", "Deep/L2/L3/L4/L5/L6/L7/L8",
    ]
    assert [(e.folder_id, e.path, e.key) for e in r.entries] == [
        (0, "loose.iso", "T-8113H"),
        # "Homebrew/openmenu" (a non-root game directory) and "Homebrew/autoboot.cue" (a
        # non-root loose image) pin that clarification 6's §4 hidden-name set applies at
        # the root only (clarification 7), unlike the root-level "openmenu" and
        # "autoboot.iso" dropped from the fingerprint/scan above.
        (3, "Homebrew/a.iso", ""), (3, "Homebrew/autoboot.cue", ""), (3, "Homebrew/b.iso", ""),
        (3, "Homebrew/openmenu/game.iso", ""),
        (5, "Racing/Daytona USA/daytona.cue", "GS-9001"), (5, "Racing/Sega Rally/rally.iso", ""),
        # 2026-11-23 (Satiator Rings' model): two images make RPG/Grandia a folder row, its discs its own entries
        (7, "RPG/Grandia/disc1.cue", "T-4507G"), (7, "RPG/Grandia/disc2.cue", "T-4507G"),
    ]
    assert r.entries[5].has_box and not r.entries[6].has_box
    assert [e.has_box for e in r.entries[1:5]] == [True, True, True, False]   # Homebrew's BOX.TGA; openmenu's none
    assert (r.entries[7].disc, r.entries[7].disc_total) == (1, 2)
    assert "satiator-rings-old" not in [f.path for f in r.folders]               # a hidden root prefix
    assert r.report["entries"] == 9 and r.report["matched"] == 4 and r.report["denominator"] == "9 images"
    # Deep/L2/.../L8/L9 is depth 9: not entered, counted once in skipped_folders.
    assert r.report["skipped_folders"] == 1
    assert "Deep/L2/L3/L4/L5/L6/L7/L8/L9" not in [f.path for f in r.folders]


def test_depth_limit_skips_folder_and_counts_it():
    tree = {
        "": {"dirs": ["d1"], "files": []},
        "d1": {"dirs": ["d2"], "files": []},
        "d1/d2": {"dirs": ["d3"], "files": []},
        "d1/d2/d3": {"dirs": ["d4"], "files": []},
        "d1/d2/d3/d4": {"dirs": ["d5"], "files": []},
        "d1/d2/d3/d4/d5": {"dirs": ["d6"], "files": []},
        "d1/d2/d3/d4/d5/d6": {"dirs": ["d7"], "files": []},
        "d1/d2/d3/d4/d5/d6/d7": {"dirs": ["d8"], "files": []},
        "d1/d2/d3/d4/d5/d6/d7/d8": {"dirs": ["d9"], "files": []},
        "d1/d2/d3/d4/d5/d6/d7/d8/d9": {"dirs": [], "files": []},
    }
    keys, aliases = _db()
    r = scan(TreeSource(tree, {}), keys, aliases, 20261117)
    paths = [f.path for f in r.folders]
    assert "d1/d2/d3/d4/d5/d6/d7/d8" in paths  # depth 8: entered
    assert "d1/d2/d3/d4/d5/d6/d7/d8/d9" not in paths  # depth 9: not entered
    assert r.report["skipped_folders"] == 1


def test_folder_cap_respects_small_max_and_skips_contents():
    tree = {
        "": {"dirs": ["A", "B"], "files": []},
        "A": {"dirs": [], "files": []},
        "B": {"dirs": ["C"], "files": []},
        "B/C": {"dirs": [], "files": ["x.iso"]},
    }
    keys, aliases = _db()
    r = scan(TreeSource(tree, {}), keys, aliases, 20261117, max_folders=2)
    assert [f.path for f in r.folders] == ["", "A"]
    assert r.report["skipped_folders"] == 1
    assert r.entries == []  # B's contents (B/C/x.iso) are skipped along with B


def test_fs_source_reads_real_files(tmp_path):
    g = tmp_path / "Game"
    g.mkdir()
    (g / "game.iso").write_bytes(make_system_id(product="GS-9001", area="JTUE", device="CD-1/1", title="X") + b"\0" * 2048)
    (g / "BOX.TGA").write_bytes(b"x")
    (tmp_path / "menu.bin").write_bytes(b"m")
    keys, aliases = _db()
    r = scan(FsSource(tmp_path), keys, aliases, 20261117)
    assert [(e.path, e.key, e.has_box) for e in r.entries] == [("Game/game.iso", "GS-9001", True)]
    assert r.fingerprint == libidx.card_fingerprint(["Game", "menu.bin"])


def test_cue_referenced_image_is_not_a_second_game():
    """2026-11-23 (clarification 7, the user's card: LARRY 6 listed "game" twice, from game.cue and game.iso): an image
    named by a sibling .cue's FILE lines (ASCII case-insensitively) is part of that cue's game. Measured on the G:
    mirror: 42 of its 842 images are such .iso files."""
    tree = {
        "": {"dirs": ["LARRY 1", "LARRY 6", "Two"], "files": []},
        "LARRY 1": {"dirs": [], "files": ["Leisure Suit Larry 1 Saturn.cue", "Leisure Suit Larry 1 Saturn.bin", "BOX.TGA"]},
        "LARRY 6": {"dirs": ["OLD"], "files": ["game.cue", "GAME.iso", "BOX.TGA"]},
        "LARRY 6/OLD": {"dirs": [], "files": ["lsl6.cue", "lsl6.bin"]},
        "Two": {"dirs": [], "files": ["x.cue", "x.iso", "y.iso"]},
    }
    cues = {"LARRY 1/Leisure Suit Larry 1 Saturn.cue": ["Leisure Suit Larry 1 Saturn.bin"],
            "LARRY 6/game.cue": ["game.iso"], "LARRY 6/OLD/lsl6.cue": ["lsl6.bin"], "Two/x.cue": ["x.iso"]}
    sid = {"product": "HB-0001", "area": "U", "device": "CD-1/1", "title": "T"}
    sysids = {p: sid for p in ("LARRY 1/Leisure Suit Larry 1 Saturn.cue", "LARRY 6/game.cue", "LARRY 6/GAME.iso",
                               "LARRY 6/OLD/lsl6.cue", "Two/x.cue", "Two/x.iso")}
    sysids["Two/y.iso"] = {"product": "HB-0002", "area": "U", "device": "CD-1/1", "title": "Y"}
    r = scan(TreeSource(tree, sysids, cues), [], {}, 20261123)
    assert [f.path for f in r.folders] == ["", "LARRY 6", "Two"]
    assert [(e.folder_id, e.path, e.has_box) for e in r.entries] == [
        (0, "LARRY 1/Leisure Suit Larry 1 Saturn.cue", True),                # LARRY 1: a single-game folder
        (1, "LARRY 6/OLD/lsl6.cue", False), (1, "LARRY 6/game.cue", True),    # GAME.iso is game.cue's own data;
                                                                              # OLD: a single-game folder in LARRY 6
        (2, "Two/x.cue", False), (2, "Two/y.iso", False)]                     # x.iso dropped; two games: a folder
    assert r.report["entries"] == 5
    # without the cue's FILE names the old rule lists GAME.iso as a second game
    assert len(scan(TreeSource(tree, sysids), [], {}, 20261123).entries) == 7


def test_fs_source_reads_cue_file_lines(tmp_path):
    (tmp_path / "g").mkdir()
    (tmp_path / "g" / "game.cue").write_bytes(b'REM x\r\n  file "sub\\game.iso" BINARY\r\nFILE track02.bin BINARY\n'
                                              b'FILE "broken.bin\nFILE ok.iso BINARY\n')
    from omsat.cardscan import FsSource
    assert FsSource(tmp_path).cue_files("g/game.cue") == ["game.iso", "track02.bin", "ok.iso"]
