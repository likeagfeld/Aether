# tools/omsat/coffrel.py
"""Normalise SEGA's COFF objects after `objcopy -Icoff-sh -Oelf32-sh` (docker/convert_sgl.sh).

Root cause (.superpowers/sdd/2026-11-17-m2-folders-view-and-launch/debug-black-screen.md): a COFF
object section that follows another one has a non-zero VMA (LIBSGL.A(sglI00.o): SLSTART at 0,
SLPROG at 0x28). A section-relative R_SH_DIR32 keeps (offset + that VMA) in place. objcopy copies
the in-place value and records the correction only as RELA addend -VMA. The SH ELF linker
(binutils 2.27) builds S + in-place, so the addend is lost. This put every SGL interrupt handler
0x28 past its entry, and the first V-blank faulted into the BIOS dead loop (a black screen).

normalise() folds each addend into the in-place word and zeroes it, so the two linker readings
agree. It only rewrites relocations that fit the measured pattern: an R_SH_DIR32 against a symbol at the start
of a section (the converted COFF section symbol) whose addend == -sh_addr of that section. Any other non-zero addend raises."""
from __future__ import annotations

import struct

SHT_RELA = 4
STT_NOTYPE, STT_SECTION = 0, 3   # objcopy turns COFF section symbols into local NOTYPE symbols at value 0 (measured)
R_SH_DIR32 = 1


def _sections(b: bytes):
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2:
        raise ValueError("not an ELF32 big-endian object")
    shoff, = struct.unpack_from(">I", b, 0x20)
    shentsize, shnum = struct.unpack_from(">HH", b, 0x2E)
    return [struct.unpack_from(">10I", b, shoff + i * shentsize) for i in range(shnum)]


def relocs(b: bytes):
    """Yield (rela_entry_file_offset, patch_file_offset, type, sym_type, sym_shndx, addend, sym_section_addr).
    sym_section_addr is the sh_addr of the symbol's section when the symbol sits at its start (st_value 0), else None."""
    secs = _sections(b)
    for sh in secs:
        if sh[1] != SHT_RELA:
            continue
        symtab, target = secs[sh[6]], secs[sh[7]]
        for i in range(sh[5] // 12):
            at = sh[4] + 12 * i
            r_offset, r_info, r_addend = struct.unpack_from(">IIi", b, at)
            st_value, = struct.unpack_from(">I", b, symtab[4] + 16 * (r_info >> 8) + 4)
            st_info, = struct.unpack_from(">B", b, symtab[4] + 16 * (r_info >> 8) + 12)
            st_shndx, = struct.unpack_from(">H", b, symtab[4] + 16 * (r_info >> 8) + 14)
            sym_sec_addr = secs[st_shndx][3] if 0 < st_shndx < len(secs) and st_value == 0 else None
            yield at, target[4] + r_offset, r_info & 0xFF, st_info & 0xF, st_shndx, r_addend, sym_sec_addr


def count_unfolded(b: bytes) -> int:
    """Relocations with a non-zero addend, which the SH ELF linker would drop."""
    return sum(1 for r in relocs(b) if r[5] != 0)


def normalise(b: bytes) -> tuple[bytes, int]:
    out = bytearray(b)
    n = 0
    for at, patch, rtype, stype, shndx, addend, sec_addr in relocs(b):
        if addend == 0:
            continue
        if not (rtype == R_SH_DIR32 and stype in (STT_NOTYPE, STT_SECTION) and sec_addr is not None and addend == -sec_addr):
            raise ValueError(f"unexpected relocation (type {rtype}, sym type {stype}, shndx {shndx}, "
                             f"addend {addend:#x}, section addr {sec_addr}) at file offset {at:#x}")
        v, = struct.unpack_from(">I", out, patch)
        struct.pack_into(">I", out, patch, (v + addend) & 0xFFFFFFFF)
        struct.pack_into(">i", out, at + 8, 0)
        n += 1
    return bytes(out), n
