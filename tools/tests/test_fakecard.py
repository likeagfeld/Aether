"""Plan D6: the synthetic card the --app-fake build embeds is valid, self-consistent and small enough."""
import struct

from omsat import fakecard
from omsat.check import check_bytes
from omsat.libidx import card_fingerprint, read_libidx


def _unpack(blob: bytes) -> dict[str, bytes]:
    assert blob[:8] == b"OMFAKEFS"
    n, = struct.unpack_from(">I", blob, 8)
    out = {}
    for i in range(n):
        row = blob[16 + 264 * i:16 + 264 * (i + 1)]
        path = row[:256].rstrip(b"\0").decode()
        off, size = struct.unpack_from(">II", row, 256)
        out[path] = blob[off:off + size]
    return out


def test_card_files_validate_and_match():
    files = fakecard.build()
    for name in ("aether/GAMES.DB", "aether/LIBRARY.IDX"):
        assert check_bytes(files[name]) == [], name
    ents, folders, fp = read_libidx(files["aether/LIBRARY.IDX"])
    roots = sorted({p.split("/")[0] for p in files})
    assert fp == card_fingerprint(roots)                              # the menu will not rescan
    paths = {e.path for e in ents}
    assert all(p in files for p in paths)                             # every entry's image exists
    for name in ("aether/FAVS.TXT", "aether/RECENT.TXT"):
        lines = files[name].decode().splitlines()
        assert lines and all(x in paths for x in lines), name
    assert len(ents) == 40 and len(folders) == 5


def test_blob_round_trips_deterministic_and_fits():
    a, b = fakecard.pack(fakecard.build()), fakecard.pack(fakecard.build())
    assert a == b
    assert len(a) <= fakecard.MAX_BLOB, len(a)
    assert _unpack(a) == fakecard.build()


def test_settings_script_marker_only_with_flag():
    """vibemenu/FAKESCRIPT.SETTINGS selects the Settings pad script (menu/app/fake/fakepad.c); it is
    present only when --settings-script (build's settings_script=True) is given."""
    assert fakecard.SETTINGS_MARKER not in fakecard.build()
    assert fakecard.SETTINGS_MARKER in fakecard.build(settings_script=True)


def test_settings_ini_flag_replaces_settings_only():
    """--settings-ini sets the card's SETTINGS.INI (the safe-area captures); the default card is unchanged."""
    assert fakecard.build()["aether/SETTINGS.INI"] == b"theme=saturn\nclock=24\nreselect=off\n"
    got = fakecard.build(settings_ini="overscan=24\ntheme=saturn")
    assert got["aether/SETTINGS.INI"] == b"overscan=24\ntheme=saturn\n"
    assert set(got) == set(fakecard.build())


def test_settings_ini_cli_unescapes_newlines(tmp_path):
    out = tmp_path / "FAKECARD.BIN"
    assert fakecard.main(["--out", str(out), "--settings-ini", "overscan=24\\ntheme=saturn"]) == 0
    assert b"overscan=24\ntheme=saturn\n" in out.read_bytes()


def test_main_rejects_an_oversized_blob(monkeypatch, tmp_path, capsys):
    """review-9.md: the MAX_BLOB-exceeded branch in main() had no test; the real card (17,672 B) never hits
    it, so force it with a MAX_BLOB too small for the real build()/pack() output."""
    monkeypatch.setattr(fakecard, "MAX_BLOB", 0)
    out = tmp_path / "FAKECARD.BIN"
    assert fakecard.main(["--out", str(out)]) == 1
    assert not out.exists()
    assert "exceeds MAX_BLOB" in capsys.readouterr().out


def test_user_card(tmp_path):
    """--user-card: a real card's LIBRARY.IDX/GAMES.DB with stand-in images (cue + System ID sector), consistent."""
    from omsat.cardscan import FsSource, scan
    from omsat.gamesdb import read_gamesdb
    from omsat.iphdr import make_system_id
    from pathlib import Path
    fix = Path(__file__).resolve().parents[2] / "contract" / "fixtures"
    db = (fix / "tiny.games.db").read_bytes()
    stage = tmp_path / "stage"
    d = stage / "Games" / "Grandia"
    d.mkdir(parents=True)
    (d / "Grandia.cue").write_text('FILE "Grandia.bin" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
    (d / "Grandia.bin").write_bytes(make_system_id(product="T-4507G", device="CD-1/1", area="U", title="T",
                                                   release_date="19971108") + b"\0" * 1792)
    recs, _, aliases = read_gamesdb(db)
    res = scan(FsSource(stage), [r.key for r in recs], aliases, 20261120)
    (tmp_path / "LIBRARY.IDX").write_bytes(res.data)
    (tmp_path / "GAMES.DB").write_bytes(db)
    files = fakecard.user_card(tmp_path, stage)
    assert set(files) >= {"Games/Grandia/Grandia.cue", "Games/Grandia/Grandia.bin"}
    for name in ("aether/GAMES.DB", "aether/LIBRARY.IDX"):
        assert check_bytes(files[name]) == [], name
    ents, _, fp = read_libidx(files["aether/LIBRARY.IDX"])
    assert fp == card_fingerprint(sorted({p.split("/")[0] for p in files}))
    assert [r.key for r in read_gamesdb(files["aether/GAMES.DB"])[0]] == ["T-4507G"]
    assert len(fakecard.pack(files)) <= fakecard.MAX_BLOB


def test_fw_flag_writes_the_firmware_marker(tmp_path):
    """--fw TEXT is what the fake s_get_fw_version returns (READFAIL = the read-failure path); without it
    there is no marker, so the fake firmware reads a passing "v163"."""
    assert "aether/FAKESCRIPT.FW" not in fakecard.build()
    assert fakecard.build(fw="v158")["aether/FAKESCRIPT.FW"] == b"v158"
    out = tmp_path / "FAKECARD.BIN"
    assert fakecard.main(["--out", str(out), "--fw", "READFAIL"]) == 0
    assert b"aether/FAKESCRIPT.FW" in out.read_bytes() and b"READFAIL" in out.read_bytes()


def test_recent_order_and_the_tour_keeps_its_root_start(tmp_path):
    """the default card turns Last game off (the frozen tour and the scripts start at the root, as their
    steps assume); --recent picks RECENT.TXT's order by game number, so a capture can start inside a folder."""
    games = fakecard._games()
    assert b"reselect=off" in fakecard.build()["aether/SETTINGS.INI"]
    assert fakecard.build()["aether/RECENT.TXT"].decode().splitlines() == [games[i][6] for i in (7, 3, 20, 1)]
    got = fakecard.build(recent=(16, 7))["aether/RECENT.TXT"].decode().splitlines()
    assert got == [games[16][6], games[7][6]] and got[0].startswith("Shooters/Classic/")
    out = tmp_path / "FAKECARD.BIN"
    assert fakecard.main(["--out", str(out), "--recent", "16,7"]) == 0
    assert games[16][6].encode() in out.read_bytes()


def test_idle_script_marker(tmp_path):
    """--idle-script selects the screensaver pad script (fakepad.c k_idle); combine with saver=1."""
    assert fakecard.IDLE_MARKER not in fakecard.build()
    assert fakecard.IDLE_MARKER in fakecard.build(idle=True)
    out = tmp_path / "FAKECARD.BIN"
    assert fakecard.main(["--out", str(out), "--idle-script", "--settings-ini", "saver=1\nreselect=off"]) == 0
    assert fakecard.IDLE_MARKER.encode() in out.read_bytes() and b"saver=1\nreselect=off\n" in out.read_bytes()
