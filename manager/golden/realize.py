"""manager/golden/realize.py -- an import case spec -> a card directory, and a card directory -> its manifest.
Aether Card Manager, GPL-3.0-or-later. The C# test (Tests/Saves/TempCard.cs) realizes the same spec the same way.

    python realize.py CASE.json OUTDIR                        lay the case's files under OUTDIR (must not exist)
    python realize.py --manifest CARDDIR --sha SHA [--counts LINE]... [--log LINE]...   print the expect JSON

Spec: {"runs": n, "files": [{"path", "hdr" (64-byte hex), "data": {"len", "seed"}, "tail" (hex)} | {"path", "raw" (hex)} |
       {"path", "head" (hex), "len"} (head then zeros) | {"path", "bkp": {"seq", "date", "marker", "cut", "recs": [...]}}],
       "dirs": [path, ...]}. Data byte i = (seed * 131 + i * 7 + (i >> 8)) & 0xFF.
Manifest: every directory (path + "/") and file under CARDDIR, sorted by the UTF-8 bytes of the path, with size and
sha256; the counts lines of each run; IMPORT.TXT's text."""
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path


def data_bytes(seed: int, n: int) -> bytes:
    return bytes((seed * 131 + i * 7 + (i >> 8)) & 0xFF for i in range(n))


def bkp_bytes(k: dict) -> bytes:
    """A BRAM backup generation (menu/launcher/pgsbak.h): a 64-byte header ("OMSATBKP", u16 version 1, u16 count, u32
    body length, u32 body CRC-32, u32 seq, date[16] NUL-padded, marker[24] space-padded), count x 32-byte table entries
    (the record header's bytes 0x10-0x2F), then each record's .BUP image padded to 4; the last `cut` bytes dropped."""
    import zlib
    imgs = [content(r) for r in k["recs"]]
    body = b"".join(i + bytes((4 - len(i) % 4) % 4) for i in imgs)
    table = b"".join(i[0x10:0x30] for i in imgs)
    h = (b"OMSATBKP" + (1).to_bytes(2, "big") + len(imgs).to_bytes(2, "big") + len(body).to_bytes(4, "big") +
         zlib.crc32(body).to_bytes(4, "big") + k["seq"].to_bytes(4, "big") + k["date"].encode().ljust(16, b"\0") +
         k["marker"].encode().ljust(24, b" "))
    raw = h + table + body
    return raw[:len(raw) - k["cut"]] if k["cut"] else raw


def content(f: dict) -> bytes:
    if "bkp" in f:
        return bkp_bytes(f["bkp"])
    if "raw" in f:
        return bytes.fromhex(f["raw"])
    if "head" in f:                                   # the head bytes, then zeros to len
        h = bytes.fromhex(f["head"])
        return h + bytes(f["len"] - len(h))
    return bytes.fromhex(f["hdr"]) + data_bytes(f["data"]["seed"], f["data"]["len"]) + bytes.fromhex(f.get("tail", ""))


def realize(spec: dict, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=False)
    for d in spec.get("dirs", []):
        (out / d).mkdir(parents=True, exist_ok=True)
    for f in spec["files"]:
        p = out / f["path"]
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content(f))


def manifest(card: Path) -> list[dict]:
    rows = []
    for dp, dns, fns in os.walk(card):
        rel = Path(dp).relative_to(card).as_posix()
        for d in dns:
            rows.append({"path": (f"{rel}/{d}" if rel != "." else d) + "/", "size": 0, "sha256": ""})
        for fn in fns:
            p = Path(dp) / fn
            b = p.read_bytes()
            rows.append({"path": f"{rel}/{fn}" if rel != "." else fn, "size": len(b),
                         "sha256": hashlib.sha256(b).hexdigest()})
    rows.sort(key=lambda r: r["path"].encode("utf-8"))
    return rows


def parse_counts(line: str) -> dict:
    """'counts kai=1 rings=2 ... rc=0' -> {kai: 1, ...}"""
    return {k: int(v) for k, v in (t.split("=") for t in line.split()[1:])}


def main(argv: list[str]) -> int:
    import argparse
    if argv[:1] == ["--manifest"]:
        ap = argparse.ArgumentParser()
        ap.add_argument("--manifest", required=True)
        ap.add_argument("--sha", required=True)
        ap.add_argument("--counts", action="append", default=[])
        ap.add_argument("--log", action="append", default=None)       # a Tidy case: tidy_main.c's lines, verbatim
        a = ap.parse_args(argv)
        card = Path(a.manifest)
        txt = card / "aether/SAVES/IMPORT.TXT"
        out = {"pgsbak_sha": a.sha, "runs": [parse_counts(c) for c in a.counts],
               "import_txt": txt.read_text("ascii") if txt.exists() else None, "tree": manifest(card)}
        if a.log is not None:
            out["log"] = a.log
        sys.stdout.write(json.dumps(out, indent=1, sort_keys=True) + "\n")
        return 0
    if len(argv) == 2:
        realize(json.loads(Path(argv[0]).read_text()), Path(argv[1]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
