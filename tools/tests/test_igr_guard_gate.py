"""Astal (hardware 2026-11-23, build 3c64908): A+B+C+Start at the title froze with no in-game-reset colour, because the
game loads its title overlays over the stub at 0x06002800 and SYS_EXECDMP pointed straight into it. sbuild.check_igr_guard
runs SYS_EXECDMP's guard (menu/launcher/igrguard.c, the bytes as linked) on a small SH-2 model and must pass it as built
and fire on each defect class: a guard that always enters the stub (the 3c64908 behaviour), one that never does, no
SYSRES, interrupts left on, the guard outside the BIOS copyright text, and SYS_EXECDMP not pointed at it."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402

# menu/launcher/out/body.bin at ___igr_guard_lma, 64 bytes, as built this session
GUARD = bytes.fromhex("d10d9317e000621643108ffc302c88a58f01d109412b0002cbf0400ed107621372bce301e40de010402840108bfd"
                      "2130aff92240015200090600280020100063")
SYMS = {"___igr_guard_start": 0x060003C0, "___igr_guard_end": 0x06000400, "_om_igr_guard": 0x060003C0}
BOOT = """002029a4 <_om_cwx_boot>:
  202c2a:\td9 26       \tmov.l\t202cc4 <_om_cwx_boot+0x320>,r9\t! 60003c0 <___igr_guard_start>
  202c2e:\td1 26       \tmov.l\t202cc8 <_om_cwx_boot+0x324>,r1\t! 600026c
"""
HOOK_LITS = [0x06009870, 0x06002800, 0x152, 0x060003C0, 0x10, 0x06009804, 0x060003C0, 0x0600026C, 0x06002270,
             0x06000284]


def patch(at: int, word: int) -> bytes:
    b = bytearray(GUARD)
    b[at:at + 2] = word.to_bytes(2, "big")
    return bytes(b)


def test_guard_as_built_passes():
    sbuild.check_igr_guard(SYMS, GUARD, BOOT, HOOK_LITS)


def test_model_runs_the_three_paths():
    longs = (sbuild.IGR_STUB_END - sbuild.IGR_STUB) // 4
    img = [k * 7 + 1 for k in range(longs)]
    img[-1] = (sbuild.IGR_SEAL - sum(img[:-1])) & 0xFFFFFFFF
    home = {sbuild.IGR_STUB + 4 * k: v for k, v in enumerate(img)}
    st = sbuild.sh2_guard_run(GUARD, 0x060003C0, home)
    assert st["pc"] == 0x06002800 and st["smpc"] == []
    home[sbuild.IGR_STUB] ^= 1
    st = sbuild.sh2_guard_run(GUARD, 0x060003C0, home, steps=2000)
    assert st["pc"] is None and st["smpc"][:2] == [(0x20100063, 1), (0x2010001F, 0x0D)]


def test_guard_that_always_enters_the_stub_is_rejected():
    """the 3c64908 behaviour (SYS_EXECDMP straight into whatever is at 0x06002800): the `bf.s` to the reset made a nop"""
    with pytest.raises(SystemExit, match=r"overlay: the guard left for 0x6002800"):
        sbuild.check_igr_guard(SYMS, patch(0x10, 0x0009), BOOT, HOOK_LITS)


def test_guard_that_never_enters_is_rejected():
    with pytest.raises(SystemExit, match=r"intact stub"):
        sbuild.check_igr_guard(SYMS, patch(0x0E, 0x88A4), BOOT, HOOK_LITS)   # cmp/eq #-92: the wrong seal


def test_guard_without_sysres_is_rejected():
    with pytest.raises(SystemExit, match=r"no repeated SMPC SYSRES"):
        sbuild.check_igr_guard(SYMS, patch(0x24, 0xE40C), BOOT, HOOK_LITS)   # mov #12,r4


def test_guard_with_interrupts_left_on_is_rejected():
    with pytest.raises(SystemExit, match=r"interrupts not masked"):
        sbuild.check_igr_guard(SYMS, patch(0x18, 0xCB00), BOOT, HOOK_LITS)   # or #0,r0


def test_guard_outside_the_copyright_text_is_rejected():
    with pytest.raises(SystemExit, match=r"not inside 0x60003c0..0x6000400"):
        sbuild.check_igr_guard(dict(SYMS, ___igr_guard_end=0x06000404), GUARD + b"\0\0\0\0", BOOT, HOOK_LITS)
    with pytest.raises(SystemExit, match=r"missing ___igr_guard_"):
        sbuild.check_igr_guard({}, b"", BOOT, HOOK_LITS)


def test_execdmp_not_pointed_at_the_guard_is_rejected():
    with pytest.raises(SystemExit, match=r"om_cwx_boot does not point"):
        sbuild.check_igr_guard(SYMS, GUARD, BOOT.replace("! 600026c", "! 6000270"), HOOK_LITS)
    with pytest.raises(SystemExit, match=r"JHL hook"):
        sbuild.check_igr_guard(SYMS, GUARD, BOOT, [v for v in HOOK_LITS if v != 0x060003C0])
