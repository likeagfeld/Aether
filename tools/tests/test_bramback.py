# tools/tests/test_bramback.py -- the BRAM backup generation file (contract/SPEC.md §5.17; pgsbak.h)
from pathlib import Path

import pytest

from omsat import bramback
from omsat.bupfile import BupDir

ROOT = Path(__file__).resolve().parents[2]


def rec(name: bytes, size: int, seed: int, date: int) -> tuple[BupDir, bytes]:
    """test_pgsbak.c's brec(): data[i] = i * 31 + seed; fake_bram.c: comment TEST, language 1, ceil((size + 4) / 58)"""
    return (BupDir(name, b"TEST", 1, date, size, (size + 4 + 57) // 58),
            bytes((i * 31 + seed) & 0xFF for i in range(size)))


RECS = [rec(b"GHEROES_001", 10, 1, 100), rec(b"GHEROES_002", 3001, 2, 101), rec(b"PSEUDO__SAT", 300, 3, 7)]


def test_round_trip_and_split(tmp_path):
    b = bramback.build(RECS, 1, "20261122-053000", "GS-9031@U 0 0000A001")
    info = bramback.parse(b)
    assert (info["seq"], info["date"], info["marker"]) == (1, "20261122-053000", "GS-9031@U 0 0000A001")
    assert [(d.name, data) for d, data in info["records"]] == [(d.name, data) for d, data in RECS]
    src = tmp_path / "00001.BKP"
    src.write_bytes(b)
    out = tmp_path / "out"
    out.mkdir()
    (out / "GHEROES_001.BUP").write_bytes(b"keep")              # never overwritten
    names = sorted(p.name for p in bramback.split(src, out))
    assert names == ["GHEROES_001~1.BUP", "GHEROES_002.BUP", "PSEUDO__SAT.BUP"]
    assert (out / "GHEROES_001.BUP").read_bytes() == b"keep"


def test_damaged_files_refused():
    b = bramback.build(RECS, 1)
    with pytest.raises(bramback.BackupError, match="length"):
        bramback.parse(b[:-1])                                   # an interrupted write
    with pytest.raises(bramback.BackupError, match="crc"):
        bramback.parse(b[:-1] + bytes([b[-1] ^ 1]))
    with pytest.raises(bramback.BackupError, match="magic"):
        bramback.parse(b"X" + b[1:])


def test_matches_the_launcher_byte_for_byte():
    """the launcher's own writer (pgsbak.c, run by test_pgsbak.c in hosttest) against this builder"""
    c = ROOT / "build" / "host-tests" / "c_gen.bkp"
    if not c.exists():
        pytest.skip("run tools/hosttest.sh first (test_pgsbak writes build/host-tests/c_gen.bkp)")
    assert c.read_bytes() == bramback.build(RECS, 1, "20261122-053000", "GS-9031@U 0 0000A001")
