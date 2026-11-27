"""CRC-32 (zlib) of each Saturn BIOS image, so the console's diagnostics line names its image.
`python -m omsat.biosid [DIR]` prints the table rows for DIR (default D:\Sega Saturn BIOS)."""
from __future__ import annotations

import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

JP, US, OTHER = 1, 2, 3


@dataclass(frozen=True)
class Bios:
    crc: int
    name: str
    family: int


# Measured 2026-11-20 with zlib.crc32 over each file in D:\Sega Saturn BIOS (all 524,288 B); re-measured in M6 Task 1
# Step 1, every value equal. Two pairs are byte-identical: "Sega Saturn BIOS (EUR)" = 1.01a (U),
# "Sega Saturn BIOS (Oct 12, 1994)" = 1.003 (J).
TABLE: tuple[Bios, ...] = (
    Bios(0x2aba43c2, "Saturn 1.00 (J)", JP),
    Bios(0x224b752c, "Saturn 1.01 (J)", JP),
    Bios(0xb3c63c25, "Saturn 1.003 (J) = Oct 12, 1994", JP),
    Bios(0xe4d61811, "V-Saturn 1.01 (J)", JP),
    Bios(0x721e1b60, "HI-Saturn 1.01 (J)", JP),
    Bios(0x3408dbf4, "Hi-Saturn 1.02 (J)", JP),
    Bios(0x6abfefea, "GameNavi HiSaturn 1.03", OTHER),
    Bios(0xf90f0089, "Saturn 1.00a (U)", US),
    Bios(0x4afcf0fa, "Saturn 1.01a (U) = EUR", US),
)


def lookup(crc: int) -> Bios | None:
    return next((b for b in TABLE if b.crc == crc), None)


def main(argv=None) -> int:
    d = Path((argv or sys.argv[1:] or [r"D:\Sega Saturn BIOS"])[0])
    for p in sorted(d.glob("*.bin")):
        data = p.read_bytes()
        fam = "JP" if "(J)" in p.name else "US" if "(U)" in p.name else "OTHER"
        print(f"    Bios(0x{zlib.crc32(data) & 0xFFFFFFFF:08x}, {p.stem!r}, {fam}),   # {len(data)} B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
