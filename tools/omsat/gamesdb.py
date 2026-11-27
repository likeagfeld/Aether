"""GAMES.DB writer/reader (contract/SPEC.md §5.3 + v1 clarifications 1 and 3)."""
from __future__ import annotations

import struct
import unicodedata
from dataclasses import dataclass

from . import binfmt as bf

REGION_BITS = {"J": 0, "T": 1, "U": 2, "E": 3, "B": 4}
PERIPH_BITS = {"J": 0, "A": 1, "M": 2, "K": 3, "S": 4, "T": 5}
USER_TITLE = 0x01   # record byte 0x35 bit0 (OM_GU_TITLE in menu/src/gamesdb.h)
_REC = struct.Struct(">16sBBBBHHIIIIIHBBIBB10s")
assert _REC.size == 64


@dataclass
class GameRecord:
    key: str
    title_en: str
    title_rom: str = ""
    maker: str = ""
    disc_count: int = 1
    genre: int = 0xFF
    region_flags: int = 0
    players: int = 0
    peripherals: int = 0
    year: int = 0
    date: int = 0
    # SPEC §5.3 amendment, bytes 0x2E-0x3F: loader hint (OM_LH_* in menu/src/gamesdb.h),
    # compat flags (OM_CF_* bits), the controller/loader note (string-pool offset; "" = none) and the
    # compat region mask (GAMES.DB region bits; 0 = applies to every region of this key).
    loader_hint: int = 0
    compat_flags: int = 0
    compat_note: str = ""
    compat_regions: int = 0
    # 2026-11-23 (SPEC §5.3 byte 0x35): bit0 USER_TITLE -- title_en is the user's own USEREDIT.INI title, which the
    # menu shows instead of the game's folder or file name.
    user_flags: int = 0


def region_flags_from(area: str) -> int:
    return sum(1 << REGION_BITS[c] for c in set(area.upper()) if c in REGION_BITS)


def periph_flags_from(s: str) -> int:
    return sum(1 << PERIPH_BITS[c] for c in set(s.upper()) if c in PERIPH_BITS)


def ascii_fold(s: str) -> str:
    out = []
    for ch in unicodedata.normalize("NFKD", s):
        if unicodedata.combining(ch):
            continue
        out.append(ch if ord(ch) < 128 else "?")
    return "".join(out)


class _Pool:
    def __init__(self):
        self.buf = bytearray(b"\x00")
        self.index: dict[str, int] = {"": 0}

    def add(self, s: str) -> int:
        s = ascii_fold(s)
        if s not in self.index:
            self.index[s] = len(self.buf)
            self.buf += s.encode("ascii") + b"\x00"
        return self.index[s]


def _cstr(pool: bytes, off: int) -> str:
    end = pool.index(b"\x00", off)
    return pool[off:end].decode("ascii")


def write_gamesdb(records, genres: list[str], aliases: dict[str, str], build_date: int) -> bytes:
    recs = sorted(records, key=lambda r: bf.key_bytes(r.key))
    keys = [r.key for r in recs]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate keys in GAMES.DB input")
    pool = _Pool()
    table = bytearray()
    for r in recs:
        table += _REC.pack(bf.key_bytes(r.key), r.disc_count, r.genre, r.region_flags, r.players,
                           r.peripherals, r.year, r.date, pool.add(r.title_en),
                           pool.add(r.title_rom or r.title_en), pool.add(r.maker), 0, 0,
                           r.loader_hint, r.compat_flags, pool.add(r.compat_note), r.compat_regions, r.user_flags,
                           b"\x00" * 10)
    genre_sec = struct.pack(">H", len(genres)) + b"".join(ascii_fold(g).encode("ascii") + b"\x00" for g in genres)
    alias_sec = b"".join(bf.key_bytes(a) + bf.key_bytes(c)
                         for a, c in sorted(aliases.items(), key=lambda kv: bf.key_bytes(kv[0])))
    table_off = bf.HEADER_SIZE
    genre_off = table_off + len(table)
    alias_off = genre_off + len(genre_sec)
    pool_off = alias_off + len(alias_sec)
    h = bf.Header(magic=bf.MAGIC_GDB, count=len(recs), table_off=table_off, table_size=len(table),
                  data_off=pool_off, data_size=len(pool.buf), build_date=build_date,
                  table_crc=bf.crc32(bytes(table)),
                  sections=((genre_off, len(genre_sec)), (alias_off, len(alias_sec)), (pool_off, len(pool.buf))))
    return bf.pack_header(h) + bytes(table) + genre_sec + alias_sec + bytes(pool.buf)


def read_gamesdb(data: bytes):
    h = bf.validate_common(data, bf.MAGIC_GDB)
    if h.table_size != 64 * h.count:
        raise bf.FormatError("table size != 64 * count")
    pool = data[h.data_off:h.data_off + h.data_size]
    recs = []
    for i in range(h.count):
        f = _REC.unpack_from(data, h.table_off + 64 * i)
        recs.append(GameRecord(key=bf.key_str(f[0]), disc_count=f[1], genre=f[2], region_flags=f[3],
                               players=f[4], peripherals=f[5], year=f[6], date=f[7],
                               title_en=_cstr(pool, f[8]), title_rom=_cstr(pool, f[9]), maker=_cstr(pool, f[10]),
                               loader_hint=f[13], compat_flags=f[14], compat_note=_cstr(pool, f[15]),
                               compat_regions=f[16], user_flags=f[17]))
    (goff, gsize), (aoff, asize), _ = h.sections
    gsec = data[goff:goff + gsize]
    n = struct.unpack_from(">H", gsec, 0)[0]
    genres = [g.decode("ascii") for g in gsec[2:].split(b"\x00")[:n]]
    aliases = {}
    for i in range(asize // 32):
        row = data[aoff + 32 * i:aoff + 32 * i + 32]
        aliases[bf.key_str(row[:16])] = bf.key_str(row[16:])
    return recs, genres, aliases


def fill_dates(data: bytes, dates: dict[str, int]) -> tuple[bytes, int]:
    """2026-11-20 hardware report: the card's GAMES.DB takes each card game's IP.BIN date (YYYYMMDD; key -> date, from
    cardscan) as its `date` and `year`, for records that have neither -- the menu's details card already showed the
    IP.BIN year, and its Year sort reads GAMES.DB. Keys are resolved through the aliases. Returns (bytes, records
    filled); with nothing to fill the input bytes come back unchanged."""
    recs, genres, aliases = read_gamesdb(data)
    by_key = {r.key: r for r in recs}
    n = 0
    for key, d in sorted(dates.items()):
        r = by_key.get(aliases.get(key, key))
        if r is not None and not r.year and not r.date:
            r.year, r.date = d // 10000, d
            n += 1
    if not n:
        return data, 0
    return write_gamesdb(recs, genres, aliases, bf.validate_common(data, bf.MAGIC_GDB).build_date), n
