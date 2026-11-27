"""sbuild.check_chkai passes on Kai's bytes and fails on each defect it exists for: a linked byte that
differs from the assembled source, a pinned offset that moved, a handler that no longer holds a code, and a
bootstrap that returns somewhere else."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402
from omsat import sh2kai  # noqa: E402

BASE = 0x00200000
BOOT_AT, HDLR_AT = 0x1000, 0x1040


def image(boot=sh2kai.KAI_BOOT, hdlr=sh2kai.KAI_HDLR):
    body = bytearray(0x2000)
    body[BOOT_AT:BOOT_AT + 40] = boot
    body[HDLR_AT:HDLR_AT + 96] = hdlr
    syms = {"_asm_cheat_bootstrap_ihr": BASE + BOOT_AT, "_asm_cheatcodes_hdlr_ram": BASE + HDLR_AT}
    objs = {"_asm_cheat_bootstrap_ihr": bytes(boot), "_asm_cheatcodes_hdlr_ram": bytes(hdlr)}
    return syms, bytes(body), objs


def test_passes_on_kais_bytes(capsys):
    sbuild.check_chkai(*image())
    assert "check_chkai:" in capsys.readouterr().out


def test_linked_bytes_differ_from_the_assembled_source():
    syms, body, objs = image()
    objs["_asm_cheatcodes_hdlr_ram"] = objs["_asm_cheatcodes_hdlr_ram"][:-2] + b"\x00\x0b"
    with pytest.raises(SystemExit, match="assembled verbatim source"):
        sbuild.check_chkai(syms, body, objs)


def test_a_changed_handler_byte_is_caught():
    h = bytearray(sh2kai.KAI_HDLR)
    h[0x2E:0x30] = bytes.fromhex("0009")                       # the 16-bit write's value load gone
    syms, body, objs = image(hdlr=bytes(h))
    with pytest.raises(SystemExit):
        sbuild.check_chkai(syms, body, objs)


def test_a_moved_literal_is_caught():
    b = bytearray(sh2kai.KAI_BOOT)
    b[13] -= 1
    syms, body, objs = image(boot=bytes(b))
    with pytest.raises(SystemExit):
        sbuild.check_chkai(syms, body, objs)


def test_missing_symbol():
    syms, body, objs = image()
    del syms["_asm_cheat_bootstrap_ihr"]
    with pytest.raises(SystemExit, match="missing"):
        sbuild.check_chkai(syms, body, objs)


def test_semantics_fail_even_with_matching_pins(monkeypatch):
    """the model part is not decoration: with the pins accepting a broken handler, the run itself fails"""
    h = bytearray(sh2kai.KAI_HDLR)
    h[0x17] = 0x3C                                             # the count read from +56: no code ever runs
    monkeypatch.setattr(sh2kai, "KAI_HDLR", bytes(h))
    syms, body, objs = image(hdlr=bytes(h))
    with pytest.raises(SystemExit, match="did not land"):
        sbuild.check_chkai(syms, body, objs)


def test_cwx_gate_wants_the_cheat_block_after_the_stub():
    syms = {"___cwx_tail_start": 0x2FF000, "___cwx_tail_end": 0x2FF154, "___igr_stub_start": 0x06002800,
            "___igr_stub_end": 0x06002D48, "_om_cwx_go": 0x2FF000, "_om_cwx_hook": 0x2FF060, "___cwx_cell": 0x2FF130,
            "___cwx_par": 0x2FF134, "___cwx_res": 0x2FF140, "___cwx_chk": 0x2FF150 - 0x10,
            "___cwx_chimg": 0x2FF154 + 0x548}
    tail = {"lits": [0x06002100, 0xFFFFFE92, 0x06002270], "direct": [], "indirect": 0,
            "order": ["jsr", "purge", "jsr", "jmp"]}
    sbuild.check_cwx(syms, tail)
    syms["___cwx_chimg"] += 4
    with pytest.raises(SystemExit, match="right after the stub"):
        sbuild.check_cwx(syms, tail)
