import hashlib
import shutil
from pathlib import Path

import pytest

from omsat.install import install
from omsat.lnchhdr import make_header


def _setup(tmp_path):
    t = tmp_path / "sd"
    t.mkdir()
    (t / "menu.bin").write_bytes(b"official menu bytes")
    (t / "autoboot.iso").write_bytes(b"rings")
    (t / "autoboot.cue").write_bytes(b"cue")
    data = tmp_path / "data"
    data.mkdir()
    for n in ("GAMES.DB", "ART.PAK", "LIBRARY.IDX"):
        (data / n).write_bytes(n.encode())
    iso = tmp_path / "new.iso"
    iso.write_bytes(b"our menu")
    body = b"\x09\x00" * 64
    lb = tmp_path / "LAUNCHER.BIN"
    lb.write_bytes(make_header(body) + body)
    return t, iso, lb, data


def test_install_backs_up_and_never_touches_menu_bin(tmp_path):
    t, iso, lb, data = _setup(tmp_path)
    h = hashlib.sha256((t / "menu.bin").read_bytes()).hexdigest()
    rep = install(t, iso, lb, data, stamp="S1")
    assert (t / "autoboot.iso").read_bytes() == b"our menu"
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"rings"
    assert (t / "autoboot.cue").read_bytes() == b"cue"          # only autoboot.iso is ever backed up
    assert not (t / "autoboot.cue.bak-S1").exists()
    assert sorted(p.name for p in (t / "aether").iterdir()) == ["ART.PAK", "GAMES.DB", "LAUNCHER.BIN", "LIBRARY.IDX",
                                                                     "SAVES", "VRTILES.BIN"]   # M6 C3
    assert not any((t / "aether" / "SAVES").iterdir())   # empty: Settings > Import Saves writes IMPORT.SEL there
    assert hashlib.sha256((t / "menu.bin").read_bytes()).hexdigest() == h
    assert rep["backups"] == ["autoboot.iso.bak-S1"] and rep["menu_bin_unchanged"] is True
    install(t, iso, lb, data, stamp="S2")                      # re-install: backs up ours, not the .bak files
    assert (t / "autoboot.iso.bak-S2").read_bytes() == b"our menu"
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"rings"


def test_bad_launcher_is_refused_before_anything_changes(tmp_path):
    t, iso, lb, data = _setup(tmp_path)
    lb.write_bytes(b"garbage" * 1000)
    with pytest.raises(SystemExit):
        install(t, iso, lb, data, stamp="S1")
    assert (t / "autoboot.iso").read_bytes() == b"rings"


def test_refuses_when_menu_bin_would_be_overwritten(tmp_path, monkeypatch):
    """Simulate a filesystem bug (or a future code change) that corrupts menu.bin partway
    through an install: the installer must fail loudly rather than let it through, per
    spec §4/§8.1 item 5 ("fail loudly if menu.bin's SHA-256 changed")."""
    import omsat.install as install_mod

    t, iso, lb, data = _setup(tmp_path)
    real_copyfile = shutil.copyfile

    def sabotaging_copyfile(src, dst):
        real_copyfile(src, dst)
        if Path(dst).name == "autoboot.iso.omsat-new":   # the staged name (stage, then rename)
            (t / "menu.bin").write_bytes(b"corrupted during install")

    monkeypatch.setattr(install_mod.shutil, "copyfile", sabotaging_copyfile)
    with pytest.raises(SystemExit, match="menu.bin changed"):
        install(t, iso, lb, data, stamp="S1")
    assert (t / "menu.bin").read_bytes() == b"corrupted during install"  # the damage is real; raising is the point


# --- Fix round 1 -------------------------------------------------------------


def test_backup_name_collision_gets_a_unique_name_and_is_never_re_backed_up(tmp_path):
    """A pre-existing backup whose name would collide with <name>.bak-<stamp> must not be
    overwritten (which `Path.rename` would otherwise refuse, or silently clobber on some
    platforms): our own backup gets a uniquified name instead. A later re-install must never
    re-back-up any `.bak-` file, including the uniquified one."""
    t, iso, lb, data = _setup(tmp_path)
    (t / "autoboot.iso.bak-S1").write_bytes(b"older backup")
    rep = install(t, iso, lb, data, stamp="S1")
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"older backup"       # untouched
    assert (t / "autoboot.iso.bak-S1.1").read_bytes() == b"rings"           # ours, uniquified
    assert "autoboot.iso.bak-S1.1" in rep["backups"]
    install(t, iso, lb, data, stamp="S2")            # re-install: no .bak- file is ever re-backed-up
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"older backup"
    assert (t / "autoboot.iso.bak-S1.1").read_bytes() == b"rings"
    assert (t / "autoboot.iso.bak-S2").read_bytes() == b"our menu"


def test_sabotaged_rename_midway_through_backups_rolls_back_completely(tmp_path, monkeypatch):
    """The backup and the placements are planned up front and performed as a unit: if any rename
    in the middle of the batch fails (the second one: placing the new autoboot.iso), every rename already done is undone and the card is left
    exactly as it was, with a named error."""
    import omsat.install as install_mod

    t, iso, lb, data = _setup(tmp_path)
    before = {p.name: p.read_bytes() for p in t.iterdir() if p.is_file()}
    real_rename = install_mod._rename
    calls = {"n": 0}

    def sabotaged(src, dst):
        calls["n"] += 1
        if calls["n"] == 2:            # fails the second forward rename; rollback calls succeed
            raise OSError("simulated rename failure")
        real_rename(src, dst)

    monkeypatch.setattr(install_mod, "_rename", sabotaged)
    with pytest.raises(SystemExit, match="install failed, card restored"):
        install(t, iso, lb, data, stamp="S1")
    after = {p.name: p.read_bytes() for p in t.iterdir() if p.is_file()}
    assert after == before           # the card is exactly as it was: no stray .bak- files, nothing renamed
    assert not (t / "aether").exists()


def test_missing_data_file_is_a_named_error_before_anything_changes(tmp_path):
    t, iso, lb, data = _setup(tmp_path)
    (data / "ART.PAK").unlink()
    with pytest.raises(SystemExit, match="ART.PAK"):
        install(t, iso, lb, data, stamp="S1")
    assert (t / "autoboot.iso").read_bytes() == b"rings"
    assert (t / "autoboot.cue").exists()
    assert not (t / "aether").exists()


# --- Fix round 2 (transactional install: stage, commit, undo-on-failure) ---------------------


def _snapshot(t):
    return {str(p.relative_to(t)): p.read_bytes() for p in t.rglob("*") if p.is_file()}


def _no_transient_files_left(t):
    return not any(p.name.endswith(".omsat-new") or p.name.endswith(".omsat-old") for p in t.rglob("*"))


def test_copy_failure_during_staging_leaves_card_byte_identical(tmp_path, monkeypatch):
    """Staging copies every new file under a transient name before anything old is touched.
    A copy failure partway through staging must remove what was staged and leave the card
    byte-identical -- nothing old was ever renamed."""
    import omsat.install as install_mod

    t, iso, lb, data = _setup(tmp_path)
    before = _snapshot(t)
    real_copyfile = shutil.copyfile

    def sabotaged_copyfile(src, dst):
        if Path(dst).name == "ART.PAK.omsat-new":
            raise OSError("simulated disk full")
        real_copyfile(src, dst)

    monkeypatch.setattr(install_mod.shutil, "copyfile", sabotaged_copyfile)
    with pytest.raises(SystemExit, match=r"install failed while staging: aether/ART\.PAK"):
        install(t, iso, lb, data, stamp="S1")
    assert _snapshot(t) == before
    assert _no_transient_files_left(t)
    assert not (t / "aether").exists()         # created fresh by this attempt, so removed again


def test_failure_at_nth_commit_rename_leaves_card_restored(tmp_path, monkeypatch):
    """A failure deep into the commit sequence (the very last rename: placing LAUNCHER.BIN,
    after the autoboot.iso backup and placement and three aether/ placements have already committed)
    must undo everything already done and leave the card exactly as it was."""
    import omsat.install as install_mod

    t, iso, lb, data = _setup(tmp_path)
    before = _snapshot(t)
    real_rename = install_mod._rename
    failed_once = {"done": False}

    def sabotaged(src, dst):
        if not failed_once["done"] and dst.name == "LAUNCHER.BIN" and src.name.endswith(".omsat-new"):
            failed_once["done"] = True
            raise OSError("simulated rename failure")
        real_rename(src, dst)

    monkeypatch.setattr(install_mod, "_rename", sabotaged)
    with pytest.raises(SystemExit, match="install failed, card restored"):
        install(t, iso, lb, data, stamp="S1")
    assert _snapshot(t) == before
    assert _no_transient_files_left(t)
    assert not (t / "aether").exists()


def test_undo_failure_produces_rollback_incomplete_with_stranded_files(tmp_path, monkeypatch):
    """If an undo itself fails, the remaining undos must still be attempted (one stuck file
    never stops the rest), and the final error must name exactly what is stranded and how to
    fix it by hand."""
    import omsat.install as install_mod

    t, iso, lb, data = _setup(tmp_path)
    real_rename = install_mod._rename

    def sabotaged(src, dst):
        if dst.name == "ART.PAK" and src.name.endswith(".omsat-new"):
            raise OSError("simulated commit failure")             # fails the ART.PAK placement
        if src.name == "autoboot.iso.bak-S1" and dst.name == "autoboot.iso":
            raise OSError("simulated undo failure")                # fails undoing the iso backup
        real_rename(src, dst)

    monkeypatch.setattr(install_mod, "_rename", sabotaged)
    with pytest.raises(SystemExit, match="install failed AND rollback incomplete") as ei:
        install(t, iso, lb, data, stamp="S1")
    msg = str(ei.value)
    assert "autoboot.iso.bak-S1" in msg and "autoboot.iso" in msg
    assert "manual fix" in msg.lower()
    # the stranded file is really left in its backed-up state -- not restored, not deleted
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"rings"
    assert not (t / "autoboot.iso").exists()
    # everything else that *could* be undone was: the new autoboot.iso and GAMES.DB placements
    # both rolled back cleanly before the stuck undo was even attempted; autoboot.cue was never touched
    assert not (t / "aether" / "GAMES.DB").exists()
    assert (t / "autoboot.cue").read_bytes() == b"cue"


def test_happy_path_run_twice_gives_unique_backups_and_correct_final_state(tmp_path):
    t, iso, lb, data = _setup(tmp_path)
    rep1 = install(t, iso, lb, data, stamp="S1")
    assert rep1["backups"] == ["autoboot.iso.bak-S1"]
    assert (t / "autoboot.iso").read_bytes() == b"our menu"
    assert (t / "aether" / "GAMES.DB").read_bytes() == b"GAMES.DB"
    assert (t / "aether" / "LAUNCHER.BIN").read_bytes() == lb.read_bytes()
    assert _no_transient_files_left(t)

    iso.write_bytes(b"our menu v2")
    (data / "GAMES.DB").write_bytes(b"GAMES.DB v2")
    rep2 = install(t, iso, lb, data, stamp="S2")
    assert rep2["backups"] == ["autoboot.iso.bak-S2"]
    assert (t / "autoboot.iso").read_bytes() == b"our menu v2"
    assert (t / "autoboot.iso.bak-S1").read_bytes() == b"rings"            # original Rings iso, untouched
    assert (t / "autoboot.iso.bak-S2").read_bytes() == b"our menu"         # first install's autoboot.iso
    assert (t / "aether" / "GAMES.DB").read_bytes() == b"GAMES.DB v2"
    assert _no_transient_files_left(t)


def test_old_openmenu_folder_is_left_alone_and_reported(tmp_path, capsys):
    """a card carrying the project's old /openmenu/ folder must not have it touched --
    not deleted, not migrated -- by an install that now writes /aether/. The CLI prints one
    line saying the old folder is no longer used."""
    t, iso, lb, data = _setup(tmp_path)
    old = t / "openmenu"
    old.mkdir()
    (old / "GAMES.DB").write_bytes(b"old project's GAMES.DB")

    from omsat.install import main as install_main
    rc = install_main(["--target", str(t), "--iso", str(iso), "--launcher", str(lb), "--data", str(data)])
    assert rc == 0
    assert (old / "GAMES.DB").read_bytes() == b"old project's GAMES.DB"      # untouched, not migrated
    assert (t / "aether" / "GAMES.DB").read_bytes() == b"GAMES.DB"         # the new folder, written normally
    out = capsys.readouterr().out
    assert "/openmenu/" in out and "no longer used" in out


def test_no_stray_notice_when_there_is_no_old_openmenu_folder(tmp_path, capsys):
    t, iso, lb, data = _setup(tmp_path)
    from omsat.install import main as install_main
    rc = install_main(["--target", str(t), "--iso", str(iso), "--launcher", str(lb), "--data", str(data)])
    assert rc == 0
    assert "no longer used" not in capsys.readouterr().out


def test_main_reports_oserror_without_a_bare_traceback(tmp_path, monkeypatch, capsys):
    """A defensive net at the CLI entry point: any OSError that escapes install() (a bug, or a
    genuinely unhandled filesystem failure) must be reported by name, never a raw traceback."""
    import omsat.install as install_mod

    def boom(*a, **k):
        raise OSError("simulated unhandled failure")

    monkeypatch.setattr(install_mod, "install", boom)
    rc = install_mod.main(["--target", str(tmp_path), "--iso", "x", "--launcher", "y", "--data", "z"])
    assert rc == 1
    assert "install failed" in capsys.readouterr().err


def test_install_loadbg_task13c(tmp_path):
    """LOADBG.BIN goes to /aether with the rest, transactionally; a missing one is refused up front."""
    t, iso, lb, data = _setup(tmp_path)
    bg = tmp_path / "LOADBG.BIN"
    bg.write_bytes(b"OMSATLBG test bytes")
    rep = install(t, iso, lb, data, stamp="S1", loadbg=bg)
    assert (t / "aether" / "LOADBG.BIN").read_bytes() == bg.read_bytes()
    assert "LOADBG.BIN" in rep["copied"]
    with pytest.raises(SystemExit):
        install(t, iso, lb, data, stamp="S2", loadbg=tmp_path / "missing.bin")
    assert (t / "aether" / "LOADBG.BIN").read_bytes() == bg.read_bytes()


def test_install_music_task1415(tmp_path):
    """BGM.MUS goes to /aether/MUSIC/ (created), transactionally; optional; a missing one is refused."""
    t, iso, lb, data = _setup(tmp_path)
    rep = install(t, iso, lb, data, stamp="S0")
    assert not (t / "aether" / "MUSIC").exists() and "MUSIC/BGM.MUS" not in rep["copied"]
    mus = tmp_path / "BGM.MUS"
    mus.write_bytes(b"OMSATMUS test bytes")
    rep = install(t, iso, lb, data, stamp="S1", music=mus)
    assert (t / "aether" / "MUSIC" / "BGM.MUS").read_bytes() == mus.read_bytes()
    assert "MUSIC/BGM.MUS" in rep["copied"]
    with pytest.raises(SystemExit):
        install(t, iso, lb, data, stamp="S2", music=tmp_path / "missing.mus")
    assert (t / "aether" / "MUSIC" / "BGM.MUS").read_bytes() == mus.read_bytes()


def test_install_theme_task16(tmp_path, monkeypatch):
    """--theme NAME=DIR puts THEME.INI (+ BG.BIN) in /aether/THEMES/NAME, inside the same transaction; a
    failed commit leaves no THEMES folder behind; no theme, no THEMES folder."""
    import omsat.install as inst
    t, iso, lb, data = _setup(tmp_path)
    rep = install(t, iso, lb, data, stamp="S0")
    assert not (t / "aether" / "THEMES").exists()
    th = tmp_path / "SAMPLE"
    th.mkdir()
    (th / "THEME.INI").write_bytes(b"[theme]\nname=SAMPLE\n")
    (th / "BG.BIN").write_bytes(b"OMSATBG test")
    rep = install(t, iso, lb, data, stamp="S1", themes={"SAMPLE": th})
    assert (t / "aether" / "THEMES" / "SAMPLE" / "BG.BIN").read_bytes() == b"OMSATBG test"
    assert {"THEMES/SAMPLE/THEME.INI", "THEMES/SAMPLE/BG.BIN"} <= set(rep["copied"])
    shutil.rmtree(t / "aether" / "THEMES")
    real = inst._rename
    n = {"i": 0}

    def flaky(src, dst):
        n["i"] += 1
        if n["i"] == 4:
            raise OSError("injected")
        real(src, dst)
    monkeypatch.setattr(inst, "_rename", flaky)
    with pytest.raises(SystemExit, match="card restored"):
        install(t, iso, lb, data, stamp="S2", themes={"SAMPLE": th})
    assert not (t / "aether" / "THEMES").exists()
    with pytest.raises(SystemExit):
        install(t, iso, lb, data, stamp="S3", themes={"bad/name": th})


def test_install_vrtiles_m6_c3(tmp_path):
    """VRTILES.BIN (the virtual root folders' tiles, out of HWRAM) is part of every install, by default the
    committed menu/app/assets/VRTILES.BIN; a missing one is refused before anything changes."""
    from omsat.install import VRTILES_ASSET
    t, iso, lb, data = _setup(tmp_path)
    rep = install(t, iso, lb, data, stamp="S0")
    assert (t / "aether" / "VRTILES.BIN").read_bytes() == VRTILES_ASSET.read_bytes()
    assert "VRTILES.BIN" in rep["copied"]
    with pytest.raises(SystemExit, match="missing"):
        install(t, iso, lb, data, stamp="S1", vrtiles=tmp_path / "missing.bin")
    assert (t / "aether" / "VRTILES.BIN").read_bytes() == VRTILES_ASSET.read_bytes()


# after a fully committed install, deleting a moved-aside original that is read-only raised
# PermissionError, and the CLI then said "install failed" (exit 1) although the new files were all in place.

@pytest.mark.skipif(__import__("sys").platform != "win32", reason="Windows read-only attribute semantics")
def test_read_only_old_file_is_replaced_and_its_held_copy_removed(tmp_path, capsys):
    import os
    import stat
    from omsat.install import HOLD_SUFFIX, main as install_main
    t, iso, lb, data = _setup(tmp_path)
    install(t, iso, lb, data, stamp="S1")
    db = t / "aether" / "GAMES.DB"
    os.chmod(db, stat.S_IREAD)                                  # FAT/exFAT cards keep a read-only attribute
    (data / "GAMES.DB").write_bytes(b"GAMES.DB v2")
    rc = install_main(["--target", str(t), "--iso", str(iso), "--launcher", str(lb), "--data", str(data)])
    cap = capsys.readouterr()
    assert rc == 0 and "install failed" not in cap.err
    assert db.read_bytes() == b"GAMES.DB v2"
    assert not (t / "aether" / ("GAMES.DB" + HOLD_SUFFIX)).exists()
    assert _no_transient_files_left(t)


def test_undeletable_held_copy_is_a_warning_not_a_failure(tmp_path, monkeypatch, capsys):
    import json
    import omsat.install as install_mod
    t, iso, lb, data = _setup(tmp_path)
    install(t, iso, lb, data, stamp="S1")
    (data / "GAMES.DB").write_bytes(b"GAMES.DB v2")
    real_unlink = Path.unlink

    def stuck(self, missing_ok=False):
        if self.name.endswith(install_mod.HOLD_SUFFIX):
            raise PermissionError(13, "Access is denied", str(self))
        return real_unlink(self, missing_ok=missing_ok)
    monkeypatch.setattr(Path, "unlink", stuck)
    rc = install_mod.main(["--target", str(t), "--iso", str(iso), "--launcher", str(lb), "--data", str(data)])
    cap = capsys.readouterr()
    assert rc == 0 and "install failed" not in cap.err
    assert (t / "aether" / "GAMES.DB").read_bytes() == b"GAMES.DB v2"     # the install committed
    held = t / "aether" / ("GAMES.DB" + install_mod.HOLD_SUFFIX)
    assert held.exists() and str(held) in cap.err and "warning" in cap.err.lower()
    rep = json.loads(cap.out[cap.out.index("{"):])
    assert str(held) in rep["leftovers"][0]


def test_a_card_from_before_the_aether_rename_is_refused_untouched(tmp_path):
    """The Aether rename: /vibemenu holds the user's saves; installing beside it would leave two folders, so the
    install refuses (the Card Manager's Update card moves it) and writes nothing."""
    t, iso, lnc, data = _setup(tmp_path)
    (t / "vibemenu" / "SAVES").mkdir(parents=True)
    (t / "vibemenu" / "SAVES" / "ACTIVE.TXT").write_text("SAVES/T-1 1 1\n")
    before = sorted(p.relative_to(t).as_posix() for p in t.rglob("*"))
    with pytest.raises(SystemExit, match="before the Aether rename"):
        install(t, iso, lnc, data)
    assert sorted(p.relative_to(t).as_posix() for p in t.rglob("*")) == before
    assert not (t / "aether").exists()


# 2026-11-25, the user's card (as the Card Manager's RootFilesTests): an install replaces /autoboot.iso only. The old one
# is kept as autoboot.iso.bak-<stamp>; every other root file -- autoboot.rings.iso (the Rings menu their swap scripts
# use), autobootdesc.txt (a Satiator autoboot descriptor), autoboot.cue/.elf, AUTOBOOT.TXT, menu.bin -- keeps its name
# and its bytes, over a first install and a re-install.

def test_install_then_reinstall_keeps_every_other_root_file_by_name_and_bytes(tmp_path):
    t, iso, lb, data = _setup(tmp_path)
    (t / "autoboot.cue").unlink()
    theirs = {
        "autoboot.rings.iso": b"the Rings menu, swapped in by the user's scripts",
        "autobootdesc.txt": b"Satiator autoboot descriptor\r\n",
        "autoboot.cue": b'FILE "autoboot.bin" BINARY\n',
        "autoboot.elf": b"\x7fELF",
        "AUTOBOOT.TXT": b"notes",
        "menu.bin": b"the official menu",
    }
    for n, b in theirs.items():
        (t / n).write_bytes(b)
    (t / "autoboot.iso").write_bytes(b"Satiator Rings")

    rep1 = install(t, iso, lb, data, stamp="s1")
    rep2 = install(t, iso, lb, data, stamp="s2")

    for n, b in theirs.items():
        assert (t / n).read_bytes() == b, n
    root = sorted(p.name for p in t.iterdir() if p.is_file())
    assert root == sorted(list(theirs) + ["autoboot.iso", "autoboot.iso.bak-s1", "autoboot.iso.bak-s2"])
    assert (t / "autoboot.iso.bak-s1").read_bytes() == b"Satiator Rings"
    assert (t / "autoboot.iso").read_bytes() == iso.read_bytes()
    assert rep1["backups"] == ["autoboot.iso.bak-s1"]
    assert rep2["backups"] == ["autoboot.iso.bak-s2"]
    assert any("autoboot.elf" in w for w in rep1["warnings"])


def test_autoboot_elf_warning_is_printed_and_absent_without_one(tmp_path, capsys):
    from omsat.install import main as install_main
    t, iso, lb, data = _setup(tmp_path)
    args = ["--target", str(t), "--iso", str(iso), "--launcher", str(lb), "--data", str(data)]
    rep = install(t, iso, lb, data, stamp="S0")
    assert rep["warnings"] == []
    (t / "AutoBoot.ELF").write_bytes(b"\x7fELF")
    assert install_main(args) == 0
    err = capsys.readouterr().err
    assert "warning:" in err and "autoboot.elf" in err
    assert (t / "AutoBoot.ELF").read_bytes() == b"\x7fELF"
