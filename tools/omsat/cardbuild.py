"""`python -m omsat.cardbuild CARD --gamesdb GAMES.DB --jp-titles jp_titles.json [--covers-zip ZIP]
[--font TTF] [--no-download] [--out DIR] [--date YYYYMMDD] [--art-cache DIR] [--title-cards]
[--folder-art PATH=IMAGE ...] [--art-prefer KEY=SOURCE ...] [--no-cheats]`
Stand-in for Aether Card Manager until M4. Scans CARD (contract/SPEC.md v1 clarification 7) and writes LIBRARY.IDX,
GAMES.DB (copied), ART.PAK (hero covers + folder art + JP-title bitmaps), CHEATS.PAK (the repo's, merged with the copy
already there, SPEC §5.15), manifest.csv and report.json into --out
(default CARD/aether). Reads the card; writes nothing outside --out (and --art-cache).

100% cover coverage: covers come, in order, from the Cover Project zip (by image or folder
name, then -- with --art-cache or --title-cards -- by GAMES.DB title + region), the image's BOX.TGA, and only with
--art-cache the retrohead box server and libretro-thumbnails (omsat.artsrc), then only with --title-cards a generated
title card, which the report flags. The by-title lookups try the libretro DAT's Redump names for the key
first (omsat.coverpack.CoverFinder), and a game with no cover of its own regions takes the same game's cover from US,
then JP, then EU (the user's order) before any generated title card; the report and manifest mark it
("fallback_region"). --folder-art injects folder art from outside the card (a FOLDER.PNG inside the
folder still wins). --art-prefer KEY=SOURCE (repeatable; SOURCE in zip/boxtga/retrohead/libretro) tries that one
source first for product KEY, then falls back to the normal order on a miss.

2026-11-23: the card's own /aether/USEREDIT.INI (Card Manager's metadata edits) is applied onto GAMES.DB, and a user
title sets record byte 0x35 bit0 (SPEC §5.3), as the Card Manager does; a game's own BOX.TGA is its first cover source;
a folder's BOX.TGA is its art (Satiator Rings' model)."""
from __future__ import annotations

import argparse
import csv
import dataclasses
import json
import os
import sys
import time
from pathlib import Path, PurePosixPath

from PIL import Image

from .artpak import ArtEntry, folder_art_key, write_artpak
from .artsrc import RemoteArt, region_names, title_card, zip_title_stem
from . import discsets
from .cardscan import FsSource, scan
from .check import check_bytes
from . import compat as compatmod
from . import cheatpak as cheatpakmod
from . import kaicheats as kaicheatsmod
from . import useredit as usereditmod
from .coverpack import CoverFinder
from .coverzip import CoverZip
from . import dbextra
from .gamesdb import fill_dates, read_gamesdb
from .jpfont import TooTall, ensure_font, render_jp

FOLDER_ART_NAMES = ("FOLDER.PNG", "FOLDER.JPG", "FOLDER.TGA")
# 2026-11-23 (Satiator Rings' model): a folder's BOX.TGA is its art, after the above (and its loose images' fallback)
FOLDER_BOX = "BOX.TGA"
ART_PREFER_SOURCES = ("zip", "boxtga", "retrohead", "libretro")
# Kai's compatibility page + PSKai v6.545 autoset.c, and the controller-compat dataset.
LOADER_COMPAT = Path(__file__).resolve().parents[2] / "tools" / "data" / "compat" / "loader_compat.json"
CONTROLLER_COMPAT = Path(__file__).resolve().parents[2] / "tools" / "data" / "compat" / "controller_compat.json"
SAVE_TOOLS = Path(__file__).resolve().parents[2] / "tools" / "data" / "compat" / "save_tools.json"   # M6 Task 24


def _card_font():
    """Noto Sans JP from build/fonts when it is there (omsat.jpfont downloads it), else PIL's default."""
    from .jpfont import DEFAULT
    return DEFAULT if DEFAULT.is_file() else None


def _folder_image(d: Path):
    try:
        names = os.listdir(d)
    except OSError:
        return None, None
    for n in sorted(names):
        if n.upper() in FOLDER_ART_NAMES:
            try:
                return Image.open(d / n).convert("RGBA"), n
            except OSError:
                return None, None
    if FOLDER_BOX in names:
        try:                                       # exact upper-case name, as the game-folder BOX.TGA (Rings)
            return Image.open(d / FOLDER_BOX).convert("RGBA"), FOLDER_BOX
        except OSError:
            return None, None
    return None, None


def _listing(d: Path):
    try:
        return set(os.listdir(d))
    except OSError:
        return set()


def _try_named_source(source: str, e, r, cz, remote, card: Path, fallbacks: bool):
    """--art-prefer: try exactly one named source for this entry (zip, boxtga, retrohead or libretro) and report
    what the normal chain would have recorded for a hit, or None on a miss (the caller then falls back to the
    normal order). Side-effect free: the caller does the entries/cov/manifest bookkeeping."""
    p = PurePosixPath(e.path)
    if source == "zip":
        if not cz:
            return None
        member = cz.find(p.name) or (cz.find(p.parent.name) if p.parent.name else None)
        by_title = False
        if not member and not e.has_box and fallbacks and r:
            for region in region_names(r.region_flags):
                member = cz.find(zip_title_stem(r.title_en, region))
                if member:
                    by_title = True
                    break
        if not member:
            return None
        return {"img": cz.image(member), "disc": CoverZip.disc_of(member),
                "cov": "zip_title" if by_title else "zip",
                "source": "coverproject" + (" (by title)" if by_title else ""), "src": member}
    if source == "boxtga":
        if not e.has_box:
            return None
        try:
            img = Image.open(card / p.parent / "BOX.TGA").convert("RGBA")
        except OSError:
            return None
        return {"img": img, "disc": 0, "cov": "box_tga", "source": "user", "src": f"{p.parent}/BOX.TGA"}
    if source == "retrohead":
        if remote is None:
            return None
        img, src = remote.retrohead(e.key)
        if img is None:
            return None
        return {"img": img, "disc": 0, "cov": "retrohead", "source": "retrohead", "src": src}
    if source == "libretro":
        if remote is None or r is None:
            return None
        img, src = remote.libretro(r.title_en, r.region_flags)
        if img is None:
            return None
        return {"img": img, "disc": 0, "cov": "libretro", "source": "libretro", "src": src}
    return None


def build(card: Path, gamesdb: bytes, jp_titles: dict, covers_zip, font_path, download: bool, out: Path, date: int,
          remote: RemoteArt | None = None, title_cards: bool = False, folder_art: dict | None = None,
          art_prefer: dict | None = None, compat: bool = True, cheats: bool = True,
          dat_text: str | None = None) -> dict:
    t0 = time.time()
    card, out = Path(card), Path(out)
    recs, _, aliases = read_gamesdb(gamesdb)
    by_key = {r.key: r for r in recs}
    per_title: list[dict] = []
    rejected: list[str] = []
    fallbacks = remote is not None or title_cards
    res = scan(FsSource(card), [r.key for r in recs], aliases, date)
    discsets.apply(res, date)                                     # SPEC §5.4: multi-disc sets for the lid swap
    gamesdb, years_filled = fill_dates(gamesdb, res.dates)       # the IP.BIN year the menu shows, sortable by it
    compat_applied = 0
    compat_notes: list[str] = []
    if compat:
        # SPEC §5.3: the loader hint, controller/compat note, flags and region mask. compat_notes
        # dropped-confidence and unrecognized-peripheral rows, named per the brief's own rule.
        # 2026-11-25: then each disc by Kai's IP CRC, its product + area, or its product alone (compat.decide)
        gamesdb, compat_applied = compatmod.fill_compat(
            gamesdb, compatmod.load(LOADER_COMPAT, CONTROLLER_COMPAT, report=compat_notes, save_tools_json=SAVE_TOOLS),
            compatmod.discs_of(res.entries, res.sysids), compat_notes)
    # 2026-11-23 (Card Manager parity, SPEC §5.3 byte 0x35): the card's own USEREDIT.INI, after the IP.BIN years and
    # the compat rows, so a user's year wins; a user title sets the flag the menu shows it by
    ue_path = card / "aether" / "USEREDIT.INI"
    ue = usereditmod.parse(ue_path.read_text(encoding="utf-8", errors="replace")) if ue_path.is_file() else {}
    gamesdb, useredit_report = usereditmod.apply(gamesdb, ue)
    cz = CoverZip(covers_zip) if covers_zip else None
    finder = CoverFinder(recs, aliases, cz, remote, dat_text)
    entries: dict[tuple[str, int], ArtEntry] = {}
    manifest: list[list] = []

    def row(key, disc, source, src, transform) -> int:
        manifest.append([len(manifest) + 1, key, disc, source, src, transform])
        return len(manifest)

    cov = {"zip": 0, "box_tga": 0, "none": 0, "box_tga_unreadable": 0}
    if fallbacks:
        cov.update({"zip_title": 0, "retrohead": 0, "libretro": 0, "region_fallback": 0, "generated": 0})

    def title_of(e) -> str:
        r = by_key.get(e.key)
        return (e.title_override or (r.title_en if r else "") or PurePosixPath(e.path).stem).strip()

    def zip_by_title(e):
        r = by_key.get(e.key)
        if not (cz and r and fallbacks):
            return None
        return finder.zip_member(r)
    matched = [e for e in res.entries if e.key]
    # 2026-11-24: a folder (not the root) whose loose images are ONE game (its discs count once) gives that game its
    # BOX.TGA at the top priority, subfolders or not; shared by several loose games it stays the fallback below
    loose_games: dict[int, set] = {}
    for e in res.entries:
        if e.folder_id and e.path.rpartition("/")[0] == res.folders[e.folder_id].path:
            loose_games.setdefault(e.folder_id, set()).add(e.key or e.path)
    cz_stats = None
    try:
        for e in matched:
            pref = art_prefer.get(e.key) if art_prefer else None
            # a single-game folder's own BOX.TGA comes first (SAROO deploy), and so does a folder's BOX.TGA for its only
            # loose game (2026-11-24); one shared by several loose games (2026-11-23,
            # Satiator Rings' model: shared by the folder's games) is only the fallback after the other sources
            loose = e.path.rpartition("/")[0] == res.folders[e.folder_id].path
            own_box = (not loose or len(loose_games.get(e.folder_id, ())) == 1) and (
                e.has_box or "BOX.TGA" in _listing(card / PurePosixPath(e.path).parent))
            if not pref and own_box:
                pref = "boxtga"       # SAROO deploy: the user's own BOX.TGA (beside the image) beats every other source
            if pref:
                hit = _try_named_source(pref, dataclasses.replace(e, has_box=own_box) if pref == "boxtga" else e,
                                        by_key.get(e.key), cz, remote, card, fallbacks)
                if hit is not None:
                    k = (e.key, hit["disc"])
                    if k not in entries:
                        entries[k] = ArtEntry(k[0], k[1], None, hit["img"], None,
                                               row(k[0], k[1], hit["source"], hit["src"], "fit+letterbox RGB555"))
                    cov[hit["cov"]] += 1
                    per_title.append({"path": e.path, "key": e.key, "source": hit["source"], "file": hit["src"]})
                    continue
            if pref == "boxtga" and own_box:   # unreadable: counted once, then the other sources
                cov["box_tga_unreadable"] += 1
                e = dataclasses.replace(e, has_box=False)
            p = PurePosixPath(e.path)
            member, by_title = None, False
            if cz:
                member = cz.find(p.name) or (cz.find(p.parent.name) if p.parent.name else None)
            if not member and not e.has_box:
                member = zip_by_title(e)
                by_title = member is not None
            if member:
                k = (e.key, CoverZip.disc_of(member))
                if k not in entries:
                    entries[k] = ArtEntry(k[0], k[1], None, cz.image(member), None, row(k[0], k[1], "coverproject", member, "fit+letterbox RGB555"))
                cov["zip_title" if by_title else "zip"] += 1
                per_title.append({"path": e.path, "key": e.key,
                                  "source": "coverproject" + (" (by title)" if by_title else ""), "file": member})
            elif e.has_box:
                try:
                    img = Image.open(card / p.parent / "BOX.TGA").convert("RGBA")
                except OSError:
                    cov["box_tga_unreadable"] += 1
                    cov["none"] += 1
                    continue
                k = (e.key, 0)
                if k not in entries:
                    entries[k] = ArtEntry(e.key, 0, None, img, None, row(e.key, 0, "user", f"{p.parent}/BOX.TGA", "fit+letterbox RGB555"))
                cov["box_tga"] += 1
                per_title.append({"path": e.path, "key": e.key, "source": "BOX.TGA", "file": f"{p.parent}/BOX.TGA"})
            else:
                img, src, source = None, None, None
                r = by_key.get(e.key)
                if remote is not None:
                    img, src = remote.retrohead(e.key)
                    if img is None and src:
                        rejected.append(src)
                    source = "retrohead"
                    if img is None and r:
                        img, src = finder.libretro(r)
                        source = "libretro"
                fb_note = ""
                if img is None and r and fallbacks:
                    fb = finder.fallback(r)          # the same game's cover from US, then JP, then EU
                    if fb is not None:
                        img, src, _src_kind, letter, from_key = fb
                        source = "region_fallback"
                        fb_note = f" (fallback_region {letter}, from {from_key})"
                if img is None and title_cards:
                    square = bool(r and r.region_flags == 1)          # a JP-only release: the square JP box
                    img, src, source = title_card(title_of(e), square, font_path or _card_font()), "generated", "generated"
                if img is None:
                    cov["none"] += 1
                    per_title.append({"path": e.path, "key": e.key, "source": "NONE", "file": ""})
                    continue
                k = (e.key, 0)
                if k not in entries:
                    entries[k] = ArtEntry(e.key, 0, None, img, None,
                                          row(e.key, 0, source + fb_note, src, "fit+letterbox RGB555"))
                cov[source] += 1
                per_title.append({"path": e.path, "key": e.key, "file": src, "source": source + fb_note + (
                    " (FLAGGED: no real cover found)" if source == "generated" else "")})
        cov["denominator"] = f"{len(matched)} matched images"
        if cz:
            cz_stats = {"indexed_masters": cz.indexed, "ambiguous_lookups": cz.ambiguous,
                        "resolved_lookups": cz.resolved}
    finally:
        # never leak the zip handle -- the real archive is 1.6 GB. Every
        # cz.image() call above has already fully read its member into memory (coverzip.py),
        # so closing here, once we are done looking things up, is safe.
        if cz:
            cz.close()

    jp_keys = sorted({e.key for e in matched if e.key in jp_titles})
    font, why = ensure_font(font_path, download)
    if font is None:
        jp = {"status": "SKIPPED", "reason": why, "denominator": f"{len(jp_keys)} matched keys with a JP title"}
    else:
        rendered = scaled_count = 0
        too_tall = []
        for key in jp_keys:
            try:
                r, scaled = render_jp(jp_titles[key], font)
            except TooTall:          # dropped and reported, never clipped (SPEC §5.2)
                too_tall.append(key)
                continue
            if r is None:
                continue
            ent = entries.get((key, 0))
            if ent is None:
                ent = entries[(key, 0)] = ArtEntry(key, 0, None, None, None, row(key, 0, "jp_title", "jp_titles.json", "Noto Sans JP 4bpp"))
            ent.jp_title = r
            rendered += 1
            scaled_count += scaled
        # over-long titles are scaled, never cropped, so "cropped" is always 0 now.
        # Kept in the report (rather than dropped) so a consumer that already reads jp.cropped
        # does not break; "scaled" is the new, meaningful count.
        jp = {"status": "OK", "font": why, "rendered": rendered, "scaled": scaled_count, "cropped": 0,
              "too_tall": too_tall, "denominator": f"{len(jp_keys)} matched keys with a JP title"}
        if too_tall:
            jp["warning"] = (f"{len(too_tall)} JP title(s) taller than the 16-row bitmap even at 10 px: no JP bitmap, "
                             "the menu shows the title as text")

    found = injected = 0
    for f in res.folders[1:]:
        img, name = _folder_image(card / f.path)
        src = f"{f.path}/{name}"
        if img is None and folder_art and f.path in folder_art:
            img, src = Image.open(folder_art[f.path]).convert("RGBA"), str(folder_art[f.path])
            injected += 1
        if img is not None:
            key = folder_art_key(f.path)
            entries[(key, 0)] = ArtEntry(key, 0, None, img, None, row(key, 0, "user", src, "fit+letterbox RGB555"))
            found += 1

    out.mkdir(parents=True, exist_ok=True)
    files = {"LIBRARY.IDX": res.data, "GAMES.DB": gamesdb, "ART.PAK": write_artpak(list(entries.values()), date)}
    errors = {}
    for name, data in files.items():
        (out / name).write_bytes(data)
        errs = check_bytes(data)
        if errs:
            errors[name] = errs
    cheats_report = None
    if cheats:
        # SPEC §5.15: the repo's CHEATS.PAK, merged with the one already in `out` (the card's
        # /aether when no --out is given): the user's on/off choices and added cheats are kept.
        cheats_report = cheatpakmod.install(out, kaicheatsmod.PAK_PATH.read_bytes())
        errs = check_bytes((out / cheatpakmod.NAME).read_bytes())
        if errs:
            errors[cheatpakmod.NAME] = errs
    with open(out / "manifest.csv", "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["id", "key", "disc", "source", "source_file", "transform"])
        w.writerows(manifest)
    report = {"card": str(card), "out": str(out), "scan": res.report, "years_from_ipbin": years_filled,
              "compat_applied": compat_applied, "useredit": useredit_report, "covers": cov, "jp": jp,
              "folder_art": {"found": found, "denominator": f"{len(res.folders) - 1} folders below root"},
              "art_entries": len(entries), "check_errors": errors, "seconds": round(time.time() - t0, 1)}
    if compat_notes:
        report["compat_notes"] = compat_notes
    if cz_stats:
        report["cover_zip"] = cz_stats
    if folder_art:
        report["folder_art"]["injected"] = injected
    if fallbacks:
        report["per_title"] = per_title
    if remote is not None:
        report["remote"] = {"rejected": rejected, "downloads": remote.log}
    if cheats_report is not None:
        report["cheats"] = cheats_report
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    return report


def _dat_text(path: str, download: bool) -> str | None:
    """The libretro DAT for Redump-name lookups: fetched (pinned, hash-checked) when downloads are on, else the cached
    file when there is one; '' = none."""
    if not path:
        return None
    if download:
        return dbextra.fetch_dat(Path(path)).read_text(encoding="utf-8")
    return Path(path).read_text(encoding="utf-8") if Path(path).is_file() else None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("card")
    ap.add_argument("--gamesdb", required=True)
    ap.add_argument("--jp-titles", required=True)
    ap.add_argument("--covers-zip")
    ap.add_argument("--font")
    ap.add_argument("--no-download", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--date", type=int, default=int(time.strftime("%Y%m%d")))
    ap.add_argument("--art-cache", help="enable the retrohead and libretro fallbacks, cached in this directory")
    ap.add_argument("--title-cards", action="store_true", help="last resort: a generated title card (flagged)")
    ap.add_argument("--folder-art", action="append", default=[], metavar="PATH=IMAGE",
                    help="folder art for the card folder PATH (e.g. Games=build/release/folder_games.png)")
    ap.add_argument("--art-prefer", action="append", default=[], metavar="KEY=SOURCE",
                    help=f"for product KEY, try SOURCE ({'/'.join(ART_PREFER_SOURCES)}) first, "
                         "then fall back to the normal order (repeatable)")
    ap.add_argument("--no-compat", action="store_true",
                    help="skip the loader-hint/controller compat rows (SPEC §5.3, on by default)")
    ap.add_argument("--libretro-dat", default=str(dbextra.DEFAULT_DAT),
                    help="libretro Saturn DAT for Redump-name cover lookups (fetched when missing unless --no-download; '' = none)")
    ap.add_argument("--no-cheats", action="store_true",
                    help="do not copy CHEATS.PAK (SPEC §5.15; by default it is copied, merged with the card's copy)")
    a = ap.parse_args(argv)
    out = Path(a.out) if a.out else Path(a.card) / "aether"
    with open(a.jp_titles, encoding="utf-8") as fh:
        jp = json.load(fh)
    remote = RemoteArt(Path(a.art_cache), download=not a.no_download) if a.art_cache else None
    fa = {}
    for spec in a.folder_art:
        k, sep, v = spec.partition("=")
        if not sep or not Path(v).is_file():
            ap.error(f"--folder-art {spec!r}: give PATH=IMAGE with an existing image")
        fa[k.strip("/")] = Path(v)
    art_prefer = {}
    for spec in a.art_prefer:
        k, sep, v = spec.partition("=")
        if not sep or v not in ART_PREFER_SOURCES:
            ap.error(f"--art-prefer {spec!r}: give KEY=SOURCE with SOURCE in {ART_PREFER_SOURCES}")
        art_prefer[k] = v
    rep = build(Path(a.card), Path(a.gamesdb).read_bytes(), jp, a.covers_zip, a.font, not a.no_download, out, a.date,
                remote=remote, title_cards=a.title_cards, folder_art=fa, art_prefer=art_prefer,
                compat=not a.no_compat, cheats=not a.no_cheats,
                dat_text=_dat_text(a.libretro_dat, not a.no_download))
    print(json.dumps(rep, indent=2, ensure_ascii=False))
    return 1 if rep["check_errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
