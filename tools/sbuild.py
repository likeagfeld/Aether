#!/usr/bin/env python3
"""Build a Saturn spike into autoboot.iso (or autoboot.cue + tracks). Runs inside the omsat-build image.
Jo-free: SaturnSDK GCC, SEGA SGL converted to ELF in build/sgl, IP.BIN linked from SEGA SYS_* objects, genisoimage."""
from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from omsat.ipbin import build_sysid, set_ip_size  # noqa: E402

PFX = "saturn-sh2-elf-"
SGL = ROOT / "build" / "sgl"
# derived caches live under build/sgl_derived/, NOT inside build/sgl/ itself: build/sgl may be a junction into another
# checkout's build inputs and is read-only by convention (Docker Desktop presents such a junction as a dangling
# symlink unless the whole drive is mounted, and a write through it lands in the other checkout). Both directories are
# gitignored, derived, and rebuilt every invocation.
SGL_INC_CLEAN = ROOT / "build" / "sgl_derived" / "inc_clean"
COMMON = ROOT / "menu" / "spikes" / "common"
SAT = ROOT / "menu" / "lib" / "satiator"
RUNTIME = ROOT / "menu" / "runtime"
APP = ROOT / "menu" / "app"
MSRC = ROOT / "menu" / "src"
UI = ROOT / "menu" / "ui"
VIEWS = ROOT / "menu" / "views"
FAKE = APP / "fake"
FAKECARD_BIN = ROOT / "build" / "fakecard" / "FAKECARD.BIN"
BOOTANIM_BIN = ROOT / "menu" / "app" / "assets" / "BOOTANIM.BIN"   # tools/omsat/bootanim.py, on the menu disc
BIGLOGO_BIN = ROOT / "menu" / "app" / "assets" / "BIGLOGO.BIN"     # tools/omsat/gen_biglogo.py, on the menu disc
LOADBG_BIN = ROOT / "menu" / "app" / "assets" / "LOADBG.BIN"       # on the fake disc only (fakesat serves it)
SOUND_BIN = ROOT / "menu" / "app" / "assets" / "SOUND.BIN"         # tools/omsat/sound.py bank, both discs
HW_END_MAX = 0x060F0000   # SGL work area start (menu/runtime/workarea.c; docs/ledger.md "M2 memory plan"; HWF-12)
HEADROOM_FLOOR = 0x10000   # user, 2026-11-20: at least 64 KB of HWRAM stays free below SGL's work area
# translation units run once per boot, launch or diagnostics visit, compiled -Os (later -O wins in GCC).
# Chosen by measurement (ledger "M6 memory plan"); never per-frame code (HOT). The plan's candidate list was pruned by
# measurement (saturn-sh2-elf-size, -O2 vs -Os, per object): menu/app/fatal.c (-20 B), menu/app/launchj.c (-0 B),
# menu/app/persist.c (-24 B) and menu/app/info.c (-36 B) each saved under the 64 B cutoff and were dropped.
# diag.c's own gate (the BIOS CRC loop's -Os timing) was measured in the review fix round: identical "401MS" on
# Mednafen at both -O2 and -Os -- condition holds, kept. Contingency C1 (libidx.c/gamesdb.c/libsort.c) was measured
# and applied in that same round (+512 B linked, both builds; ledger "M6 memory plan").
COLD_OS = frozenset({
    "menu/views/diag.c", "menu/app/launch.c",
    "menu/app/themes.c", "menu/app/card.c", "menu/src/scan.c", "menu/src/ini.c",
    "menu/src/settings.c", "menu/src/theme.c", "menu/src/plist.c", "menu/src/isodir.c", "menu/src/match.c",
    "menu/src/sysid.c", "menu/src/libidx.c", "menu/src/gamesdb.c", "menu/src/libsort.c",
    "menu/app/fwcheck.c",   # once at boot; -O2 1,116 B vs -Os 1,036 B text (docs/ledger.md)
    # once per launch (the loader choice, PERGAME.INI lookup, game id); the Task 15 ruling moved pergame.c
    # and gameid.c here for Task 18 -- Task 17 links them first (docs/ledger.md)
    "menu/src/loader.c", "menu/src/pergame.c", "menu/src/gameid.c",
    # the overlays' key handling runs once per key press, never per frame (docs/ledger.md)
    "menu/src/overlay.c",
    "menu/src/handoff.c",   # M6 Task 19 reclaim: built once per launch (menu/app/launch.c, already cold)
    "menu/src/sdwrite.c",   # once per save (and the diagnostics stress page), never per frame
    # the slot picker and ACTIVE.TXT (once per launch / picker key / selection change), and the planner's
    # folder names the picker borrows (the launcher builds its own copy -O2)
    "menu/src/slotpick.c", "menu/src/pgsplan.c",
    "menu/src/savegen.c",   # the slot status's reader rule (once per picker key, like slotpick.c)
    "menu/src/impscan.c",   # the import picker's scan and IMPORT.SEL (once per Settings > Import)
    # Batch B (measured -O2 -> -Os, 1,695 B together): boot only (main.c, vertag.c, logoblend.c, biosid.c), once per
    # panel open or launch (cheatpak.c, cheats.c, bupfile.c, satfs.c), once per message or timeout (msgfmt.c, saver.c)
    "menu/app/main.c", "menu/ui/vertag.c", "menu/src/logoblend.c", "menu/src/biosid.c", "menu/src/cheatpak.c",
    "menu/app/cheats.c", "menu/src/bupfile.c", "menu/app/satfs.c", "menu/src/msgfmt.c", "menu/src/saver.c",
    # the centered panels (measured -O2 -> -Os, 138 B together): the firmware check (boot), the list sort (a folder,
    # sort or filter change), the lists' load and save (boot, a favorite, a launch)
    "menu/src/fwver.c", "menu/src/msort.c", "menu/app/persist.c",
    "menu/src/discset.c",   # 2026-11-24 (the lid swap): once per launch or Launch Options open, never per frame
    # the console setup file /aether/DIAG.TXT (app/setup.h): once a boot, linked only into POPUP.BIN (pop/psetup.c)
    "menu/src/setupid.c", "menu/app/setupw.c",
})
HOT = frozenset({
    "menu/views/folders.c", "menu/ui/v1.c", "menu/ui/uitext.c", "menu/app/bootplay.c", "menu/src/bootanim.c",
    # review-task-19.md Important: title.c/uiline.c were added to COLD_OS on a false
    # "never per-frame" claim. om_title runs unconditionally every call to row_label_of, which folders.c's
    # row_prepare (the per-frame, raster-budget-limited row loop) and prepare_selected (called twice a frame from
    # frame_draw, before its own cache check) both call without gating; om_primary_area (uiline.c) runs the same way
    # through flag_of and through placeholder_square (cover_box, whenever the selected item shows NO ART). Both
    # files are genuinely hot and stay compiled -O2 with the rest of folders.c's support code.
    "menu/src/title.c", "menu/src/uiline.c",
    "menu/src/loadbg.c", "menu/app/audio.c", "menu/src/sndmix.c", "menu/ui/art.c", "menu/app/artjob.c",
    "menu/ui/vdp2theme.c", "menu/views/interstitial.c", "menu/views/loading.c", "menu/views/ovdraw.c",
})
BASE_CFLAGS = ["-m2", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-fno-common", "-fno-builtin", "-fomit-frame-pointer"]
CFLAGS = BASE_CFLAGS + [f"-I{SGL_INC_CLEAN}", f"-I{SAT}", f"-I{COMMON}", f"-I{RUNTIME}", f"-I{MSRC}", "-c"]
LAUNCHER = ROOT / "menu" / "launcher"
LCFLAGS = BASE_CFLAGS + [f"-I{d}" for d in (LAUNCHER, MSRC, SAT, SGL_INC_CLEAN)] + ["-c", "-DSAT_INITIAL_MODE=s_api"]  # HWF-54
# LAUNCHER last: the menu includes only cdblk.h/vbwait.h from it; no launcher header shares a name with
# a menu one (checked when the path was added)
APP_CFLAGS = BASE_CFLAGS + [f"-I{d}" for d in (SGL_INC_CLEAN, SAT, MSRC, UI, VIEWS, APP, RUNTIME, LAUNCHER)] + ["-c"]
# HWRAM recovery: the menu's own objects get one section per function and per object, and the link
# drops the unreferenced ones (--gc-sections). menu/app/app.ld KEEP()s SGL's entry (SLSTART), SLPROG, the ctor tables
# and every section of the three SEGA archives, so only our code and data (and libc/libgcc members) are collected.
APP_GC_CFLAGS = ["-ffunction-sections", "-fdata-sections"]


def _clean_sgl_headers() -> None:
    """DEVIATION (docs/ledger.md "Build findings"): measured this session -- 11 of 17 headers in
    build/sgl/inc (a Task 14 artifact, not owned here) end with a single trailing 0x1A (DOS Ctrl-Z EOF
    marker), e.g. build/sgl/inc/SGL.H's last byte. saturn-sh2-elf-gcc 6.2.0's preprocessor (unlike the
    1994-era Hitachi/DOS toolchain these headers were written for) treats it as a hard error:
    "error: stray '\\32' in program". build/sgl/inc is out of this task's file ownership and is
    regenerated by docker/convert_sgl.sh, so this does not edit it in place; it makes a derived,
    byte-identical-except-for-the-marker copy under build/sgl/inc_clean (gitignored, like all of build/)
    and compiles against that instead.
    """
    SGL_INC_CLEAN.mkdir(parents=True, exist_ok=True)
    for src in (SGL / "inc").iterdir():
        if not src.is_file():
            continue
        data = src.read_bytes()
        if data.endswith(b"\x1a"):
            data = data[:-1]
        (SGL_INC_CLEAN / src.name).write_bytes(data)


SGL_LIB_FOLDED = ROOT / "build" / "sgl_derived" / "lib_folded"   # see SGL_INC_CLEAN's comment above
FOLDED_LIBS = ("SEGA_SYS.A", "LIBCD.A", "LIBSGL.A")


def _fold_sgl_libs() -> None:
    """Black-screen fix (debug-black-screen.md): derived copies of the converted SEGA archives under
    build/sgl/lib_folded, made the same way _clean_sgl_headers derives inc_clean. In each member, every COFF
    section-relative reloc addend is folded into its in-place word (tools/omsat/coffrel.py). Measured on the
    unfolded libraries: 49 relocs in 7 members (LIBSGL sglI00/sglIc0, 4 LIBCD members, SEGA_SYS sys_exit). The SH
    ELF linker dropped those addends and placed SGL's interrupt handlers 0x28 bytes late. SYS_*.O (the IP) have
    none and are not touched. Every link (spikes, --app, --launcher) uses these copies.
    Post-condition: no member of a folded archive keeps a non-zero addend."""
    from omsat.coffrel import count_unfolded, normalise
    shutil.rmtree(SGL_LIB_FOLDED, ignore_errors=True)
    SGL_LIB_FOLDED.mkdir(parents=True)
    for name in FOLDED_LIBS:
        src = SGL / "lib" / name
        members = subprocess.run([PFX + "ar", "t", str(src)], check=True, capture_output=True, text=True).stdout.split()
        if len(set(members)) != len(members):
            raise SystemExit(f"_fold_sgl_libs FAILED: {name} has duplicate member names; ar x would drop some")
        tmp = SGL_LIB_FOLDED / (name + ".d")
        tmp.mkdir()
        subprocess.run([PFX + "ar", "x", str(src)], check=True, cwd=tmp)
        folded = 0
        for m in members:
            data, n = normalise((tmp / m).read_bytes())
            (tmp / m).write_bytes(data)
            folded += n
        subprocess.run([PFX + "ar", "rcs", str(SGL_LIB_FOLDED / name), *members], check=True, cwd=tmp)
        left = sum(count_unfolded((tmp / m).read_bytes()) for m in members)
        if left:
            raise SystemExit(f"_fold_sgl_libs FAILED: {name} still has {left} unfolded reloc addends")
        print(f"_fold_sgl_libs: {name}: {len(members)} members, {folded} addends folded, 0 left", flush=True)


# 1st Read Address (ST-040-R4 §4.2 offset F0H): where the BIOS DMAs the 1st Read File (our spike's 0.BIN)
# during the licence screen. Must be > 0x06002000+IP_SIZE and < 0x06100000-4 (§4.2 "1st READ ADDRESS" range).
# Also the SLSTART link address in spike.ld, and the jump target baked into the AIP stub below -- one
# constant, three places it must agree.
SPK_START = 0x06004000


def run(cmd, cwd=None):
    print(" ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, cwd=cwd)


AIP_MIN_SIZE = 0x20   # ST-040-R4 Table 4.1 "IP Structure": Application Initial Program size is 20H~71E0H


def build_sddrvs(out: Path) -> Path:
    """Coordinator instruction (Task 18, S1 sound-driver follow-up): embeds SEGA's sound driver
    build/sgl/inc/SDDRVS.TSK into the spike ELF as a plain .data blob, so a spike's C code can
    `extern Uint8 sddrvs[], sddrvs_end[];` and pass them to slInitSound exactly as
    NOV96_DTS/EXAMPLES/SGL/CDDA_SGL/SRC/MAIN.C:14-16/32 does.

    Opt-in only: called from build() when a USE_SDDRVS marker file sits in the spike directory, so
    spikes that don't need sound are unaffected (no SDDRVS.TSK dependency, no size/link-time cost).

    `saturn-sh2-elf-objcopy -I binary -O elf32-sh -B sh` on a *relative* filename (hence the copy to
    out/sddrvs.bin before objcopy'ing with cwd=out, matching the sysid.bin/aip.bin pattern below)
    produces one `.data` section (confirmed this session via `saturn-sh2-elf-objdump -h` -- ALLOC,
    LOAD, DATA, not SLSTART, so it lands in spike.ld's ordinary `.data` output section like any other
    initialized global) and three symbols: `_binary_sddrvs_bin_start`, `_binary_sddrvs_bin_end`,
    `_binary_sddrvs_bin_size` (confirmed this session via `saturn-sh2-elf-nm`). This toolchain's C
    symbols carry a leading underscore (every other function in this build, e.g. `_ss_main`, shows the
    same), so a C-visible `sddrvs`/`sddrvs_end` needs the object's `_binary_sddrvs_bin_start`/`_end`
    symbols renamed to `_sddrvs`/`_sddrvs_end` -- done with `--redefine-sym` in the same objcopy
    invocation (confirmed this session via `nm` on the renamed object: `_sddrvs` and `_sddrvs_end`
    replace the two `_binary_...` symbols; the unused `_binary_sddrvs_bin_size` absolute symbol is
    left in place, harmless since nothing references it).

    Sound-RAM tenancy (docs/ledger.md "Task 18, sound driver follow-up"): slInitSound loads this
    driver into the Saturn's sound RAM (SCSP-side), but the destination address is UNVERIFIED -- not
    found in build/sgl/inc/SEGA_SND.H (grepped this session for SNDRAM/SOUND_RAM/0x25a.*; only hit was
    SND_ADR_INTR_RESET, an SCSP register, not a RAM base) nor in NOV96_DTS's ST-238-R1 (SGL function
    reference; its slInitSound entry gives the parameters only, no address) nor its SOUND.DOC prose
    equivalent. The 68000 sound CPU owns that destination, so this build step does not claim a WRAM/
    VRAM ledger entry for it (see the skill's "Free is established by a ledger" rule) -- it is flagged
    unverified instead of asserted.
    """
    sddrvs_bin = out / "sddrvs.bin"
    shutil.copy(SGL / "inc" / "SDDRVS.TSK", sddrvs_bin)
    run([PFX + "objcopy", "-I", "binary", "-O", "elf32-sh", "-B", "sh",
         "--redefine-sym", "_binary_sddrvs_bin_start=_sddrvs",
         "--redefine-sym", "_binary_sddrvs_bin_end=_sddrvs_end",
         "sddrvs.bin", "sddrvs.o"], cwd=out)
    return out / "sddrvs.o"


def _aip_stub_bytes(target: int) -> bytes:
    """DEVIATION (docs/ledger.md "Build findings"): a minimal Application Initial Program.

    ST-040-R4 Appendix smpsys.lnk links SYS_ID, SYS_SEC, SYS_ARE?, SYS_INIT, then an application object
    (smpsys.obj, the AIP) -- the brief's ip.ld omits that last object. Measured this session via
    `saturn-sh2-elf-objdump -dr build/sgl/lib/SYS_INIT.O`: SYS_INIT's START routine ends
    "SETHOOK: bra ENDOFPG" where ENDOFPG == the end of SYS_INIT.O's own .text (offset 0x80, its full
    section size) -- i.e. it falls through into whatever is linked immediately after it. ST-040-R4 §4.6:
    the AIP "is executed immediately after the area code is executed by placing it after the area code
    group" -- that fall-through IS the AIP call. Without one, the CPU falls into set_ip_size's zero
    padding, which is not a jump to SPK_START (whatever SH-2 opcode 0x0000 decodes to is unverified here --
    not measured this session, not checked against the SH-2 Programming Manual). So the AIP here is our own
    stub ("mov.l SPK_START,r0 ; jmp @r0 ; nop ; nop"), encoding verified by disassembling the assembled
    bytes with saturn-sh2-elf-objdump this session: `mov.l 8 <...>,r0 ! <target> / jmp @r0 / nop`.

    the jump target is fixed at byte offset 8 regardless of padding. Padded with
    trailing `nop` (0x0009) words to AIP_MIN_SIZE (0x20 = 32 bytes), ST-040-R4 Table 4.1's documented
    AIP minimum size, instead of the bare 12-byte jump sequence.
    """
    b = struct.pack(">H", 0xD001)        # mov.l @(1,PC),r0  (PC_base = here+4; loads from here+8)
    b += struct.pack(">H", 0x402B)       # jmp @r0
    b += struct.pack(">H", 0x0009)       # nop (branch delay slot)
    b += struct.pack(">H", 0x0009)       # nop (padding, aligns the constant to a 4-byte boundary)
    b += struct.pack(">I", target)       # the jump target, big-endian (SH-2 on Saturn is big-endian), offset 8
    while len(b) < AIP_MIN_SIZE:
        b += struct.pack(">H", 0x0009)   # nop, padding to the ST-040-R4 Table 4.1 AIP minimum (dead -- after jmp)
    assert len(b) == AIP_MIN_SIZE
    return b


def _nm_underscore_symbols(obj: Path) -> set[str]:
    out = subprocess.run([PFX + "nm", str(obj)], check=True, capture_output=True, text=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2].startswith("_"):
            syms.add(parts[2])
    return syms


def check_workarea_symbols(workarea_obj: Path) -> None:
    """M3: the Step 5 brief's grep-based symbol-set diff is broken (docs/ledger.md
    "Build findings" #4 -- it only matches 4 of 23 symbols, silently passing if all 19 const-declared
    ones are deleted). This is the committed, toolchain-grounded replacement: nm on the actual compiled
    workarea.o against the actual compiled SGLAREA.O (the object our workarea.c replaces), compared by
    symbol name. Proved to fail in docs/ledger.md (removed TransList -> real link failure AND this
    check reporting it missing); re-proved as part of this fix round's RED/GREEN demo."""
    ours = _nm_underscore_symbols(workarea_obj)
    theirs = _nm_underscore_symbols(SGL / "lib" / "SGLAREA.O")
    missing, extra = theirs - ours, ours - theirs
    if missing or extra:
        raise SystemExit(f"workarea.c symbol-set gate FAILED vs build/sgl/lib/SGLAREA.O: "
                          f"missing={sorted(missing)} extra={sorted(extra)}")


def verify_entry(elf: Path, aip_bin: Path, ip_bin: Path) -> None:
    """M1 / RED-first gate (Controller Ruling R10). crt0.c's main() must NOT occupy
    SLSTART: SGL_Start (LIBSGL.A sglI00.o) must be the sole occupant and the real CPU entry point,
    because it is SGL_Start -- not our own code -- that sets r15 (stack pointer, from MasterStack), SR,
    the SH-2 cache control register (0xFFFFFE92) and GBR, before "jmp @main" (measured this session via
    `objdump -dr -j SLSTART` on the extracted sglI00.o member). GBR is load-bearing: 215 of LIBSGL.A's
    object files use @(disp,gbr)-relative addressing (grep count, this session) -- skipping SGL_Start
    leaves GBR unset and those accesses land on whatever GBR happened to hold at reset.

    Checks that four independent paths to SPK_START all agree: SGL_Start's own linked address, the ELF
    entry point, the System ID's 1st Read Address (IP.BIN offset 0xF0), and the AIP stub's jump target
    (its bytes, offset 8). Any mismatch fails the build -- this is not a warning."""
    nm_out = subprocess.run([PFX + "nm", str(elf)], check=True, capture_output=True, text=True).stdout
    sgl_start = entry = None
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ("SGL_Start", "_SGL_Start"):
            sgl_start = int(parts[0], 16)
    readelf_out = subprocess.run([PFX + "readelf", "-h", str(elf)], check=True, capture_output=True,
                                  text=True).stdout
    for line in readelf_out.splitlines():
        if "Entry point address" in line:
            entry = int(line.split(":", 1)[1].strip(), 16)
    sysid_1st_read = struct.unpack(">I", ip_bin.read_bytes()[0xF0:0xF4])[0]
    aip_target = struct.unpack(">I", aip_bin.read_bytes()[8:12])[0]
    got = {"SGL_Start (nm)": sgl_start, "ELF entry (readelf -h)": entry,
           "System ID 1st Read Address (IP.BIN+0xF0)": sysid_1st_read, "AIP stub jump target": aip_target}
    bad = {k: v for k, v in got.items() if v != SPK_START}
    if bad:
        raise SystemExit("verify_entry gate FAILED: expected all == "
                          f"{hex(SPK_START)}, got {', '.join(f'{k}={hex(v) if v is not None else v}' for k, v in bad.items())}")
    print(f"verify_entry: SGL_Start/entry/1st-read/AIP-target all == {hex(SPK_START)}", flush=True)


def _parse_nm(text: str) -> dict[str, int]:
    syms = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                syms[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return syms


def nm_symbols(elf: Path) -> dict[str, int]:
    return _parse_nm(subprocess.run([PFX + "nm", str(elf)], check=True, capture_output=True, text=True).stdout)


def check_hw_end(syms: dict, limit: int = HW_END_MAX, floor: int = HEADROOM_FLOOR) -> int:
    """M2 gate: the menu's HWRAM image + bss must end below SGL's work area (ledger). Measured from the linked ELF,
    never from a comment (skill: 'check _end, not the comment'). Also enforce a 64 KB headroom floor
    below that limit (HEADROOM_FLOOR) -- the gate fails before the image ever overruns HW_END_MAX itself."""
    end = syms.get("_end")
    if end is None:
        raise SystemExit("check_hw_end FAILED: no _end symbol in the ELF")
    if end > limit:
        raise SystemExit(f"check_hw_end FAILED: _end={end:#x} > {limit:#x} (SGL work area); shrink the HWRAM buffers")
    if limit - end < floor:
        raise SystemExit(f"check_hw_end FAILED: _end={end:#x} leaves {limit - end} B headroom < {floor} B floor "
                         "(M6 Task 3); reclaim before adding (docs/ledger.md 'M6 memory plan')")
    print(f"check_hw_end: _end={end:#x} <= {limit:#x} ({limit - end} B headroom)", flush=True)
    return end


SGL_VECTORS = ("BlankIn", "BlankOut")   # SGL's V-blank-in/out handlers; slInitInterrupt registers IMM_<name>


FORBIDDEN_APP_SYMBOLS = ("_slInitSound", "_slPCMOn", "_slCDDAOn", "_slSndVolume", "_SND_Init")


def check_forbidden_symbols(syms: dict) -> None:
    """Decision D2 (docs/decisions/m3-sound.md): the menu plays sound through SCSP slots directly. SGL/SBL sound must
    not be linked: slInitSound + slCDDAOn bias the SCSP (skill, measured on the reference port), and slPCMOn streams
    from main RAM through PCM_Work and stops at the end of its data (ST-238-R1). Baseline before any sound code (Task
    14/15, the --app ELF at 29dc4f1): 0 of these 5 names among its 2,590 nm symbols."""
    bad = sorted(s for s in FORBIDDEN_APP_SYMBOLS if s in syms)
    if bad:
        raise SystemExit(f"check_forbidden_symbols FAILED: {bad} linked; menu sound is direct SCSP (decision D2)")
    print("check_forbidden_symbols: none of " + ", ".join(FORBIDDEN_APP_SYMBOLS) + " linked", flush=True)


def check_sgl_vectors(syms: dict, body: bytes, base: int = SPK_START) -> None:
    """Black-screen gate (debug-black-screen.md): the handler address SGL hands to SYS_SETSINT is the literal at
    IMM_BlankIn / IMM_BlankOut. It must equal the handler's own symbol. When it was 0x28 high (unfolded COFF reloc
    addend, tools/omsat/coffrel.py), the first V-blank jumped into the middle of _BlankIn, faulted into the BIOS
    dead loop, and the screen stayed black. Read from the linked 0.BIN bytes, not from a comment."""
    for name in SGL_VECTORS:
        lit, want = syms.get("IMM_" + name), syms.get("_" + name)
        if lit is None or want is None:
            raise SystemExit(f"check_sgl_vectors FAILED: IMM_{name} or _{name} missing from the ELF")
        got = struct.unpack(">I", body[lit - base:lit - base + 4])[0]
        if got != want:
            raise SystemExit(f"check_sgl_vectors FAILED: IMM_{name} holds {got:#x}, _{name} is {want:#x} "
                             f"({got - want:+#x}); SGL's V-blank handler would be entered mid-function")
    print("check_sgl_vectors: " + ", ".join(f"IMM_{n} == _{n} == {syms['_' + n]:#x}" for n in SGL_VECTORS), flush=True)


SGL_LONG_TABLES = ("_om_tex",         # TEXTURE table handed to slInitSystem (menu/ui/vdp1tex.c)
                   "_om_v1_cmd")      # SPRITE commands passed to slSetSprite (menu/ui/v1.c, Task 13a; the header logo
                                      # and the covers are raw commands now, so om_logo_attr is gone)


def check_sgl_tables(syms: dict) -> None:
    """Folders-hang gate (.superpowers/sdd/2026-11-18-m3-library-themes-music/debug-folders-vdp1-hang.md): SGL's slave
    reads TEXTURE+4 (CGadr, HVsize) with one mov.l (LIBSGL EntryZbuf). A table at 2 mod 4 raises a CPU address error
    on the slave (SH7604 Hardware Manual 4.3.1, Table 4.6), which parks it in the BIOS dead loop, and the master's
    slSynch waits for it forever. The table address must therefore be a multiple of 4. Read from the linked ELF."""
    for name in SGL_LONG_TABLES:
        addr = syms.get(name)
        if addr is None:
            raise SystemExit(f"check_sgl_tables FAILED: {name} missing from the ELF")
        if addr % 4:
            raise SystemExit(f"check_sgl_tables FAILED: {name}={addr:#x} is not 4-byte aligned; SGL's slave would "
                             f"take an address error on its first sprite and slSynch would hang")
    print("check_sgl_tables: " + ", ".join(f"{n}={syms[n]:#x}" for n in SGL_LONG_TABLES) + " 4-byte aligned",
          flush=True)


UIFONT_MAX = 40_000   # tools/omsat/uifont.py MAX_BYTES: the UI font atlas + icons + flags


def check_uifont_size(obj: Path, limit: int = UIFONT_MAX) -> int:
    """Task 13a gate: the generated atlas (menu/src/uifont_data.c, committed; tools/omsat/uifont.py) is all .rodata;
    measured from the compiled object, never from the generator's own count."""
    out = subprocess.run([PFX + "size", str(obj)], check=True, capture_output=True, text=True).stdout.splitlines()
    text, data = (int(v) for v in out[1].split()[:2])
    if text + data > limit:
        raise SystemExit(f"check_uifont_size FAILED: {obj.name} is {text + data} B > {limit} B")
    print(f"check_uifont_size: {obj.name} {text + data} B <= {limit} B", flush=True)
    return text + data


def embed_blob(out: Path, src: Path, sym: str) -> Path:
    """Generalises build_sddrvs: objcopy -I binary, renaming _binary_<sym>_bin_start/_end to C-visible sym/sym_end."""
    name = f"{sym}.bin"
    shutil.copy(src, out / name)
    run([PFX + "objcopy", "-I", "binary", "-O", "elf32-sh", "-B", "sh",
         "--redefine-sym", f"_binary_{sym}_bin_start=_{sym}",
         "--redefine-sym", f"_binary_{sym}_bin_end=_{sym}_end",
         name, f"{sym}.o"], cwd=out)
    return out / f"{sym}.o"


def _compile(srcs, out: Path, flags) -> list[Path]:
    objs = []
    for s in srcs:
        o = out / f"{s.parent.name}_{s.stem}.o"
        run([PFX + "gcc", *flags, s, "-o", o])
        objs.append(o)
    return objs


def _make_iso(out: Path, cd: Path, elf: Path, product: str, title: str, abstract: str, extras=(), patch=None) -> Path:
    run([PFX + "objcopy", "-O", "binary", elf, cd / "0.BIN"])
    if patch:   # 2026-11-25: words written into the image after the link (the popup overlay's CRC, build_popup)
        body = bytearray((cd / "0.BIN").read_bytes())
        for off, (old, data) in patch.items():
            if body[off:off + len(old)] != old:   # in .data (a .bss word is not in 0.BIN), and the word it should be
                raise SystemExit(f"_make_iso FAILED: 0.BIN+{off:#x} holds {bytes(body[off:off + 4]).hex()}, not the "
                                 f"placeholder {old.hex()}")
            body[off:off + len(data)] = data
        (cd / "0.BIN").write_bytes(bytes(body))
    check_sgl_vectors(nm_symbols(elf), (cd / "0.BIN").read_bytes())  # black-screen gate
    for name, text in (("ABS.TXT", abstract), ("CPY.TXT", "See CREDITS.md"), ("BIB.TXT", "none")):
        (cd / name).write_text(text + "\n")
    for extra in extras:
        shutil.copy(extra, cd / extra.name)
    ip = build_ip(out, product=product, title=title)
    verify_entry(elf, out / "aip.bin", ip)  # Ruling R10 gate -- before genisoimage
    iso = out / "autoboot.iso"
    run(["genisoimage", "-quiet", "-sysid", "SEGA SATURN", "-volid", "AETHER", "-volset", "AETHER",
         "-publisher", "AETHER", "-preparer", "AETHER", "-appid", "OMSAT",
         "-abstract", "ABS.TXT", "-copyright", "CPY.TXT", "-biblio", "BIB.TXT",
         "-generic-boot", ip, "-full-iso9660-filenames", "-o", iso, "."], cwd=cd)
    return iso


def build_id_flags() -> list[str]:
    """the diagnostics page's build string (menu/src/version.h OM_BUILD_ID). tools/sbuild.sh computes it on the
    host ("<git short SHA>[-dirty] <commit date>") and passes it in the environment; argv goes to gcc without a shell,
    so the quotes below are part of the macro (a C string literal). Without it, version.h says "dev"."""
    bid = os.environ.get("OM_BUILD_ID", "").strip()
    if not bid:
        return []
    if not re.fullmatch(r"[0-9A-Za-z .:_-]{1,40}", bid):
        raise SystemExit(f"OM_BUILD_ID {bid!r}: only letters, digits, space and . : _ - (at most 40)")
    return [f'-DOM_BUILD_ID="{bid}"']


def _fresh(*dirs: Path) -> None:
    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)


def _app_sources(fake: bool) -> list[Path]:
    """Plan D6: the fake-card build is the shipping menu with libsatiator swapped for menu/app/fake/*.c.
    DEVIATION (brief): menu/src/fakefs.c is filtered out of the real build's MSRC sources -- it is the test-only
    OMFAKEFS reader that only menu/app/fake/fakesat.c calls. _compile passes every source straight to the linker
    as its own object (no --gc-sections in BASE_CFLAGS, no archive), so an unfiltered fakefs.o would be linked
    into the shipping --app ELF as dead code and move _end, contradicting "the real --app build must be
    byte-unchanged in behaviour." It is still built for --app-fake (needed there) and for the host tests, which
    pick it up straight off menu/src/*.c (menu/host-tests/Makefile's $(SRCS) wildcard)."""
    msrc = [p for p in sorted(MSRC.glob("*.c")) if (fake or p.name != "fakefs.c") and not p.name.endswith("_host.c")]
    # Task 13c round 3: *_host.c holds host-test / tool helpers (menu/src/loadbg_host.c) the menu never calls
    srcs = [*msrc, *sorted(UI.glob("*.c")), *sorted(VIEWS.glob("*.c")), *sorted(APP.glob("*.c")),
            *sorted(RUNTIME.glob("*.c"))]
    return srcs + (sorted(FAKE.glob("*.c")) if fake else [SAT / "satiator.c"])


def _split_cold(srcs: list[Path]) -> tuple[list[Path], list[Path]]:
    """partitions srcs into (cold, hot) by repo-relative POSIX path against COLD_OS. Object names are
    unaffected (_compile names by s.parent.name/s.stem alone, independent of which list/flags compiled it)."""
    cold, hot = [], []
    for s in srcs:
        rel = s.relative_to(ROOT).as_posix()
        (cold if rel in COLD_OS else hot).append(s)
    return cold, hot


POP = ROOT / "menu" / "pop"
POP_BASE, POP_END = 0x00268800, 0x0027F000   # omlayout.h OM_LW_LBG .. + OM_LW_LBG_MAX (pop.ld asserts the end too)
POP_CRC_UNSET = 0x504F5021                    # menu/app/popup.h: om_pop_crc's placeholder, replaced in 0.BIN
# the engines the popups run, the launcher's own files (each host-tested there); the menu links none of them
POP_LAUNCHER = ("svmgr.c", "chedit.c", "chkai.c", "bram.c", "pgsbak.c", "pgsrun.c")
# objects whose state must exist once: the overlay binds to the menu's copy or fails the build (never a second copy)
POP_RESIDENT_ONLY = ("satiator_satiator.o", "app_sd.o", "app_audio.o", "fake_fakesat.o", "fake_fakepad.o",
                     "ui_uitext.o", "ui_v1.o", "ui_art.o", "src_yield.o")


def build_popup(out: Path, menu_elf: Path, flags: list) -> tuple[Path, dict]:
    """2026-11-25 (the in-menu popups, menu/app/popabi.h): POPUP.BIN, linked to run at OM_LW_LBG against menu.elf's
    symbols (--just-symbols: every function the menu has is the menu's), with the menu/src pieces the menu's
    --gc-sections dropped taken from an archive of the menu's own objects. Gates: the header first, image + .bss inside
    the LW region, the file the header's length, no second copy of a resident object (POP_RESIDENT_ONLY). Returns the
    file and {0.BIN offset: bytes} for om_pop_crc."""
    pout = out / "pop"
    _fresh(pout)
    pflags = flags + [f"-I{POP}"]
    objs = _compile(sorted(POP.glob("*.c")), pout, pflags)
    objs += _compile([LAUNCHER / n for n in POP_LAUNCHER], pout, pflags)
    members = [o for o in sorted(out.glob("*.o")) if o.name.startswith(("src_", "app_", "launcher_", "satiator_", "fake_"))
               and o.name not in ("app_main.o",)]
    lib = pout / "libmenu.a"
    run([PFX + "ar", "rcs", lib, *members])
    elf = pout / "popup.elf"
    run([PFX + "gcc", "-m2", "-nostartfiles", "-T", POP / "pop.ld", f"-Wl,-Map,{pout / 'popup.map'}", "-Wl,--gc-sections",
         f"-Wl,--just-symbols={menu_elf}", *objs, lib, "-lc", "-lgcc", "-o", elf])
    syms = nm_symbols(elf)
    need = lambda ok, why: None if ok else (_ for _ in ()).throw(SystemExit(f"build_popup FAILED: {why}"))
    need(syms.get("_om_pop_header") == POP_BASE, "_om_pop_header is not at OM_LW_LBG (pop.ld .pophdr first)")
    need(syms.get("___pop_bss_end", 1 << 32) <= POP_END, "image + .bss past OM_LW_LBG's end")
    # a resident object's code in the overlay means the menu dropped a function the overlay needs: keep it in the menu
    dup = []
    for m in POP_RESIDENT_ONLY:
        if (out / m).is_file():
            for s in _parse_nm(subprocess.run([PFX + "nm", "-g", "--defined-only", str(out / m)], check=True,
                                              capture_output=True, text=True).stdout):
                if POP_BASE <= syms.get(s, 0) < POP_END and not (m == "satiator_satiator.o" and s in POP_DUP_OK):
                    dup.append(f"{m}:{s}")
    need(not dup, f"a second copy of resident state in the overlay: {dup} (keep them in the menu: POP_MENU_KEEP)")
    # every allocated section is one the file carries (or .bss): an orphan section would be left out of POPUP.BIN
    hdr = subprocess.run([PFX + "objdump", "-h", str(elf)], check=True, capture_output=True, text=True).stdout
    secs = re.findall(r"^\s+\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)", hdr, re.M)
    stray = [n for n, size, vma in secs if int(vma, 16) and int(size, 16) and n not in (".text", ".rodata", ".data", ".bss")]
    need(not stray, f"sections outside the file's .text/.rodata/.data/.bss: {stray} (pop.ld)")
    # archive members: only those reviewed as stateless here (their file statics, if any, are never set by the menu
    # either: sdwrite.c's timer, om_sdw_set_timer, is not called) -- a new one fails until it is reviewed
    pulled = sorted(set(re.findall(r"libmenu\.a\(([A-Za-z0-9_]+\.o)\)", (pout / "popup.map").read_text(errors="replace"))))
    extra = [m for m in pulled if m not in POP_ARCHIVE_OK]
    need(not extra, f"menu objects copied into the overlay, not reviewed as stateless: {extra} (POP_ARCHIVE_OK)")
    img = pout / "POPUP.BIN"
    run([PFX + "objcopy", "-O", "binary", "-j", ".text", "-j", ".rodata", "-j", ".data", elf, img])
    data = img.read_bytes()
    need(len(data) == syms["___pop_img_len"], f"POPUP.BIN is {len(data)} B, its header says {syms['___pop_img_len']}")
    menu_syms = nm_symbols(menu_elf)
    off = menu_syms["_om_pop_crc"] - SPK_START
    crc = zlib.crc32(data) & 0xFFFFFFFF
    need(crc != POP_CRC_UNSET, "POPUP.BIN's CRC equals the placeholder: change any byte of the overlay")
    print(f"POPUP.BIN: {len(data)} B at {POP_BASE:#x}, .bss to {syms['___pop_bss_end']:#x} "
          f"({POP_END - syms['___pop_bss_end']} B free of {POP_END - POP_BASE}), CRC {crc:08x}", flush=True)
    return img, {off: (struct.pack(">I", POP_CRC_UNSET), struct.pack(">I", crc))}


# kept in the menu although only the overlay reads it (the CRC word sbuild patches)
POP_MENU_KEEP = ("_om_pop_crc",
                 "_om_fw_rc", "_om_fw_string")   # the console setup run (psetup.c) reads the firmware the boot read
# libsatiator commands with no C state of their own (one exec_cmd round trip each, satiator.c): the overlay may carry
# its own copy when the menu does not call them (2026-11-25: s_mkdir and s_chdir, 584 B of HWRAM not spent)
POP_DUP_OK = frozenset({"_s_mkdir", "_s_chdir", "_s_rename", "_s_opendir", "_s_stat", "_s_unlink"})
# menu objects reviewed as safe to copy into the overlay (no file state, or state the menu never sets either)
POP_ARCHIVE_OK = frozenset({"app_cheats.o", "satiator_satiator.o", "src_bupfile.o", "src_cheatpak.o", "src_impscan.o",
                            "src_pgsplan.o", "src_savegen.o", "src_sdwrite.o", "app_setupw.o", "src_setupid.o"})
# the fake card's libsatiator (menu/app/fake/fakesat.c) does keep state (the card, the working folder): one copy
POP_FAKE_KEEP = ("_s_mkdir", "_s_chdir", "_s_rename", "_s_opendir", "_s_stat", "_s_unlink")


def build_app(fake: bool = False, no_biglogo: bool = False, sdtest: bool = False) -> Path:
    """The menu: SGL app at 0x06004000 (same entry chain and gates as the spikes) -> menu/app/out/autoboot.iso.
    fake=True (plan D6): menu/app/out_fake/autoboot.iso with the OMFAKEFS card and scripted pad; emulator-only.
    sdtest=True: the same menu plus the diagnostics SD stress page (OMSAT_SDTEST), a test disc for
    hardware session H2 only (menu/app/out_sdtest, or out_fake_sdtest with fake). The page does not fit the shipping
    image's 64 KB headroom floor (docs/ledger.md), so this disc is held to SGL's work area alone."""
    _clean_sgl_headers()
    _fold_sgl_libs()
    out, cd = (APP / "out_fake", APP / "cd_fake") if fake else (APP / "out", APP / "cd")
    if sdtest:
        out, cd = out.with_name(out.name + "_sdtest"), cd.with_name(cd.name + "_sdtest")
    if fake and not FAKECARD_BIN.is_file():
        raise SystemExit("build_app --app-fake: build/fakecard/FAKECARD.BIN missing; run "
                         "`cd tools && python -m omsat.fakecard` first")
    _fresh(out, cd)
    flags = APP_CFLAGS + APP_GC_CFLAGS + ([f"-I{FAKE}", "-DOMSAT_FAKECARD"] if fake else []) + build_id_flags()
    flags += ["-DOMSAT_SDTEST"] if sdtest else []
    cold_srcs, hot_srcs = _split_cold(_app_sources(fake))
    objs = _compile(hot_srcs, out, flags)
    if cold_srcs:  # cold set compiled -Os (later -O wins in GCC); same object names either way
        objs += _compile(cold_srcs, out, flags + ["-Os"])
    # the boot animation streams through our own CD-block driver; the launcher's file, not a copy
    objs += _compile([LAUNCHER / "cdblk.c", LAUNCHER / "vbwait.c"], out, LCFLAGS)
    # the direct SMPC SSHOFF and its issue window, the menu's fallback when SGL refuses (launchj.c)
    # with -ffunction-sections, so --gc-sections drops smpcwait.c's pad-only functions
    objs += _compile([LAUNCHER / "sshoff.c", LAUNCHER / "smpcwait.c"], out, LCFLAGS + APP_GC_CFLAGS)
    check_workarea_symbols(out / "runtime_workarea.o")
    check_uifont_size(out / "src_uifont_data.o")
    # placeholder.bin retired (51,200 B of HWRAM .data and VDP1 VRAM); the view vector-draws NO ART
    objs.append(embed_blob(out, APP / "assets" / "logo.bin", "om_logo"))       # Ruling M3-R12; tools/omsat/gen_logo.py
    # the interstitial's big logo left HWRAM -- BIGLOGO.BIN on the menu disc (audio.c reads it into LWRAM)
    objs.append(embed_blob(out, APP / "assets" / "footlogo.bin", "om_footlogo"))  # M6 Task 7; tools/omsat/gen_logo.py
    # the virtual root folders' tiles left HWRAM -- the card file /aether/VRTILES.BIN (card.c, LWRAM)
    if fake:
        objs.append(embed_blob(out, FAKECARD_BIN, "om_fakecard"))
    elf = out / "menu.elf"
    run([PFX + "gcc", "-m2", "-nostartfiles", "-T", APP / "app.ld", f"-Wl,-Map,{out / 'menu.map'}",
         "-Wl,--gc-sections", "-Wl,--print-gc-sections",
         *[f"-Wl,--undefined={s}" for s in POP_MENU_KEEP + (POP_FAKE_KEEP if fake else ())],
         *objs, SGL_LIB_FOLDED / "SEGA_SYS.A", SGL_LIB_FOLDED / "LIBCD.A", SGL_LIB_FOLDED / "LIBSGL.A", "-lc", "-lgcc", "-o", elf])
    syms = nm_symbols(elf)
    check_hw_end(syms, floor=0 if sdtest else HEADROOM_FLOOR)
    pop_img, pop_patch = build_popup(out, elf, flags + APP_GC_CFLAGS)
    if not fake:   # the fake-card build never calls om_launch_jump (folders.c, OMSAT_FAKECARD)
        dis = subprocess.run([PFX + "objdump", "-d", "-j", ".text", str(elf)], check=True, capture_output=True,
                             text=True).stdout
        check_slave_off(dis)
    check_forbidden_symbols(syms)
    check_sgl_tables(syms)
    return _make_iso(out, cd, elf, "OMSAT-FAKE" if fake else "OMSAT-SDT" if sdtest else "OMSAT-MENU",
                     ("AETHER FAKE" if fake else "AETHER") + (" SDTEST" if sdtest else ""), "Aether",
                     extras=[BOOTANIM_BIN, SOUND_BIN, pop_img] + ([] if no_biglogo else [BIGLOGO_BIN])
                     + ([LOADBG_BIN] if fake else []), patch=pop_patch)


_FN = re.compile(r"^[0-9a-f]+ <(_[A-Za-z0-9_.]+)>:$")


def fn_blocks(disasm: str) -> dict:
    """objdump -d text split by function label: {symbol: [(op, args), ...]} (literal-pool words included)."""
    out, cur = {}, None
    for line in disasm.splitlines():
        if (m := _FN.match(line.strip())):
            cur = out.setdefault(m.group(1), [])
        elif cur is not None and (m := _DIS.match(line)):
            cur.append((m.group(2), m.group(3)))
    return out


def check_slave_off(disasm: str) -> None:
    """the menu's jump stops the slave SH-2 through the checked sequence, not a bare
    slSlaveOffWait (= slRequestCommand(SMPC_SSHOFF = 2, SMPC_WAIT), SL_DEF.H:1557), which always failed under the
    persistent INTBACK and whose rc was ignored. In om_launch_jump the call to om_slave_off_seq comes before the
    interrupt mask (or #-16 into SR) and slRequestCommand is not called there directly; the sequence's ops call
    slIntBackCancel, slRequestCommand with r4 = 2 and om_smpc_sshoff. The order and every rc check are host-tested
    (menu/host-tests/test_slaveoff.c). Mutation demo: the c903a1b / pre-fix menu.elf fails here."""
    f = fn_blocks(disasm)
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_slave_off FAILED: {why}")
    lit = lambda ins, sym: any(op == "mov.l" and f"<{sym}>" in a for op, a in ins)
    jump = f.get("_om_launch_jump")
    need(jump is not None, "no _om_launch_jump in menu.elf")
    need("_om_slave_off_seq" in f, "no _om_slave_off_seq in menu.elf")
    need(not lit(jump, "_slRequestCommand"), "om_launch_jump calls slRequestCommand directly (the bare slSlaveOffWait)")
    seq = next((i for i, (op, a) in enumerate(jump) if op == "mov.l" and "<_om_slave_off_seq>" in a), None)
    mask = next((i for i, (op, a) in enumerate(jump) if op == "or" and a.startswith("#-16,")), None)
    need(seq is not None, "om_launch_jump does not call om_slave_off_seq")
    need(mask is not None and seq < mask, "om_slave_off_seq is not called before the interrupt mask in om_launch_jump")
    for fn, sym in (("_op_cancel", "_slIntBackCancel"), ("_op_offwait", "_slRequestCommand"),
                    ("_op_direct", "_om_smpc_sshoff")):
        need(fn in f and lit(f[fn], sym), f"{fn} does not call {sym}")
    need(any(op == "mov" and a.startswith("#2,r4") for op, a in f["_op_offwait"]),
         "op_offwait does not pass SMPC_SSHOFF (2) to slRequestCommand")
    print("check_slave_off: om_launch_jump -> om_slave_off_seq before the SR mask; ops slIntBackCancel, "
          "slRequestCommand(2, ...), om_smpc_sshoff", flush=True)


def check_launcher_sshoff(disasm: str) -> None:
    """the release LAUNCHER.BIN issues its own SSHOFF (HWF-78) on every launch,
    before the boot: om_run_launch (with lseq.c's slave_off inlined, or calling it) loads om_smpc_sshoff before
    om_boot_disc (JHL) and om_cwx_boot (CWX). The only guarantee on the error screen's exit A and when the menu's
    SGL path refused. Mutation demo: slave_off() removed from om_run_launch fails here."""
    f = fn_blocks(disasm)
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_launcher_sshoff FAILED: {why}")
    run_ = f.get("_om_run_launch")
    need(run_ is not None, "no _om_run_launch in launcher.elf")
    def first(ins, sym):
        return next((i for i, (op, a) in enumerate(ins) if op == "mov.l" and f"<{sym}>" in a), None)
    at = first(run_, "_om_smpc_sshoff")
    if at is None and "_slave_off" in f and first(f["_slave_off"], "_om_smpc_sshoff") is not None:
        at = first(run_, "_slave_off")
        if at is None:
            at = next((i for i, (op, a) in enumerate(run_) if op == "bsr" and "<_slave_off>" in a), None)
    need(at is not None, "om_run_launch does not call om_smpc_sshoff (lseq.c slave_off)")
    boots = {b: first(run_, b) for b in ("_om_boot_disc", "_om_cwx_boot")}
    need(boots["_om_boot_disc"] is not None, "om_run_launch does not call om_boot_disc (objdump format changed?)")
    for boot, b in boots.items():   # om_cwx_boot is absent before M6 Task 17 (the c903a1b A/B tree): JHL only there
        need(b is None or at < b, f"om_smpc_sshoff is not called before {boot[1:]} in om_run_launch")
    print("check_launcher_sshoff: om_run_launch -> om_smpc_sshoff before " + " and ".join(b[1:] for b, i in boots.items() if i is not None), flush=True)


def check_ip_objects(paths) -> None:
    """M2 deferred (b): the IP links SEGA's SYS_*.O directly (they are not in FOLDED_LIBS). Measured clean at M2; this
    gate fails the build if any of them carries a COFF reloc addend the SH ELF linker would drop (coffrel.py)."""
    from omsat.coffrel import count_unfolded
    paths = [Path(p) for p in paths]
    for p in paths:
        n = count_unfolded(p.read_bytes())
        if n:
            raise SystemExit(f"check_ip_objects FAILED: {p.name}: {n} unfolded reloc addend(s); fold it the way "
                             f"_fold_sgl_libs does before linking the IP")
    print(f"check_ip_objects: {len(paths)} SYS_*.O objects, 0 unfolded addends", flush=True)


def build_ip(out: Path, product: str, title: str) -> Path:
    sid = build_sysid(maker="SEGA TP OMSAT", product=product, version="V0.001", date="20261117", device="CD-1/1",
                      area="JTUE", peripherals="JA", title=title, ip_size=0x1000, first_read=SPK_START)
    (out / "sysid.bin").write_bytes(sid)
    run([PFX + "objcopy", "-I", "binary", "-O", "elf32-sh", "-B", "sh",
         "--rename-section", ".data=SYSID,alloc,load,contents", "sysid.bin", "sysid.o"], cwd=out)
    (out / "aip.bin").write_bytes(_aip_stub_bytes(SPK_START))
    run([PFX + "objcopy", "-I", "binary", "-O", "elf32-sh", "-B", "sh",
         "--rename-section", ".data=AIP,alloc,load,contents", "aip.bin", "aip.o"], cwd=out)
    objs = ["sysid.o"] + [SGL / "lib" / n for n in
                          ("SYS_SEC.O", "SYS_AREJ.O", "SYS_ARET.O", "SYS_AREU.O", "SYS_AREE.O", "SYS_INIT.O")
                          ] + ["aip.o"]
    check_ip_objects([o for o in objs if isinstance(o, Path)])      # M2 deferred (b)
    run([PFX + "ld", "-T", RUNTIME / "ip.ld", "-Map", "ip.map", "-o", "ip.elf", *objs], cwd=out)
    run([PFX + "objcopy", "-O", "binary", "ip.elf", "ip_raw.bin"], cwd=out)
    ip = set_ip_size((out / "ip_raw.bin").read_bytes())
    (out / "IP.BIN").write_bytes(ip)
    return out / "IP.BIN"


LAUNCHJ_SRCS = [APP / "launchj.c", MSRC / "slaveoff.c", LAUNCHER / "sshoff.c", LAUNCHER / "smpcwait.c"]   # M6 Task 18c


def build(spike: Path, tracks: Path | None) -> Path:
    _clean_sgl_headers()
    _fold_sgl_libs()
    out, cd = spike / "out", spike / "cd"
    _fresh(out, cd)
    srcs = sorted(spike.glob("*.c")) + sorted(COMMON.glob("*.c")) + sorted(RUNTIME.glob("*.c")) + [SAT / "satiator.c"]
    objs = _compile(srcs, out, CFLAGS)
    check_workarea_symbols(out / "runtime_workarea.o")  # M3: gate, not just an ad hoc ledger command
    if (spike / "USE_SDDRVS").exists():
        objs.append(build_sddrvs(out))  # opt-in SEGA sound-driver blob
    if (spike / "EMBED_LAUNCHER").exists():
        # the harness embeds its own launcher, built here with OM_L1_HARNESS
        objs += _compile([MSRC / "om_fmt.c", MSRC / "handoff.c"], out, CFLAGS + [f"-I{MSRC}", "-DOM_L1_HARNESS"])
        objs.append(embed_blob(out, build_launcher(l1=True), "om_launcher_bin"))
        # the harnesses jump through the menu's own om_launch_jump (the slave SH-2 stop), not a copy
        objs += _compile(LAUNCHJ_SRCS, out, APP_CFLAGS)
    if (spike / "EMBED_LOADBG").exists():   # the launcher's screens with no card: the harness puts it at OM_LG_FILE
        objs.append(embed_blob(out, LOADBG_BIN, "om_loadbg_bin"))
        # and a cover for its captures, if one was made locally (build/<spike>_cover.raw, 128x200 RGB555; never
        # committed); none: the harness draws a stand-in
        cover = ROOT / "build" / f"{spike.name}_cover.raw"
        if not cover.exists():
            cover = out / "nocover.raw"
            cover.write_bytes(bytes(4))
        objs.append(embed_blob(out, cover, "om_cover_bin"))
    elf = out / "spike.elf"
    run([PFX + "gcc", "-m2", "-nostartfiles", "-T", COMMON / "spike.ld", f"-Wl,-Map,{out / 'spike.map'}",
         *objs, SGL_LIB_FOLDED / "SEGA_SYS.A", SGL_LIB_FOLDED / "LIBCD.A", SGL_LIB_FOLDED / "LIBSGL.A",
         "-lc", "-lgcc", "-o", elf])
    tag = "".join(c for c in spike.name.upper() if c.isalnum())[:4]          # ST-040-R4 §4.2: alphanumerics only
    iso = _make_iso(out, cd, elf, "OMSAT-" + tag, "AETHER " + tag, "Aether spike",
                    extras=sorted(spike.glob("*.DAT")))
    if tracks:
        data = out / "autoboot_01.iso"
        iso.rename(data)
        lines = [f'FILE "{data.name}" BINARY', "  TRACK 01 MODE1/2048", "    INDEX 01 00:00:00"]
        for n, t in enumerate(sorted(tracks.glob("*.bin")), start=2):
            shutil.copy(t, out / f"autoboot_{n:02d}.bin")
            lines += [f'FILE "autoboot_{n:02d}.bin" BINARY', f"  TRACK {n:02d} AUDIO", "    INDEX 01 00:00:00"]
        (out / "autoboot.cue").write_text("\n".join(lines) + "\n")
        return out / "autoboot.cue"
    return iso


_DIS = re.compile(r"^\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)$")
HOP_INDIRECT = ("jsr", "jmp", "bsrf", "braf")         # register-target transfers: only the two computed jsr allowed
HOP_DIRECT = ("bra", "bsr", "bt", "bf", "bt.s", "bf.s", "bt/s", "bf/s")   # objdump prints bf.s (measured)
CCR_ADDR = 0xFFFFFE92                                 # SH7604 CCR (HWF-13)


def hop_facts(disasm: str) -> dict:
    """Parse `objdump -d -j .hop` into what check_launcher needs: count of each register-target transfer, every
    direct branch target, and the order of the two jsr against the cache-purge `mov #17` (CCR <- 0x11)."""
    f = {m: 0 for m in HOP_INDIRECT}
    f.update(direct=[], order=[])
    for line in disasm.splitlines():
        m = _DIS.match(line)
        if not m:
            continue
        op, args = m.group(2), m.group(3)
        if op in HOP_INDIRECT:
            f[op] += 1
            f["order"].append(op)
        elif op in HOP_DIRECT:
            t = re.match(r"([0-9a-f]+)", args)
            f["direct"].append((op, int(t.group(1), 16) if t else -1))
        elif op == "mov" and args.startswith("#17,"):
            f["order"].append("purge")
        elif op == "or" and (im := re.match(r"#(-?\d+),r0", args)) and (int(im.group(1)) & 0xF0) == 0xF0:
            f["order"].append("sr")                   # SR |= 0xF0: I3-I0 = 1111 (objdump prints #-16)
        elif op == "mov.l" and ",r4" in args and args.rstrip().endswith("! 2f9862ac"):
            f["order"].append("r4arg")                # S_BOOT_NO_AUTOLOAD (HWF-22) as the official menu's argument
    return f


L1_ONLY_SYMS = ("_om_bram_format", "_om_bram_l1_roundtrip", "_om_bram_l1_finish",   # OM_L1_HARNESS only (bramtest.c)
                "_om_l1g_pre", "_om_l1g_halt")                                      # M6 Task 18c (lseq.c, l1gate.h)


LBG_LW = 0x00268800   # menu/src/omlayout.h OM_LW_LBG; test_sbuild_targets pins the gate


def check_launcher(syms: dict, hop: dict, body: bytes, libc_sys: int, l1: bool = False) -> None:
    """Gates for LAUNCHER.BIN (docs/ledger.md M2 memory plan; HWF-12, HWF-13, HWF-19):
    - the entry is the first body byte (0x00200000);
    - .hop sits above the 4 KB that get_mpeg_rom(2,2,0x200000) overwrites, and transfers control nowhere but its two
      computed jsr targets (BIOS get_mpeg_rom, then 0x200000): 0 jmp / bsrf / braf / bsr, and every direct branch
      (bra, bt, bf, their /s forms) lands inside .hop (a bra reaches +-4 KB, i.e. below 0x201000);
    - .hop purges the cache between the two jsr (CCR 0xFFFFFE92 <- 0x11, as SEGA CSH_AllClr): the
      CCR address is a literal inside .hop's bytes and a `mov #17` sits between the first and the second jsr;
    - .hop masks every interrupt level (`or #0xF0` into SR) before the first jsr, and loads r4 = 0x2f9862ac
      (S_BOOT_NO_AUTOLOAD) between the two jsr;
    - no newlib syscall member was linked: libc.a carries lib_a-syscalls.o, which defines __read/__write/__open/
      __sbrk as SH simulator traps (measured while writing the plan: `ar t` + `nm` on sh-elf/lib/libc.a). If lsys.c
      misses one syscall, the linker silently pulls that member and the launcher would trap on a Saturn.
      (Counting `trapa` in the disassembly does not work: literal pools inside strtod/gethex decode as trapa. The
      plain wrappers lib_a-sysstat.o etc. are fine -- they call our _stat; measured: with lsys.c the map lists only
      lib_a-sysstat.o, without it lib_a-syscalls.o appears and the link fails on its `main` reference.)
    - the stack top is the hand-off block's start, and the heap is at least 64 KB + 0x28000 (M6 Task 22: the BRAM
      buffers; the save swap's arena, pgsrun.h OM_PGS_ARENA, which holds them);
    - the exception vector table is intact (check_vectab)."""
    def need(name, ok, why):
        if not ok:
            raise SystemExit(f"check_launcher FAILED: {name}: {why}")
    lo, hi = syms.get("__hop_start", 0), syms.get("__hop_end", 0)
    need("_lstart", syms.get("_lstart") == 0x00200000, f"entry {syms.get('_lstart')!r} != 0x200000")
    # the menu reads the body to 0x200000.. during the loading screen, while LOADBG.BIN's LW region
    # (omlayout.h OM_LW_LBG 0x268800) holds frames still to be shown: the body must end below it
    need("body_below_loadbg", len(body) + 4 <= LBG_LW - 0x00200000,   # launch.c: body + 4 <= OM_LAUNCHER_BODY_CAP
         f"body {len(body)} B reaches OM_LW_LBG {LBG_LW:#x} (max {LBG_LW - 0x00200000 - 4} B)")
    need("__hop_start", lo >= 0x00201000, f"{lo:#x} < 0x201000")
    need("__stack_top", syms.get("__stack_top") == 0x0027F000, "stack top must be OM_LW_HANDOFF")
    need("heap", syms.get("__heap_end", 0) - syms.get("__heap_start", 1 << 32) >= 0x38000,
         "heap < 64 KB + 0x28000 (cue2desc + the save swap's arena, pgsrun.h OM_PGS_ARENA; launcher.ld; M6 Task 23)")
    need("hop", hop["jsr"] == 2 and all(hop[m] == 0 for m in HOP_INDIRECT[1:]),
         "hop has " + " / ".join(f"{hop[m]} {m}" for m in HOP_INDIRECT) + " (want 2 jsr, 0 of the rest)")
    out = [(op, t) for op, t in hop["direct"] if op == "bsr" or not lo <= t < hi]
    need("hop", not out, f"hop branches outside .hop [{lo:#x},{hi:#x}) or uses bsr: {out}")
    seg = body[lo - 0x00200000:hi - 0x00200000]
    need("hop_purge", CCR_ADDR.to_bytes(4, "big") in seg, "no CCR 0xFFFFFE92 literal in .hop (HWF-13)")
    o = hop["order"]
    need("hop_purge", o.count("jsr") == 2 and "purge" in o[o.index("jsr") + 1:len(o) - o[::-1].index("jsr") - 1],
         f"no `mov #17` (CCR <- 0x11) between the two jsr in .hop: order {o}")
    j1 = o.index("jsr")
    j2 = len(o) - o[::-1].index("jsr") - 1
    need("hop_sr", "sr" in o[:j1], f"SR I3-I0 not set to 1111 (or #0xF0) before the first jsr in .hop: order {o}")
    need("hop_arg", "r4arg" in o[j1 + 1:j2],
         f"r4 = 0x2f9862ac (S_BOOT_NO_AUTOLOAD, HWF-22) not loaded between the two jsr in .hop: order {o}")
    need("libc_sys", libc_sys == 0, "newlib lib_a-syscalls.o (SH simulator traps) linked: lsys.c is missing a syscall")
    if not l1:   # the BRAM format / write-delete test is the L1 harness's only
        bad = sorted(s for s in L1_ONLY_SYMS if s in syms)
        need("l1_only", not bad, f"release LAUNCHER.BIN links the L1-only BRAM test code: {', '.join(bad)}")
    check_vectab(syms, body)
    print("check_launcher: entry 0x200000, .hop >= 0x201000, hop 2 jsr / 0 jmp,bsrf,braf,bsr, branches in .hop, "
          "CCR purge between the jsr, SR masked, r4 = S_BOOT_NO_AUTOLOAD, 0 libc syscall members, heap >= 64 KB + 0x28000", flush=True)


VEC_N, VEC_STUB = 128, 8                              # menu/launcher/lvec.h


def check_vectab(syms: dict, body: bytes, base: int = 0x00200000) -> None:
    """VBR points at _om_vectab (menu/launcher/lvec.c). Per SH7604_Hardware.txt Table 4.3/4.4 vector n
    is the long at VBR + 4n (n = 0..127) and VBR must be a multiple of 4 (sec. 4.8.2). Re-read from the linked body:
    word n == _om_vec_stubs + 8n, and stub n is `mov.l r0,@-r15` (0x2F06), `mov #n,r0` (0xE0nn),
    `bra _om_vec_common` (0xAddd), `nop` (0x0009)."""
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_vectab FAILED: {why}")
    tab, stubs, common = (syms.get(k) for k in ("_om_vectab", "_om_vec_stubs", "_om_vec_common"))
    need(None not in (tab, stubs, common), "missing _om_vectab / _om_vec_stubs / _om_vec_common")
    need(tab % 4 == 0 and stubs % 2 == 0, f"table {tab:#x} not 4-aligned (SH7604 sec. 4.8.2)")
    need(base <= tab and tab + 4 * VEC_N <= base + len(body), f"table {tab:#x} outside the body")
    for n in range(VEC_N):
        w = int.from_bytes(body[tab - base + 4 * n:tab - base + 4 * n + 4], "big")
        need(w == stubs + VEC_STUB * n, f"vector {n}: {w:#x} != stub {stubs + VEC_STUB * n:#x}")
        at = stubs + VEC_STUB * n
        ins = [int.from_bytes(body[at - base + 2 * i:at - base + 2 * i + 2], "big") for i in range(4)]
        disp = (common - (at + 4 + 4)) // 2         # bra at at+4: target = PC + 4 + 2*disp
        need(ins[0] == 0x2F06 and ins[1] == 0xE000 | n and ins[3] == 0x0009, f"stub {n} at {at:#x}: {ins}")
        need(ins[2] == 0xA000 | (disp & 0xFFF) and -2048 <= disp < 2048, f"stub {n}: bra does not reach common")
    print(f"check_vectab: {VEC_N} vectors at {tab:#x}, stubs {stubs:#x}..{stubs + VEC_STUB * VEC_N:#x}", flush=True)


IGR_STUB, IGR_STUB_END = 0x06002800, 0x06002D48      # menu/launcher/igr.h OM_IGR_STUB / _END
IGR_STAGE, IGR_STAGE_END = 0x06009800, 0x0600A000    # OM_IGR_STAGE / _END
IGR_STUB_HW = ((0x25890000, 0x25890030), (0x25818000, 0x25818004), (0x25F80000, 0x25F80120),
               (0x25E7FFFE, 0x25E80000), (0x25B00400, 0x25B00402), (0x20100000, 0x20100080),
               (0x06002000, 0x06002001), (0x06001000, 0x0600100C), (0x3FFFF, 0x40000))   # CD block, DATATRNS, VDP2,
# back color, SCSP MVOL, SMPC, the stack top, the stub state (igrstub.c CTX), the BKTAU:BKTAL value
IGR_HOOK_HW = ((0x0600026C, 0x06000270), (0x06002270, 0x06002274), (0x06000284, 0x06000288),
               (0x060003C0, 0x06000400))   # + SYS_EXECDMP's guard home (Astal; igrguard.c, HWF-80)
IGR_GUARD, IGR_GUARD_END, IGR_SEAL = 0x060003C0, 0x06000400, 0xFFFFFFA5   # igr.h OM_IGR_GUARD / _END / OM_IGR_SEAL


def igr_facts(disasm: str) -> dict:
    """Parse `objdump -d -j <section>`: every literal value objdump annotates ("! <hex>") and every direct branch.
    Literal-pool words are skipped as instructions (fix round 1: the pool word 0x0003 of 0x0003FFFF decodes as
    `bsrf r0`): every PC-relative load names its pool address, so those addresses are known before the scan."""
    f = {"lits": [], "direct": [], "indirect": 0, "order": []}
    rows, pool = [], set()
    for line in disasm.splitlines():
        m = _DIS.match(line)
        if not m:
            continue
        op, args = m.group(2), m.group(3)
        rows.append((int(m.group(1), 16), op, args))
        # objdump labels a literal that matches a symbol ("! 6002af4 <_om_igr_run>", "! 2000c0 <_memcpy>")
        if (lit := re.search(r"!\s*([0-9a-f]+)(?:\s*<[^>]*>)?\s*$", args)) and op in ("mov.l", "mov.w"):
            f["lits"].append(int(lit.group(1), 16))
            if t := re.match(r"([0-9a-f]+)", args):
                at = int(t.group(1), 16)
                pool.update((at, at + 2) if op == "mov.l" else (at,))
    for at, op, args in rows:
        if at in pool:
            continue
        if op in HOP_DIRECT:
            t = re.match(r"([0-9a-f]+)", args)
            f["direct"].append((op, int(t.group(1), 16) if t else -1))
        if op in ("bsrf", "braf"):
            f["indirect"] += 1
        if op in ("jsr", "jmp"):                    # check_cwx: the register-target calls, in order
            f["order"].append(op)
        elif op == "mov" and args.startswith("#17,"):
            f["order"].append("purge")               # CCR <- 0x11 (HWF-13)
    return f


def check_igr(syms: dict, stub: dict, hook: dict) -> None:
    """M6 Task 20a gates (menu/launcher/igr.h, launcher.ld): the in-game reset stub runs when the game calls
    SYS_EXECDMP, long after the launcher and LWRAM are gone, and the hook runs after the BIOS re-init. So:
    - the stub is linked at 0x06002800 and fills its home to exactly 0x06002D48 (the last long is the seal word the
      guard checks, Astal 2026-11-23); its entry is the home's first byte and om_igr_run is inside it;
    - the hook section is linked at 0x06009800 and the staged hook + stub + guard images end by 0x0600A000;
    - every literal either section loads that addresses BIOS ROM, LWRAM, HWRAM or register space is inside its own
      section, inside the stub, or on a fixed list of registers/BIOS cells (IGR_STUB_HW / IGR_HOOK_HW); no bsrf/braf;
    - every direct branch lands inside its own section (so no call reaches the launcher or a library)."""
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_igr FAILED: {why}")
    s0, s1 = syms.get("___igr_stub_start"), syms.get("___igr_stub_end")
    h0, h1 = syms.get("___igr_hook_start"), syms.get("___igr_hook_end")
    need(None not in (s0, s1, h0, h1), "missing ___igr_* symbols")
    g0, g1 = syms.get("___igr_guard_start", IGR_GUARD), syms.get("___igr_guard_end", IGR_GUARD)
    need(s0 == IGR_STUB and s1 == IGR_STUB_END, f"stub {s0:#x}..{s1:#x} does not fill {IGR_STUB:#x}..{IGR_STUB_END:#x}")
    need(syms.get("___igr_code_end", s1 - 4) <= IGR_STUB_END - 4, "stub code runs into the seal word")
    need(syms.get("_om_igr_entry") == s0, "_om_igr_entry is not the stub home's first byte (the guard jumps there)")
    stage = (s1 - s0) + (g1 - g0)
    need(h0 == IGR_STAGE and h1 + stage <= IGR_STAGE_END, f"staging {h0:#x}..{h1 + stage:#x} past {IGR_STAGE_END:#x}")
    for name in ("_om_igr_entry", "_om_igr_run"):
        need(s0 <= syms.get(name, 0) < s1, f"{name} not inside the stub")
    need(h0 <= syms.get("_om_igr_hook", 0) < h1, "_om_igr_hook not inside the hook section")

    def allowed(v, own, extra):
        if own[0] <= v < own[1] or s0 <= v < s1 or any(a <= v < b for a, b in extra):
            return True
        # anything that addresses BIOS ROM, LWRAM, HWRAM or the peripheral/register space (either cache alias) must be
        # on the lists above; other values are plain constants (poll counts, the '/' word)
        return not any(a <= v < b for a, b in ((0x10000, 0x80000), (0x00200000, 0x00300000), (0x06000000, 0x06100000),
                                              (0x20000000, 0x26100000)))
    # the hook's own range is the whole staged image: it reads the stub image right after itself (___igr_hook_end)
    for name, f, own, extra in (("stub", stub, (s0, s1), IGR_STUB_HW),
                                ("hook", hook, (h0, h1 + stage), IGR_HOOK_HW)):
        bad = [hex(v) for v in f["lits"] if not allowed(v, own, extra)]
        need(not bad, f"{name} loads addresses outside itself and the allowed list: {bad}")
        out = [(op, hex(t)) for op, t in f["direct"] if not own[0] <= t < own[1]]
        need(not out, f"{name} branches outside itself: {out}")
        need(f["indirect"] == 0, f"{name} uses bsrf/braf")
        need(f["lits"], f"{name}: no literal found (objdump format changed?)")
    print(f"check_igr: stub {s0:#x}..{s1:#x} (code {syms.get('___igr_code_end', s1) - s0} B + seal), staging {h0:#x}.."
          f"{h1 + stage:#x}, {len(stub['lits'])}+{len(hook['lits'])} literals and every branch inside", flush=True)


CWX_TAIL, CWX_TAIL_END = 0x002FF000, 0x00300000   # menu/launcher/cwxplan.h OM_CWX_TAIL / _END
# every address-like literal .cwxtail may load (menu/launcher/cwxtail.c): the IP's stack top, LWRAM's start, the two IP
# work words, the SYS_CHGSCUIM vector, CCR, the IP entry, SYS_EXECDMP, the security-code cell, the BIOS read vector
CHK_MAX = 400                                     # menu/launcher/chkai.h OM_CHK_MAX: Kai's block, staged after the stub
CWX_HW = frozenset({0x06002000, 0x00200000, 0x06000290, 0x060002B0, 0x06000344, 0xFFFFFE92, 0x06002100, 0x0600026C,
                    0x06002270, 0x06000284})


def check_cwx(syms: dict, tail: dict) -> None:
    """M6 Task 17 gates (menu/launcher/cwxtail.c, launcher.ld): .cwxtail runs after the launcher's own LWRAM is gone
    (it zeroes it) and its hook runs from inside the game's IP, so:
    - it is linked at 0x002FF000 and, with the IGR stub image staged after it, ends by 0x00300000;
    - _om_cwx_go, _om_cwx_hook and the cell / parameter / resident words are inside it;
    - every literal that addresses BIOS ROM, LWRAM, HWRAM or register space is inside the tail + staged image or on
      CWX_HW; no bsrf/braf; every direct branch lands inside the tail;
    - the register-target transfers are exactly: jsr (SYS_CHGSCUIM), the cache purge (`mov #17`, CCR <- 0x11),
      jsr (the IP at 0x06002100), then the hook's jmp (the BIOS read) -- so the IP is never entered unpurged."""
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_cwx FAILED: {why}")
    t0, t1 = syms.get("___cwx_tail_start"), syms.get("___cwx_tail_end")
    s0, s1 = syms.get("___igr_stub_start"), syms.get("___igr_stub_end")
    need(None not in (t0, t1, s0, s1), "missing ___cwx_tail_* / ___igr_stub_* symbols")
    need(t0 == CWX_TAIL and t1 + (s1 - s0) + CHK_MAX <= CWX_TAIL_END,
         f"tail {t0:#x}..{t1:#x} + staged stub {s1 - s0} B + cheat block {CHK_MAX} B not inside "
         f"{CWX_TAIL:#x}..{CWX_TAIL_END:#x}")
    for name in ("_om_cwx_go", "_om_cwx_hook", "___cwx_cell", "___cwx_par", "___cwx_res", "___cwx_chk"):
        need(t0 <= syms.get(name, 0) < t1, f"{name} not inside .cwxtail")
    need(syms.get("___cwx_chimg") == t1 + (s1 - s0), "the staged cheat block (___cwx_chimg) is not right after the stub")
    own = (t0, t1 + (s1 - s0) + 1)                    # the hook reads the staged images from ___cwx_img = tail end

    def addr_like(v):
        return any(a <= v < b for a, b in ((0x10000, 0x80000), (0x00200000, 0x00300000), (0x06000000, 0x06100000),
                                           (0x20000000, 0x26100000), (0xFFFFFE00, 0x100000000)))
    bad = [hex(v) for v in tail["lits"] if addr_like(v) and not own[0] <= v < own[1] and v not in CWX_HW]
    need(not bad, f"tail loads addresses outside itself and CWX_HW: {bad}")
    out = [(op, hex(t)) for op, t in tail["direct"] if not t0 <= t < t1]
    need(not out, f"tail branches outside itself: {out}")
    need(tail["indirect"] == 0, "tail uses bsrf/braf")
    need(tail["order"] == ["jsr", "purge", "jsr", "jmp"],
         f"tail transfers {tail['order']}, want jsr (SYS_CHGSCUIM), purge, jsr (IP), jmp (hook -> BIOS read)")
    for v, what in ((0x06002100, "the IP entry"), (0xFFFFFE92, "CCR"), (0x06002270, "the security cell")):
        need(v in tail["lits"], f"no {what} literal {v:#x} in the tail")
    print(f"check_cwx: tail {t0:#x}..{t1:#x} ({t1 - t0} B) + staged stub {s1 - s0} B = end {t1 + (s1 - s0):#x} "
          f"(<= {CWX_TAIL_END:#x}), {len(tail['lits'])} literals, order {tail['order']}", flush=True)


class _Sh2Stop(Exception):
    pass


def sh2_guard_run(code: bytes, base: int, mem: dict, steps: int = 200000) -> dict:
    """Runs SYS_EXECDMP's guard (igrguard.c: the linked .igrguard bytes, at `base`) on a small SH-2 model: the
    instructions the guard is written with and nothing else (an unknown opcode stops the run), delay slots, and long
    reads from `mem` (address -> long: the stub home). A `dt Rn; bf <the dt>` delay loop is collapsed to Rn = 0.
    Returns {"pc": where control left the guard (None: it never did), "smpc": [(address, byte) written], "sr": SR,
    "why": the stop reason}."""
    r = [0] * 16
    st = {"sr": 0, "t": 0, "smpc": [], "pc": None, "why": ""}

    def rd(a, n):
        if base <= a and a + n <= base + len(code):
            return int.from_bytes(code[a - base:a - base + n], "big")
        if n == 4 and a in mem:
            return mem[a]
        if n == 2 and ("w", a) in mem:                        # a 16-bit register (the stub's entry: HIRQ)
            return mem[("w", a)]
        raise _Sh2Stop(f"read {a:#x}")

    def s8(v):
        return v - 0x100 if v & 0x80 else v

    def s12(v):
        return v - 0x1000 if v & 0x800 else v

    pc, slot = base, None
    try:
        for _ in range(steps):
            if not base <= pc < base + len(code):
                st["pc"], st["why"] = pc, "left the guard"
                return st
            op = rd(pc, 2)
            n, m, i8 = (op >> 8) & 15, (op >> 4) & 15, op & 0xFF
            nxt = pc + 2
            branch = op >> 8 in (0x89, 0x8B, 0x8D, 0x8F) or op >> 12 == 0xA or op & 0xF0FF == 0x402B
            if branch and slot is not None:
                raise _Sh2Stop(f"branch in a delay slot at {pc:#x}")
            if op >> 12 == 0xD:                                   # mov.l @(disp,PC),Rn
                r[n] = rd(((pc & ~3) + 4) + i8 * 4, 4)
            elif op >> 12 == 0x9:                                 # mov.w @(disp,PC),Rn
                w = rd(pc + 4 + i8 * 2, 2)
                r[n] = (w - 0x10000 if w & 0x8000 else w) & 0xFFFFFFFF
            elif op >> 12 == 0xE:                                 # mov #imm,Rn
                r[n] = s8(i8) & 0xFFFFFFFF
            elif op >> 12 == 0x7:                                 # add #imm,Rn
                r[n] = (r[n] + s8(i8)) & 0xFFFFFFFF
            elif op & 0xF00F == 0x6003:                           # mov Rm,Rn
                r[n] = r[m]
            elif op & 0xF00F == 0x300C:                           # add Rm,Rn
                r[n] = (r[n] + r[m]) & 0xFFFFFFFF
            elif op & 0xF00F == 0x6001:                           # mov.w @Rm,Rn
                w = rd(r[m], 2)
                r[n] = (w - 0x10000 if w & 0x8000 else w) & 0xFFFFFFFF
            elif op & 0xF00F == 0x2008:                           # tst Rm,Rn
                st["t"] = int(r[n] & r[m] == 0)
            elif op & 0xF00F == 0x6006:                           # mov.l @Rm+,Rn
                r[n] = rd(r[m], 4)
                if m != n:
                    r[m] = (r[m] + 4) & 0xFFFFFFFF
            elif op & 0xF00F == 0x2000:                           # mov.b Rm,@Rn
                if not 0x20100000 <= r[n] < 0x20100080:
                    raise _Sh2Stop(f"byte write outside the SMPC: {r[n]:#x}")
                st["smpc"].append((r[n], r[m] & 0xFF))
            elif op & 0xF0FF == 0x4010:                           # dt Rn
                if slot is None and rd(pc + 2, 2) == 0x8BFD:      # dt Rn; bf <the dt>: the delay loop
                    r[n], st["t"], nxt = 0, 1, pc + 4
                else:
                    r[n] = (r[n] - 1) & 0xFFFFFFFF
                    st["t"] = int(r[n] == 0)
            elif op & 0xF0FF == 0x4028:                           # shll16 Rn
                r[n] = (r[n] << 16) & 0xFFFFFFFF
            elif op >> 8 == 0x88:                                 # cmp/eq #imm,R0
                st["t"] = int(r[0] == (s8(i8) & 0xFFFFFFFF))
            elif op >> 8 == 0xCB:                                 # or #imm,R0
                r[0] |= i8
            elif op & 0xF0FF == 0x0002:                           # stc sr,Rn
                r[n] = st["sr"]
            elif op & 0xF0FF == 0x400E:                           # ldc Rm,sr
                st["sr"] = r[n] & 0x3F3
            elif op == 0x0009:                                    # nop
                pass
            elif branch:
                if op >> 12 == 0xA:                               # bra
                    tgt, delayed = pc + 4 + 2 * s12(op & 0xFFF), True
                elif op & 0xF0FF == 0x402B:                       # jmp @Rm
                    tgt, delayed = r[n], True
                else:                                             # bt / bf / bt.s / bf.s
                    take = st["t"] == (1 if op >> 8 in (0x89, 0x8D) else 0)
                    delayed = op >> 8 in (0x8D, 0x8F)
                    tgt = pc + 4 + 2 * s8(i8) if take else pc + 4
                    if not delayed:
                        tgt = tgt if take else pc + 2
                if delayed:
                    slot, pc = tgt, pc + 2
                    continue
                nxt = tgt
            else:
                raise _Sh2Stop(f"unknown opcode {op:#06x} at {pc:#x}")
            if slot is not None:
                nxt, slot = slot, None
            pc = nxt
        st["why"] = "step limit"
    except _Sh2Stop as e:
        st["why"] = str(e)
    return st


def check_igr_guard(syms: dict, code: bytes, text: str, hook_lits: list) -> None:
    """Astal (hardware 2026-11-23, 3c64908): the game GFS_Loads its title overlays to 0x06002000, over the stub, and
    A+B+C+Start froze because SYS_EXECDMP pointed straight into it. SYS_EXECDMP's guard (igrguard.c) sits at
    0x060003C0..0x06000400 (the BIOS copyright text, HWF-80), and the linked bytes are RUN on sh2_guard_run:
    - the stub home sealed (it sums to OM_IGR_SEAL): control leaves for 0x06002800 and nothing is written;
    - the home overwritten (synthetic overlay bytes), zeroed, or one long changed: control never leaves, interrupts
      are masked (SR I = 15), and SMPC SF = 1 then COMREG = 0x0D, repeated (SYSRES retried);
    - om_cwx_boot points 0x0600026C at the guard (CWX, armed or not) and the JHL hook loads its address."""
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_igr_guard FAILED: {why}")
    g0, g1 = syms.get("___igr_guard_start"), syms.get("___igr_guard_end")
    need(None not in (g0, g1), "missing ___igr_guard_* symbols (SYS_EXECDMP points straight at the stub)")
    need(g0 == IGR_GUARD and g0 < g1 <= IGR_GUARD_END,
         f"guard {g0:#x}..{g1:#x} not inside {IGR_GUARD:#x}..{IGR_GUARD_END:#x}")
    need(syms.get("_om_igr_guard") == g0, "_om_igr_guard is not the guard's first byte")
    need(len(code) == g1 - g0, f"guard bytes {len(code)} != section {g1 - g0}")
    longs = (IGR_STUB_END - IGR_STUB) // 4

    def home(vals):
        return {IGR_STUB + 4 * k: v & 0xFFFFFFFF for k, v in enumerate(vals)}
    img = [((k + 1) * 2654435761 ^ 0x2F06E000) & 0xFFFFFFFF for k in range(longs)]
    img[-1] = (IGR_SEAL - sum(img[:-1])) & 0xFFFFFFFF
    ok = sh2_guard_run(code, g0, home(img))
    need(ok["pc"] == IGR_STUB and not ok["smpc"],
         f"intact stub: the guard went to {ok['pc']} ({ok['why']}) and wrote {ok['smpc']}")
    bad_cases = {"overlay": [0x8B0B53E5 ^ (k * 0x9E3779B9) for k in range(longs)], "zeroed": [0] * longs,
                 "one long": img[:longs // 2] + [img[longs // 2] ^ 0x10000] + img[longs // 2 + 1:]}
    for what, vals in bad_cases.items():
        st = sh2_guard_run(code, g0, home(vals), steps=4000)
        w = st["smpc"]
        need(st["pc"] is None, f"{what}: the guard left for {st['pc'] or 0:#x} instead of resetting the console")
        need((st["sr"] >> 4) & 15 == 15, f"{what}: interrupts not masked before SYSRES (SR {st['sr']:#x})")
        need(len(w) >= 4 and w[:4] == [(0x20100063, 1), (0x2010001F, 0x0D)] * 2,
             f"{what}: no repeated SMPC SYSRES (SF = 1, COMREG = 0x0D): {w[:4]} ({st['why']})")
    boot = fn_blocks(text).get("_om_cwx_boot", [])
    lit = lambda v: any(op == "mov.l" and re.search(rf"!\s*{v:x}\b", a) for op, a in boot)
    need(lit(0x0600026C) and lit(g0), "om_cwx_boot does not point 0x0600026c at the guard")
    need(g0 in hook_lits, "the JHL hook does not load the guard's address")
    print(f"check_igr_guard: {g0:#x}..{g1:#x} ({g1 - g0} B) run: intact -> {IGR_STUB:#x}; overlay, zeroed, one long -> "
          f"SYSRES retried; CWX and JHL point SYS_EXECDMP at it", flush=True)


def check_igr_entry(syms: dict, body: bytes) -> None:
    """Enemy Zero (hardware 2026-11-25): a game's lid exit (SYS_EXECDMP with the CD block's DCHG set, at its own disc
    prompt) goes to the BIOS's own SYS_EXECDMP -- the CD player, which boots the disc the Satiator presents on the lid's
    close, as the official menu does -- when the launcher recorded it (om_igr_bios_execdmp, JHL), else the in-game reset
    runs. The stub's linked entry bytes are RUN on sh2_guard_run with that long patched in, as om_igr_w_arm does:
    - recorded, HIRQ with DCHG: control leaves for the recorded address and nothing is written;
    - recorded, no DCHG (A+B+C+Start), or not recorded (CWX) with DCHG: control stays in the stub and reaches the
      in-game reset's own path (the jsr to om_igr_run)."""
    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_igr_entry FAILED: {why}")
    s0, s1, lma = syms.get("___igr_stub_start"), syms.get("___igr_stub_end"), syms.get("___igr_stub_lma")
    at = syms.get("_om_igr_bios_execdmp")
    need(None not in (s0, s1, lma, at) and s0 <= at < s1 - 4 and not (at - s0) % 4,
         "missing or misplaced _om_igr_bios_execdmp / ___igr_stub_* symbols")
    img = bytearray(body[lma - 0x00200000:lma - 0x00200000 + (s1 - s0)])
    need(len(img) == s1 - s0, "stub image not in the body")
    need(int.from_bytes(img[at - s0:at - s0 + 4], "big") == 0, "the linked image records a BIOS entry (must be 0)")
    bios = 0x0000186C                               # BIOS 1.01 (Mednafen sega_101.bin): 0x0600026C = 0x186C, ROM 0x11B6

    def run(rec, hirq):
        code = bytearray(img)
        code[at - s0:at - s0 + 4] = rec.to_bytes(4, "big")
        return sh2_guard_run(bytes(code), s0, {("w", 0x25890008): hirq}, steps=200)
    st = run(bios, 0x0021)
    need(st["pc"] == bios and not st["smpc"], f"lid exit: went to {st['pc']} ({st['why']}), not the BIOS's {bios:#x}")
    for what, rec, hirq in (("A+B+C+Start", bios, 0x0001), ("CWX (not recorded)", 0, 0x0021)):
        st = run(rec, hirq)
        need(st["pc"] is None and "0x400b" in st["why"],
             f"{what}: the entry left for {st['pc']} or stopped elsewhere ({st['why']}), not the reset's jsr")
    print(f"check_igr_entry: lid exit (DCHG) -> the recorded BIOS SYS_EXECDMP; A+B+C+Start or none recorded -> the "
          f"in-game reset", flush=True)


def check_chkai(syms: dict, body: bytes, kai_objs: dict, base: int = 0x00200000) -> None:
    """M6 Task 27b gate (menu/launcher/chkai.h, kai/*.s): the bytes cwxboot.c copies as Kai's bootstrap (40 B) and
    in-RAM handler (96 B) are, in the linked LAUNCHER.BIN, exactly the objects assembled from the verbatim sources
    (kai_objs: name -> bytes, assembled again for this check) and exactly tools/omsat/sh2kai.py's KAI_BOOT/KAI_HDLR;
    the bootstrap's two pc-relative literals land on +40 (handler) and +44 (config), the slot is +32; and the linked
    bytes, run on the SH-2 model from a planted trapa #5, hold a 16-bit code, write an 8-bit one, honour an enable code,
    and return to the instruction after the trapa with the game's registers (bar the displaced instruction's own)."""
    from omsat import sh2kai

    def need(ok, why):
        if not ok:
            raise SystemExit(f"check_chkai FAILED: {why}")
    got = {}
    for sym, n, ref in (("_asm_cheat_bootstrap_ihr", 40, sh2kai.KAI_BOOT), ("_asm_cheatcodes_hdlr_ram", 96, sh2kai.KAI_HDLR)):
        a = syms.get(sym)
        need(a is not None and base <= a and a + n <= base + len(body), f"{sym} missing or outside the body")
        b = body[a - base:a - base + n]
        need(b == kai_objs.get(sym, b"")[:n], f"{sym}: linked bytes differ from the assembled verbatim source")
        need(b == ref, f"{sym}: linked bytes differ from sh2kai's pinned copy")
        got[sym] = b
    boot, hdlr = got["_asm_cheat_bootstrap_ihr"], got["_asm_cheatcodes_hdlr_ram"]
    o4, o0 = int.from_bytes(boot[12:14], "big"), int.from_bytes(boot[14:16], "big")
    need(o4 >> 8 == 0xD4 and 12 + 4 + 4 * (o4 & 0xFF) == sh2kai.BOOT_P_CFG, "the bootstrap's config literal is not +44")
    need(o0 >> 8 == 0xD0 and (14 & ~3) + 4 + 4 * (o0 & 0xFF) == sh2kai.BOOT_P_HDLR, "the handler literal is not +40")
    need(boot[sh2kai.BOOT_SLOT:sh2kai.BOOT_SLOT + 2] == bytes((0, 9)), "the displaced-instruction slot is not a nop at +32")
    place, master = 0x06002580, 0x06000914
    regs = [0x11111111 * i for i in range(15)]
    try:
        blk = sh2kai.config(place, 0x224B, [(0x06078A5C, 0x0080, 1), (0x06070001, 0x1263, 3)], boot=boot, hdlr=hdlr)
        cpu, m = sh2kai.run_trap(blk, place, master, 0xC305, {0x06078A5C: 0x0001, 0x06070000: 0}, regs=regs)
        ok16, ok8 = m.rd(0x06078A5C, 2) == 0x0080, m.rd(0x06070000, 2) == 0x0063
        want = list(regs)
        want[2] |= regs[4]
        back = cpu.pc == master + 2 and cpu.r[:15] == want and cpu.r[15] == 0x06002000
        blk = sh2kai.config(place, 0x224B, [(0x06070100, 0xBEEF, 1)], en=(0x06070000, 7), boot=boot, hdlr=hdlr)
        _, m1 = sh2kai.run_trap(blk, place, master, 0xC305, {0x06070000: 6, 0x06070100: 0})
        _, m2 = sh2kai.run_trap(blk, place, master, 0xC305, {0x06070000: 7, 0x06070100: 0})
        en = m1.rd(0x06070100, 2) == 0 and m2.rd(0x06070100, 2) == 0xBEEF
    except sh2kai.Stop as e:
        raise SystemExit(f"check_chkai FAILED: the linked bytes stopped on the SH-2 model: {e}")
    need(ok16 and ok8, "a 16-bit or 8-bit code did not land")
    need(en, "the enable code did not gate the codes")
    need(back, "the bootstrap did not return to the master's next instruction with the game's registers")
    print(f"check_chkai: Kai bootstrap {syms['_asm_cheat_bootstrap_ihr']:#x} (40 B) + handler "
          f"{syms['_asm_cheatcodes_hdlr_ram']:#x} (96 B) = the assembled sources; literals +40/+44, slot +32; "
          "SH-2 model: 16/8-bit codes, enable code, rte back", flush=True)


def check_lorb(syms: dict, obj: Path) -> None:
    """lorb.c draws the page of a launch that failed at or after step 9, when High Work RAM is gone: it, what it calls
    and what those call (uifont.c, om_fmt.c, lgap.c) must be in Low Work RAM, never in .lui (launcher.ld)."""
    lo, hi = syms["___lui_start"], syms["___lui_end"]
    und = []
    for o in (obj, obj.with_name("src_uifont.o"), obj.with_name("src_om_fmt.o"), obj.with_name("launcher_lgap.o")):
        und += subprocess.run([PFX + "nm", "-u", str(o)], check=True, capture_output=True, text=True).stdout.split()
    und += [n for n in syms if n.startswith(("_om_uf_", "_om_crc32", "_om_lclock_"))]
    # uifont.c's om_face_get and om_flag_for name the .lui faces and flags; the page takes its faces from the VRAM stash
    und = [u for u in und if not u.startswith(("_om_face_", "_om_flags"))]
    own = [n for n, a in syms.items() if n.startswith("_om_orb_")]
    bad = [n for n in [u for u in und if u != "U"] + own if lo <= syms.get(n, 0) < hi]
    if bad or not own:
        raise SystemExit(f"check_lorb FAILED: in .lui (gone after step 9): {', '.join(bad) or 'no om_orb_ symbols'}")
    print(f"check_lorb: lorb.o, its callees and theirs outside .lui", flush=True)


def build_launcher(l1: bool = False) -> Path:
    """LAUNCHER.BIN: SGL-free LWRAM program (spec §7.1). Body linked at 0x00200000 by menu/launcher/launcher.ld;
    file = 4 KB OMSATLNC header (tools/omsat/lnchhdr.py) + body.
    l1=True: the copy only the L1 harness embeds, built with -DOM_L1_HARNESS into
    menu/launcher/out_l1 -- it alone carries the emulator BRAM write/delete/format test and accepts OM_HO_TEST_BRAM."""
    from omsat.lnchhdr import make_header
    _clean_sgl_headers()
    _fold_sgl_libs()
    out = LAUNCHER / ("out_l1" if l1 else "out")
    _fresh(out)
    srcs = [*sorted(LAUNCHER.glob("*.c")), MSRC / "om_fmt.c", MSRC / "handoff.c", MSRC / "msgfmt.c", MSRC / "padgest.c",
            MSRC / "isodir.c", MSRC / "bupfile.c", MSRC / "sdwrite.c", MSRC / "pgsplan.c", MSRC / "gameid.c",
            MSRC / "frcgap.c", MSRC / "savegen.c", MSRC / "impscan.c", MSRC / "cheatpak.c", MSRC / "marquee.c",
            MSRC / "loadbg.c", MSRC / "bootanim.c", MSRC / "uifont.c", MSRC / "uifont_data.c", MSRC / "biglogo.c",
            MSRC / "logoblend.c", MSRC / "yield.c",
            SAT / "satiator.c", *sorted((LAUNCHER / "kai").glob("*.s"))]
    # loadbg.c, bootanim.c, uifont*.c: the screens (lgfx.c, in .lui): the eye background's decoder and the menu's faces
    # biglogo.c, logoblend.c: the orbiting logo's art (lorbpage.c, .lui); lorb.c draws its pages after step 9 too
    # cheatpak.c, marquee.c, kai/*.s: M6 Tasks 27b/28 -- the cheat load and editor (lcheat.c), the
    # menu's marquee for a long name, Kai's bootstrap and in-RAM handler (verbatim, GPL-2.0; check_chkai)
    # isodir.c: M6 Task 17, the CWX boot's PVD read. bupfile.c: M6 Task 22, the BRAM layer's names and messages.
    # sdwrite.c, pgsplan.c, gameid.c, frcgap.c: M6 Task 23, the per-game-save executor (pgsrun.c) -- the launcher's
    # first SD writer through Task 21's helper, its planner, the slot folders, and the gap's time bound (lgap.c);
    # savegen.c: M6 Task 24b, the add-only save folders' reader rule (the menu's slot status links it too)
    # yield.c: sdwrite.c's reads offer the menu's start-up screen a frame (yield.h); never set in the launcher
    objs = _compile(srcs, out, LCFLAGS + ["-DOM_CD_DIAG"] + (["-DOM_L1_HARNESS"] if l1 else []))   # cdblk.c's error-screen registers
    elf = out / "launcher.elf"

    def link(bss_base: int, probe: int) -> None:
        run([PFX + "gcc", "-m2", "-nostartfiles", "-T", LAUNCHER / "launcher.ld", f"-Wl,-Map,{out / 'launcher.map'}",
             f"-Wl,--defsym=__bss_base={bss_base:#x}", f"-Wl,--defsym=__ld_probe={probe}",
             *objs, SGL_LIB_FOLDED / "SEGA_SYS.A", SGL_LIB_FOLDED / "LIBCD.A", "-lc", "-lgcc", "-o", elf])
    # .bss goes at the top of LWRAM, under the stack (launcher.ld): link once to measure it, then for real
    link(0x00400000, 1)
    s0 = nm_symbols(elf)
    link((0x0027B000 - (s0["__bss_end"] - s0["__bss_start"])) & ~0xF, 0)
    hop = subprocess.run([PFX + "objdump", "-d", "-j", ".hop", str(elf)], check=True, capture_output=True, text=True).stdout
    (out / "hop.dis").write_text(hop)
    libc_sys = len(set(re.findall(r"libc\.a\(lib_a-syscalls\.o\)", (out / "launcher.map").read_text())))
    run([PFX + "objcopy", "-O", "binary", elf, out / "body.bin"])
    body = (out / "body.bin").read_bytes()
    check_launcher(nm_symbols(elf), hop_facts(hop), body, libc_sys=libc_sys, l1=l1)
    igr = {sec: subprocess.run([PFX + "objdump", "-d", "-j", sec, str(elf)], check=True, capture_output=True,
                               text=True).stdout for sec in (".igrstub", ".igrhook")}
    (out / "igr.dis").write_text(igr[".igrstub"] + igr[".igrhook"])
    check_igr(nm_symbols(elf), igr_facts(igr[".igrstub"]), igr_facts(igr[".igrhook"]))
    cwx = subprocess.run([PFX + "objdump", "-d", "-j", ".cwxtail", str(elf)], check=True, capture_output=True,
                         text=True).stdout
    (out / "cwx.dis").write_text(cwx)
    check_cwx(nm_symbols(elf), igr_facts(cwx))
    text = subprocess.run([PFX + "objdump", "-d", "-j", ".text", str(elf)], check=True, capture_output=True,
                          text=True).stdout
    syms = nm_symbols(elf)
    g_off = syms.get("___igr_guard_lma", 0x00200000) - 0x00200000
    g_len = syms.get("___igr_guard_end", 0) - syms.get("___igr_guard_start", 0)
    check_igr_guard(syms, body[g_off:g_off + max(g_len, 0)], text, igr_facts(igr[".igrhook"])["lits"])
    check_igr_entry(syms, body)
    check_launcher_sshoff(text)
    kai = {}
    for s_, sym in (("cheat_bootstrap_ihr", "_asm_cheat_bootstrap_ihr"), ("cheat_codes_hdlr_ram", "_asm_cheatcodes_hdlr_ram")):
        o = out / f"kai_alone_{s_}.o"
        run([PFX + "as", LAUNCHER / "kai" / f"{s_}.s", "-o", o])
        run([PFX + "objcopy", "-O", "binary", "-j", ".text", o, out / f"kai_alone_{s_}.bin"])
        kai[sym] = (out / f"kai_alone_{s_}.bin").read_bytes()
    check_chkai(syms, body, kai)
    check_lorb(syms, out / "launcher_lorb.o")
    (out / "LAUNCHER.BIN").write_bytes(make_header(body) + body)
    heap = syms["__heap_end"] - syms["__heap_start"]
    print(f"LAUNCHER.BIN: body {len(body)} B, file {len(body) + 0x1000} B, .lui {syms['___lui_end'] - syms['___lui_start']} B "
          f"(HWRAM), heap {heap} B = floor + {heap - 0x38000} B", flush=True)
    return out / "LAUNCHER.BIN"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("spike", nargs="?")
    ap.add_argument("--tracks")
    ap.add_argument("--app", action="store_true")
    ap.add_argument("--app-fake", action="store_true")
    ap.add_argument("--launcher", action="store_true")
    ap.add_argument("--sdtest", action="store_true",
                    help="M6 Task 21: with --app/--app-fake, the H2 test disc with the diagnostics SD stress page")
    ap.add_argument("--no-biglogo", action="store_true",
                    help="M6 C2 mutation: a menu disc without BIGLOGO.BIN (the interstitial's logo stays blank, named)")
    a = ap.parse_args()
    if a.app:
        print("built", build_app(no_biglogo=a.no_biglogo, sdtest=a.sdtest))
    elif a.app_fake:
        print("built", build_app(fake=True, no_biglogo=a.no_biglogo, sdtest=a.sdtest))
    elif a.launcher:
        print("built", build_launcher())
    elif a.spike:
        print("built", build(Path(a.spike).resolve(), Path(a.tracks).resolve() if a.tracks else None))
    else:
        ap.error("give a spike directory, --app or --launcher")
    return 0


if __name__ == "__main__":
    sys.exit(main())
