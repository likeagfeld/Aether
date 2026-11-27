"""omsat.cardput writes only the release files, reads every one back, leaves the game folders alone, and never
replaces a /menu.bin it did not write."""
import hashlib
from pathlib import Path

import pytest

from omsat import cardput
from omsat.lnchhdr import make_header


def _release(tmp_path: Path) -> Path:
    rel = tmp_path / "release"
    (rel / "card").mkdir(parents=True)
    (rel / "THEMES" / "SAMPLE").mkdir(parents=True)
    for n in ("LIBRARY.IDX", "GAMES.DB", "ART.PAK"):
        (rel / "card" / n).write_bytes(n.encode())
    for n in ("autoboot.iso", "LOADBG.BIN", "VRTILES.BIN", "BGM.MUS", "THEMES/SAMPLE/THEME.INI", "THEMES/SAMPLE/BG.BIN"):
        (rel / n).write_bytes(n.encode())
    body = b"\x09\x00" * 64
    (rel / "LAUNCHER.BIN").write_bytes(make_header(body) + body)
    (rel / "menu.bin").write_bytes(b"official v71")
    return rel


def _card(tmp_path: Path) -> Path:
    card = tmp_path / "E"
    for d in ("Games/Gex (USA)", "XBAND/Touge XBAND H14"):
        (card / d).mkdir(parents=True)
        (card / d / "game.cue").write_bytes(b"cue")
    return card


def _run(card, rel, man, sha):
    return cardput.main(["--card", str(card), "--release", str(rel), "--version", "0.1", "--menu-url", "u",
                         "--menu-sha", sha, "--manifest", str(man)])


def test_put_twice_is_idempotent_and_leaves_games_alone(tmp_path):
    rel, card = _release(tmp_path), _card(tmp_path)
    sha = hashlib.sha256(b"official v71").hexdigest()
    man = tmp_path / "card_manifest.txt"
    assert _run(card, rel, man, sha) == 0
    assert (card / "menu.bin").read_bytes() == b"official v71"
    assert (card / "aether" / "MUSIC" / "BGM.MUS").read_bytes() == b"BGM.MUS"
    assert (card / "aether" / "THEMES" / "SAMPLE" / "BG.BIN").is_file()
    assert (card / "aether" / "VRTILES.BIN").read_bytes() == b"VRTILES.BIN"     # the root folders' tiles
    text = man.read_text(encoding="utf-8")
    assert text.count("  OK  ") == 11 and "MISMATCH" not in text and "Free space" in text
    before = sorted(p.relative_to(card).as_posix() for p in card.rglob("*"))
    assert _run(card, rel, man, sha) == 0                      # rerun: nothing new, no backups
    assert sorted(p.relative_to(card).as_posix() for p in card.rglob("*")) == before
    assert "install skipped" in man.read_text(encoding="utf-8")
    assert (card / "Games" / "Gex (USA)" / "game.cue").read_bytes() == b"cue"


def test_foreign_menu_bin_is_never_replaced(tmp_path):
    rel, card = _release(tmp_path), _card(tmp_path)
    (card / "menu.bin").write_bytes(b"someone else's menu")
    with pytest.raises(SystemExit, match="not replacing"):
        _run(card, rel, tmp_path / "m.txt", hashlib.sha256(b"official v71").hexdigest())
    assert (card / "menu.bin").read_bytes() == b"someone else's menu" and not (card / "aether").exists()


def test_zip_on_card_fails(tmp_path):
    rel, card = _release(tmp_path), _card(tmp_path)
    (card / "XBAND" / "old.zip").write_bytes(b"PK")
    with pytest.raises(SystemExit, match="zip"):
        _run(card, rel, tmp_path / "m.txt", hashlib.sha256(b"official v71").hexdigest())


BRIGHT = (b"name=BRIGHT TEST\nauthor=VibeMenu diag\ntext=FFFFFF\ntext_hi=FFE060\npanel=102040\npanel_alpha=60\n"
          b"cursor=FF8040\nbg_top=3070E0\nbg_bottom=C03060\n")


def test_retire_removes_the_known_diag_theme(tmp_path):
    assert hashlib.sha256(BRIGHT).hexdigest() == "c8e12acb7aeae5b630eb5b05c9d793f8e923b542d4959a4767fca220d414fa3b"
    rel, card = _release(tmp_path), _card(tmp_path)
    (card / "aether" / "THEMES" / "BRIGHTTEST").mkdir(parents=True)
    (card / "aether" / "THEMES" / "BRIGHTTEST" / "THEME.INI").write_bytes(BRIGHT)
    man = tmp_path / "m.txt"
    assert _run(card, rel, man, hashlib.sha256(b"official v71").hexdigest()) == 0
    assert not (card / "aether" / "THEMES" / "BRIGHTTEST").exists()
    assert "retired: aether/THEMES/BRIGHTTEST" in man.read_text(encoding="utf-8")


def test_retire_refuses_a_changed_folder(tmp_path):
    rel, card = _release(tmp_path), _card(tmp_path)
    d = card / "aether" / "THEMES" / "BRIGHTTEST"
    d.mkdir(parents=True)
    (d / "THEME.INI").write_bytes(BRIGHT + b"# mine\n")
    with pytest.raises(SystemExit, match="BRIGHTTEST"):
        _run(card, rel, tmp_path / "m.txt", hashlib.sha256(b"official v71").hexdigest())
    assert (d / "THEME.INI").is_file()


def test_retire_refuses_when_selected(tmp_path):
    rel, card = _release(tmp_path), _card(tmp_path)
    d = card / "aether" / "THEMES" / "BRIGHTTEST"
    d.mkdir(parents=True)
    (d / "THEME.INI").write_bytes(BRIGHT)
    (card / "aether" / "SETTINGS.INI").write_bytes(b"theme=BRIGHTTEST\n")
    with pytest.raises(SystemExit, match="SETTINGS.INI"):
        _run(card, rel, tmp_path / "m.txt", hashlib.sha256(b"official v71").hexdigest())
