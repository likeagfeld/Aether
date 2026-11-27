# tools/tests/test_bupfile.py -- the .BUP save file (contract/SPEC.md §5.16)
import os
from pathlib import Path

import pytest

from omsat.bupfile import BupDir, BupError, corpus_json, decode, encode, index_folder, slot_path

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"
TINY = BupDir(b"TESTSAVE01", b"VIBEMENU", 1, 0x01020304, 6, 1)
# the official Pseudo Saturn Kai release's SAVES tree (pskai_release_20240114), when it is on this machine:
# OMSAT_KAI_SAVES names it; the tests that read it skip without it
KAI_SAVES = Path(os.environ.get("OMSAT_KAI_SAVES", r"D:\Satiator Menu Customization\pskai_release_20240114\SAVES"))
KAI_BUP = [KAI_SAVES / "Ark35" / "CART_001" / "BLASTWIND_0.BUP"]


def test_fixture_round_trip():
    b = (FIX / "tiny.bup").read_bytes()
    assert b == encode(TINY, b"ABCDEF")
    d, data, date2 = decode(b)
    assert (d, data, date2) == (TINY, b"ABCDEF", 0x01020304)


def test_corrupt_and_truncated():
    b = (FIX / "tiny.bup").read_bytes()
    for i in range(4):
        with pytest.raises(BupError, match="magic"):
            decode(b[:i] + bytes([b[i] ^ 1]) + b[i + 1:])
    for n in range(len(b)):
        with pytest.raises(BupError, match="short"):
            decode(b[:n])
    with pytest.raises(BupError, match="name"):
        decode(b[:0x10] + bytes(12) + b[0x1C:])
    with pytest.raises(BupError):
        encode(BupDir(b"", datasize=0), b"")
    with pytest.raises(BupError):
        encode(BupDir(b"ABCDEFGHIJKL", datasize=0), b"")


def test_slot_paths():
    assert slot_path("/aether/SAVES", "MK-81214[UT]", 0, b"TESTSAVE01") == "/aether/SAVES/MK-81214[UT]/TESTSAVE01.BUP"
    assert slot_path("/S", "G", 9, b"TESTSAVE01") == "/S/G/09/TESTSAVE01.BUP"
    for bad in ((10, b"A"), (0, b"A:B"), (0, b"")):
        with pytest.raises(BupError):
            slot_path("/S", "G", *bad)


def test_sgc_name_byte_11():
    b = bytearray((FIX / "tiny.bup").read_bytes())
    b[0x10:0x1C] = b"DRACULAX_02."                 # Save Game Copier: '.' in byte 11
    assert decode(bytes(b))[0].name == b"DRACULAX_02"


def test_kai_release_corpus():
    """Every .BUP in the PSKai release's SAVES tree (Kai and Save Game Copier files) decodes: 326 of 326."""
    files = sorted(KAI_SAVES.rglob("*.BUP")) if KAI_SAVES.is_dir() else []
    if not files:
        pytest.skip("Kai release not on this machine")
    bad = []
    for p in files:
        try:
            decode(p.read_bytes())
        except BupError as e:
            bad.append((p.name, str(e)))
    assert len(files) == 326 and bad == []


def test_kai_release_headers_fixture_and_duplicates():
    """Review I2/M2: the checked-in header corpus is the release's (when it is here); readers key on the record name
    (89 files are named otherwise); the 3 duplicated records resolve by date (DAYTONA__00 -> DAYTONA__00.BUP)."""
    import json
    fix = FIX / "kai_bup_headers.json"
    if KAI_SAVES.is_dir():
        assert fix.read_text(encoding="utf-8") == corpus_json(str(KAI_SAVES))
    rows = json.loads(fix.read_text(encoding="utf-8"))["files"]
    folders: dict = {}
    own = 0
    for r in rows:
        b = bytes.fromhex(r["hdr"]).ljust(r["len"], bytes(1))
        folder, fn = r["path"].rsplit("/", 1)
        folders.setdefault(folder, {})[fn] = b
        own += fn.upper() == decode(b)[0].name.decode().upper() + ".BUP"
    assert (len(rows), len(rows) - own) == (326, 89)
    win = index_folder(folders["vbt/20200620"])
    assert win[b"DAYTONA__00"] == "DAYTONA__00.BUP"
    assert index_folder(folders["delta7890/PANZER_ZWEI"])[b"PANDRA_ZWEI"] == "PANDRA_ZWEI_02.BUP"   # date over save_id
    dups = sum(1 for f in folders.values() for n in index_folder(f)
               if sum(decode(b)[0].name == n for b in f.values()) > 1)
    assert dups == 3


def test_real_kai_file():
    p = next((p for p in KAI_BUP if p.is_file()), None)
    if p is None:
        pytest.skip("Kai release not on this machine")
    b = p.read_bytes()
    d, data, date2 = decode(b)
    assert len(b) == 332
    assert d.name == b"BLASTWIND_0" and d.language == 0 and d.date == 0x0093D4D0
    assert d.datasize == 268 and d.blocks == 2 and date2 == 0x014FC6DB
    assert d.comment == bytes.fromhex("ccded7bdc4b3b2ddc4de")    # Shift-JIS, passed through
    # our writer reproduces Kai's bytes except the second date (Kai stamps its own start-up date at 0x34)
    ours = encode(d, data)
    assert ours[:0x34] == b[:0x34] and ours[0x38:] == b[0x38:]
