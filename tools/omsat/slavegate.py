"""the slave-SH-2 hand-off gate (emulator tier, Mednafen 1.32.1 savestates).

`python -m omsat.slavegate PHASE1.mcs PHASE2.mcs [--menu-res 0x<_om_slvoff_last from spike.map>]`

menu/spikes/l1_slave boots, turns on SGL's persistent INTBACK the way menu/app/main.c does (HWF-40), and jumps through
the menu's own om_launch_jump to the L1 launcher, which halts twice (menu/src/l1gate.h): phase 1 at its entry (the
state the menu side left) and phase 2 just before step 9's boot (the state a game would get). Each savestate is read
for SMPC SlaveSH2On (Mednafen's own flag: 0 = the slave is held in reset), the slave's PC, and the l1gate words, which
also identify the phase the state was taken in (a state from the wrong moment is refused, never scored).

GREEN needs SlaveSH2On == 0 in both phases, and in phase 2 the launcher's own SSHOFF issued with rc 0 (fix round 1,
review I1). A plain BIOS boot hands a game a halted slave (docs/research/
gex-freeze-rootcause.md §4 p0, measured), so 1 is RED.

Format facts (measured on the investigation's states, docs/research/gex-freeze-rootcause.md §10): a state is gzip or
raw "MDFNSVST", a 32-byte header, a preview image (w*h*3), then sections of {32-byte name, u32 LE size, payload}, each
payload a run of {u8 name length, name, u32 LE size, data}. SMPC scalars are stored little-endian; Work RAM is stored
as host-LE uint16s, so each 16-bit pair of a Saturn big-endian word comes back swapped ("HALT" reads "AHTL")."""
from __future__ import annotations

import argparse
import gzip
import json
import struct
import sys
from pathlib import Path

MAGIC = b"MDFNSVST"
L1G_BASE = 0x0027F810                    # menu/src/l1gate.h OM_L1G_BASE (OM_LW_HANDOFF + 0x810)
REQ, PRE, HALT, LSSH = 0x534C5647, 0x50524531, 0x484C5444, 0x4C535348
SYMPTOM = ("a game that does not reset the slave SH-2 itself starts with Aether's SGL slave code still running "
           "out of HWRAM the game is overwriting: a crash or hang at any moment, never on a plain BIOS boot")


def sections(raw: bytes) -> dict[str, dict[str, bytes]]:
    dec = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
    if len(dec) < 32 or dec[:8] != MAGIC:
        raise ValueError("not a Mednafen savestate (no MDFNSVST magic)")
    w, h = struct.unpack("<II", dec[24:32])
    off, out = 32 + w * h * 3, {}
    while off + 36 <= len(dec):
        name = dec[off:off + 32].rstrip(b"\0").decode("ascii", "replace")
        size = struct.unpack("<I", dec[off + 32:off + 36])[0]
        p, end = off + 36, off + 36 + size
        if end > len(dec):
            raise ValueError(f"section {name!r} runs past the end of the state")
        v = {}
        while p < end:
            n = dec[p]
            vname = dec[p + 1:p + 1 + n].rstrip(b"\0").decode("ascii", "replace")
            p += 1 + n
            vs = struct.unpack("<I", dec[p:p + 4])[0]
            p += 4
            if p + vs > end:
                raise ValueError(f"variable {name}/{vname} runs past its section")
            v[vname] = dec[p:p + vs]
            p += vs
        out[name] = v
        off = end
    return out


def _var(s: dict, sec: str, name: str) -> bytes:
    try:
        return s[sec][name]
    except KeyError:
        raise ValueError(f"state has no {sec}/{name}") from None


def wram_l_word(s: dict, addr: int) -> int:
    """A big-endian Saturn word in Work RAM-L (or -H for 0x06xxxxxx), undoing Mednafen's uint16 host-LE storage."""
    ram = _var(s, "MAIN", "WorkRAMH" if (addr & 0x0F000000) == 0x06000000 else "WorkRAML")
    o = (addr & 0x000FFFFF)
    b = ram[o:o + 4]
    return int.from_bytes(bytes((b[1], b[0], b[3], b[2])), "big")


RES_FIELDS = ("result", "cancel_rc", "cancel_tries", "off_rc", "off_tries", "direct_rc")   # slaveoff.h om_slvoff_res


def _signed(v: int) -> int:
    return v - (1 << 32) if v >> 31 else v


def read_phase(raw: bytes, menu_res: int | None = None) -> dict:
    s = sections(raw)
    smpc_le = lambda n: int.from_bytes(_var(s, "SMPC", n), "little")
    oreg = _var(s, "SMPC", "OREG")
    words = [wram_l_word(s, L1G_BASE + 4 * i) for i in range(4)]
    return {
        "slave_on": smpc_le("SlaveSH2On"),
        "slave_pc": int.from_bytes(_var(s, "SH2-S", "PC"), "little"),
        "master_pc": int.from_bytes(_var(s, "SH2-M", "PC"), "little"),
        "oreg31": oreg[31] if len(oreg) > 31 else -1,
        "req": words[0], "phase": words[1], "launcher_rc": words[2] - (1 << 32) if words[2] >> 31 else words[2],
        "launcher_issued": words[3] == LSSH,
        "menu_res": ({k: _signed(wram_l_word(s, menu_res + 4 * i)) for i, k in enumerate(RES_FIELDS)}
                     if menu_res is not None else None),
    }


def verdict(p1: dict, p2: dict) -> dict:
    why = []
    for tag, p, want in (("phase 1", p1, PRE), ("phase 2", p2, HALT)):
        if p["req"] != REQ or p["phase"] != want:
            why.append(f"{tag}: not taken in that phase (req {p['req']:#x}, phase word {p['phase']:#x}, want "
                       f"{want:#x}): BLIND, retake the state")
    blind = bool(why)
    if not blind:
        if p1["slave_on"]:
            why.append(f"phase 1: SlaveSH2On = 1 after the menu's om_launch_jump (slave PC {p1['slave_pc']:#x})")
        if p2["slave_on"]:
            why.append(f"phase 2: SlaveSH2On = 1 just before the boot (slave PC {p2['slave_pc']:#x})")
        # the launcher's own SSHOFF is the only guarantee on exit A and when SGL refused, so
        # it is gated on its own -- a working menu side must not hide a missing or failing launcher side
        if not p2["launcher_issued"]:
            why.append("phase 2: the launcher never issued its SSHOFF (l1gate word 3 is not LSSH)")
        elif p2["launcher_rc"] != 0:
            why.append(f"phase 2: the launcher's SSHOFF failed, rc {p2['launcher_rc']}")
    return {"gate": "slave-off-at-hand-off", "tier": "Mednafen 1.32.1 (emulator only, never a hardware result)",
            "verdict": "BLIND" if blind else "RED" if why else "GREEN", "why": why, "symptom": SYMPTOM,
            "phase1": p1, "phase2": p2}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("phase1", type=Path)
    ap.add_argument("phase2", type=Path)
    ap.add_argument("--menu-res", type=lambda x: int(x, 0), default=None,
                    help="address of _om_slvoff_last in the harness (nm spike.elf): reports the menu side's result")
    a = ap.parse_args(argv)
    v = verdict(read_phase(a.phase1.read_bytes(), a.menu_res), read_phase(a.phase2.read_bytes(), a.menu_res))
    print(json.dumps(v, indent=1, default=lambda x: hex(x) if isinstance(x, int) else x))
    return 0 if v["verdict"] == "GREEN" else 1


if __name__ == "__main__":
    sys.exit(main())
