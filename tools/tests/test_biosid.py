"""every BIOS image on this machine has a CRC-32 in omsat.biosid's table, the table has no duplicate CRC,
and the C table (menu/src/biosid.c) carries the same rows."""
import re
import zlib
from pathlib import Path

import pytest

from omsat import biosid

ROOT = Path(__file__).resolve().parents[2]
BIOS_DIR = Path(r"D:\Sega Saturn BIOS")


def test_table_unique():
    crcs = [r.crc for r in biosid.TABLE]
    assert len(crcs) == len(set(crcs)) == 9     # 11 files, 2 byte-identical pairs (measured 2026-11-20)


@pytest.mark.skipif(not BIOS_DIR.is_dir(), reason="BIOS folder not on this machine")
def test_every_local_image_is_known():
    for p in sorted(BIOS_DIR.glob("*.bin")):
        crc = zlib.crc32(p.read_bytes()) & 0xFFFFFFFF
        assert biosid.lookup(crc) is not None, f"{p.name} {crc:08x} missing from TABLE"


def test_c_table_matches():
    src = (ROOT / "menu" / "src" / "biosid.c").read_text()
    rows = {int(h, 16) for h in re.findall(r"\{\s*0x([0-9a-fA-F]{8})u,", src)}
    assert rows == {r.crc for r in biosid.TABLE}
