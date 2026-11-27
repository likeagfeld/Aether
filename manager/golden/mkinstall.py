"""manager/golden/mkinstall.py -- the installer's golden (M4 Task 7): tools/omsat/install.py run on generated cards.
Aether Card Manager, GPL-3.0-or-later. Uses tools/omsat read-only.

    python manager/golden/mkinstall.py   -> manager/golden/install_cases/<case>.json, manager/golden/expect/install_<case>.json

A case is a realize.py tree with two top folders: card/ (the SD card before) and src/ (the files to install), plus the
install arguments and a fixed stamp. The expect is the card after omsat.install.install ran (runs times), as a
realize.py manifest of card/ (every directory and file, size, SHA-256), and each run's report."""
from __future__ import annotations

import json
import shutil
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from omsat import install as omi  # noqa: E402
from omsat.lnchhdr import make_header  # noqa: E402

import realize  # noqa: E402


def raw(path: str, b: bytes) -> dict:
    return {"path": path, "raw": b.hex()}


def body(seed: int, n: int) -> bytes:
    return realize.data_bytes(seed, n)


LAUNCHER = make_header(body(1, 300)) + body(1, 300)


def sources(loadbg=False, music=False, theme=False) -> list[dict]:
    f = [raw("src/autoboot.iso", body(2, 5000)), raw("src/LAUNCHER.BIN", LAUNCHER), raw("src/VRTILES.BIN", body(3, 700)),
         raw("src/data/GAMES.DB", body(4, 900)), raw("src/data/ART.PAK", body(5, 4096)), raw("src/data/LIBRARY.IDX", body(6, 333))]
    if loadbg:
        f.append(raw("src/LOADBG.BIN", body(7, 1200)))
    if music:
        f.append(raw("src/BGM.MUS", body(8, 2048)))
    if theme:
        f += [raw("src/themes/neon/THEME.INI", b"name=Neon\ntext=FFFFFF\n"), raw("src/themes/neon/BG.BIN", body(9, 512)),
              raw("src/themes/plain/THEME.INI", b"name=Plain\n")]
    return f


CASES = {
    "fresh": {"runs": 1, "args": {}, "files": [raw("card/menu.bin", b"official menu bytes")] + sources()},
    "replace": {
        "runs": 1, "args": {"loadbg": True, "music": True, "themes": {"neon": "neon", "plain": "plain"}},
        "files": [raw("card/menu.bin", body(10, 4096)),
                  raw("card/autoboot.iso", b"Satiator Rings"), raw("card/autoboot.cue", b"FILE autoboot.bin BINARY\n"),
                  raw("card/autoboot.iso.bak-20261122-120000", b"an earlier backup, never re-backed up"),
                  raw("card/aether/GAMES.DB", b"old db"), raw("card/aether/SETTINGS.INI", b"theme=neon\n"),
                  raw("card/aether/THEMES/neon/THEME.INI", b"name=Old\n"),
                  raw("card/openmenu/OLD.TXT", b"the old folder name: left alone"),
                  raw("card/Games/RPG/x.iso", body(11, 100))] + sources(loadbg=True, music=True, theme=True)},
    "twice": {"runs": 2, "args": {}, "files": [raw("card/autoboot.cue", b"Pseudo Saturn Kai")] + sources()},
}
STAMP = "20261122-120000"


def run_case(spec: dict, tmp: Path) -> dict:
    realize.realize(spec, tmp / "t")
    card, src = tmp / "t/card", tmp / "t/src"
    card.mkdir(exist_ok=True)
    a = spec["args"]
    reports = []
    for _ in range(spec["runs"]):
        r = omi.install(card, src / "autoboot.iso", src / "LAUNCHER.BIN", src / "data", stamp=STAMP,
                        loadbg=src / "LOADBG.BIN" if a.get("loadbg") else None,
                        music=src / "BGM.MUS" if a.get("music") else None,
                        themes={n: src / "themes" / d for n, d in a.get("themes", {}).items()},
                        vrtiles=src / "VRTILES.BIN")
        r.pop("target")
        reports.append(r)
    return {"stamp": STAMP, "reports": reports, "tree": realize.manifest(card)}


def main() -> int:
    cdir, edir = REPO / "manager/golden/install_cases", REPO / "manager/golden/expect"
    cdir.mkdir(exist_ok=True)
    for name, spec in CASES.items():
        (cdir / f"{name}.json").write_text(json.dumps(spec, indent=1, sort_keys=True) + "\n", newline="\n")
        with tempfile.TemporaryDirectory() as t:
            out = run_case(spec, Path(t))
        out["source"] = "omsat.install.install (mkinstall.py)"
        (edir / f"install_{name}.json").write_text(json.dumps(out, indent=1, sort_keys=True) + "\n", newline="\n")
        print(name, out["reports"][-1]["backups"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
