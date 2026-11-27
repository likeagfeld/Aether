"""M2 deferred (b): the IP's SEGA SYS_*.O must carry no unfolded COFF reloc addend (count_unfolded == 0).
Measured clean at M2 (sbuild._fold_sgl_libs docstring); this gate keeps them that way. (c): launcher caps agree."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sbuild  # noqa: E402
from test_coffrel_gate import _obj  # noqa: E402  (minimal converted-COFF ELF with one DIR32)

ROOT = Path(__file__).resolve().parents[2]


def test_ip_gate_red_on_unfolded_addend(tmp_path):
    bad = tmp_path / "SYS_BAD.O"
    bad.write_bytes(_obj(inplace=0x5E, addend=-0x28))
    with pytest.raises(SystemExit, match=r"SYS_BAD\.O: 1 unfolded"):
        sbuild.check_ip_objects([bad])


def test_ip_gate_green_on_clean_object(tmp_path):
    ok = tmp_path / "SYS_OK.O"
    ok.write_bytes(_obj(inplace=0x36, addend=0))
    sbuild.check_ip_objects([ok])


def test_launcher_cap_comment_and_values_agree():
    from omsat.lnchhdr import MAX_BODY
    ld = (ROOT / "menu/launcher/launcher.ld").read_text()
    length = int(re.search(r"LENGTH = (0x[0-9A-Fa-f]+)", ld).group(1), 16)
    lay = (ROOT / "menu/src/omlayout.h").read_text()
    om_max = int(re.search(r"OM_LAUNCHER_MAX\s+(0x[0-9A-Fa-f]+)u", lay).group(1), 16)
    assert length == 0x7B000 and MAX_BODY == om_max == 0x7F000
    assert 0x00200000 + length == 0x0027F000 - 0x4000      # the image ends where the 16 KB stack begins
    assert "0x7F000" in ld and "OM_LAUNCHER_MAX" in ld     # the reconciling comment exists
