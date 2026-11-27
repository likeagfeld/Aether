"""LIBRARY.IDX writer/reader (contract/SPEC.md §5.4). Paths <= 255 bytes (unverified, from research_satiator.md §4)."""
from __future__ import annotations

import struct
from dataclasses import dataclass

from . import binfmt as bf

_ENT = struct.Struct(">16sBBBBIHHI")
assert _ENT.size == 32
_FOLD = struct.Struct(">IHH")


@dataclass
class LibEntry:
    path: str
    key: str = ""
    disc: int = 1
    disc_total: int = 1
    area_flags: int = 0
    image_type: int = 1
    folder_id: int = 0
    has_box: bool = False
    title_override: str = ""
    sets: int = 0          # flags bits 2-15: a multi-disc set's id << 4 | 4 for an alternate (discsets.py, SPEC §5.4)


@dataclass
class Folder:
    path: str
    parent: int


def fingerprint(listing: list[str]) -> int:
    return bf.crc32("\n".join(sorted(listing)).encode("utf-8"))


def write_libidx(entries, folders, fp: int, build_date: int) -> bytes:
    pool = bytearray(b"\x00")
    index = {"": 0}

    def add(s: str) -> int:
        raw = s.encode("utf-8")
        if len(raw) > 255:
            raise ValueError(f"path longer than 255 bytes: {s[:40]}...")
        if s not in index:
            index[s] = len(pool)
            pool.extend(raw + b"\x00")
        return index[s]

    table = bytearray()
    for e in entries:
        flags = (1 if e.has_box else 0) | (2 if e.title_override else 0) | (e.sets & 0xFFFC)
        table += _ENT.pack(bf.key_bytes(e.key), e.disc, e.disc_total, e.area_flags, e.image_type,
                           add(e.path), e.folder_id, flags, add(e.title_override))
    ftab = b"".join(_FOLD.pack(add(f.path), f.parent, 0) for f in folders)
    fpsec = struct.pack(">I", fp)
    t_off = bf.HEADER_SIZE
    f_off = t_off + len(table)
    p_off = f_off + len(ftab)
    fp_off = p_off + len(pool)
    h = bf.Header(magic=bf.MAGIC_LIB, count=len(entries), table_off=t_off, table_size=len(table),
                  data_off=p_off, data_size=len(pool), build_date=build_date, table_crc=bf.crc32(bytes(table)),
                  sections=((f_off, len(ftab)), (p_off, len(pool)), (fp_off, 4)))
    return bf.pack_header(h) + bytes(table) + ftab + bytes(pool) + fpsec


def read_libidx(data: bytes):
    h = bf.validate_common(data, bf.MAGIC_LIB)
    if h.table_size != 32 * h.count:
        raise bf.FormatError("table size != 32 * count")
    (f_off, f_size), (p_off, p_size), (fp_off, _) = h.sections
    pool = data[p_off:p_off + p_size]

    def s(off: int) -> str:
        return pool[off:pool.index(b"\x00", off)].decode("utf-8")

    ents = []
    for i in range(h.count):
        k, d, dt, af, it, po, fid, fl, to = _ENT.unpack_from(data, h.table_off + 32 * i)
        ents.append(LibEntry(path=s(po), key=bf.key_str(k), disc=d, disc_total=dt, area_flags=af, image_type=it,
                             folder_id=fid, has_box=bool(fl & 1), title_override=s(to) if fl & 2 else "",
                             sets=fl & 0xFFFC))
    folders = [Folder(s(po), parent) for po, parent, _ in
               (_FOLD.unpack_from(data, f_off + 8 * i) for i in range(f_size // 8))]
    fp = struct.unpack_from(">I", data, fp_off)[0]
    return ents, folders, fp


# "openmenu" is the folder's old name, kept hidden so a card still carrying it never shows it as a folder.
# region.txt and ar_backup.bin are written to the root by the official menu itself (satiator-menu@75bf6e5
# main.c:203-206 and 390 -- region.txt on every boot, before autoboot; ar.c:44 -- its Action Replay backup).
# "saves", Pseudo Saturn Kai's /SAVES/BRAM/... save tree.
# The Aether rename (2026-11-25): "aether" is the menu's folder, "vibemenu" its name before; both hidden, so moving the
# folder leaves the fingerprint as it was.
HIDDEN_ROOT = ("aether", "vibemenu", "openmenu", "satiator-rings", "satiator-saves", "saves", "menu.bin",
               "region.txt", "ar_backup.bin")


def is_hidden_name(name: str, at_root: bool) -> bool:
    """SPEC.md v1 clarifications 6-7: names dropped from listings."""
    if name.startswith((".", "$")) or name.lower() == "system volume information":   # "$": Windows ($RECYCLE.BIN)
        return True
    if at_root:
        low = name.lower()
        return (low in HIDDEN_ROOT or low.startswith(("autoboot", "satiator-rings", "satiator-saves"))
                or low == "out.desc" or (len(low) == 9 and low[:3] == "out" and "1" <= low[3] <= "9"
                                         and low[4:] == ".desc"))   # LAUNCHER.BIN's launch descriptors (lid swap)
    return False


def card_fingerprint(root_names) -> int:
    """SPEC.md v1 clarification 6."""
    return fingerprint([n for n in root_names if not is_hidden_name(n, True)])
