"""Card scanner (contract/SPEC.md v1 clarification 7). The C scanner (menu/src/scan.c) implements
the same rule; contract/fixtures/scan_tree.* gates both."""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol

from .binfmt import FormatError
from .gamesdb import region_flags_from
from .iphdr import SystemId, disc_n_m, parse_system_id, read_system_id
from .libidx import Folder, LibEntry, card_fingerprint, is_hidden_name, write_libidx
from .match import Matcher

IMAGE_EXT = (".cue", ".iso")
CUE_READ = 4096          # the cue bytes a scanner reads (the console's om_satfs_cue_files reads the same prefix)


def cue_file_names(text: str) -> list[str]:
    """Every FILE line's file name (SPEC v1 clarification 7, 2026-11-23), path components stripped: a line whose first
    non-blank word is FILE (any case), then a name in double quotes (no closing quote on that line: the line is
    skipped) or a bare word. menu/src/sysid.c om_cue_files is the same rule."""
    out = []
    for line in text.split("\n"):
        t = line.lstrip(" \t")
        if t[:4].lower() != "file" or t[4:5] not in (" ", "\t"):
            continue
        t = t[4:].lstrip(" \t")
        if t.startswith('"'):
            end = t.find('"', 1)
            if end < 0:
                continue
            name = t[1:end]
        else:
            name = ""
            for ch in t:
                if ch in " \t\r":
                    break
                name += ch
        name = name.replace("\\", "/").split("/")[-1]
        if name:
            out.append(name)
    return out


class DirSource(Protocol):
    def listdir(self, path: str) -> list[tuple[str, bool]]: ...
    def sysid(self, path: str) -> SystemId | None: ...
    def cue_files(self, path: str) -> list[str]: ...


class FsSource:
    def __init__(self, root: Path):
        self.root = Path(root)

    def listdir(self, path: str) -> list[tuple[str, bool]]:
        with os.scandir(self.root / path if path else self.root) as it:
            return [(e.name, e.is_dir()) for e in it]

    def sysid(self, path: str) -> SystemId | None:
        try:
            return read_system_id(self.root / path)
        except (OSError, FormatError, ValueError):
            return None

    def cue_files(self, path: str) -> list[str]:
        try:
            with open(self.root / path, "rb") as fh:
                return cue_file_names(fh.read(CUE_READ).decode("latin-1"))
        except OSError:
            return []


class TreeSource:
    """In-memory card for fixtures: tree = {path: {"dirs": [...], "files": [...]}},
    sysids = {image path: {product, area, device, title}}, cues = {cue path: [its FILE names]}."""

    def __init__(self, tree: dict, sysids: dict, cues: dict | None = None):
        self.tree, self.sysids, self.cues = tree, sysids, cues or {}

    def cue_files(self, path: str) -> list[str]:
        return list(self.cues.get(path, []))

    def listdir(self, path: str) -> list[tuple[str, bool]]:
        node = self.tree[path]
        return [(d, True) for d in node["dirs"]] + [(f, False) for f in node["files"]]

    def sysid(self, path: str) -> SystemId | None:
        f = self.sysids.get(path)
        if f is None:
            return None
        from .iphdr import make_system_id
        return parse_system_id(make_system_id(**f))


@dataclass
class ScanResult:
    data: bytes
    entries: list
    folders: list
    fingerprint: int
    report: dict = field(default_factory=dict)
    dates: dict = field(default_factory=dict)    # matched key -> earliest IP.BIN date (YYYYMMDD) among its images
    products: dict = field(default_factory=dict)  # path -> IP.BIN product number (discsets.py's input)
    sysids: dict = field(default_factory=dict)    # path -> SystemId (compat.fill_compat's per-disc rules; C# SystemIds)


def _b(s: str) -> bytes:
    return s.encode("utf-8")


def _join(a: str, b: str) -> str:
    return f"{a}/{b}" if a else b


def _is_image(name: str) -> bool:
    return name.lower().endswith(IMAGE_EXT)


def _split(listing, at_root: bool):
    dirs = sorted((n for n, d in listing if d and not is_hidden_name(n, at_root)), key=_b)
    files = sorted((n for n, d in listing if not d and not is_hidden_name(n, at_root)), key=_b)
    return dirs, files


def _ascii_lower(s: str) -> str:
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def _images(src, path: str, files: list[str]) -> list[str]:
    """The folder's images (clarification 7): .cue / .iso files, less every one a sibling .cue's FILE lines name
    (ASCII case-insensitively; 2026-11-23): such a file is that cue's own data, not a second game."""
    cues = [f for f in files if f.lower().endswith(".cue")]
    refs = set()
    for c in cues:
        refs |= {_ascii_lower(n) for n in getattr(src, "cue_files", lambda p: [])(_join(path, c))} - {_ascii_lower(c)}
    return [f for f in files if _is_image(f) and _ascii_lower(f) not in refs]


def _is_game_dir(sdirs, images) -> bool:
    """2026-11-23 (Satiator Rings' model): a single-game folder holds exactly one image and no subfolder; it is not a
    folder row, its image is an entry of its parent folder, and the menu names it by the folder."""
    return not sdirs and len(images) == 1


def _entry(path, sid, matcher, folder_id, has_box) -> LibEntry:
    key, area, disc, total = "", 0, 1, 1
    if sid is not None:
        r = matcher.match(sid.product, sid.area)
        key = r.key or ""
        area = region_flags_from(sid.area)
        disc, total = disc_n_m(sid.device)
    return LibEntry(path=path, key=key, disc=disc, disc_total=total, area_flags=area,
                    image_type=1 if path.lower().endswith(".cue") else 2, folder_id=folder_id, has_box=has_box)


MAX_DEPTH = 8
MAX_FOLDERS_DEFAULT = 1024


def scan(src: DirSource, keys, aliases, build_date: int, max_folders: int = MAX_FOLDERS_DEFAULT) -> ScanResult:
    """SPEC.md v1 clarification 7, including the depth <= 8 / <= max_folders folder limits."""
    matcher = Matcher(keys, aliases)
    folders = [Folder("", 0xFFFF)]
    entries: list[LibEntry] = []
    unreadable = skipped_long = skipped_folders = 0
    root_listing = src.listdir("")
    dates: dict[str, int] = {}
    products: dict[str, str] = {}
    sysids: dict = {}

    def add(path, sid, fid, box):
        e = _entry(path, sid, matcher, fid, box)
        if sid is not None:
            products[path] = sid.product
            sysids[path] = sid
        d = sid.release_date if sid is not None else ""
        if e.key and len(d) == 8 and d.isdigit() and 1900 <= int(d) // 10000 <= 2099:
            dates[e.key] = min(dates.get(e.key, int(d)), int(d))
        entries.append(e)
    queue, qi = [(0, "", 0)], 0
    while qi < len(queue):
        fid, path, depth = queue[qi]
        qi += 1
        dirs, files = _split(root_listing if path == "" else src.listdir(path), path == "")
        here_box = "BOX.TGA" in files              # 2026-11-23: a loose image's cover falls back to its folder's
        for name in _images(src, path, files):
            p = _join(path, name)
            if len(_b(p)) > 255:
                skipped_long += 1
                continue
            sid = src.sysid(p)
            unreadable += sid is None
            add(p, sid, fid, here_box)
        for d in dirs:
            sub = _join(path, d)
            sdirs, sfiles = _split(src.listdir(sub), False)
            images = _images(src, sub, sfiles)
            sids = [src.sysid(_join(sub, f)) for f in images] if _is_game_dir(sdirs, images) else []
            if sids:
                box = "BOX.TGA" in sfiles
                for f, sid in zip(images, sids):
                    if len(_b(_join(sub, f))) > 255:
                        skipped_long += 1
                        continue
                    unreadable += sid is None
                    add(_join(sub, f), sid, fid, box)
            elif len(_b(sub)) > 255:
                skipped_long += 1
            elif depth + 1 > MAX_DEPTH:
                # SPEC.md v1 clarification 7: a folder at depth 9 or deeper is not entered;
                # it and its contents are skipped.
                skipped_folders += 1
            elif len(folders) >= max_folders:
                # SPEC.md v1 clarification 7: once max_folders folders exist (root included),
                # further folders are skipped together with their contents.
                skipped_folders += 1
            else:
                folders.append(Folder(sub, fid))
                queue.append((len(folders) - 1, sub, depth + 1))
    entries.sort(key=lambda e: (e.folder_id, _b(e.path)))
    fp = card_fingerprint([n for n, _ in root_listing])
    data = write_libidx(entries, folders, fp, build_date)
    matched = sum(1 for e in entries if e.key)
    report = {"entries": len(entries), "matched": matched, "unmatched": len(entries) - matched,
              "unreadable_sysid": unreadable, "skipped_long_paths": skipped_long,
              "skipped_folders": skipped_folders, "folders": len(folders),
              "denominator": f"{len(entries)} images"}
    return ScanResult(data, entries, folders, fp, report, dates, products, sysids)
