"""`python -m omsat.install --target SDROOT --iso autoboot.iso --launcher LAUNCHER.BIN --data DIR`
Spec §8.1 Installer: writes /autoboot.iso and /aether/, never touches /menu.bin (verified by SHA-256 before and
after), and backs up the existing /autoboot.iso (e.g. Rings or Menu Kai) first -- only that file: every other root
file, other autoboot.* names included (a Rings autoboot.rings.iso, a Satiator autobootdesc.txt, an autoboot.cue), is
left exactly as it is (2026-11-25, the user's card: the old "every autoboot.*" rule renamed their autoboot.rings.iso,
the Rings menu their swap scripts use, and autobootdesc.txt). The official menu boots autoboot.elf, then .iso, then
.cue (unverified, from satiator-menu main.c:215-232), so an autoboot.elf is left in place and named in a warning.
As the Card Manager's Installer does.
an old /openmenu/ folder (the project's pre-rename name) is left exactly alone -- never deleted, never
migrated -- and its presence is only reported with one printed line.

the whole install is transactional, in three phases:
  1. Stage -- copy every new file onto the card under a transient name (STAGE_SUFFIX) next to its
     final destination. Nothing that already existed on the card is touched. A copy failure here
     removes whatever was staged and raises before anything old has changed.
  2. Commit -- a single flat plan of plain renames: back up an existing /autoboot.iso to a
     unique `.bak-<stamp>` name, then, for each staged file, move its existing destination aside
     (transiently, HOLD_SUFFIX) if one exists, then rename the staged file into place. Every
     commit step is therefore a plain rename with a known, exact undo.
  3. Undo on failure -- if any commit rename fails, every rename already done is undone, in
     reverse order. Each undo is attempted independently (one failing does not stop the rest). If
     every undo succeeds, the leftover staged/held files are cleaned up and the card is exactly as
     it was, reported with a named SystemExit. If any undo itself fails, nothing further is
     touched -- the SystemExit lists exactly which files are stranded and how to fix them by hand.
menu.bin is never staged, never planned, never renamed -- it is only ever read, for the
before/after SHA-256 check once the commit has fully succeeded."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import tempfile
import time
from pathlib import Path

from .lnchhdr import parse_header

DATA_FILES = ("GAMES.DB", "ART.PAK", "LIBRARY.IDX")
STAGE_SUFFIX = ".omsat-new"   # transient: a freshly-copied file, not yet renamed into place
HOLD_SUFFIX = ".omsat-old"    # transient: an existing destination, moved aside during commit


def _sha(p: Path):
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None


def _rename(src: Path, dst: Path) -> None:
    """Thin wrapper around Path.rename so fix-round tests can inject a mid-batch failure
    (e.g. a simulated FileExistsError/OSError on Windows) without monkeypatching Path itself."""
    src.rename(dst)


def _remove_held(p: Path) -> str | None:
    """Delete a moved-aside original once the install has committed. A read-only file (FAT's read-only attribute,
    which Windows refuses to delete) gets the attribute cleared and one retry -- as the Card Manager's
    Installer.TryDelete does. Returns None when the file is gone, else the error: a leftover is only a warning,
    since every new file is already in place."""
    for attempt in (0, 1):
        try:
            p.unlink(missing_ok=True)
            return None
        except OSError as e:
            if attempt or not p.exists():
                return f"{type(e).__name__}: {e}"
            try:
                os.chmod(p, stat.S_IWRITE | stat.S_IREAD)
            except OSError as e2:
                return f"{type(e2).__name__}: {e2}"
    return None


def _unique(path: Path, claimed: set[str]) -> Path:
    """`path`, or path with ".1", ".2", ... appended -- whichever name is free on disk and not
    already claimed by an earlier step in this same plan. Guards against a stamp collision (the
    stamp has 1-second resolution) or a leftover name from a previous run."""
    candidate = path
    n = 0
    while candidate.exists() or candidate.name in claimed:
        n += 1
        candidate = path.with_name(f"{path.name}.{n}")
    claimed.add(candidate.name)
    return candidate


def _plan_commit(target: Path, stamp: str, sources, staged: list[Path]):
    """Build the full commit plan as a flat list of (src, dst) renames -- never executing any of
    them -- plus the backup names (for the report) and the list of transient HOLD_SUFFIX paths
    that will need deleting once the whole plan has committed.

    Existence is tracked virtually (`will_exist`) rather than re-queried from disk for each step,
    because earlier steps in the *same* plan have not run yet when later steps are being planned
    -- e.g. the root autoboot.iso backup step frees up `target/autoboot.iso` for the later
    autoboot.iso placement step, even though autoboot.iso still exists on disk right now."""
    will_exist: dict[Path, bool] = {}

    def exists(p: Path) -> bool:
        return will_exist.get(p, p.exists())

    plan: list[tuple[Path, Path]] = []
    claimed: set[str] = set()

    def add(src: Path, dst: Path) -> None:
        plan.append((src, dst))
        will_exist[src] = False
        will_exist[dst] = True

    backup_names: list[str] = []
    for p in sorted(target.iterdir()):
        # Only /autoboot.iso itself, the one root file this install replaces (any case, as FAT matches names). No other
        # root file is ever renamed, moved or removed: not autoboot.rings.iso, autobootdesc.txt, autoboot.cue/.elf,
        # nor an earlier .bak-<stamp> or our own staged autoboot.iso.omsat-new.
        if p.is_file() and p.name.lower() == "autoboot.iso":
            dst = _unique(p.with_name(f"{p.name}.bak-{stamp}"), claimed)
            add(p, dst)
            backup_names.append(dst.name)

    held: list[Path] = []
    for (_label, final, _source), tmp in zip(sources, staged):
        if exists(final):
            old = _unique(final.with_name(final.name + HOLD_SUFFIX), claimed)
            add(final, old)
            held.append(old)
        add(tmp, final)

    return plan, backup_names, held


LOADBG_ASSET = Path(__file__).resolve().parents[2] / "menu" / "app" / "assets" / "LOADBG.BIN"   # Task 13c
VRTILES_ASSET = Path(__file__).resolve().parents[2] / "menu" / "app" / "assets" / "VRTILES.BIN"   # M6 C3 (SPEC §5.18)
MUSIC_ASSET = Path(__file__).resolve().parents[2] / "build" / "music" / "BGM.MUS"   # python -m omsat.sound music


THEME_FILES = ("THEME.INI", "BG.BIN")      # spec §7.6: THEME.INI required, BG.BIN optional


def install(target, iso, launcher, data, stamp=None, loadbg=None, music=None, themes=None, vrtiles=VRTILES_ASSET,
            cheats=None) -> dict:
    """cheats: the shipped CHEATS.PAK; what goes to /aether/CHEATS.PAK is it merged with the card's own
    copy (omsat.cheatpak.merged_for: the user's on/off choices and added cheats are kept), in the same plan. themes: {name: directory} -- each directory's THEME.INI (and BG.BIN, if present) goes to
    /aether/THEMES/<name>/ inside the same stage/commit/undo plan as everything else. vrtiles: the virtual
    root folders' tiles, which the menu no longer carries in HWRAM -- always installed with the menu."""
    target, iso, launcher, data = Path(target), Path(iso), Path(launcher), Path(data)
    themes = {n: Path(d) for n, d in (themes or {}).items()}
    for n, d in themes.items():
        if not n or "/" in n or "\\" in n or len(n) > 31 or not (d / "THEME.INI").is_file():
            raise SystemExit(f"theme {n!r}: needs a plain name (31 chars at most) and {d / 'THEME.INI'}")
    if not target.is_dir():
        raise SystemExit(f"target {target} is not a directory")
    for p in (iso, launcher, Path(vrtiles)) + ((Path(loadbg),) if loadbg is not None else ()) + ((Path(music),) if music is not None else ()):
        if not p.is_file():
            raise SystemExit(f"missing {p}")
    # A missing data file must fail loudly, by name, before anything changes.
    missing = [n for n in DATA_FILES if not (data / n).is_file()]
    if missing:
        raise SystemExit(f"missing data file(s): {', '.join(missing)}")
    lb = launcher.read_bytes()
    try:
        parse_header(lb[:0x1000], len(lb))
    except ValueError as e:
        raise SystemExit(f"LAUNCHER.BIN rejected: {e}")

    menu = target / "menu.bin"      # never staged, never planned, never renamed -- read-only
    before = _sha(menu)
    stamp = stamp or time.strftime("%Y%m%d-%H%M%S")

    old_om = target / "openmenu"
    old_om_notice = old_om.is_dir()
    # the Aether rename (2026-11-25): a card set up before it keeps the user's saves, favorites and settings in
    # /vibemenu. Installing beside it would leave two folders (the menu then refuses save swaps), so this tool refuses
    # and leaves the move to Aether Card Manager's Update card (PC backup, verified, one atomic folder rename).
    if (target / "vibemenu").is_dir():
        raise SystemExit(f"{target / 'vibemenu'} is the menu's folder from before the Aether rename: run Aether Card "
                         "Manager > Update card on this card first (it backs it up and moves it to /aether); nothing "
                         "was written")

    vm = target / "aether"
    vm_existed = vm.is_dir()
    vm.mkdir(exist_ok=True)

    sources = [("autoboot.iso", target / "autoboot.iso", iso)]
    sources += [(f"aether/{n}", vm / n, data / n) for n in DATA_FILES]
    sources += [("aether/LAUNCHER.BIN", vm / "LAUNCHER.BIN", launcher)]
    sources += [("aether/VRTILES.BIN", vm / "VRTILES.BIN", Path(vrtiles))]     # M6 C3
    if loadbg is not None:                      # the loading screen's background (SPEC §5.11), optional
        sources += [("aether/LOADBG.BIN", vm / "LOADBG.BIN", Path(loadbg))]
    cheats_rep, cheats_tmp = None, None
    if cheats is not None:                      # SPEC §5.15: merged, never a plain overwrite
        from .cheatpak import NAME as CHEATS_NAME, merged_for
        merged, cheats_rep = merged_for(vm, Path(cheats).read_bytes())
        fd, tmpname = tempfile.mkstemp(suffix=".PAK")
        with os.fdopen(fd, "wb") as fh:
            fh.write(merged)
        cheats_tmp = Path(tmpname)
        cheats_rep["sha256"] = hashlib.sha256(merged).hexdigest()
        sources += [(f"aether/{CHEATS_NAME}", vm / CHEATS_NAME, cheats_tmp)]
    mus_dir = vm / "MUSIC"
    mus_existed = mus_dir.is_dir()
    if music is not None:                       # the streamed background music (SPEC §5.12), optional
        mus_dir.mkdir(exist_ok=True)
        sources += [("aether/MUSIC/BGM.MUS", mus_dir / "BGM.MUS", Path(music))]

    th_root = vm / "THEMES"
    new_dirs = [(th_root, th_root.is_dir())]
    if themes:
        th_root.mkdir(exist_ok=True)
    for n, d in sorted(themes.items()):
        td = th_root / n
        new_dirs.insert(0, (td, td.is_dir()))
        td.mkdir(exist_ok=True)
        sources += [(f"aether/THEMES/{n}/{f}", td / f, d / f) for f in THEME_FILES if (d / f).is_file()]

    def drop_new_dirs() -> None:
        for d, existed in new_dirs + [(mus_dir, mus_existed), (vm, vm_existed)]:
            if not existed:
                try:
                    d.rmdir()
                except OSError:
                    pass

    # 1. Stage: copy every new file under a transient name. Nothing old is touched yet.
    staged: list[Path] = []
    for label, final, source in sources:
        tmp = final.with_name(final.name + STAGE_SUFFIX)
        try:
            shutil.copyfile(source, tmp)
        except OSError as e:
            for s in staged:
                s.unlink(missing_ok=True)
            drop_new_dirs()
            raise SystemExit(f"install failed while staging: {label}: {e}")
        staged.append(tmp)

    # 2. Commit: a flat plan of plain renames (backups, then each staged file into place).
    plan, backup_names, held = _plan_commit(target, stamp, sources, staged)
    done: list[tuple[Path, Path]] = []
    try:
        for src, dst in plan:
            _rename(src, dst)
            done.append((src, dst))
    except OSError as commit_err:
        # 3. Undo on failure: reverse every rename already done. Each undo is independent, so
        # one stuck file never stops the rest from being attempted.
        undo_failures: list[tuple[Path, Path, Exception]] = []
        for src, dst in reversed(done):
            try:
                _rename(dst, src)
            except OSError as undo_err:
                undo_failures.append((src, dst, undo_err))
        if not undo_failures:
            # Fully restored: the staged files simply never got placed. Remove them (and the
            # aether/ dir, if this install created it) so the card is exactly as it was.
            for tmp in staged:
                tmp.unlink(missing_ok=True)
            drop_new_dirs()
            raise SystemExit(f"install failed, card restored: {commit_err}")
        # Partial rollback: leave every file exactly where the undo loop left it -- no further
        # cleanup -- and name precisely what is stranded and how to put it back by hand.
        stranded = "; ".join(f"{dst} should be renamed back to {src} ({err})"
                              for src, dst, err in undo_failures)
        manual = "; ".join(f'mv "{dst}" "{src}"' for src, dst, _ in undo_failures)
        raise SystemExit(
            f"install failed AND rollback incomplete: {commit_err}. "
            f"Stranded: {stranded}. Manual fix: {manual}"
        )

    # Everything committed: the transient "moved aside" originals are no longer needed. One that cannot be deleted
    # (read-only even after clearing the attribute, or in use) is left behind and reported, never a failure.
    leftovers = []
    for old in held:
        err = _remove_held(old)
        if err:
            leftovers.append(f"{old} ({err})")

    after = _sha(menu)
    if before != after:
        raise SystemExit("menu.bin changed during install -- this must never happen")
    if cheats_tmp is not None:
        cheats_tmp.unlink()
    warnings = []
    # an empty SAVES (2026-11-27): Settings > Import Saves writes IMPORT.SEL there before any launch has made it
    try:
        (vm / "SAVES").mkdir(exist_ok=True)
    except OSError as e:
        warnings.append(f"could not make /aether/SAVES: {e}; the menu makes it when it needs it")
    if any(p.is_file() and p.name.lower() == "autoboot.elf" for p in target.iterdir()):
        warnings.append("/autoboot.elf is on the card: the Satiator menu may start it before /autoboot.iso "
                        "(unverified); it was left as it is")

    return {"target": str(target), "backups": sorted(backup_names),
            "copied": list(DATA_FILES) + ["LAUNCHER.BIN", "VRTILES.BIN"] + (["LOADBG.BIN"] if loadbg is not None else [])
                      + (["MUSIC/BGM.MUS"] if music is not None else [])
                      + [f"THEMES/{n}/{f}" for n, d in sorted(themes.items()) for f in THEME_FILES if (d / f).is_file()]
                      + (["CHEATS.PAK"] if cheats is not None else []) + ["autoboot.iso"],
            "menu_bin_present": before is not None, "menu_bin_unchanged": True,
            "old_openmenu_folder_untouched": old_om_notice, "leftovers": leftovers, "cheats": cheats_rep,
            "warnings": warnings}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--launcher", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--loadbg", default=str(LOADBG_ASSET) if LOADBG_ASSET.is_file() else None,
                    help="LOADBG.BIN for /aether (default: the committed menu/app/assets/LOADBG.BIN)")
    ap.add_argument("--vrtiles", default=str(VRTILES_ASSET),
                    help="VRTILES.BIN for /aether (M6 C3; default: the committed menu/app/assets/VRTILES.BIN)")
    ap.add_argument("--music", default=None,
                    help=f"BGM.MUS for /aether/MUSIC/ (about 1 GB; only when given: the release passes {MUSIC_ASSET})")
    ap.add_argument("--cheats", default=None,
                    help="CHEATS.PAK for /aether, merged with the card's copy (M6 Task 26b; e.g. tools/data/cheats/CHEATS.PAK)")
    ap.add_argument("--theme", action="append", default=[], metavar="NAME=DIR",
                    help="a theme directory (THEME.INI, optional BG.BIN) for /aether/THEMES/NAME (Task 16)")
    a = ap.parse_args(argv)
    themes = {}
    for spec in a.theme:
        n, sep, d = spec.partition("=")
        if not sep:
            ap.error(f"--theme {spec!r}: give NAME=DIR")
        themes[n] = d
    try:
        rep = install(a.target, a.iso, a.launcher, a.data, loadbg=a.loadbg, music=a.music, themes=themes,
                      vrtiles=a.vrtiles, cheats=a.cheats)
    except OSError as e:
        # never let a bare traceback escape the CLI entry point.
        print(f"install failed: {type(e).__name__}: {e}", file=sys.stderr)
        return 1
    for w in rep.get("warnings", []):
        print(f"warning: {w}", file=sys.stderr)
    for left in rep.get("leftovers", []):
        print(f"warning: installed, but an old copy could not be deleted -- delete it by hand: {left}",
              file=sys.stderr)
    if rep.get("old_openmenu_folder_untouched"):
        print("note: an old /openmenu/ folder exists on this card -- it is no longer used "
              "(Aether now reads /aether/) and was left untouched.")
    print(json.dumps(rep, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
