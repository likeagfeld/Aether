"""_end must leave at least 64 KB of HWRAM below SGL's work area (HW_END_MAX), and the cold -Os set
names real files, none of them per-frame code."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def test_floor_passes_at_exactly_64k():
    assert sbuild.check_hw_end({"_end": sbuild.HW_END_MAX - 0x10000}) == sbuild.HW_END_MAX - 0x10000


def test_floor_fails_one_byte_under():
    with pytest.raises(SystemExit, match="headroom"):
        sbuild.check_hw_end({"_end": sbuild.HW_END_MAX - 0x10000 + 1})


def test_cold_set_exists_and_is_not_hot():
    assert sbuild.COLD_OS and not (sbuild.COLD_OS & sbuild.HOT)
    for p in sbuild.COLD_OS | sbuild.HOT:
        assert (ROOT / p).is_file(), p
