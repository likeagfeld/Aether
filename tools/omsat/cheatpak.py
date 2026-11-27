"""`CHEATS.PAK` (contract/SPEC.md §5.15): every game's cheats, the user's on/off choices and user-added cheats in one
file, `/aether/CHEATS.PAK`. Big-endian, the §5.1 common header (magic `OMSATCHT`), a sorted index,
then one record per game. The menu's reader is menu/src/cheatpak.c; the Card Manager mirrors `merge`.

The repo's copy, `tools/data/cheats/CHEATS.PAK`, is built from `kai_cheats.txt` and `kai_keymap.json` by
`build_from_source` (`python -m omsat.kaicheats pak`); `install` puts it on a card, merged with the card's own copy.
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

from .binfmt import FormatError, Header, crc32, key_bytes, key_str, pack_header, unpack_header

MAGIC = b"OMSATCHT"
NAME = "CHEATS.PAK"
ROW = 24                         # key[16], u32 record offset, u32 record length
REC_HDR = 20
MAX_CHEATS = 64                  # menu/src/cheatpak.h OM_CH_MAX_CHEATS
MAX_CODES = 256                  # OM_CH_MAX_CODES
MAX_NAME = 30                    # OM_CH_MAX_NAME
MAX_REC = REC_HDR + MAX_CHEATS * (3 + MAX_NAME) + MAX_CODES * 8    # 4,180 B: menu/src/cheatpak.h OM_CP_MAX_REC
MAX_FILE = 0x1C000               # 114,688 B: menu/src/cheatpak.h OM_CP_MAX_FILE (the editor holds it in OM_LW_STAGE)
PAK_DATE = 20261124              # the build date written into the repo's copy, so it rebuilds byte-exact

G_MASTER, G_CWX, G_JHL = 0x01, 0x02, 0x04          # record flags
C_ON, C_USER, C_HEADER = 0x01, 0x02, 0x04          # cheat flags


@dataclass
class Cheat:
    name: str
    flags: int = 0
    codes: list[tuple[int, int, int]] = field(default_factory=list)    # (addr, value, cmd)


@dataclass
class Record:
    master_addr: int = 0
    master_value: int = 0
    flags: int = 0
    location: int = 0
    cache4: int = 0
    cheats: list[Cheat] = field(default_factory=list)


def encode_record(r: Record) -> bytes:
    if len(r.cheats) > MAX_CHEATS:
        raise FormatError(f"{len(r.cheats)} cheats, over {MAX_CHEATS}")
    ncodes = sum(len(c.codes) for c in r.cheats)
    if ncodes > MAX_CODES:
        raise FormatError(f"{ncodes} codes, over {MAX_CODES}")
    body = bytearray(struct.pack(">IHBBIHH", r.master_addr, r.master_value, r.flags, len(r.cheats), r.location,
                                 ncodes, r.cache4))
    for c in r.cheats:
        nm = c.name.encode("ascii")
        if not 1 <= len(nm) <= MAX_NAME or any(b < 0x20 or b > 0x7E for b in nm):
            raise FormatError(f"cheat name {c.name!r}: 1-{MAX_NAME} printable ASCII bytes")
        if (c.flags & C_HEADER) and (c.codes or c.flags & C_ON):
            raise FormatError(f"header row {c.name!r} has codes or is on")
        if not (c.flags & C_HEADER) and not 1 <= len(c.codes) <= 255:
            raise FormatError(f"cheat {c.name!r}: 1-255 codes")
        body += bytes((c.flags, len(c.codes), len(nm))) + nm
    for c in r.cheats:
        for a, v, cmd in c.codes:
            body += struct.pack(">IHBB", a, v, cmd, 0)
    body += b"\0" * (-(len(body) + 4) % 4)
    return struct.pack(">I", crc32(bytes(body))) + bytes(body)


def decode_record(b: bytes) -> Record:
    if len(b) < REC_HDR or len(b) % 4 or len(b) > MAX_REC:
        raise FormatError(f"record length {len(b)}")
    if crc32(b[4:]) != struct.unpack_from(">I", b, 0)[0]:
        raise FormatError("record CRC")
    ma, mv, fl, nch, loc, ncodes, cache4 = struct.unpack_from(">IHBBIHH", b, 4)
    if nch > MAX_CHEATS or ncodes > MAX_CODES:
        raise FormatError("record counts")
    r = Record(ma, mv, fl, loc, cache4)
    p, counts = REC_HDR, []
    for _ in range(nch):
        if p + 3 > len(b):
            raise FormatError("cheat row past the record")
        cf, cn, ln = b[p], b[p + 1], b[p + 2]
        nm = b[p + 3:p + 3 + ln]
        if not 1 <= ln <= MAX_NAME or len(nm) != ln or any(x < 0x20 or x > 0x7E for x in nm):
            raise FormatError("cheat name")
        if (cn and cf & C_HEADER) or (not cn and cf & (C_HEADER | C_ON) != C_HEADER):
            raise FormatError("cheat code count / header flag")
        r.cheats.append(Cheat(nm.decode("ascii"), cf))
        counts.append(cn)
        p += 3 + ln
    if sum(counts) != ncodes or p + 8 * ncodes > len(b) or len(b) - (p + 8 * ncodes) > 3:
        raise FormatError("code table")
    for c, n in zip(r.cheats, counts):
        for _ in range(n):
            a, v, cmd, z = struct.unpack_from(">IHBB", b, p)
            c.codes.append((a, v, cmd))
            p += 8
    if any(b[p:]):
        raise FormatError("non-zero padding")
    return r


def write_pak(records: dict[str, Record], date: int = PAK_DATE) -> bytes:
    keys = sorted(records, key=key_bytes)
    blobs = [encode_record(records[k]) for k in keys]
    table_off = 64
    data_off = table_off + ROW * len(keys)
    # identical records (one Kai game serving several product keys, until the user changes one) are stored once
    rows, at, data = bytearray(), {}, bytearray()
    for k, blob in zip(keys, blobs):
        if blob not in at:
            at[blob] = data_off + len(data)
            data += blob
        rows += key_bytes(k) + struct.pack(">II", at[blob], len(blob))
    data = bytes(data)
    hdr = Header(MAGIC, len(keys), table_off, len(rows), data_off, len(data), date, crc32(bytes(rows)),
                 ((0, 0), (0, 0), (0, 0)))
    out = pack_header(hdr) + bytes(rows) + data
    if len(out) > MAX_FILE:
        raise FormatError(f"CHEATS.PAK would be {len(out)} B, over {MAX_FILE}")
    return out


def read_index(data: bytes) -> list[tuple[str, int, int]]:
    """The header and index, checked as the menu checks them: (key, offset, length) rows."""
    if len(data) > MAX_FILE:
        raise FormatError(f"file is {len(data)} B, over {MAX_FILE}")
    h = unpack_header(data)
    if h.magic != MAGIC:
        raise FormatError(f"magic: expected {MAGIC!r}, got {h.magic!r}")
    if h.version != 1 or h.header_size != 64:
        raise FormatError("version / header size")
    if h.table_off != 64 or h.table_size != ROW * h.count or h.data_off != 64 + h.table_size:
        raise FormatError("table layout")
    if h.data_off + h.data_size > len(data):
        raise FormatError("range: data past the end of the file")
    if any(o or s for o, s in h.sections):
        raise FormatError("section table not zero")
    rows = data[64:64 + h.table_size]
    if crc32(rows) != h.table_crc:
        raise FormatError("table CRC")
    out, prev = [], None
    for i in range(h.count):
        kb = rows[i * ROW:i * ROW + 16]
        off, ln = struct.unpack_from(">II", rows, i * ROW + 16)
        if prev is not None and kb <= prev:
            raise FormatError(f"index not sorted / duplicate key at row {i}")
        if off % 4 or off < h.data_off or off + ln > h.data_off + h.data_size:
            raise FormatError(f"row {i}: record out of range")
        prev = kb
        out.append((key_str(kb), off, ln))
    return out


def read_pak(data: bytes) -> dict[str, Record]:
    return {k: decode_record(data[o:o + n]) for k, o, n in read_index(data)}


def check_pak(data: bytes) -> list[str]:
    try:
        read_pak(data)
    except (FormatError, struct.error, UnicodeDecodeError) as e:
        return [str(e)]
    return []


# --------------------------------------------------------------------------------------------------------------
# Building the repo's copy from the converted Kai data
# --------------------------------------------------------------------------------------------------------------


def record_from_kai(db, g, jhl_only: bool) -> Record:
    from .kaicheats import CT_PARAM_BF_DEFAULT_ON
    flags = G_MASTER | G_CWX | (G_JHL if jhl_only else 0)
    r = Record(g.master_addr, g.master_opc, flags, g.cfg_location, g.cache_4_capacity)
    for ch in db.cheats_for(g):
        codes = [(c.addr, c.value, c.cmd) for c in db.codes_for(ch)]
        if codes:
            r.cheats.append(Cheat(ch.name[:MAX_NAME], C_ON if ch.flags & CT_PARAM_BF_DEFAULT_ON else 0, codes))
        else:      # CT_PARAM_BF_GREYOUT: a sub-menu header in Kai's menu, kept as a label row
            r.cheats.append(Cheat(ch.name[:MAX_NAME], C_HEADER))
    return r


def build_records(db, keymap: dict) -> dict[str, Record]:
    by_name = {g.name: g for g in db.games}
    out: dict[str, Record] = {}
    for row in keymap["games"]:
        if row["how"] == "unmatched":
            continue
        for key in row["products"]:
            if key in out:
                raise FormatError(f"product {key} claimed twice in the keymap")
            out[key] = record_from_kai(db, by_name[row["kai_name"]], row["jhl_only"])
    return out


def build_from_source(source: Path | None = None, keymap: Path | None = None) -> bytes:
    """The shipped dataset by default (kaicheats.SOURCE_PATH + KEYMAP_PATH); any other conversion by path."""
    from .kaicheats import KEYMAP_PATH, SOURCE_PATH, load_db
    km = json.loads((keymap or KEYMAP_PATH).read_text(encoding="utf-8"))
    return write_pak(build_records(load_db(source or SOURCE_PATH), km))


# --------------------------------------------------------------------------------------------------------------
# Updating a card: merge, never overwrite the user's choices
# --------------------------------------------------------------------------------------------------------------


def _ident(cheats: list[Cheat]) -> list[tuple[str, int] | None]:
    """A Kai cheat's identity in its record: (name, how many earlier Kai cheats share the name). Header and
    user-added rows have none."""
    seen: dict[str, int] = {}
    out = []
    for c in cheats:
        if c.flags & (C_HEADER | C_USER):
            out.append(None)
            continue
        n = seen.get(c.name, 0)
        seen[c.name] = n + 1
        out.append((c.name, n))
    return out


def merge_record(old: Record, new: Record) -> tuple[Record, bool]:
    """new's data with old's choices: each Kai cheat takes old's on/off for the same identity, and old's user-added
    cheats follow (one already in new, same name and codes, is not added twice, so merging again changes nothing).
    Returns (record, fitted); when they do not fit the caps, old comes back unchanged."""
    old_on = {i: bool(c.flags & C_ON) for i, c in zip(_ident(old.cheats), old.cheats) if i is not None}
    cheats = []
    for i, c in zip(_ident(new.cheats), new.cheats):
        f = c.flags
        if i is not None and i in old_on:
            f = (f | C_ON) if old_on[i] else (f & ~C_ON)
        cheats.append(Cheat(c.name, f, list(c.codes)))
    have = {(c.name, tuple(c.codes)) for c in cheats if c.flags & C_USER}
    cheats += [Cheat(c.name, c.flags, list(c.codes)) for c in old.cheats
               if c.flags & C_USER and (c.name, tuple(c.codes)) not in have]
    m = Record(new.master_addr, new.master_value, new.flags, new.location, new.cache4, cheats)
    if len(cheats) > MAX_CHEATS or sum(len(c.codes) for c in cheats) > MAX_CODES:
        return old, False
    return m, True


def merge(old: bytes | None, new: bytes, date: int = PAK_DATE) -> tuple[bytes, dict]:
    """SPEC §5.15 update rule. `old` is the card's CHEATS.PAK (None when it has none), `new` the shipped one. A key in
    both: merge_record. A key only on the card: kept as it is. A corrupt `old` is reported and not used."""
    new_recs = read_pak(new)
    rep = {"merged": 0, "kept_card_only": 0, "kept_unfitted": [], "old_error": None}
    if old is None:
        return new, rep
    try:
        old_recs = read_pak(old)
    except (FormatError, struct.error, UnicodeDecodeError) as e:
        rep["old_error"] = str(e)
        return new, rep
    out = dict(new_recs)
    for k, o in old_recs.items():
        if k in out:
            out[k], fitted = merge_record(o, out[k])
            rep["merged"] += 1
            if not fitted:
                rep["kept_unfitted"].append(k)
        else:
            out[k] = o
            rep["kept_card_only"] += 1
    if out == new_recs:
        return new, rep
    return write_pak(out, date), rep


def read_card_copy(vm: Path) -> bytes | None:
    """sdwrite.h's recovery rule: CHEATS.PAK wins whenever it exists; absent, a CHEATS.PAK.NEW is the latest file."""
    for p in (vm / NAME, vm / (NAME + ".NEW")):
        if p.is_file():
            return p.read_bytes()
    return None


def merged_for(vm: Path, new: bytes) -> tuple[bytes, dict]:
    """The file to put in `vm` (a card's /aether): `new` merged with the copy already there."""
    old = read_card_copy(vm)
    data, rep = merge(old, new)
    if rep["old_error"]:
        (vm / "CHEATS.BAK").write_bytes(old)       # a corrupt card copy is kept beside the new one, never lost
        rep["old_kept_as"] = "CHEATS.BAK"
    return data, rep


def install(vm: Path, new: bytes) -> dict:
    """Puts `new`, merged with the copy already in `vm`, into `vm` -- write-then-replace like the menu's
    om_sdw_replace: CHEATS.PAK.NEW written and read back, then the old file removed and .NEW renamed."""
    vm.mkdir(parents=True, exist_ok=True)
    data, rep = merged_for(vm, new)
    tmp, dst = vm / (NAME + ".NEW"), vm / NAME
    tmp.write_bytes(data)
    if tmp.read_bytes() != data:
        raise OSError(f"{tmp}: read-back mismatch")
    if dst.exists():
        dst.unlink()
    tmp.rename(dst)
    rep["bytes"] = len(data)
    rep["games"] = len(read_index(data))
    return rep
