"""Black-screen fix gate (.superpowers/sdd/2026-11-17-m2-folders-view-and-launch/debug-black-screen.md):
tools/omsat/coffrel.py folding and sbuild.check_sgl_vectors, each shown RED on the defect and GREEN on the fix."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402
from omsat.coffrel import count_unfolded, normalise  # noqa: E402


def _obj(inplace: int, addend: int, slprog_addr: int = 0x28) -> bytes:
    """Minimal ELF32 big-endian relocatable laid out like converted LIBSGL.A(sglI00.o): SLPROG at sh_addr 0x28 and
    a DIR32 in SLPROG at offset 4 against a NOTYPE local symbol at the start of SLPROG."""
    shstr = b"\0SLPROG\0.symtab\0.strtab\0.relaSLPROG\0.shstrtab\0"
    slprog = struct.pack(">II", 0x00090009, inplace)
    symtab = b"\0" * 16 + struct.pack(">IIIBBH", 0, 0, 0, 0, 0, 1)   # sym 1: value 0, NOTYPE, shndx 1 (SLPROG)
    strtab = b"\0"
    rela = struct.pack(">IIi", 4, (1 << 8) | 1, addend)                 # r_offset 4, sym 1, R_SH_DIR32
    blobs = [slprog, symtab, strtab, rela, shstr]
    off, offs = 0x34, []
    for b in blobs:
        offs.append(off)
        off += len(b)
    shoff = (off + 3) & ~3
    sh = [struct.pack(">10I", *[0] * 10),
          struct.pack(">10I", 1, 1, 6, slprog_addr, offs[0], len(slprog), 0, 0, 4, 0),
          struct.pack(">10I", 8, 2, 0, 0, offs[1], len(symtab), 3, 2, 4, 16),
          struct.pack(">10I", 16, 3, 0, 0, offs[2], len(strtab), 0, 0, 1, 0),
          struct.pack(">10I", 24, 4, 0, 0, offs[3], len(rela), 2, 1, 4, 12),
          struct.pack(">10I", 36, 3, 0, 0, offs[4], len(shstr), 0, 0, 1, 0)]
    hdr = b"\x7fELF\x01\x02\x01" + b"\0" * 9 + struct.pack(">HHIIIIIHHHHHH", 1, 42, 1, 0, 0, shoff, 0, 0x34, 0, 0,
                                                           40, len(sh), 5)
    body = hdr + b"".join(blobs)
    return body + b"\0" * (shoff - len(body)) + b"".join(sh)


def test_unfolded_object_is_red_and_normalise_folds_it():
    raw = _obj(inplace=0x5E, addend=-0x28)          # measured sglI00.o IMM_BlankIn: 0x36 + VMA 0x28, addend -0x28
    assert count_unfolded(raw) == 1                 # RED: the linker would build S + 0x5E
    fixed, n = normalise(raw)
    assert n == 1 and count_unfolded(fixed) == 0
    assert struct.unpack_from(">I", fixed, 0x34 + 4)[0] == 0x36


def test_normalise_refuses_an_addend_outside_the_measured_pattern():
    with pytest.raises(ValueError):
        normalise(_obj(inplace=0x5E, addend=-0x10))


def _image(blankin_literal: int) -> tuple[dict, bytes]:
    base = sbuild.SPK_START
    syms = {"IMM_BlankIn": base + 0x10, "IMM_BlankOut": base + 0x14, "_BlankIn": base + 0x40, "_BlankOut": base + 0x80}
    body = bytearray(0x100)
    struct.pack_into(">II", body, 0x10, blankin_literal, base + 0x80)
    return syms, bytes(body)


def test_check_sgl_vectors_red_when_handler_literal_is_0x28_high():
    syms, body = _image(sbuild.SPK_START + 0x40 + 0x28)
    with pytest.raises(SystemExit, match=r"IMM_BlankIn holds .*\(\+0x28\)"):
        sbuild.check_sgl_vectors(syms, body)


def test_check_sgl_vectors_green_when_literal_matches():
    syms, body = _image(sbuild.SPK_START + 0x40)
    sbuild.check_sgl_vectors(syms, body)
