"""`python -m omsat.cardput --card E: --release build/release --version 0.1 --menu-url URL --menu-sha SHA
--manifest build/release/card_manifest.txt` -- put a release on the user's SD card (Task 16; tools/m3_release.sh step 4).

Writes ONLY: /menu.bin (the official menu, when the card has none), and through omsat.install's transactional plan
/autoboot.iso and /aether/{LIBRARY.IDX,GAMES.DB,ART.PAK,LAUNCHER.BIN,LOADBG.BIN,VRTILES.BIN,MUSIC/BGM.MUS,
THEMES/SAMPLE/*}, and /aether/CHEATS.PAK merged with the card's own (plus CHEATS.BAK when the card's is unreadable).
Never writes, moves or deletes anything else, except the folders named in RETIRED (content-checked; M3.5 Task 2: the
one named exception is aether/THEMES/BRIGHTTEST, a diagnostic theme hardware run 2 left on the card, removed only when
it holds exactly the THEME.INI that run wrote and SETTINGS.INI does not select it). The game folders are listed (name
+ size) before and after and must match.
A card /menu.bin that differs from the official one is never replaced (the run stops). Every written file is read back
and its SHA-256 compared with the release file. Writes the manifest (full card tree with sizes, the SHA-256 of every
file written, the menu.bin source, free space)."""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from .cheatpak import merged_for
from .install import install

GAME_DIRS = ("Games", "XBAND")

# user, 2026-11-20: folders a hardware run left on the card, removed only when they are exactly what that
# run wrote. Anything else in them, or a SETTINGS.INI that selects them, stops the run for a person to look.
RETIRED = {"aether/THEMES/BRIGHTTEST":
           {"THEME.INI": "c8e12acb7aeae5b630eb5b05c9d793f8e923b542d4959a4767fca220d414fa3b"}}


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree(root: Path) -> list[tuple[str, int]]:
    out = []
    for dp, dns, fns in os.walk(root):
        dns.sort()
        rel = Path(dp).relative_to(root)
        if rel.parts[:1] == ("System Volume Information",):
            dns[:] = []
            continue
        for n in sorted(fns):
            p = Path(dp) / n
            out.append(((rel / n).as_posix(), p.stat().st_size))
    return out


def plan(rel: Path) -> list[tuple[str, Path]]:
    """(card path, release file) for everything omsat.install writes."""
    c = rel / "card"
    return ([("autoboot.iso", rel / "autoboot.iso")]
            + [(f"aether/{n}", c / n) for n in ("LIBRARY.IDX", "GAMES.DB", "ART.PAK")]
            + [("aether/LAUNCHER.BIN", rel / "LAUNCHER.BIN"), ("aether/LOADBG.BIN", rel / "LOADBG.BIN"),
               ("aether/VRTILES.BIN", rel / "VRTILES.BIN"),
               ("aether/MUSIC/BGM.MUS", rel / "BGM.MUS")]
            + [(f"aether/THEMES/SAMPLE/{n}", rel / "THEMES" / "SAMPLE" / n) for n in ("THEME.INI", "BG.BIN")])


def retire(card: Path, log: list[str]) -> list[str]:
    done = []
    settings = card / "aether" / "SETTINGS.INI"
    chosen = ""
    if settings.is_file():
        for line in settings.read_text(encoding="ascii", errors="replace").splitlines():
            if line.strip().startswith("theme="):
                chosen = line.strip()[6:]
    for rel, files in RETIRED.items():
        d = card / rel
        if not d.exists():
            continue
        if chosen and chosen == Path(rel).name:
            raise SystemExit(f"cardput: SETTINGS.INI selects theme={chosen}; choose another theme before retiring {rel}")
        if any(p.is_dir() for p in d.iterdir()) or {p.name: sha256(p) for p in d.iterdir()} != files:
            raise SystemExit(f"cardput: {rel} is not the diagnostic folder this tool knows; check it by hand")
        for n in files:
            (d / n).unlink()
        d.rmdir()
        log.append(f"retired: {rel} (diagnostic leftover, content checked)")
        done.append(rel)
    return done


def git_id() -> str:
    try:
        return subprocess.run(["git", "log", "-1", "--format=%h %cd", "--date=format:%Y-%m-%d"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", required=True)
    ap.add_argument("--release", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--menu-url", required=True)
    ap.add_argument("--menu-sha", required=True)
    ap.add_argument("--manifest", required=True)
    a = ap.parse_args(argv)
    card, rel = Path(a.card + ("/" if a.card.endswith(":") else "")), Path(a.release)
    t0 = time.time()
    for d in GAME_DIRS:
        if not (card / d).is_dir():
            raise SystemExit(f"cardput: {card / d} missing")
    games_before = {d: tree(card / d) for d in GAME_DIRS}
    log: list[str] = []
    written: list[tuple[str, str, str]] = []           # (card path, sha on card, sha of the source)

    # 1. the official menu
    menu, src_menu = card / "menu.bin", rel / "menu.bin"
    if sha256(src_menu) != a.menu_sha:
        raise SystemExit(f"cardput: {src_menu} is not the official menu.bin ({a.menu_sha})")
    if menu.exists():
        got = sha256(menu)
        if got != a.menu_sha:
            raise SystemExit(f"cardput: {menu} exists and is not the official v71 menu.bin (SHA-256 {got}); "
                             "not replacing a file this tool did not write -- check it by hand")
        log.append("menu.bin: already the official file, not rewritten")
    else:
        with open(src_menu, "rb") as s, open(menu, "wb") as d:
            shutil.copyfileobj(s, d)
            d.flush()
            os.fsync(d.fileno())
        log.append("menu.bin: written")
    got = sha256(menu)
    if got != a.menu_sha:
        raise SystemExit(f"cardput: /menu.bin read back as {got}, expected {a.menu_sha}")
    written.append(("menu.bin", got, a.menu_sha))
    retire(card, log)                                   # before the install, after the official menu

    # 2. Aether, transactionally (omsat.install)
    items = plan(rel)
    for _, src in items:
        if not src.is_file():
            raise SystemExit(f"cardput: release file missing: {src}")
    # CHEATS.PAK is never copied over the card's: the card gets the release's merged with its own copy
    # (the user's on/off choices and added cheats kept), so that merged file is what is compared and read back.
    rel_cheats = rel / "card" / "CHEATS.PAK"
    want_cheats = None
    if rel_cheats.is_file():
        merged, crep = merged_for(card / "aether", rel_cheats.read_bytes())
        want_cheats = hashlib.sha256(merged).hexdigest()
        if crep.get("old_error"):
            log.append(f"CHEATS.PAK: the card's copy was unreadable ({crep['old_error']}), kept as CHEATS.BAK")
    current = all((card / dst).is_file() and (card / dst).stat().st_size == src.stat().st_size
                  and sha256(card / dst) == sha256(src) for dst, src in items)
    if want_cheats is not None:
        cc = card / "aether" / "CHEATS.PAK"
        current = current and cc.is_file() and sha256(cc) == want_cheats
    if current:
        log.append("aether: every file already matches the release, install skipped")
    else:
        rep = install(card, rel / "autoboot.iso", rel / "LAUNCHER.BIN", rel / "card", loadbg=rel / "LOADBG.BIN",
                      vrtiles=rel / "VRTILES.BIN", music=rel / "BGM.MUS", themes={"SAMPLE": rel / "THEMES" / "SAMPLE"},
                      cheats=rel_cheats if want_cheats is not None else None)
        log.append(f"install: copied {len(rep['copied'])} files, backups {rep['backups'] or 'none'}, "
                   f"menu.bin unchanged {rep['menu_bin_unchanged']}")
        log += [f"install warning: {w}" for w in rep.get("warnings", [])]
    for dst, src in items:
        on_card, want = sha256(card / dst), sha256(src)
        if on_card != want:
            raise SystemExit(f"cardput: read-back mismatch {dst}: card {on_card} vs release {want}")
        written.append((dst, on_card, want))
    if want_cheats is not None:
        on_card = sha256(card / "aether" / "CHEATS.PAK")
        if on_card != want_cheats:
            raise SystemExit(f"cardput: read-back mismatch aether/CHEATS.PAK: card {on_card} vs merged {want_cheats}")
        written.append(("aether/CHEATS.PAK", on_card, want_cheats))

    # 3. nothing else changed
    games_after = {d: tree(card / d) for d in GAME_DIRS}
    if games_after != games_before:
        raise SystemExit("cardput: the game folders changed during the run -- STOP and check the card")
    full = tree(card)
    zips = [p for p, _ in full if p.lower().endswith(".zip")]
    if zips:
        raise SystemExit(f"cardput: .zip files on the card: {zips}")
    du = shutil.disk_usage(card)

    lines = [f"Aether {a.version} card manifest -- {time.strftime('%Y-%m-%d %H:%M:%S')}",
             f"build: {git_id()}; card {card} (label checked by tools/m3_release.sh: VIBEMENU or AETHER)",
             f"official menu: {a.menu_url} -> menu.bin v71, SHA-256 {a.menu_sha}", ""]
    lines += ["Run log:"] + [f"  {x}" for x in log] + [""]
    lines += [f"Files written by this task ({len(written)}), SHA-256 read back from the card = release file:"]
    lines += [f"  {s}  {'OK' if s == w else 'MISMATCH'}  {p}" for p, s, w in written] + [""]
    summary = []
    for d in GAME_DIRS:
        folders = len({p.split("/")[0] for p, _ in games_after[d]})
        summary.append(f"{d}: {folders} folders, {len(games_after[d])} files, {sum(s for _, s in games_after[d]):,} B")
    lines += [f"Game folders: unchanged by this run ({'; '.join(summary)})", "No .zip files on the card.", "",
              f"Free space: {du.free:,} B of {du.total:,} B ({du.free / du.total:.1%} free)", "",
              f"Full card tree ({len(full)} files, {sum(s for _, s in full):,} B; System Volume Information skipped):"]
    lines += [f"  {s:>14,}  {p}" for p, s in full]
    Path(a.manifest).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[: 8 + len(log) + len(written) + 6]))
    print(f"cardput: PASS in {time.time() - t0:.0f} s; manifest {a.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
