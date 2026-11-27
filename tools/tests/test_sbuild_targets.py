import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402

NM = """06004000 T SGL_Start
06004120 T _ss_main
060a1234 B _end
00200000 T _lstart
         U _undefined_thing
"""


def test_parse_nm():
    s = sbuild._parse_nm(NM)
    assert s["_end"] == 0x060A1234 and s["SGL_Start"] == 0x06004000 and "_undefined_thing" not in s


def test_check_hw_end_passes_and_fails():
    assert sbuild.check_hw_end({"_end": 0x060A1234}) == 0x060A1234
    with pytest.raises(SystemExit, match="_end=0x60f0004"):
        sbuild.check_hw_end({"_end": 0x060F0004})
    with pytest.raises(SystemExit, match="no _end"):
        sbuild.check_hw_end({})


def test_runtime_promoted_and_spike_common_trimmed():
    root = Path(sbuild.ROOT)
    for n in ("crt0.c", "workarea.c", "ip.ld"):
        assert (root / "menu" / "runtime" / n).is_file()
        assert not (root / "menu" / "spikes" / "common" / n).exists()
    assert (root / "menu" / "spikes" / "common" / "spike.ld").is_file()


BASE = 0x00200000
STUBS, COMMON, TAB = 0x00201000, 0x00201400, 0x00202000
HOP_LO, HOP_HI = 0x00203000, 0x00203040
SYMS = {"_lstart": BASE, "__hop_start": HOP_LO, "__hop_end": HOP_HI, "__stack_top": 0x0027F000,
        "__heap_start": 0x00230000, "__heap_end": 0x0027B000,
        "_om_vectab": TAB, "_om_vec_stubs": STUBS, "_om_vec_common": COMMON}


def dis(addr, raw, op, args=""):
    """One line in the shape of `saturn-sh2-elf-objdump -d` (measured this session on launcher.elf)."""
    return f"  {addr:x}:\t{raw}       \t{op}\t{args}\n"


# The shape of the real .hop disassembly: get_mpeg_rom jsr, the CCR purge, the 0x200000 jsr, an in-section loop.
HOP_OK = ("00203000 <_om_hop_to_official>:\n"
          + dis(0x202ffa, "00 02", "stc", "sr,r0")
          + dis(0x202ffc, "cb f0", "or", "#-16,r0")
          + dis(0x202ffe, "40 0e", "ldc", "r0,sr")
          + dis(0x203000, "60 12", "mov.l", "@r1,r0")
          + dis(0x203002, "40 0b", "jsr", "@r0")
          + dis(0x203004, "8f 10", "bf.s", "203020 <_om_hop_to_official+0x20>")
          + dis(0x203006, "d1 02", "mov.l", "203010 <_om_hop_to_official+0x10>,r1\t! fffffe92")
          + dis(0x203008, "e0 11", "mov", "#17,r0")
          + dis(0x20300a, "21 00", "mov.b", "r0,@r1")
          + dis(0x20300c, "a0 02", "bra", "203014 <_om_hop_to_official+0x14>")
          + dis(0x20300e, "00 09", "nop")
          + dis(0x203010, "ff ff", ".word", "0xffff")
          + dis(0x203012, "fe 92", ".word", "0xfe92")
          + dis(0x203013, "d4 09", "mov.l", "203040 <_om_hop_to_official+0x40>,r4\t! 2f9862ac")
          + dis(0x203014, "48 0b", "jsr", "@r8")
          + dis(0x203016, "af fc", "bra", "203016 <_om_hop_to_official+0x16>"))
PURGE = dis(0x203008, "e0 11", "mov", "#17,r0")
SRMASK = dis(0x202ffc, "cb f0", "or", "#-16,r0")
R4ARG = dis(0x203013, "d4 09", "mov.l", "203040 <_om_hop_to_official+0x40>,r4\t! 2f9862ac")


def body(tab_fix=None, ccr=True):
    b = bytearray(0x4000)
    for n in range(128):
        b[TAB - BASE + 4 * n:TAB - BASE + 4 * n + 4] = (STUBS + 8 * n).to_bytes(4, "big")
        at = STUBS + 8 * n - BASE
        disp = (COMMON - (STUBS + 8 * n + 8)) // 2
        for i, w in enumerate((0x2F06, 0xE000 | n, 0xA000 | (disp & 0xFFF), 0x0009)):
            b[at + 2 * i:at + 2 * i + 2] = w.to_bytes(2, "big")
    if ccr:
        b[0x3010:0x3014] = (0xFFFFFE92).to_bytes(4, "big")
    if tab_fix:
        tab_fix(b)
    return bytes(b)


def test_hop_facts_parses_the_real_shape():
    f = sbuild.hop_facts(HOP_OK)
    assert f["jsr"] == 2 and f["jmp"] == f["bsrf"] == f["braf"] == 0
    assert f["order"] == ["sr", "jsr", "purge", "r4arg", "jsr"]
    assert ("bra", 0x203014) in f["direct"] and ("bf.s", 0x203020) in f["direct"]


def test_check_launcher_passes():
    sbuild.check_launcher(SYMS, sbuild.hop_facts(HOP_OK), body(), libc_sys=0)


def test_check_launcher_l1_variant_may_carry_the_bram_test():
    s = dict(SYMS, _om_bram_format=0x00210000, _om_bram_l1_roundtrip=0x00210100)
    sbuild.check_launcher(s, sbuild.hop_facts(HOP_OK), body(), libc_sys=0, l1=True)


def _syms(**kw):
    s = dict(SYMS)
    s.update(kw)
    return s


@pytest.mark.parametrize("syms,hop,libc_sys,msg", [
    (_syms(_lstart=0x00200010), HOP_OK, 0, "_lstart"),
    (_syms(__hop_start=0x00200800), HOP_OK, 0, "__hop_start"),
    (_syms(__heap_start=0x00278000), HOP_OK, 0, "heap"),
    (_syms(__heap_start=0x0027B000 - 0x37FF0), HOP_OK, 0, "heap"),     # under 64 KB + the save arena
    (_syms(_om_bram_format=0x00210000), HOP_OK, 0, "l1_only"),          # M6 Task 22 I1: BRAM format in a release
    (_syms(_om_bram_l1_roundtrip=0x00210000), HOP_OK, 0, "l1_only"),    # ... or its write/delete test
    (SYMS, HOP_OK, 1, "libc_sys"),
    (SYMS, HOP_OK + dis(0x203018, "40 0b", "jsr", "@r0"), 0, "hop"),                 # a third jsr
    (SYMS, HOP_OK.replace("jsr\t@r8", "jmp\t@r8"), 0, "hop"),                         # tail jmp
    (SYMS, HOP_OK + dis(0x203018, "04 23", "braf", "r4"), 0, "hop"),
    (SYMS, HOP_OK + dis(0x203018, "04 03", "bsrf", "r4"), 0, "hop"),
    (SYMS, HOP_OK + dis(0x203018, "b0 02", "bsr", "203020 <x>"), 0, "hop"),
    (SYMS, HOP_OK.replace("bra\t203016", "bra\t200016"), 0, "outside .hop"),           # bra below 0x201000
    (SYMS, HOP_OK.replace("bf.s\t203020", "bf.s\t200020"), 0, "outside .hop"),         # bf.s below 0x201000
    (SYMS, HOP_OK.replace("#17,r0", "#16,r0"), 0, "hop_purge"),                       # no CP|CE store
    (SYMS, HOP_OK.replace(PURGE, "") + PURGE, 0, "hop_purge"),                         # purge after the jump
    (SYMS, HOP_OK.replace(SRMASK, ""), 0, "hop_sr"),                                    # SR never masked
    (SYMS, HOP_OK.replace("#-16,r0", "#-32,r0"), 0, "hop_sr"),                          # I3-I0 not all set
    (SYMS, HOP_OK.replace(SRMASK, "") + SRMASK, 0, "hop_sr"),                           # masked only after the jsr
    (SYMS, HOP_OK.replace(R4ARG, ""), 0, "hop_arg"),                                    # no S_BOOT_NO_AUTOLOAD
    (SYMS, HOP_OK.replace("! 2f9862ac", "! 2f9862ad"), 0, "hop_arg"),                   # wrong magic
    (SYMS, HOP_OK.replace(R4ARG, "") + R4ARG, 0, "hop_arg"),                            # r4 set after the jump
])
def test_check_launcher_fails(syms, hop, libc_sys, msg):
    with pytest.raises(SystemExit, match=msg):
        sbuild.check_launcher(syms, sbuild.hop_facts(hop), body(), libc_sys=libc_sys)


def test_check_launcher_fails_without_ccr_literal():
    with pytest.raises(SystemExit, match="hop_purge"):
        sbuild.check_launcher(SYMS, sbuild.hop_facts(HOP_OK), body(ccr=False), libc_sys=0)


def test_check_launcher_fails_when_the_body_reaches_the_loadbg_lw_region():
    # the menu reads the body into 0x200000.. while LOADBG.BIN's LW region (omlayout.h OM_LW_LBG,
    # 0x268800) holds the loading screen's frames; a body past 0x68800 B would overwrite them mid-launch
    big = body() + bytes(0x68800 - 4 - 0x4000 + 1)
    with pytest.raises(SystemExit, match="body_below_loadbg"):
        sbuild.check_launcher(SYMS, sbuild.hop_facts(HOP_OK), big, libc_sys=0)
    sbuild.check_launcher(SYMS, sbuild.hop_facts(HOP_OK), body() + bytes(0x68800 - 4 - 0x4000), libc_sys=0)
    import re
    h = (Path(sbuild.__file__).resolve().parent.parent / "menu" / "src" / "omlayout.h").read_text()
    assert int(re.search(r"#define OM_LW_LBG\s+(0x[0-9A-Fa-f]+)u", h).group(1), 16) == sbuild.LBG_LW


def _swap(b):
    o = TAB - BASE
    b[o + 36:o + 40], b[o + 40:o + 44] = b[o + 40:o + 44], b[o + 36:o + 40]


def _imm(b):
    b[STUBS - BASE + 8 * 11 + 2:STUBS - BASE + 8 * 11 + 4] = (0xE000 | 12).to_bytes(2, "big")


def _bra(b):
    b[STUBS - BASE + 8 * 127 + 4:STUBS - BASE + 8 * 127 + 6] = (0xA001).to_bytes(2, "big")   # true disp is 0


@pytest.mark.parametrize("fix,msg", [(_swap, "vector 9"), (_imm, "stub 11"), (_bra, "stub 127")])
def test_check_vectab_fails(fix, msg):
    with pytest.raises(SystemExit, match=msg):
        sbuild.check_vectab(SYMS, body(tab_fix=fix))


def test_fake_build_swaps_libsatiator_for_the_fake():
    real, fake = sbuild._app_sources(False), sbuild._app_sources(True)
    assert sbuild.SAT / "satiator.c" in real and sbuild.SAT / "satiator.c" not in fake
    assert any(p.name == "fakesat.c" for p in fake) and not any(p.name == "fakesat.c" for p in real)
    assert {p for p in real if p.name != "satiator.c"} <= set(fake)


def test_check_vectab_alignment_and_symbols():
    sbuild.check_vectab(SYMS, body())
    with pytest.raises(SystemExit, match="4-aligned"):
        sbuild.check_vectab(_syms(_om_vectab=TAB + 2), body())
    s = dict(SYMS)
    del s["_om_vec_common"]
    with pytest.raises(SystemExit, match="missing"):
        sbuild.check_vectab(s, body())
