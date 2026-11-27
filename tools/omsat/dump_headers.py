"""F-real: dump Saturn System IDs (256 bytes, metadata only) from a directory of disc images."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .binfmt import FormatError
from .iphdr import read_system_id
from .keys import is_pal_suffixed, normalise


def dump_dir(root):
    entries, skipped = [], 0
    for p in sorted(Path(root).rglob("*")):
        if p.suffix.lower() not in (".iso", ".cue") or not p.is_file():
            continue
        try:
            s = read_system_id(p)
        except (FormatError, OSError):
            skipped += 1
            continue
        entries.append({"file": p.name, "product": s.product, "version": s.version, "date": s.release_date,
                        "device": s.device, "area": s.area, "peripherals": s.peripherals, "maker": s.maker,
                        "title": s.title, "raw_hex": s.raw.hex()})
    return entries, skipped


def summarise(entries) -> dict:
    e_only = [e for e in entries if "E" in e["area"] and not set(e["area"]) & set("JTU")]
    return {"discs": len(entries),
            "pal_suffixed": sum(is_pal_suffixed(normalise(e["product"])) for e in entries),
            "area_E_only": len(e_only),
            "area_E_only_with_suffix": sum(is_pal_suffixed(normalise(e["product"])) for e in e_only),
            "product_len_10": sum(len(e["product"]) == 10 for e in entries)}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    entries, skipped = dump_dir(a.root)
    with open(a.out, "w", encoding="utf-8") as fh:
        json.dump({"entries": entries, "skipped_non_saturn": skipped}, fh, indent=2)
    print(json.dumps({"skipped_non_saturn": skipped, **summarise(entries)}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
