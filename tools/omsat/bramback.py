"""omsat.bramback -- Aether's BRAM backup generation file (contract/SPEC.md §5.17, "BRAM backup";
menu/launcher/pgsbak.h). /aether/SAVES/_BACKUP/<seq>[-<YYYYMMDD-HHMMSS>].BKP holds one copy of the console's
backup RAM, taken before a launch. This module reads one, checks it, and splits it back into §5.16 .BUP files that
Pseudo Saturn Kai, Save Game Copier or Aether's own SAVES folders take as they are.

Layout (big-endian): a 64-byte header -- "OMSATBKP", u16 version (1), u16 count, u32 body length, u32 body CRC-32,
u32 seq, char date[16], char marker[24] -- then count x 32-byte entries (name[12], comment[11], language, u32 date,
u32 datasize), then the body: each record as a .BUP image (64-byte header + data), padded to 4 bytes.

    python -m omsat.bramback list  <file.BKP>
    python -m omsat.bramback split <file.BKP> <out_dir>      (never overwrites: an existing name gets ~1..~9)
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

from . import bupfile

MAGIC = b"OMSATBKP"
HDR = 64
ENT = 32


class BackupError(ValueError):
    pass


def build(records: list[tuple[bupfile.BupDir, bytes]], seq: int, date: str = "", marker: str = "") -> bytes:
    """The file the launcher writes, for tests and tools: records as (dir, data)."""
    table, body = b"", b""
    for d, data in records:
        img = bupfile.encode(d, data)
        body += img + bytes((4 - len(data) % 4) % 4)
        table += (d.name.ljust(12, b"\0") + d.comment.ljust(11, b"\0")[:11] + bytes([d.language])
                  + struct.pack(">II", d.date, d.datasize))
    head = (MAGIC + struct.pack(">HHIII", 1, len(records), len(body), zlib.crc32(body), seq)
            + date.encode("ascii")[:15].ljust(16, b"\0") + marker.encode("ascii")[:23].ljust(24, b"\0"))
    assert len(head) == HDR
    return head + table + body


def parse(b: bytes) -> dict:
    """-> {"seq", "date", "marker", "records": [(dir, data)]}. Raises BackupError on any inconsistency."""
    if len(b) < HDR or b[:8] != MAGIC:
        raise BackupError("magic: not an OMSATBKP file")
    ver, count, blen, crc, seq = struct.unpack(">HHIII", b[8:24])
    if ver != 1:
        raise BackupError(f"version {ver}")
    off = HDR + count * ENT
    if len(b) != off + blen:
        raise BackupError(f"length: {len(b)} B, header says {off + blen} (an interrupted write?)")
    if zlib.crc32(b[off:]) != crc:
        raise BackupError("crc: the body does not match its CRC-32")
    recs = []
    for k in range(count):
        e = b[HDR + k * ENT:HDR + (k + 1) * ENT]
        try:
            d, data, _ = bupfile.decode(b[off:])
        except bupfile.BupError as x:
            raise BackupError(f"record {k}: {x}") from None
        if d.name.ljust(12, b"\0") != e[:12] or struct.unpack(">I", e[28:32])[0] != d.datasize:
            raise BackupError(f"record {k}: the table and the image disagree")
        recs.append((d, data))
        off += HDR + d.datasize + (4 - d.datasize % 4) % 4
    return {"seq": seq, "date": b[24:40].split(b"\0", 1)[0].decode("ascii", "replace"),
            "marker": b[40:64].split(b"\0", 1)[0].decode("ascii", "replace"), "records": recs}


def file_name(name: bytes, taken: set[str]) -> str:
    """The launcher's archive name rule (pgsplan.h om_pgs_file_name): A-Z 0-9 _ - kept, others '_', ~1..~9."""
    base = "".join(chr(c) if (chr(c).isupper() or chr(c).isdigit() or chr(c) in "_-") and c < 128 else "_"
                   for c in name)
    for i in range(10):
        f = base + (f"~{i}" if i else "") + ".BUP"
        if f.upper() not in taken:
            return f
    raise BackupError(f"no free file name for {name!r}")


def split(src: Path, out: Path) -> list[Path]:
    info = parse(src.read_bytes())
    out.mkdir(parents=True, exist_ok=True)
    taken = {p.name.upper() for p in out.iterdir()}
    written = []
    for d, data in info["records"]:
        f = file_name(d.name, taken)
        taken.add(f.upper())
        p = out / f
        p.write_bytes(bupfile.encode(bupfile.BupDir(d.name, d.comment, d.language, d.date, d.datasize, d.blocks), data))
        written.append(p)
    return written


def main(argv: list[str] | None = None) -> int:
    a = sys.argv[1:] if argv is None else argv
    if len(a) == 2 and a[0] == "list":
        info = parse(Path(a[1]).read_bytes())
        print(f"seq {info['seq']}  date {info['date'] or '(clock not set)'}  marker {info['marker'] or '-'}")
        for d, data in info["records"]:
            print(f"  {d.name.decode('ascii'):<11}  {d.datasize:6d} B  date {d.date:#010x}")
        return 0
    if len(a) == 3 and a[0] == "split":
        for p in split(Path(a[1]), Path(a[2])):
            print(p)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
