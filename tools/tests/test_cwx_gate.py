"""sbuild.check_cwx must pass the CWX tail as built and fire on each defect class it exists for -- a
literal into the launcher's (zeroed) LWRAM, a branch out of the tail, an unpurged jump into the IP, a tail that no
longer fits under 0x00300000 with the staged IGR stub."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402

SYMS = {"___cwx_tail_start": 0x002FF000, "___cwx_tail_end": 0x002FF0FC, "___igr_stub_start": 0x06002800,
        "___igr_stub_end": 0x06002D38, "_om_cwx_go": 0x002FF000, "_om_cwx_hook": 0x002FF080,
        "___cwx_cell": 0x002FF0DC, "___cwx_par": 0x002FF0E0, "___cwx_res": 0x002FF0EC,
        "___cwx_chk": 0x002FF0F0, "___cwx_chimg": 0x002FF0FC + 0x538}   # the cheat record and block

# excerpts of menu/launcher/out/cwx.dis as built this session (objdump -d -j .cwxtail)
GO = """
  2ff000:\tdf 15       \tmov.l\t2ff058 <___cwx_tail_start+0x58>,r15\t! 6002000
  2ff002:\td1 16       \tmov.l\t2ff05c <___cwx_tail_start+0x5c>,r1\t! 200000 <_lstart>
  2ff004:\td2 16       \tmov.l\t2ff060 <___cwx_tail_start+0x60>,r2\t! 2ff000 <___cwx_tail_start>
  2ff00e:\t8b fb       \tbf\t2ff008 <___cwx_tail_start+0x8>
  2ff010:\td3 14       \tmov.l\t2ff064 <___cwx_tail_start+0x64>,r3\t! 2ff0e0 <___cwx_par>
  2ff034:\td1 0c       \tmov.l\t2ff068 <___cwx_tail_start+0x68>,r1\t! 6000290
  2ff038:\td1 0c       \tmov.l\t2ff06c <___cwx_tail_start+0x6c>,r1\t! 60002b0
  2ff03c:\td0 0c       \tmov.l\t2ff070 <___cwx_tail_start+0x70>,r0\t! 6000344
  2ff040:\td5 0c       \tmov.l\t2ff074 <___cwx_tail_start+0x74>,r5\t! ffff7fff
  2ff042:\t40 0b       \tjsr\t@r0
  2ff046:\td1 0c       \tmov.l\t2ff078 <___cwx_tail_start+0x78>,r1\t! fffffe92
  2ff048:\te0 11       \tmov\t#17,r0
  2ff04c:\td0 0b       \tmov.l\t2ff07c <___cwx_tail_start+0x7c>,r0\t! 6002100
  2ff04e:\t40 0b       \tjsr\t@r0
  2ff052:\taf fe       \tbra\t2ff052 <___cwx_tail_start+0x52>
"""
HOOK = """
  2ff084:\td6 10       \tmov.l\t2ff0c8 <_om_cwx_hook+0x48>,r6\t! 2ff0ec <___cwx_res>
  2ff08e:\td1 0f       \tmov.l\t2ff0cc <_om_cwx_hook+0x4c>,r1\t! 2ff0fc <___cwx_img>
  2ff0a2:\t8f f7       \tbf.s\t2ff094 <_om_cwx_hook+0x14>
  2ff0b2:\td1 07       \tmov.l\t2ff0d0 <_om_cwx_hook+0x50>,r1\t! 600026c
  2ff0b6:\td1 07       \tmov.l\t2ff0d4 <_om_cwx_hook+0x54>,r1\t! 6002270
  2ff0b8:\td0 07       \tmov.l\t2ff0d8 <_om_cwx_hook+0x58>,r0\t! 6000284
  2ff0c2:\t40 2b       \tjmp\t@r0
"""


def test_tail_as_built_passes():
    f = sbuild.igr_facts(GO + HOOK)
    assert f["order"] == ["jsr", "purge", "jsr", "jmp"]
    sbuild.check_cwx(SYMS, f)


def test_launcher_literal_is_rejected():
    leak = GO + "  2ff006:\td0 02       \tmov.l\t2ff010 <x>,r0\t! 2000c0 <_memcpy>\n" + HOOK
    with pytest.raises(SystemExit, match=r"tail loads addresses outside.*0x2000c0"):
        sbuild.check_cwx(SYMS, sbuild.igr_facts(leak))


def test_branch_out_of_the_tail_is_rejected():
    leak = GO + "  2ff006:\tbf fe       \tbsr\t2003000 <_far>\n" + HOOK
    with pytest.raises(SystemExit, match=r"tail branches outside itself"):
        sbuild.check_cwx(SYMS, sbuild.igr_facts(leak))


def test_unpurged_ip_jump_is_rejected():
    with pytest.raises(SystemExit, match=r"tail transfers \['jsr', 'jsr', 'jmp'\]"):
        sbuild.check_cwx(SYMS, sbuild.igr_facts(GO.replace("mov\t#17,r0", "mov\t#16,r0") + HOOK))


def test_tail_too_big_for_the_staged_stub_is_rejected():
    big = dict(SYMS, ___cwx_tail_end=0x002FFB00)
    with pytest.raises(SystemExit, match=r"staged stub 1336 B \+ cheat block 400 B not inside"):
        sbuild.check_cwx(big, sbuild.igr_facts(GO + HOOK))


def test_tail_with_room_for_the_stub_but_not_the_cheat_block_is_rejected():
    """the staged cheat block (OM_CHK_MAX = 400 B) follows the stub image under 0x00300000"""
    end = 0x00300000 - 0x538 - 200
    tight = dict(SYMS, ___cwx_tail_end=end, ___cwx_chimg=end + 0x538)
    with pytest.raises(SystemExit, match="cheat block 400 B not inside"):
        sbuild.check_cwx(tight, sbuild.igr_facts(GO + HOOK))
