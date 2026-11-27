"""Validator for ART.PAK / GAMES.DB / LIBRARY.IDX / BG.BIN / BOOTANIM.BIN / LOADBG.BIN / SOUND.BIN / MUSIC / VRTILES.BIN /
BIGLOGO.BIN / CHEATS.PAK, and PERGAME.INI (recognized by name: it has no magic, §5.14)
(contract/SPEC.md §5).
`python -m omsat.check FILE...`"""
from __future__ import annotations

import re
import sys

from . import artpak, binfmt as bf, bootanim, cheatpak, gamesdb, gen_biglogo, gen_vrtiles, libidx, loadbg, pergame, sound, theme

_FOLDER_ART_KEY = re.compile(r"^F[0-9A-F]{8}$")

# Console load limits (contract/SPEC.md §5.9), derived from the menu's C source; tests/test_limits.py re-derives them.
GDB_MAX_FILE = 0x0003C000 - 4        # menu/app/sd.c:65 `size + 4 > cap`; cap OM_LW_GAMESDB_MAX (omlayout.h:8)
LIB_MAX_FILE = 208 * 1024 - 4        # sd.c:65; cap OM_LIBBUF (menu/app/main.c:23)
ART_MAX_HEAD = 0x0001E000 - 4        # menu/app/card.c:37 `need + 4 > arttab_cap`; cap OM_LW_ARTTAB_MAX (0x20000 -> 0x1E000)
ART_MAX_ENTRIES = (ART_MAX_HEAD - bf.HEADER_SIZE) // 48
VRT_MAX_FILE = 0x00004000 - 4        # sd.c:65; cap OM_LW_VRTILES_MAX (omlayout.h), menu/app/card.c load_vrt
BLG_MAX_FILE = 0x00002000            # whole sectors <= OM_LW_BIGLOGO_MAX (omlayout.h), menu/app/audio.c disc read


def limit_errors(data: bytes) -> list[str]:
    """Files the menu would refuse with OM_E_FULL ('buffer full'): reported so the PC side never writes one."""
    magic = data[:8]
    if magic == bf.MAGIC_GDB and len(data) > GDB_MAX_FILE:
        return [f"GAMES.DB is {len(data)} B; the console loads at most {GDB_MAX_FILE} B (SPEC §5.9)"]
    if magic == gen_vrtiles.MAGIC and len(data) > VRT_MAX_FILE:
        return [f"VRTILES.BIN is {len(data)} B; the console loads at most {VRT_MAX_FILE} B (SPEC §5.9)"]
    if magic == gen_biglogo.MAGIC and (len(data) + 2047) // 2048 * 2048 > BLG_MAX_FILE:
        return [f"BIGLOGO.BIN is {len(data)} B; the console reads at most {BLG_MAX_FILE} B (SPEC §5.9)"]
    if magic == bf.MAGIC_LIB and len(data) > LIB_MAX_FILE:
        return [f"LIBRARY.IDX is {len(data)} B; the console loads at most {LIB_MAX_FILE} B (SPEC §5.9)"]
    if magic == bf.MAGIC_ART and len(data) >= bf.HEADER_SIZE:
        h = bf.unpack_header(data)
        if h.table_off + h.table_size > ART_MAX_HEAD:
            return [f"ART.PAK has {h.count} entries ({h.table_off + h.table_size} B header+table); the console loads "
                    f"at most {ART_MAX_HEAD} B = {ART_MAX_ENTRIES} entries (SPEC §5.9)"]
    return []


def _sorted_unique(keys) -> bool:
    return all(a < b for a, b in zip(keys, keys[1:]))


def _check_art(data: bytes) -> list[str]:
    errs = []
    ents = artpak.read_artpak(data)
    h = bf.unpack_header(data)
    if h.data_off % bf.SECTOR:
        errs.append("data offset not 2048-aligned")
    order = [(bf.key_bytes(e["key"]), e["disc"]) for e in ents]
    if not _sorted_unique(order):
        errs.append("table not sorted by (key, disc) or has duplicates")
    nsec = h.data_size // bf.SECTOR
    for e in ents:
        if _FOLDER_ART_KEY.match(e["key"]):
            if e["disc"] != 0:
                errs.append(f"{e['key']}: folder-art key must have disc == 0, got {e['disc']}")
            if not e["flags"] & artpak.F_HERO:
                errs.append(f"{e['key']}: folder-art key must carry the hero bit")
            if e["flags"] & artpak.F_THUMB:
                errs.append(f"{e['key']}: folder-art key must not carry a thumb")
            if e["flags"] & artpak.F_JP:
                errs.append(f"{e['key']}: folder-art key must not carry a JP-title bitmap")
        for kind, w, hgt, sec, bit in (("thumb", e["thumb_w"], e["thumb_h"], e["thumb_sec"], 1),
                                       ("hero", e["hero_w"], e["hero_h"], e["hero_sec"], 2),
                                       ("jp", e["jp_w"], e["jp_h"], e["jp_sec"], 4)):
            if not e["flags"] & bit:
                continue
            if w % 8 or not (8 <= w <= 504) or not (1 <= hgt <= 255):
                errs.append(f"{e['key']}: {kind} size {w}x{hgt} violates VDP1 limits (width multiple of 8, 8-504; height 1-255)")
            if sec >= nsec:
                errs.append(f"{e['key']}: {kind} sector {sec} beyond data")
        blobs = b"".join(artpak.get_blob(data, e, k) for k, bit in (("thumb", 1), ("hero", 2), ("jp", 4)) if e["flags"] & bit)
        if bf.crc32(blobs) != e["blob_crc"]:
            errs.append(f"{e['key']}: blob CRC mismatch")
    return errs


def _check_gdb(data: bytes) -> list[str]:
    errs = []
    recs, genres, aliases = gamesdb.read_gamesdb(data)
    if not _sorted_unique([bf.key_bytes(r.key) for r in recs]):
        errs.append("records not sorted by key or duplicate keys")
    for r in recs:
        if r.genre != 0xFF and r.genre >= len(genres):
            errs.append(f"{r.key}: genre id {r.genre} >= {len(genres)}")
    keys = {r.key for r in recs}
    for a, c in aliases.items():
        if c not in keys:
            errs.append(f"alias {a} -> unknown key {c}")
    akeys = [bf.key_bytes(a) for a in aliases]
    if akeys != sorted(akeys):
        errs.append("alias table not sorted")
    return errs


def _check_lib(data: bytes) -> list[str]:
    errs = []
    ents, folders, _ = libidx.read_libidx(data)
    for e in ents:
        if e.folder_id >= len(folders):
            errs.append(f"{e.path}: folder id {e.folder_id} >= {len(folders)}")
        if e.image_type not in (1, 2):
            errs.append(f"{e.path}: image type {e.image_type}")
    return errs


_BY_MAGIC = {bf.MAGIC_ART: _check_art, bf.MAGIC_GDB: _check_gdb, bf.MAGIC_LIB: _check_lib}
_BY_MAGIC[theme.MAGIC] = theme.check_bg
_BY_MAGIC[bootanim.MAGIC] = bootanim.check_bootanim
_BY_MAGIC[loadbg.MAGIC] = loadbg.check_loadbg
_BY_MAGIC[sound.MAGIC] = sound.check_snd
_BY_MAGIC[sound.MAGIC_MUS] = sound.check_mus
_BY_MAGIC[gen_vrtiles.MAGIC] = gen_vrtiles.check_vrtiles          # M6 C3 (SPEC §5.18)
_BY_MAGIC[gen_biglogo.MAGIC] = gen_biglogo.check_biglogo          # M6 C2 (SPEC §5.19)

# SPEC §5.15: CHEATS.PAK, every game's cheats in one file
_BY_MAGIC[cheatpak.MAGIC] = cheatpak.check_pak


def check_bytes(data: bytes) -> list[str]:
    magic = data[:8]
    fn = _BY_MAGIC.get(magic)
    if fn is None:
        return [f"unknown magic {magic!r}"]
    try:
        return limit_errors(data) + fn(data)
    except (bf.FormatError, ValueError, IndexError, UnicodeDecodeError) as e:
        return [f"{type(e).__name__}: {e}"]


def main(argv=None) -> int:
    rc = 0
    for path in argv if argv is not None else sys.argv[1:]:
        with open(path, "rb") as fh:
            data = fh.read()
        base = str(path).replace("\\", "/").rsplit("/", 1)[-1].upper()
        is_pergame = base == "PERGAME.INI" or base.endswith(".PERGAME.INI")
        errs = pergame.check(data) if is_pergame else check_bytes(data)
        if errs:
            rc = 1
            print(f"FAIL {path}")
            for m in errs:
                print(f"  - {m}")
        else:
            print(f"PASS {path}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
