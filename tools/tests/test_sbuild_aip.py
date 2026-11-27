"""M2: unit test for sbuild.py's AIP stub bytes. sbuild.py is a script, not part of the
omsat package, so it is imported by adding tools/ (its parent) to sys.path, the same way sbuild.py
itself locates the omsat package."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402


def test_aip_stub_bytes_exact():
    # mov.l @(1,PC),r0 ; jmp @r0 ; nop ; nop ; .long 0x06004000 ; nop*10 (padded to AIP_MIN_SIZE/0x20)
    expected = (
        b"\xd0\x01"          # mov.l @(1,PC),r0
        b"\x40\x2b"          # jmp @r0
        b"\x00\x09"          # nop (delay slot)
        b"\x00\x09"          # nop (alignment padding before the constant)
        b"\x06\x00\x40\x00"  # .long 0x06004000, big-endian
        + b"\x00\x09" * 10   # nop * 10 -- padding to the ST-040-R4 Table 4.1 AIP minimum (0x20 bytes)
    )
    assert len(expected) == 0x20
    assert sbuild._aip_stub_bytes(0x06004000) == expected


def test_aip_stub_bytes_length_is_table_4_1_minimum():
    assert len(sbuild._aip_stub_bytes(0x06004000)) == sbuild.AIP_MIN_SIZE == 0x20


def test_aip_stub_bytes_disassembly_fields():
    """Decode the three live instructions by hand (same opcode layout verified against
    saturn-sh2-elf-objdump this session -- docs/ledger.md "Build findings" #2) and check the literal
    jump target, rather than only comparing opaque bytes."""
    b = sbuild._aip_stub_bytes(0x0BADF00D)
    mov_l, jmp, nop1, nop2 = struct.unpack(">HHHH", b[0:8])
    assert mov_l == 0xD001          # mov.l @(1,PC),r0
    assert jmp == 0x402B            # jmp @r0
    assert nop1 == 0x0009 and nop2 == 0x0009
    target = struct.unpack(">I", b[8:12])[0]
    assert target == 0x0BADF00D
    # everything after the constant is nop padding up to AIP_MIN_SIZE
    tail = b[12:]
    assert tail == b"\x00\x09" * (len(tail) // 2)


def test_aip_stub_bytes_ip_size_stays_0x1000(tmp_path):
    """M2: padding the AIP from 12 to 0x20 bytes must not push the IP past the 0x1000 minimum set_ip_size
    pads to -- the real build's IP output (sysid 0x100 + SYS_SEC 0xd00 + 4 area codes 0x80 + SYS_INIT 0x80
    + AIP 0x20 = 0xf20) stays comfortably under 0x1000."""
    from omsat.ipbin import build_sysid, set_ip_size
    sid = build_sysid(maker="SEGA TP OMSAT", product="OMSAT-S0", version="V0.001", date="20261117",
                       device="CD-1/1", area="JTUE", peripherals="JA", title="X", ip_size=0x1000)
    fake_ip = sid + bytes(0xD00 + 0x20 * 4 + 0x80) + sbuild._aip_stub_bytes(sbuild.SPK_START)
    out = set_ip_size(fake_ip)
    assert len(out) == 0x1000
    assert struct.unpack(">I", out[0xE0:0xE4])[0] == 0x1000
