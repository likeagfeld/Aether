"""tools/omsat/slavegate.py on synthetic Mednafen savestates (the real RED/GREEN states are emulator runs,
recorded in task-18c-report.md; this pins the parser, the byte order and the verdict rules, and proves the gate fails)."""
import gzip
import struct

import pytest

from omsat import slavegate as sg


def _var(name: str, data: bytes) -> bytes:
    n = name.encode()
    return bytes([len(n)]) + n + struct.pack("<I", len(data)) + data


def _sect(name: str, body: bytes) -> bytes:
    return name.encode().ljust(32, b"\0") + struct.pack("<I", len(body)) + body


def _swap16(b: bytes) -> bytes:
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def state(slave_on: int, words=(sg.REQ, sg.PRE, 0, 0), gz=True, oreg31=3) -> bytes:
    ram = bytearray(0x80000)
    for i, w in enumerate(words):                 # Saturn big-endian words, stored as host-LE uint16s
        o = (sg.L1G_BASE & 0xFFFFF) + 4 * i
        ram[o:o + 4] = _swap16(w.to_bytes(4, "big"))
    oreg = bytes(31) + bytes([oreg31])
    body = (_sect("MAIN", _var("WorkRAML", bytes(ram)))
            + _sect("SMPC", _var("OREG", oreg) + _var("SlaveSH2On", bytes([slave_on])))
            + _sect("SH2-S", _var("PC", struct.pack("<I", 0x060081AA)))
            + _sect("SH2-M", _var("PC", struct.pack("<I", 0x0020885C))))
    hdr = sg.MAGIC + bytes(8) + bytes(8) + struct.pack("<II", 1, 1) + b"\0\0\0"   # 1x1 preview
    raw = hdr + body
    return gzip.compress(raw) if gz else raw


def phases(on1, on2, w1=None, w2=None):
    return (sg.read_phase(state(on1, w1 or (sg.REQ, sg.PRE, 0xFFFFFFFF, 0))),
            sg.read_phase(state(on2, w2 or (sg.REQ, sg.HALT, 0, sg.LSSH), gz=False)))


def test_green_when_off_in_both_phases():
    p1, p2 = phases(0, 0)
    v = sg.verdict(p1, p2)
    assert v["verdict"] == "GREEN" and not v["why"]
    assert p1["launcher_rc"] == -1 and p2["launcher_rc"] == 0 and p2["launcher_issued"]
    assert p1["oreg31"] == 3 and p1["slave_pc"] == 0x060081AA


@pytest.mark.parametrize("on1,on2", [(1, 1), (1, 0), (0, 1)])
def test_red_when_the_slave_runs(on1, on2):          # mutation demo: either phase on -> RED
    v = sg.verdict(*phases(on1, on2))
    assert v["verdict"] == "RED" and len(v["why"]) == on1 + on2 and v["symptom"]


@pytest.mark.parametrize("words,why", [((sg.REQ, sg.HALT, 0, 0), "never issued"),
                                       ((sg.REQ, sg.HALT, (-63) & 0xFFFFFFFF, sg.LSSH), "rc -63"),
                                       ((sg.REQ, sg.HALT, (-60) & 0xFFFFFFFF, sg.LSSH), "rc -60")])
def test_red_when_the_launcher_sshoff_is_missing_or_fails(words, why):   # the menu side alone is not enough
    v = sg.verdict(*phases(0, 0, w2=words))
    assert v["verdict"] == "RED" and len(v["why"]) == 1 and why in v["why"][0]


def test_blind_when_taken_at_the_wrong_moment():      # a state outside its phase is never scored
    v = sg.verdict(*phases(0, 0, w2=(sg.REQ, sg.PRE, 0, 0)))
    assert v["verdict"] == "BLIND"
    v = sg.verdict(*phases(0, 0, w1=(0, 0, 0, 0)))
    assert v["verdict"] == "BLIND"


def test_byte_order_is_undone():                       # "HALT" is stored "AHTL"
    p = sg.read_phase(state(0, (sg.REQ, sg.HALT, 0, 0)))
    assert p["phase"] == 0x484C5444


def test_not_a_state():
    with pytest.raises(ValueError):
        sg.sections(b"not a savestate at all, no magic")


def test_cli_exit_codes(tmp_path):
    a, b, c = tmp_path / "a.mcs", tmp_path / "b.mcs", tmp_path / "c.mcs"
    a.write_bytes(state(0, (sg.REQ, sg.PRE, 0, 0)))
    b.write_bytes(state(0, (sg.REQ, sg.HALT, 0, sg.LSSH)))
    c.write_bytes(state(1, (sg.REQ, sg.HALT, 0, sg.LSSH)))
    assert sg.main([str(a), str(b)]) == 0
    assert sg.main([str(a), str(c)]) == 1
