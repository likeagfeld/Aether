"""sbuild.check_igr must see every literal the stub and hook load, including the ones objdump
labels with a symbol ("! 2000c0 <_memcpy>"), and reject any that points outside the stub/hook and the register list."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402

SYMS = {"___igr_stub_start": 0x06002800, "___igr_stub_end": 0x06002D48, "___igr_hook_start": 0x06009800,
        "___igr_hook_end": 0x06009858, "_om_igr_entry": 0x06002800, "_om_igr_run": 0x06002AF4,
        "_om_igr_hook": 0x06009808, "___igr_code_end": 0x06002BC4}

STUB_OK = """
 6002800:\td1 03       \tmov.l\t6002810 <_om_igr_entry+0x10>,r1\t! 6002af4 <_om_igr_run>
 6002802:\td2 03       \tmov.l\t6002814 <_om_igr_entry+0x14>,r2\t! 25890008
 6002804:\taf fc       \tbra\t6002800 <_om_igr_entry>
"""
HOOK_OK = """
 6009808:\td1 0b       \tmov.l\t6009838 <_om_igr_hook+0x30>,r1\t! 6009858 <___igr_hook_end>
 600980c:\td3 0c       \tmov.l\t6009840 <_om_igr_hook+0x38>,r3\t! f1 <___igr_stub_longs>
 600982a:\td1 09       \tmov.l\t6009850 <_om_igr_hook+0x48>,r1\t! 6002270
"""


def test_symbol_labelled_literals_are_parsed():
    f = sbuild.igr_facts(STUB_OK)
    assert f["lits"] == [0x06002AF4, 0x25890008]
    assert sbuild.igr_facts(HOOK_OK)["lits"] == [0x06009858, 0xF1, 0x06002270]


def test_clean_stub_and_hook_pass():
    sbuild.check_igr(SYMS, sbuild.igr_facts(STUB_OK), sbuild.igr_facts(HOOK_OK))


def test_memcpy_literal_is_rejected():
    leak = STUB_OK + " 6002806:\td0 02       \tmov.l\t6002810 <_om_igr_entry+0x10>,r0\t! 2000c0 <_memcpy>\n"
    with pytest.raises(SystemExit, match=r"stub loads addresses outside.*0x2000c0"):
        sbuild.check_igr(SYMS, sbuild.igr_facts(leak), sbuild.igr_facts(HOOK_OK))


def test_branch_out_of_the_stub_is_rejected():
    leak = STUB_OK + " 6002806:\tbf fe       \tbsr\t6003000 <_far>\n"
    with pytest.raises(SystemExit, match=r"stub branches outside itself"):
        sbuild.check_igr(SYMS, sbuild.igr_facts(leak), sbuild.igr_facts(HOOK_OK))


def test_pool_words_are_not_instructions():
    """the literal 0x0003FFFF's first word decodes as `bsrf r0`; it is data, not a branch."""
    dis = STUB_OK + (" 6002806:\td0 01       \tmov.l\t600280c <_om_igr_entry+0xc>,r0\t! 3ffff\n"
                     " 600280c:\t00 03       \tbsrf\tr0\n"
                     " 600280e:\tff ff       \t.word 0xffff\n")
    f = sbuild.igr_facts(dis)
    assert f["indirect"] == 0 and 0x3FFFF in f["lits"]
    real = STUB_OK + " 6002806:\t00 03       \tbsrf\tr0\n"
    assert sbuild.igr_facts(real)["indirect"] == 1


def test_stub_not_filling_its_home_is_rejected():
    """Astal (2026-11-23): the stub image must reach 0x06002D48, whose last long is the seal SYS_EXECDMP's guard checks."""
    with pytest.raises(SystemExit, match=r"does not fill"):
        sbuild.check_igr(dict(SYMS, ___igr_stub_end=0x06002BC4), sbuild.igr_facts(STUB_OK), sbuild.igr_facts(HOOK_OK))
    with pytest.raises(SystemExit, match=r"runs into the seal word"):
        sbuild.check_igr(dict(SYMS, ___igr_code_end=0x06002D48), sbuild.igr_facts(STUB_OK), sbuild.igr_facts(HOOK_OK))
    with pytest.raises(SystemExit, match=r"first byte"):
        sbuild.check_igr(dict(SYMS, _om_igr_entry=0x06002804), sbuild.igr_facts(STUB_OK), sbuild.igr_facts(HOOK_OK))
