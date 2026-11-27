"""Pseudo Saturn Kai's cheat bootstrap and in-RAM handler (menu/launcher/kai/*.s), their assembled bytes
run on the SH-2 model (tools/omsat/sh2kai.py) from the trapa a master code plants: the handler's semantics against
simulated memory, the bootstrap's register save/restore, the displaced instruction and the return. sbuild.check_chkai
requires the linked LAUNCHER.BIN copies to equal these bytes, and reruns run_trap on them."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from omsat import sh2kai as k  # noqa: E402

PLACE, MASTER = 0x06002580, 0x06000914
OR_R4_R2 = 0x224B                                      # the BIOS dispatcher's instruction at 0x06000914 (plan M1)


def ram(*addrs):
    return {a: 0 for a in addrs}


def test_bytes_are_kais():
    assert len(k.KAI_BOOT) == 40 and len(k.KAI_HDLR) == 96
    assert k.KAI_BOOT[0x20:0x24] == bytes.fromhex("00090009")      # the patched-opcode slot, two nops
    assert k.KAI_BOOT[0x24:0x26] == bytes.fromhex("002b")          # rte
    assert k.KAI_HDLR[0x50:0x54] == bytes.fromhex("cafea1fa")      # Kai's end marker


def test_literal_offsets_measured():
    """mov.l @(disp,PC): (PC & ~3) + 4 + 4 disp. The bootstrap loads r4 (cfg) at +12 and r0 (handler) at +14."""
    op_r4 = int.from_bytes(k.KAI_BOOT[12:14], "big")
    op_r0 = int.from_bytes(k.KAI_BOOT[14:16], "big")
    assert op_r4 >> 8 == 0xD4 and (12 & ~3) + 4 + 4 * (op_r4 & 0xFF) == k.BOOT_P_CFG == 44
    assert op_r0 >> 8 == 0xD0 and (14 & ~3) + 4 + 4 * (op_r0 & 0xFF) == k.BOOT_P_HDLR == 40


def test_codes_16_and_8_bit():
    blk = k.config(PLACE, OR_R4_R2, [(0x06078A5C, 0x0080, 1), (0x06070001, 0x1263, 3)])
    cpu, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06078A5C, 0x06070000))
    assert m.rd(0x06078A5C, 2) == 0x0080
    assert m.rd(0x06070000, 2) == 0x0063                       # 0x03: the value's low byte, at the odd address
    assert [(a, n) for a, n, v in m.writes if a < 0x06001000 or a >= 0x06002000] == [(0x06078A5C, 2), (0x06070001, 1)]


def test_other_cached_command_writes_a_byte():
    """plan M5: the `bf` after the `bra` is dead, so any cached command but 0x01 writes the low byte."""
    blk = k.config(PLACE, OR_R4_R2, [(0x06070000, 0xABCD, 8)])
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06070000))
    assert m.rd(0x06070000, 2) == 0xCD00


def test_enable_code():
    blk = k.config(PLACE, OR_R4_R2, [(0x06070100, 0xBEEF, 1)], en=(0x06070000, 7))
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, {0x06070000: 6, 0x06070100: 0})
    assert m.rd(0x06070100, 2) == 0
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, {0x06070000: 7, 0x06070100: 0})
    assert m.rd(0x06070100, 2) == 0xBEEF


def test_zero_codes_runs_nothing():
    blk = k.config(PLACE, OR_R4_R2, [])
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06070000))
    assert [w for w in m.writes if not 0x06001000 <= w[0] < 0x06002000] == []


def test_bootstrap_restores_and_runs_the_displaced_instruction():
    regs = [0x11111111 * i for i in range(15)]
    blk = k.config(PLACE, OR_R4_R2, [(0x06078A5C, 0x0080, 1)])
    cpu, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06078A5C), regs=regs)
    want = list(regs)
    want[2] = regs[2] | regs[4]                                 # `or r4,r2`, run once, with the game's registers
    assert cpu.r[:15] == want
    assert cpu.r[15] == 0x06002000 and cpu.pr == 0xCAFEF00D
    assert cpu.pc == MASTER + 2                                 # rte back after the trapa
    assert cpu.sr == 0xF0


@pytest.mark.parametrize("vec", [5, 10])
def test_vector_from_the_opcode(vec):
    blk = k.config(PLACE, 0x0009, [(0x06078A5C, 0x0080, 1)])
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC300 | vec, ram(0x06078A5C))
    assert m.rd(0x06078A5C, 2) == 0x0080


def test_mutation_a_wrong_cfg_offset_is_caught():
    """the gate can fail: a bootstrap whose cfg literal is one long off reads the handler pointer as the config"""
    bad = bytearray(k.KAI_BOOT)
    bad[13] -= 1                                                # mov.l @(+40),r4 instead of @(+44)
    blk = k.config(PLACE, OR_R4_R2, [(0x06078A5C, 0x0080, 1)], boot=bytes(bad))
    try:
        _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06078A5C))
        held = m.rd(0x06078A5C, 2) == 0x0080
    except k.Stop:
        held = False
    assert not held


def test_mutation_count_offset_is_caught():
    bad = bytearray(k.KAI_HDLR)
    bad[0x17] = 0x3C                                            # mov.w @(24,r3) instead of @(26,r3): count = 0
    blk = k.config(PLACE, OR_R4_R2, [(0x06078A5C, 0x0080, 1)], hdlr=bytes(bad))
    _, m = k.run_trap(blk, PLACE, MASTER, 0xC305, ram(0x06078A5C))
    assert m.rd(0x06078A5C, 2) != 0x0080
