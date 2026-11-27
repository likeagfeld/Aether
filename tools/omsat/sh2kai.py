"""M6 Task 27b: a small SH-2 interpreter for Pseudo Saturn Kai's cheat bootstrap and in-RAM handler
(menu/launcher/kai/*.s, cafe-alpha, GPL-2.0) -- exactly the instructions those two routines use, plus the trapa that
reaches them and the one-word instructions a master code typically displaces; any other opcode stops the run.

Semantics per SH2_Programming.txt (Hitachi SH-2 Programming Manual, the instruction descriptions): big-endian; a
`mov.w`/`mov.b` load sign-extends; `mov.l @(disp,PC)` reads (PC & ~3) + 4 + 4*disp; `mov.w/b @(disp,Rm),R0` scales
disp by 2/1; delayed branches (`bra`, `bt.s`, `bf.s`, `jsr`, `rts`, `rte`) run their slot first; `trapa #imm` pushes
SR then PC+2 and jumps through VBR + 4*imm; `rte` pops PC then SR. Memory is a dict of address -> byte (missing = a
stop), so every byte the code touches is accounted for.

`KAI_BOOT` / `KAI_HDLR` are the assembled bytes of the two routines (saturn-sh2-elf-as of the verbatim sources,
objdump'd this session); sbuild.check_chkai requires the linked LAUNCHER.BIN copies to equal them byte for byte.
"""
from __future__ import annotations

KAI_BOOT = bytes.fromhex(
    "4f222f062f162f262f362f46d407d006400b000964f663f662f661f660f64f26"
    "00090009002b0009")
KAI_HDLR = bytes.fromhex(
    "514c634321188d0673206211853a622d610d32108b14853d7440610d71014110"
    "890e8446600c88018f05880362428542a00422018b02624285422200afef7408"
    "000b0009000900090009000900090009cafea1fa000900090009000900090009")

BOOT_SLOT, BOOT_P_HDLR, BOOT_P_CFG = 0x20, 0x28, 0x2C   # the pinned offsets (menu/launcher/chkai.h)


class Stop(Exception):
    pass


class Mem:
    def __init__(self):
        self.b: dict[int, int] = {}
        self.writes: list[tuple[int, int, int]] = []        # (address, size, value), in order

    def load(self, at: int, data: bytes):
        for i, x in enumerate(data):
            self.b[at + i] = x

    def rd(self, a: int, n: int) -> int:
        if a % n:
            raise Stop(f"address error: {n}-byte read at {a:#x}")
        v = 0
        for i in range(n):
            if a + i not in self.b:
                raise Stop(f"read of unmapped {a + i:#x}")
            v = v << 8 | self.b[a + i]
        return v

    def wr(self, a: int, n: int, v: int):
        if a % n:
            raise Stop(f"address error: {n}-byte write at {a:#x}")
        for i in range(n):
            if a + i not in self.b:
                raise Stop(f"write to unmapped {a + i:#x}")
            self.b[a + i] = (v >> (8 * (n - 1 - i))) & 0xFF
        self.writes.append((a, n, v & ((1 << (8 * n)) - 1)))

    def w16(self, a):
        return self.rd(a, 2)


def _sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Cpu:
    def __init__(self, mem: Mem, vbr: int = 0x06000000):
        self.m, self.r, self.sr, self.t, self.pr, self.vbr = mem, [0] * 16, 0xF0, 0, 0, vbr
        self.pc = 0

    def _set(self, n, v):
        self.r[n] = v & 0xFFFFFFFF

    def push(self, v):
        self._set(15, self.r[15] - 4)
        self.m.wr(self.r[15], 4, v)

    def pop(self):
        v = self.m.rd(self.r[15], 4)
        self._set(15, self.r[15] + 4)
        return v

    def trapa(self, imm, ret):
        """trapa #imm at ret - 2: SR, then the return PC, onto the stack; PC from the vector table."""
        self.push(self.sr | self.t)
        self.push(ret)
        self.pc = self.m.rd(self.vbr + 4 * imm, 4)

    def step(self, slot=False):
        """One instruction at self.pc. Returns the branch target of a delayed branch (run its slot first), or None."""
        pc, op = self.pc, self.m.rd(self.pc, 2)
        n, m, i8, d4 = (op >> 8) & 15, (op >> 4) & 15, op & 0xFF, op & 15
        r, nxt, tgt = self.r, pc + 2, None
        if op == 0x0009:                                       # nop
            pass
        elif op >> 12 == 0x4 and op & 0xFF == 0x22:            # sts.l pr,@-Rn
            self._set(n, r[n] - 4); self.m.wr(r[n], 4, self.pr)
        elif op >> 12 == 0x4 and op & 0xFF == 0x26:            # lds.l @Rm+,pr
            self.pr = self.m.rd(r[n], 4); self._set(n, r[n] + 4)
        elif op >> 12 == 0x2 and d4 == 0x6:                    # mov.l Rm,@-Rn
            v = r[m]; self._set(n, r[n] - 4); self.m.wr(r[n], 4, v)
        elif op >> 12 == 0x6 and d4 == 0x6:                    # mov.l @Rm+,Rn
            v = self.m.rd(r[m], 4)
            if m != n:
                self._set(m, r[m] + 4)
            self._set(n, v)
        elif op >> 12 == 0xD:                                  # mov.l @(disp,PC),Rn
            self._set(n, self.m.rd((pc & ~3) + 4 + 4 * i8, 4))
        elif op >> 12 == 0x5:                                  # mov.l @(disp,Rm),Rn
            self._set(n, self.m.rd(r[m] + 4 * d4, 4))
        elif op >> 8 == 0x85:                                  # mov.w @(disp,Rm),R0
            self._set(0, _sx(self.m.rd(r[m] + 2 * d4, 2), 16))
        elif op >> 8 == 0x84:                                  # mov.b @(disp,Rm),R0
            self._set(0, _sx(self.m.rd(r[m] + d4, 1), 8))
        elif op >> 12 == 0x6 and d4 == 0x1:                    # mov.w @Rm,Rn
            self._set(n, _sx(self.m.rd(r[m], 2), 16))
        elif op >> 12 == 0x6 and d4 == 0x2:                    # mov.l @Rm,Rn
            self._set(n, self.m.rd(r[m], 4))
        elif op >> 12 == 0x2 and d4 == 0x1:                    # mov.w Rm,@Rn
            self.m.wr(r[n], 2, r[m] & 0xFFFF)
        elif op >> 12 == 0x2 and d4 == 0x0:                    # mov.b Rm,@Rn
            self.m.wr(r[n], 1, r[m] & 0xFF)
        elif op >> 12 == 0x2 and d4 == 0x2:                    # mov.l Rm,@Rn
            self.m.wr(r[n], 4, r[m])
        elif op >> 12 == 0x6 and d4 == 0x3:                    # mov Rm,Rn
            self._set(n, r[m])
        elif op >> 12 == 0x6 and d4 == 0xD:                    # extu.w
            self._set(n, r[m] & 0xFFFF)
        elif op >> 12 == 0x6 and d4 == 0xC:                    # extu.b
            self._set(n, r[m] & 0xFF)
        elif op >> 12 == 0x7:                                  # add #imm,Rn
            self._set(n, r[n] + _sx(i8, 8))
        elif op >> 12 == 0x3 and d4 == 0xC:                    # add Rm,Rn
            self._set(n, r[n] + r[m])
        elif op >> 12 == 0x2 and d4 == 0xB:                    # or Rm,Rn
            self._set(n, r[n] | r[m])
        elif op >> 12 == 0x2 and d4 == 0x8:                    # tst Rm,Rn
            self.t = int((r[n] & r[m]) == 0)
        elif op >> 12 == 0x3 and d4 == 0x0:                    # cmp/eq Rm,Rn
            self.t = int(r[n] == r[m])
        elif op >> 8 == 0x88:                                  # cmp/eq #imm,R0
            self.t = int(r[0] == (_sx(i8, 8) & 0xFFFFFFFF))
        elif op >> 12 == 0x4 and op & 0xFF == 0x10:            # dt Rn
            self._set(n, r[n] - 1); self.t = int(r[n] == 0)
        elif op >> 8 in (0x89, 0x8B):                          # bt / bf
            if self.t == (1 if op >> 8 == 0x89 else 0):
                nxt = pc + 4 + 2 * _sx(i8, 8)
        elif op >> 8 in (0x8D, 0x8F):                          # bt.s / bf.s
            tgt = pc + 4 + 2 * _sx(i8, 8) if self.t == (1 if op >> 8 == 0x8D else 0) else pc + 4
        elif op >> 12 == 0xA:                                  # bra
            tgt = pc + 4 + 2 * _sx(op & 0xFFF, 12)
        elif op >> 12 == 0x4 and op & 0xFF == 0x0B:            # jsr @Rm
            tgt, self.pr = r[n], pc + 4
        elif op == 0x000B:                                     # rts
            tgt = self.pr
        elif op == 0x002B:                                     # rte
            tgt = self.pop()
            v = self.pop()
            self.sr, self.t = v & 0x3F2, v & 1
        elif op >> 8 == 0xC3:                                  # trapa #imm
            if slot:
                raise Stop(f"trapa in a delay slot at {pc:#x}")
            self.trapa(i8, pc + 2)
            return None
        else:
            raise Stop(f"unknown opcode {op:#06x} at {pc:#x}")
        if tgt is not None and slot:
            raise Stop(f"branch in a delay slot at {pc:#x}")
        self.pc = nxt
        return tgt

    def run(self, until: int, steps: int = 10000) -> int:
        """Run until PC == until; returns the instructions run."""
        for k in range(steps):
            if self.pc == until:
                return k
            tgt = self.step()
            if tgt is not None:
                self.step(slot=True)
                self.pc = tgt
        raise Stop(f"step limit at {self.pc:#x}")


def config(place: int, saved: int, codes: list[tuple[int, int, int]], en: tuple[int, int] = (0, 0),
           boot: bytes = KAI_BOOT, hdlr: bytes = KAI_HDLR) -> bytes:
    """The block chkai.c's om_chk_build lays out (the C test checks the C side byte for byte; this is the model's)."""
    h = 64 + 8 * len(codes)
    b = bytearray(h + 96)
    b[:40] = boot
    b[BOOT_SLOT:BOOT_SLOT + 2] = saved.to_bytes(2, "big")
    b[BOOT_P_HDLR:BOOT_P_HDLR + 4] = (place + h).to_bytes(4, "big")
    b[BOOT_P_CFG:BOOT_P_CFG + 4] = place.to_bytes(4, "big")
    b[48:52] = en[0].to_bytes(4, "big")
    b[52:54] = en[1].to_bytes(2, "big")
    b[58:60] = len(codes).to_bytes(2, "big")
    for i, (a, v, c) in enumerate(codes):
        b[64 + 8 * i:64 + 8 * i + 8] = a.to_bytes(4, "big") + v.to_bytes(2, "big") + bytes((c, 0))
    b[h:h + len(hdlr)] = hdlr
    return bytes(b)


def run_trap(block: bytes, place: int, master: int, opc: int, ram: dict[int, int], regs=None) -> tuple[Cpu, Mem]:
    """A dispatcher word at `master` holds the trapa `opc`; vector (opc & 15) points at the block at `place`. Runs from
    the trapa to the instruction after it (the master's return point) and returns the CPU and memory."""
    m = Mem()
    m.load(0x06000000, bytes(0x400))                           # the vector table
    m.load(0x06001000, bytes(0x1000))                          # a stack
    m.load(place, block)
    for a, v in ram.items():
        m.load(a, v.to_bytes(2, "big"))
    m.load(master, opc.to_bytes(2, "big") + (0x0009).to_bytes(2, "big"))
    m.load(0x06000000 + 4 * (opc & 15), place.to_bytes(4, "big"))
    cpu = Cpu(m)
    for i, v in enumerate(regs or [0x11111111 * k for k in range(15)]):
        cpu.r[i] = v
    cpu.r[15] = 0x06002000
    cpu.pr = 0xCAFEF00D
    cpu.pc = master
    m.writes.clear()
    cpu.run(master + 2)
    return cpu, m
