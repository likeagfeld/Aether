"""Decision D2: the menu drives the SCSP directly; SGL/SBL sound must never be linked (slInitSound + slCDDAOn is the
skill's measured SCSP-bias defect; slPCMOn would claim PCM_Work and cannot loop seamlessly). Task 14/15."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402


def test_red_when_sgl_sound_is_linked():
    with pytest.raises(SystemExit, match="_slInitSound"):
        sbuild.check_forbidden_symbols({"_end": 1, "_slInitSound": 0x6005000})
    with pytest.raises(SystemExit, match="_slCDDAOn"):
        sbuild.check_forbidden_symbols({"_slCDDAOn": 1})
    for name in sbuild.FORBIDDEN_APP_SYMBOLS:                     # every listed name trips it on its own
        with pytest.raises(SystemExit, match=name):
            sbuild.check_forbidden_symbols({name: 1})


def test_green_when_absent(capsys):
    sbuild.check_forbidden_symbols({"_end": 1, "_ss_main": 2, "_slSynch": 3})
    assert "none of" in capsys.readouterr().out


def test_gate_runs_on_the_menu_link():
    src = (Path(sbuild.__file__)).read_text(encoding="utf-8")
    assert "check_forbidden_symbols(syms)" in src.split("def build_app")[1].split("def ")[0]
