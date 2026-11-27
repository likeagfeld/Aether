"""Baked cover pack for the Aether Card Manager (COVERS.PAK) -- M-art-pack Task 1.

For every product code in GAMES.DB, up to four covers are kept, one per source (Task spec order):
  coverproject  -- the Rings Cover Project zip, matched by GAMES.DB title + region (omsat.coverzip/artsrc)
  retrohead2d   -- files-ds-scene.net/retrohead/satiator/box/2d/<product>.TGA (flat front; omsat.artsrc)
  retrohead3d   -- files-ds-scene.net/retrohead/satiator/box/3d/<product>.TGA (box render; same naming as 2d,
                   confirmed by directory listing 2026-11-23; sometimes carries an alpha channel)
  libretro      -- libretro-thumbnails Sega_-_Saturn Named_Boxarts/<Title> (<Region>).png (omsat.artsrc)
All sources that have a cover for a code are kept side by side -- the Card Manager picks which one to show,
rather than this tool picking a single "best" cover. Downloads go through omsat.artsrc.RemoteArt, which caches
both hits and misses on disk, so a rerun is offline and reproduces the same bytes (never re-fetches).

Every kept image is re-encoded: downscaled (never upscaled) so its long side is at most 512px -- comfortably
above the Saturn's 320x240 output and plenty for a Card Manager preview -- then saved as quality-85 JPEG if
it is fully opaque, or PNG if any pixel has partial/zero alpha (the 3D renders sometimes do; flat fronts and
scans never do).

COVERS.PAK format (deliberately simple -- this is a PC-side pack for the Card Manager, not an on-disc OMSAT
format, so it does not reuse omsat.binfmt's sector-aligned header):

  bytes  0- 7  magic b"OMSATCVR"
  bytes  8- 9  version, uint16 BE (=1)
  bytes 10-11  reserved, 0
  bytes 12-15  index_len, uint32 BE -- byte length of the index that follows the header
  bytes 16-19  index_crc32, uint32 BE -- zlib.crc32 of the index bytes
  bytes 20-23  data_off, uint32 BE -- always 32 + index_len; where the cover blobs start
  bytes 24-27  build_date, uint32 BE (YYYYMMDD)
  bytes 28-31  reserved, 0
  bytes 32 .. 32+index_len       the index: UTF-8 JSON, no padding
  bytes 32+index_len .. EOF      cover blobs, back to back; each entry's "offset" is relative to data_off

Index JSON: {"version": 1, "codes": {"<product code>": [<entry>, ...]}}, entry =
  {"source": "coverproject"|"retrohead2d"|"retrohead3d"|"libretro", "format": "jpeg"|"png",
   "offset": int, "size": int, "width": int, "height": int, "crc32": int}
A reader needs one seek per blob: absolute offset = header.data_off + entry["offset"]. Entries may share a blob.
Task 18e: a code with no cover of its own region carries one entry with two more fields, "fallback_region" (the
area letter U, J or E of the cover it shows) and "fallback_from" (the GAMES.DB key that cover belongs to; the code
itself when the same title was found under another region's name), so the Card Manager can flag it.

Lookups by name (Task 18e): the libretro DAT's Redump names for a key come first -- the Cover Project zip and
libretro-thumbnails are both named that way -- then the GAMES.DB title plus each region name. Region fallback
(user-approved): with no cover for the game's own regions, the same game (same title) under US, then JP, then EU.

CLI: `python -m omsat.coverpack build|check|stats` (see each subcommand's --help)."""
from __future__ import annotations

import argparse
import io
import json
import struct
import sys
import threading
import time
import urllib.parse
import zlib
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from . import dbextra
from .artsrc import LIBRETRO, REGION_BITS, RemoteArt, _LIBRETRO_BAD, region_names, retrohead_names, zip_title_stem
from .coverzip import CoverZip
from .gamesdb import read_gamesdb
from .match import Matcher

MAGIC = b"OMSATCVR"
VERSION = 1
HEADER_SIZE = 32
LONG_SIDE = 512
JPEG_QUALITY = 85
SOURCES = ("coverproject", "retrohead2d", "retrohead3d", "libretro")
RETROHEAD_3D = "https://files-ds-scene.net/retrohead/satiator/box/3d/"
_HDR = struct.Struct(">8sHHIIII4s")
assert _HDR.size == HEADER_SIZE

_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GAMESDB = _ROOT / "build" / "db" / "GAMES.DB"
DEFAULT_ZIP = _ROOT.parent / "SEGA SATURN RINGS Cover Project-20261117T130330Z-1-001.zip"
DEFAULT_OUT = _ROOT / "build" / "coverpack"
DEFAULT_CACHE = DEFAULT_OUT / "cache"


class FormatError(Exception):
    """COVERS.PAK violates its own format (see module docstring)."""


@dataclass
class CoverEntry:
    code: str
    source: str
    data: bytes
    format: str
    width: int
    height: int
    fallback_region: str = ""
    fallback_from: str = ""


# --------------------------------------------------------------------------- finding covers (shared with cardbuild)

FALLBACK_ORDER = ("U", "J", "E")                       # the user's order for another region's cover
_LETTER_NAME = {"U": "USA", "J": "Japan", "E": "Europe"}


def _tnorm(t: str) -> str:
    w = "".join(c if c.isalnum() else " " for c in t.lower()).split()
    return " ".join(w[1:] if w[:1] == ["the"] else w)


def _letters(region_flags: int) -> set[str]:
    return {c for c, b in REGION_BITS.items() if region_flags & (1 << b)}


def dat_names(recs, aliases, dat_text: str | None) -> dict[str, list[str]]:
    """GAMES.DB key -> the libretro DAT's (Redump) game names whose serial is that key (exact/alias/skeleton)."""
    if not dat_text:
        return {}
    m = Matcher([r.key for r in recs], aliases)
    games, _ = dbextra.parse_dat(dat_text)
    out: dict[str, list[str]] = {}
    for g in games:
        r = m.match(g.serial, g.area)
        if r.key and r.how in ("exact", "alias", "skeleton") and g.name not in out.get(r.key, []):
            out.setdefault(r.key, []).append(g.name)
    return out


class CoverFinder:
    """Cover lookups by name and the region fallback: one implementation for coverpack and cardbuild."""

    def __init__(self, recs, aliases, cz: CoverZip | None, remote: RemoteArt | None, dat_text: str | None):
        self.cz, self.remote = cz, remote
        self.names = dat_names(recs, aliases, dat_text)
        self.by_title: dict[str, list] = {}
        for r in recs:
            self.by_title.setdefault(_tnorm(r.title_en), []).append(r)

    def _dat(self, key: str, letter: str | None) -> list[str]:
        ns = self.names.get(key, [])
        if not letter:
            return ns
        w = _LETTER_NAME[letter]
        return [n for n in ns if f"({w}" in n or f", {w}" in n]

    def zip_member(self, r, letter: str | None = None):
        """The zip member for record `r` (its own regions, or only region `letter`), or None."""
        if self.cz is None:
            return None
        for n in self._dat(r.key, letter):
            m = self.cz.find(n.replace("'", "_"))
            if m:
                return m
        regions = [_LETTER_NAME[letter]] if letter else region_names(r.region_flags)
        for region in regions:
            m = self.cz.find(zip_title_stem(r.title_en, region))
            if m:
                return m
        return None

    def libretro(self, r, letter: str | None = None):
        """(image, url) from libretro-thumbnails by DAT name, then by title + region; (None, None) on a miss."""
        if self.remote is None:
            return None, None
        for n in self._dat(r.key, letter):
            url = LIBRETRO + urllib.parse.quote(_LIBRETRO_BAD.sub("_", n) + ".png")
            for _ in range(2):         # two keys can share a DAT name: a second worker may read the cache file
                data = self.remote.get(url)      # while the first is still writing it -- read it once more
                if data is None:
                    break
                try:
                    return Image.open(io.BytesIO(data)).convert("RGBA"), url
                except OSError:
                    time.sleep(0.5)
        flags = (1 << REGION_BITS[letter]) if letter else r.region_flags
        return self.remote.libretro(r.title_en, flags)

    def fallback(self, r):
        """With no cover for `r`'s own regions: the same game's cover from US, then JP, then EU -- `r`'s own title
        under that region's name, then each other record of the same title that carries that region. Returns
        (image, src, source, letter, from_key) or None."""
        own = _letters(r.region_flags)
        for letter in FALLBACK_ORDER:
            if letter in own:
                continue
            sibs = [r] + sorted((x for x in self.by_title.get(_tnorm(r.title_en), [])
                                 if x.key != r.key and letter in _letters(x.region_flags)), key=lambda x: x.key)
            for x in sibs:
                m = self.zip_member(x, letter)
                if m:
                    return self.cz.image(m), m, "coverproject", letter, x.key
                if x is not r and self.remote is not None:
                    img, src = self.remote.retrohead(x.key)
                    if img is not None:
                        return img, src, "retrohead2d", letter, x.key
                img, src = self.libretro(x, letter)
                if img is not None:
                    return img, src, "libretro", letter, x.key
        return None


# --------------------------------------------------------------------------- encoding

def encode_cover(img: Image.Image, long_side: int = LONG_SIDE) -> tuple[bytes, str, int, int]:
    """Re-encode `img` for the pack: downscale (never upscale) so its long side is at most `long_side` px,
    then JPEG q85 if it ends up fully opaque, PNG if any pixel keeps partial/zero alpha. Returns
    (data, "jpeg"|"png", width, height) of the encoded image."""
    rgba = img.convert("RGBA")
    w, h = rgba.size
    scale = min(1.0, long_side / max(w, h))
    if scale < 1.0:
        rgba = rgba.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
    w, h = rgba.size
    opaque = rgba.getchannel("A").getextrema() == (255, 255)
    buf = io.BytesIO()
    if opaque:
        rgba.convert("RGB").save(buf, "JPEG", quality=JPEG_QUALITY)
        fmt = "jpeg"
    else:
        rgba.save(buf, "PNG", optimize=True)
        fmt = "png"
    return buf.getvalue(), fmt, w, h


# --------------------------------------------------------------------------- pack read/write

def write_coverpack(entries: list[CoverEntry], build_date: int) -> bytes:
    codes: dict[str, list[dict]] = {}
    blobs = bytearray()
    seen: dict[bytes, int] = {}                   # identical blobs (a region fallback's copy) are stored once
    for e in entries:
        off = seen.get(e.data)
        if off is None:
            off = seen[e.data] = len(blobs)
            blobs += e.data
        ent = {"source": e.source, "format": e.format, "offset": off, "size": len(e.data),
               "width": e.width, "height": e.height, "crc32": zlib.crc32(e.data) & 0xFFFFFFFF}
        if e.fallback_region:
            ent.update(fallback_region=e.fallback_region, fallback_from=e.fallback_from)
        codes.setdefault(e.code, []).append(ent)
    index_bytes = json.dumps({"version": VERSION, "codes": codes},
                              separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    data_off = HEADER_SIZE + len(index_bytes)
    header = _HDR.pack(MAGIC, VERSION, 0, len(index_bytes), zlib.crc32(index_bytes) & 0xFFFFFFFF,
                        data_off, build_date & 0xFFFFFFFF, b"\x00" * 4)
    return header + index_bytes + bytes(blobs)


def read_coverpack(data: bytes) -> dict:
    """Validate the header and index, return {"data_off", "build_date", "codes": {...}} (no blob bytes)."""
    if len(data) < HEADER_SIZE:
        raise FormatError("range: file shorter than header")
    magic, version, _r16, index_len, index_crc32, data_off, build_date, _r2 = _HDR.unpack_from(data, 0)
    if magic != MAGIC:
        raise FormatError(f"magic: expected {MAGIC!r}, got {magic!r}")
    if version != VERSION:
        raise FormatError(f"version: unsupported {version}")
    if HEADER_SIZE + index_len > len(data):
        raise FormatError("range: index beyond end of file")
    index_bytes = data[HEADER_SIZE:HEADER_SIZE + index_len]
    if zlib.crc32(index_bytes) & 0xFFFFFFFF != index_crc32:
        raise FormatError("CRC: index CRC mismatch")
    if data_off != HEADER_SIZE + index_len:
        raise FormatError("range: data_off does not follow the index")
    index = json.loads(index_bytes)
    index["data_off"] = data_off
    index["build_date"] = build_date
    return index


def read_cover(data: bytes, index: dict, code: str, source: str) -> bytes:
    for e in index["codes"].get(code, []):
        if e["source"] == source:
            start = index["data_off"] + e["offset"]
            end = start + e["size"]
            if e["offset"] < 0 or end > len(data):
                raise FormatError(f"range: {code}/{source} blob beyond end of file")
            blob = data[start:end]
            if zlib.crc32(blob) & 0xFFFFFFFF != e["crc32"]:
                raise FormatError(f"CRC: {code}/{source} blob CRC mismatch")
            return blob
    raise KeyError(f"{code}/{source} not in pack")


def check_coverpack(data: bytes) -> list[str]:
    """`coverpack check`: validate every entry's bounds and CRC. Returns a list of problems (empty = clean)."""
    try:
        index = read_coverpack(data)
    except FormatError as e:
        return [str(e)]
    problems = []
    for code, covers in index["codes"].items():
        for e in covers:
            try:
                read_cover(data, index, code, e["source"])
            except FormatError as ex:
                problems.append(f"{code}/{e['source']}: {ex}")
    return problems


def stats(index: dict) -> dict:
    """`coverpack stats`: counts per source and how many codes have at least one cover."""
    by_source = {s: 0 for s in SOURCES}
    for covers in index["codes"].values():
        for e in covers:
            by_source[e["source"]] = by_source.get(e["source"], 0) + 1
    with_cover = sum(1 for covers in index["codes"].values() if covers)
    return {"codes": len(index["codes"]), "codes_with_cover": with_cover, "by_source": by_source,
            "build_date": index.get("build_date")}


# --------------------------------------------------------------------------- build (network + zip)

def _retrohead3d(remote: RemoteArt, key: str):
    """Same naming as omsat.artsrc.RemoteArt.retrohead (product code, MK- prefix for digit-form keys),
    just the 3d/ box-render directory instead of 2d/ -- confirmed by listing both directories 2026-11-23."""
    for name in retrohead_names(key):
        url = RETROHEAD_3D + urllib.parse.quote(name + ".TGA")
        data = remote.get(url)
        if data is None:
            continue
        return Image.open(io.BytesIO(data)).convert("RGBA"), url
    return None, None


def build(gamesdb: bytes, covers_zip, out: Path, remote: RemoteArt | None = None,
          workers: int = 8, limit: int | None = None, dat_text: str | None = None) -> dict:
    t0 = time.time()
    out = Path(out)
    recs, _genres, _aliases = read_gamesdb(gamesdb)
    all_recs = recs
    if limit:
        recs = recs[:limit]
    errors: list[str] = []
    err_lock = threading.Lock()

    def err(msg: str) -> None:
        with err_lock:
            errors.append(msg)

    # 1) the Cover Project zip -- one ZipFile handle, read sequentially (zipfile is not safe to share
    # across threads, and the central-directory lookups are fast enough that it does not need to be).
    zip_hits: dict[str, Image.Image] = {}
    cz_stats = None
    cz = CoverZip(covers_zip) if covers_zip else None
    finder = CoverFinder(all_recs, _aliases, cz, remote, dat_text)
    if cz is not None:
        for r in recs:
            member = finder.zip_member(r)
            if member:
                try:
                    zip_hits[r.key] = cz.image(member)
                except Exception as ex:   # pragma: no cover - a corrupt member in the real archive
                    err(f"{r.key} coverproject: {type(ex).__name__}: {ex}")

    # 2) retrohead2d, retrohead3d, libretro -- independent per-code HTTP lookups, fanned out across
    # `workers` threads. RemoteArt.get() caches hits and misses as separate files per URL, so concurrent
    # workers never touch the same cache file and a rerun after an interruption costs nothing.
    net_results: dict[str, dict[str, Image.Image]] = {}

    def fetch_one(r):
        got: dict[str, Image.Image] = {}
        if remote is None:
            return r.key, got
        for source, fn in (("retrohead2d", lambda: remote.retrohead(r.key)),
                           ("retrohead3d", lambda: _retrohead3d(remote, r.key)),
                           ("libretro", lambda: finder.libretro(r))):
            try:
                img, _src = fn()
            except Exception as ex:
                err(f"{r.key} {source}: {type(ex).__name__}: {ex}")
                continue
            if img is not None:
                got[source] = img
        return r.key, got

    if remote is not None:
        with ThreadPoolExecutor(max_workers=max(1, workers)) as ex:
            for key, got in ex.map(fetch_one, recs):
                net_results[key] = got

    # 3) assemble the pack entries and the coverage tallies
    entries: list[CoverEntry] = []
    counts = {s: 0 for s in SOURCES}
    region_counts: dict[str, int] = {}
    fallback_counts: dict[str, int] = {}
    no_cover: list[str] = []

    for r in recs:
        code = r.key
        got_any = False
        imgs = dict(net_results.get(code, {}))
        if code in zip_hits:
            imgs["coverproject"] = zip_hits[code]
        for source in SOURCES:
            img = imgs.get(source)
            if img is None:
                continue
            try:
                data, fmt, w, h = encode_cover(img)
            except Exception as ex:
                err(f"{code} {source}: {type(ex).__name__}: {ex}")
                continue
            entries.append(CoverEntry(code, source, data, fmt, w, h))
            counts[source] += 1
            got_any = True
        if not got_any:
            try:
                fb = finder.fallback(r)
            except Exception as ex:
                err(f"{code} fallback: {type(ex).__name__}: {ex}")
                fb = None
            if fb is not None:
                img, _src, source, letter, from_key = fb
                data, fmt, w, h = encode_cover(img)
                entries.append(CoverEntry(code, source, data, fmt, w, h, letter, from_key))
                fallback_counts[letter] = fallback_counts.get(letter, 0) + 1
                got_any = True
        if got_any:
            for region in region_names(r.region_flags):
                region_counts[region] = region_counts.get(region, 0) + 1
        else:
            no_cover.append(code)
    if cz is not None:
        cz_stats = {"indexed_masters": cz.indexed, "ambiguous_lookups": cz.ambiguous, "resolved_lookups": cz.resolved}
        cz.close()

    build_date = int(time.strftime("%Y%m%d"))
    pak = write_coverpack(entries, build_date)
    out.mkdir(parents=True, exist_ok=True)
    (out / "COVERS.PAK").write_bytes(pak)

    coverage = {
        "build_date": build_date,
        "total_codes": len(recs),
        "codes_with_any_cover": len(recs) - len(no_cover),
        "codes_with_no_cover": no_cover,
        "by_source": counts,
        "by_region": region_counts,
        "region_fallback": fallback_counts,
        "dat_names": sum(len(v) for v in finder.names.values()),
        "pack_size": len(pak),
        "seconds": round(time.time() - t0, 1),
        "errors": errors,
    }
    if cz_stats is not None:
        coverage["cover_zip"] = cz_stats
    if remote is not None:
        coverage["downloads"] = len(remote.log)
    (out / "coverage.json").write_text(json.dumps(coverage, indent=2, ensure_ascii=False), encoding="utf-8")
    return coverage


# --------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m omsat.coverpack")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="fetch/encode covers for every GAMES.DB code into COVERS.PAK")
    b.add_argument("--gamesdb", default=str(DEFAULT_GAMESDB))
    b.add_argument("--covers-zip", default=str(DEFAULT_ZIP))
    b.add_argument("--out", default=str(DEFAULT_OUT))
    b.add_argument("--cache", default=str(DEFAULT_CACHE), help="RemoteArt download cache (hits and misses)")
    b.add_argument("--no-download", action="store_true", help="cache only; a miss stays a miss")
    b.add_argument("--no-remote", action="store_true", help="skip retrohead2d/retrohead3d/libretro entirely")
    b.add_argument("--workers", type=int, default=8, help="concurrent HTTP lookups (default 8)")
    b.add_argument("--limit", type=int, help="only the first N GAMES.DB records (smoke-testing)")
    b.add_argument("--libretro-dat", default=str(dbextra.DEFAULT_DAT),
                   help="libretro Saturn DAT for Redump names (downloaded, pinned, when missing; '' = none)")

    c = sub.add_parser("check", help="validate COVERS.PAK bounds and per-entry CRCs")
    c.add_argument("pak", nargs="?", default=str(DEFAULT_OUT / "COVERS.PAK"))

    s = sub.add_parser("stats", help="counts per source for an existing COVERS.PAK")
    s.add_argument("pak", nargs="?", default=str(DEFAULT_OUT / "COVERS.PAK"))

    a = ap.parse_args(argv)
    if a.cmd == "build":
        remote = None if a.no_remote else RemoteArt(Path(a.cache), download=not a.no_download)
        covers_zip = a.covers_zip if a.covers_zip and Path(a.covers_zip).is_file() else None
        dat = dbextra.fetch_dat(a.libretro_dat).read_text(encoding="utf-8") if a.libretro_dat else None
        rep = build(Path(a.gamesdb).read_bytes(), covers_zip, Path(a.out),
                    remote=remote, workers=a.workers, limit=a.limit, dat_text=dat)
        print(json.dumps(rep, indent=2, ensure_ascii=False))
        return 1 if rep["errors"] else 0
    if a.cmd == "check":
        problems = check_coverpack(Path(a.pak).read_bytes())
        print(json.dumps({"problems": problems}, indent=2))
        return 1 if problems else 0
    if a.cmd == "stats":
        print(json.dumps(stats(read_coverpack(Path(a.pak).read_bytes())), indent=2))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
