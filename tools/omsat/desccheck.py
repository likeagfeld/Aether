"""Validate Satiator .desc files (little-endian; research_satiator.md §1.4: u16 nseg, packed 23-byte seg_desc_t
{start FAD, length, file_offset, filename_offset, flags, secsize, track, index, q_mode}, then u8-length names)
against the image files they name. `python -m omsat.desccheck DESC IMAGE_DIR`."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

SEG = struct.Struct("<IIIIHHBBB")
assert SEG.size == 23


def parse(data: bytes) -> list[dict]:
    n = struct.unpack_from("<H", data, 0)[0]
    out = []
    for i in range(n):
        start, length, fofs, fnoff, flags, secsize, track, index, q = SEG.unpack_from(data, 2 + SEG.size * i)
        name = data[fnoff + 1:fnoff + 1 + data[fnoff]].decode("latin-1") if fnoff else ""
        out.append({"start": start, "length": length, "file_offset": fofs, "flags": flags, "secsize": secsize,
                    "track": track, "index": index, "q": q, "name": name})
    return out


def check(desc: bytes, image_dir: Path) -> list[str]:
    errs, fad = [], 150
    segs = parse(desc)
    if not segs:
        return ["no segments"]
    for i, s in enumerate(segs):
        if s["start"] != fad:
            errs.append(f"seg {i}: start FAD {s['start']} != {fad} (not contiguous from 150)")
        fad = s["start"] + s["length"]
        if s["secsize"] not in (2048, 2352):
            errs.append(f"seg {i}: secsize {s['secsize']}")
        if s["q"] not in (0x41, 0x01):
            errs.append(f"seg {i}: q_mode {s['q']:#x}")
        f = Path(image_dir) / s["name"]
        if not f.is_file():
            errs.append(f"seg {i}: file {s['name']!r} missing")
        elif s["file_offset"] + s["length"] * s["secsize"] > f.stat().st_size:
            errs.append(f"seg {i}: data beyond end of {s['name']!r}")
    return errs


if __name__ == "__main__":
    e = check(Path(sys.argv[1]).read_bytes(), Path(sys.argv[2]))
    print("PASS" if not e else "FAIL\n  " + "\n  ".join(e))
    sys.exit(1 if e else 0)
