"""The per-game save file (contract/SPEC.md §5.16; M6 Task 22): one backup-RAM record as a Pseudo Saturn Kai ".BUP"
file, a 64-byte big-endian "Vmem" header then the record's data. The same rules as menu/src/bupfile.c.
`python -m omsat.bupfile FILE...` prints each file's fields and exits 1 on any that does not decode.

The layout is Kai's vmem_bup_header_t (cafe-alpha, pseudosat_kai/vmem/vmem_defs.h:82-135, GPL-2.0; re-implemented
from docs/research/m6-research.md §5, not copied); the directory fields are the BIOS backup library's BupDir
(ST-162-R1 Backup Library User's Manual §2.1; docs/decisions/m2-hw-facts.md HWF-72)."""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass

HDR = 64
MAGIC = b"Vmem"
SLOTS = 10
_FAT_BAD = set(b'"*/:<>?\\|')


class BupError(ValueError):
    pass


@dataclass
class BupDir:
    name: bytes                 # 1..11 printable ASCII
    comment: bytes = b""        # up to 11 raw bytes (Shift-JIS in real files)
    language: int = 0           # 0 Japanese .. 5 Italian (SEGA_BUP.H)
    date: int = 0               # BUP_SetDate's packed date (HWF-74)
    datasize: int = 0           # bytes
    blocks: int = 0             # the device's block count (informational: BUP_Write ignores it)


def name_ok(name: bytes) -> bool:
    return 1 <= len(name) <= 11 and all(0x20 <= c <= 0x7E for c in name)


def encode(d: BupDir, data: bytes) -> bytes:
    """Header + data. save_id, stats and the unused fields are 0; the date goes to 0x28 and 0x34."""
    if not name_ok(d.name):
        raise BupError(f"bad record name {d.name!r}")
    if len(d.comment) > 11:
        raise BupError("comment over 11 bytes")
    if d.datasize != len(data):
        raise BupError(f"datasize {d.datasize} != {len(data)} data bytes")
    h = (MAGIC + bytes(12) + d.name.ljust(12, b"\0") + d.comment.ljust(11, b"\0")
         + struct.pack(">BIIHHI", d.language, d.date, d.datasize, d.blocks, 0, d.date) + bytes(8))
    assert len(h) == HDR
    return h + data


def decode(b: bytes) -> tuple[BupDir, bytes, int]:
    """-> (dir, data, the second date at 0x34). Raises BupError("short"/"magic"/"name")."""
    if len(b) < HDR:
        raise BupError("short: under 64 bytes")
    if b[:4] != MAGIC:
        raise BupError("magic: not Vmem")
    raw = b[0x10:0x1C]
    name = raw[:11].split(b"\0", 1)[0]          # byte 11 is never part of the name (Save Game Copier writes '.')
    if not name_ok(name):
        raise BupError(f"name: {raw!r}")
    lang, date, size, blocks, _pad, date2 = struct.unpack(">BIIHHI", b[0x27:0x38])
    if len(b) - HDR < size:
        raise BupError(f"short: datasize {size}, {len(b) - HDR} data bytes")
    return BupDir(name, b[0x1C:0x27].rstrip(b"\0"), lang, date, size, blocks), b[HDR:HDR + size], date2


def slot_path(base: str, folder: str, slot: int, name: bytes) -> str:
    """Kai's slot layout (m6-research.md §1.3): slot 0 in the folder, 1..9 in "01".."09"."""
    if not 0 <= slot < SLOTS:
        raise BupError(f"slot {slot}")
    if not name_ok(name) or any(c in _FAT_BAD for c in name):
        raise BupError(f"bad record name {name!r}")
    return f"{base}/{folder}/" + (f"{slot:02d}/" if slot else "") + name.decode("ascii") + ".BUP"


def prefer(a: bytes, fa: str, b: bytes, fb: str) -> bool:
    """SPEC §5.16 duplicates: True when file a (named fa) wins over b (fb), two decoded files holding the same record
    name in one folder. The later record date (0x28: the game's own BUP_SetDate stamp, in every writer's files); then
    the higher save_id (0x04: Kai's counter, 0 from Save Game Copier and from us); then the file named "<record>.BUP"
    (case-insensitive); then the smaller file name, byte order. The same rule as menu/src/bupfile.c om_bup_prefer."""
    ka = (struct.unpack(">I", a[0x28:0x2C])[0], struct.unpack(">I", a[4:8])[0])
    kb = (struct.unpack(">I", b[0x28:0x2C])[0], struct.unpack(">I", b[4:8])[0])
    if ka != kb:
        return ka > kb
    own = decode(a)[0].name.decode("ascii").upper() + ".BUP"
    ma, mb = fa.upper() == own, fb.upper() == own
    if ma != mb:
        return ma
    return fa.encode() < fb.encode()


def index_folder(files: dict[str, bytes]) -> dict[bytes, str]:
    """{file name: bytes} of one slot folder -> {record name: the file to restore}. Readers key on the header's record
    name, never the file name (SPEC §5.16); files that do not decode are skipped."""
    out: dict[bytes, str] = {}
    for fn in sorted(files):
        try:
            name = decode(files[fn])[0].name
        except BupError:
            continue
        if name not in out or prefer(files[fn], fn, files[out[name]], out[name]):
            out[name] = fn
    return out


def corpus_json(root: str) -> str:
    """every .BUP under root as {path, len, hdr (64 B hex)}, sorted by path, for
    contract/fixtures/kai_bup_headers.json (the C decoder's corpus, through menu/host-tests/gen_cases.py)."""
    import json
    from pathlib import Path
    rows = []
    for p in sorted(Path(root).rglob("*"), key=lambda q: q.relative_to(root).as_posix()):
        if p.is_file() and p.suffix.upper() == ".BUP":
            b = p.read_bytes()
            rows.append({"path": p.relative_to(root).as_posix(), "len": len(b), "hdr": b[:HDR].hex()})
    return json.dumps({"source": "Pseudo Saturn Kai v6.545 release (pskai_release_20240114) SAVES tree, cafe-alpha "
                       "and contributors; headers only", "files": rows}, indent=1) + "\n"


def main(argv: list[str]) -> int:
    if argv[:1] == ["--corpus-json"]:
        sys.stdout.write(corpus_json(argv[1]))
        return 0
    bad = 0
    for p in argv:
        try:
            with open(p, "rb") as f:
                d, data, date2 = decode(f.read())
        except (OSError, BupError) as e:
            print(f"{p}: {e}")
            bad += 1
            continue
        print(f"{p}: name={d.name.decode()} comment={d.comment.hex()} language={d.language} date={d.date:#010x} "
              f"datasize={d.datasize} blocks={d.blocks} date2={date2:#010x}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
