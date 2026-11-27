"""manager/golden/mkcases.py -- Aether Card Manager golden inputs and Python-side expects (M4 Task 1).
GPL-3.0-or-later. Uses tools/omsat read-only (the Python reference, design spec §9).

    python manager/golden/mkcases.py --bup-corpus     -> manager/golden/expect/bup_corpus.json
    python manager/golden/mkcases.py --cases          -> manager/golden/cases/{corpus,merge,existing,rerun,cart,sticky,gens,highslot}.json
    python manager/golden/mkcases.py --tidy-cases     -> manager/golden/tidy_cases/*.json (M6 Task 24b)
    python manager/golden/mkcases.py --match-real     -> manager/golden/expect/match_real.json
    python manager/golden/mkcases.py --gamesdb-fill   -> manager/golden/expect/gamesdb_fill.json
    python manager/golden/mkcases.py --artpak         -> manager/golden/expect/artpak.json

match_real.json: the keys and aliases of a full GAMES.DB (build/db/GAMES.DB, cardbuild's input: product codes only, no
titles) and omsat.match.Matcher's result for every header of contract/fixtures/real_headers.json (the user's 36 retail
discs) and xband_headers.json (15 discs), so the C# matcher is checked on real discs against the full key set.

bup_corpus.json: for every row of contract/fixtures/kai_bup_headers.json (the 326 .BUP headers of Pseudo Saturn Kai's
release SAVES tree, cafe-alpha and contributors) omsat.bupfile.decode's verdict on a file of that length (the header plus
len-64 zero bytes: decode reads only the header and the length), and for every pair of rows in one folder holding the
same decoded record name, omsat.bupfile.prefer (SPEC §5.16 Duplicates)."""
from __future__ import annotations

import json
import sys
from itertools import combinations
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
from omsat import bupfile  # noqa: E402


def _err(msg: str) -> str:
    return msg.split(":", 1)[0]          # "short" / "magic" / "name"


def bup_corpus() -> dict:
    rows = json.loads((REPO / "contract/fixtures/kai_bup_headers.json").read_text())["files"]
    out, by = [], {}
    for r in rows:
        b = bytes.fromhex(r["hdr"]) + bytes(max(0, r["len"] - 64))
        b = b[:r["len"]]
        try:
            d, _data, _d2 = bupfile.decode(b)
            out.append({"path": r["path"], "ok": True, "err": "", "name": d.name.decode("ascii"), "date": d.date,
                        "datasize": d.datasize, "blocks": d.blocks})
            folder, fn = r["path"].rsplit("/", 1)
            by.setdefault((folder, d.name), []).append((fn, b))
        except bupfile.BupError as e:
            out.append({"path": r["path"], "ok": False, "err": _err(str(e)), "name": "", "date": 0, "datasize": 0,
                        "blocks": 0})
    pairs = []
    for (folder, _name), files in sorted(by.items()):
        for (fa, a), (fb, b) in combinations(files, 2):
            pairs.append({"a": f"{folder}/{fa}", "b": f"{folder}/{fb}", "a_wins": bupfile.prefer(a, fa, b, fb)})
            pairs.append({"a": f"{folder}/{fb}", "b": f"{folder}/{fa}", "a_wins": bupfile.prefer(b, fb, a, fa)})
    return {"source": "omsat.bupfile over contract/fixtures/kai_bup_headers.json (mkcases.py --bup-corpus)",
            "rows": out, "pairs": pairs}


# --- import cases (SPEC §5.17 Import), realized by realize.py and by the C# TempCard ------------------------------
def bup(path: str, name: str, length: int, date: int, seed: int, save_id: int = 0, comment: bytes = b"TEST",
        tail: bytes = b"", byte1b: int = 0, datasize: int | None = None) -> dict:
    """A .BUP file of `length` data bytes; save_id, byte 0x1B and a header datasize other than length (a truncated
    file) are patched over omsat.bupfile.encode's header."""
    h = bytearray(bupfile.encode(bupfile.BupDir(name.encode("ascii"), comment, 1, date, length, 0), bytes(length))[:64])
    h[4:8] = save_id.to_bytes(4, "big")
    h[0x1B] = byte1b
    if datasize is not None:
        h[0x2C:0x30] = datasize.to_bytes(4, "big")
    return {"path": path, "hdr": bytes(h).hex(), "data": {"len": length, "seed": seed}, "tail": tail.hex()}


def case_corpus() -> dict:
    """The 326 corpus headers laid out as menu/host-tests/test_pgsbak.c:167-221 does (at 2c5968b): corpus folder f (in
    the fixture's path order) -> f%4 0 Kai region, 1 Kai product-only slot f%9+1, 2 Rings id (lower case), 3 Rings
    [SLOT0n]; data zero-filled to the row's length; plus the fixed extras."""
    rows = json.loads((REPO / "contract/fixtures/kai_bup_headers.json").read_text())["files"]
    folds: list[str] = []
    files = []
    for r in rows:
        folder, fn = r["path"].rsplit("/", 1)
        if folder not in folds:
            folds.append(folder)
        f = folds.index(folder)
        n = f % 9 + 1
        src = [f"SAVES/BRAM/T-{f:03d}[JU]", f"SAVES/BRAM/T-{f:03d}/0{n}", f"satiator-saves/t-{f:03d}",
               f"satiator-saves/[SLOT0{n}]/T-{f:03d}"][f % 4]
        files.append({"path": f"{src}/{fn}", "head": r["hdr"][:2 * min(64, r["len"])], "len": r["len"]})
    files += [bup("SAVES/BRAM/_BACKUP/GENERAL_01.BUP", "GENERAL_01", 40, 1, 5),
              bup("satiator-saves/_BACKUP/GENERAL_02.BUP", "GENERAL_02", 40, 2, 5),
              bup("satiator-saves/[SLOT12]/T-900/SLOT12_SAVE.BUP", "SLOT12_SAVE", 40, 3, 5),
              bup("satiator-saves/T-901/SAVRING.BUP", "SAVRING", 0, 4, 1),
              {"path": "SAVES/BRAM/T-000[JU]/SLOT.TXT", "raw": b"id=0\r\n".hex()}]
    return {"runs": 1, "dirs": ["aether"], "files": files}


def merge_files() -> list[dict]:
    K, KP, R = "SAVES/BRAM/MK-81214[UT]", "SAVES/BRAM/MK-81214", "satiator-saves/MK-81214"
    return [
        # one record in three sources: a later date beats the Kai region copy; on the same date the higher save_id wins
        bup(f"{K}/SCORCHER_01.BUP", "SCORCHER_01", 300, 50, 1, save_id=1),
        bup(f"{KP}/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 2),
        bup(f"{R}/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 3, save_id=2),
        # byte-identical copies under other file names: the own-name file wins, the other is not quarantined
        bup(f"{K}/SCORCHER_02.BUP", "SCORCHER_02", 200, 70, 4),
        bup(f"{R}/S2.BUP", "SCORCHER_02", 200, 70, 4),
        # a full tie down to the file name: X3 < Y3; the loser differs, so it is quarantined
        bup(f"{K}/X3.BUP", "SCORCHER_03", 120, 80, 5),
        bup(f"{KP}/Y3.BUP", "SCORCHER_03", 120, 80, 6),
        # Save Game Copier's '.' at 0x1B after an 11-character name; trailing bytes after the data are copied too
        bup(f"{K}/SGCNAME_ABC.BUP", "SGCNAME_ABC", 77, 90, 7, byte1b=0x2E, tail=bytes([1, 2, 3])),
        # a name with bytes outside A-Z 0-9 _ - (the destination file name sanitizes it)
        bup(f"{K}/SWAGMAN.BUP", "SWAGMAN.DAT", 64, 91, 8),
        # reserved (R4), too big, kept at the limit, truncated
        bup(f"{K}/PSEUDO_BRAM.BUP", "PSEUDO_BRAM", 32, 92, 9),
        bup(f"{K}/BIGSAVE_001.BUP", "BIGSAVE_001", 32833 - 64, 93, 10),
        bup(f"{K}/BIGSAVE_002.BUP", "BIGSAVE_002", 32832 - 64, 94, 11),
        bup(f"{K}/TRUNC_00001.BUP", "TRUNC_00001", 100, 95, 12, datasize=500),
        {"path": f"{K}/README.TXT", "raw": b"not a save\n".hex()},
        # slot 3: Kai's region sub-folder against Rings' [SLOT03]; Rings' later copy wins, Kai's goes to quarantine
        bup(f"{K}/03/SLOTSAVE_03.BUP", "SLOTSAVE_03", 50, 10, 13),
        bup(f"satiator-saves/[SLOT03]/MK-81214/SLOTSAVE_03.BUP", "SLOTSAVE_03", 50, 20, 14),
        # skipped: Rings slot 12, a folder that is not a product id, the '_' general sets
        bup("satiator-saves/[SLOT12]/MK-81214/X.BUP", "XSLOT12_SAV", 40, 1, 15),
        bup("SAVES/BRAM/Gosub/SF3-1_1.BUP", "SFORCE31_01", 40, 1, 16),
        bup("SAVES/BRAM/_BACKUP/GENERAL_01.BUP", "GENERAL_01", 40, 1, 17),
        # a second region folder VibeMenu already has: it receives the product-only sources too
        bup("aether/SAVES/MK-81214[J]/OWN_0000001.BUP", "OWN_0000001", 30, 1, 18),
        # a product only in Rings, in lower case; a product only in Kai's product-only folder, slot 2
        bup("satiator-saves/t-123/ABC_SAVE_01.BUP", "ABC_SAVE_01", 60, 3, 19),
        bup("SAVES/BRAM/GS-9001/02/GS_SAVE_002.BUP", "GS_SAVE_002", 60, 4, 20),
        bup("SAVES/BRAM/GS-9001/GS_SAVE_000.BUP", "GS_SAVE_000", 60, 5, 21),
    ]


def case_merge() -> dict:
    return {"runs": 1, "dirs": ["aether"], "files": merge_files()}


def case_existing() -> dict:
    """VibeMenu already holds SCORCHER_01 in the winning version (its own header: save_id 0) -> present, and
    SCORCHER_02 in another version -> conflict; an earlier run's quarantine copy of the Kai product-only SCORCHER_01."""
    files = merge_files()
    files += [bup("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 3),
              bup("aether/SAVES/MK-81214[UT]/SCORCHER_02.BUP", "SCORCHER_02", 200, 71, 30),
              bup("aether/SAVES/_QUARANTINE/IMPORT/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 2)]
    return {"runs": 1, "dirs": ["aether"], "files": files}


def case_cart() -> dict:
    """saves over 32 KB (a cartridge or floppy save: file length + 4 > 64 + 32768 + 4) are copied
    cart-only into <folder>[/0n]/CART/ -- the header and data only (64 + datasize bytes), in pieces -- with the same
    newest-intact rule, quarantine (into _QUARANTINE/IMPORT/<folder>[/0n]/CART/) and present/conflict checks; a copy of
    the same record name small enough for the internal memory is a separate group in the folder itself."""
    K, KP, R = "SAVES/BRAM/MK-81214[UT]", "SAVES/BRAM/MK-81214", "satiator-saves/MK-81214"
    return {"runs": 2, "dirs": ["aether"], "files": [
        # one cart record from Kai and Rings: Rings' later copy wins; Kai's goes to the quarantine's CART folder;
        # Kai's trailing bytes after the data are not part of the copy
        bup(f"{K}/CARTSAV_001.BUP", "CARTSAV_001", 40000, 10, 40, tail=bytes(range(5))),
        bup(f"{R}/CARTSAV_001.BUP", "CARTSAV_001", 40000, 20, 41, tail=bytes([9, 9])),
        # the same record name at an internal-memory size: its own group, into the folder itself
        bup(f"{KP}/CARTSAV_001.BUP", "CARTSAV_001", 200, 30, 42),
        # exactly one byte over the limit, and the size of the corpus' largest file (161,325 B), in slot 2
        bup(f"{K}/CARTSAV_002.BUP", "CARTSAV_002", 32833 - 64, 5, 43),
        bup(f"{K}/02/CARTSAV_009.BUP", "CARTSAV_009", 161325 - 64, 6, 44),
        # already in the destination's CART folder in the same version (present), and in another version (conflict)
        bup(f"{K}/CARTSAV_003.BUP", "CARTSAV_003", 50000, 7, 45),
        bup("aether/SAVES/MK-81214[UT]/CART/CARTSAV_003.BUP", "CARTSAV_003", 50000, 7, 45),
        bup(f"{K}/CARTSAV_004.BUP", "CARTSAV_004", 50000, 8, 46),
        bup("aether/SAVES/MK-81214[UT]/CART/CARTSAV_004.BUP", "CARTSAV_004", 50000, 9, 47),
        # a truncated cart file (datasize past the end) is still skipped
        bup(f"{K}/CARTSAV_005.BUP", "CARTSAV_005", 33000, 9, 48, datasize=40000),
    ]}


def case_rerun() -> dict:
    """The merge case imported twice: the second run copies nothing and quarantines nothing again."""
    return {"runs": 2, "dirs": ["aether"], "files": merge_files()}


def case_sticky() -> dict:
    """test_pgsbak.c sim_i5: a folder VibeMenu already uses is sticky. VibeMenu reads
    SAVES/MK-81214 (an earlier import) and Kai has MK-81214[UT]: merged into MK-81214, never a new region folder. Five
    Kai region folders of one product, plus its product-only folder and Rings, merge into VibeMenu's product-only folder
    (more sources than fix round 2's 4 for one slot). VibeMenu's own GS-9001[U] gets Kai's product-only GS-9001, while
    Kai's GS-9001[J], which VibeMenu has not, becomes a new folder; Kai's product-only T-777 goes into VibeMenu's
    T-777[J]."""
    files = [
        bup("aether/SAVES/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 50, 900, 1),
        bup("aether/SAVES/MK-81214/03/SCORCHER_01.BUP", "SCORCHER_01", 60, 950, 2),
        bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 40, 500, 3),
        bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_02.BUP", "SCORCHER_02", 40, 500, 4),
        bup("SAVES/BRAM/MK-81214[UT]/03/SCORCHER_03.BUP", "SCORCHER_03", 40, 500, 5),
        bup("aether/SAVES/T-555/OWN_SAVE_01.BUP", "OWN_SAVE_01", 30, 1, 6),
        bup("satiator-saves/T-555/RINGS_SAV_1.BUP", "RINGS_SAV_1", 30, 2, 7),
        bup("SAVES/BRAM/T-555/KAIPO_SAV_1.BUP", "KAIPO_SAV_1", 30, 3, 8),
        bup("aether/SAVES/GS-9001[U]/OWN_GS_0001.BUP", "OWN_GS_0001", 30, 1, 9),
        bup("SAVES/BRAM/GS-9001/GS_SAVE_000.BUP", "GS_SAVE_000", 60, 5, 10),
        bup("SAVES/BRAM/GS-9001[J]/GS_SAVE_JP0.BUP", "GS_SAVE_JP0", 60, 6, 11),
        bup("aether/SAVES/T-777[J]/OWN_777_01.BUP", "OWN_777_01", 30, 1, 12),
        bup("SAVES/BRAM/T-777/KAI_777_01.BUP", "KAI_777_01", 30, 2, 13),
    ]
    for i, reg in enumerate(["U", "J", "E", "T", "UT"]):        # five Kai regions + po + Rings into one slot of T-555
        files.append(bup(f"SAVES/BRAM/T-555[{reg}]/REG_SAV_{i:03d}.BUP", f"REG_SAV_{i:03d}", 30, 10 + i, 20 + i))
        files.append(bup(f"SAVES/BRAM/T-555[{reg}]/SHARED_SAVE.BUP", "SHARED_SAVE", 30, 30 + i, 30 + i))
    return {"runs": 2, "dirs": ["aether"], "files": files}


def done(path: str) -> dict:
    return {"path": path, "raw": ""}


def case_highslot() -> dict:
    """test_pgsbak.c sim_hs/sim_gl, ported: T-300's VibeMenu slot 01 holds GAMEOPT_001
    (shared, unchanged) and its own GAMESAV_001; Rings [SLOT18]/T-300 holds the same GAMEOPT_001 plus a GAMESAV_001 of
    its own -- 01 is not SLOT18's (one shared record is not a run again), so it goes into the first empty slot, 02;
    a run again (runs: 2) finds 02 already its (present, nothing copied). T-301 has all 10 slots already filled with a
    GAMESAV_001 that [SLOT17] does not match: every slot full, into ALT/ as an alternate; a run again finds it there
    (present). Kai's SAVES/LATEST holds GENSAVE_001, in no BRAM/<game> folder: copied into _GENERAL and noted on
    GENLIST.TXT's "I" line (fix I9); a run again is idempotent (present, the same GENLIST.TXT)."""
    files = [
        bup("aether/SAVES/T-300/01/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 10, 101),
        bup("aether/SAVES/T-300/01/GAMESAV_001.BUP", "GAMESAV_001", 60, 50, 102),
        bup("satiator-saves/[SLOT18]/T-300/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 10, 101),
        bup("satiator-saves/[SLOT18]/T-300/GAMESAV_001.BUP", "GAMESAV_001", 60, 90, 103),
    ]
    for k in range(10):                                           # T-301: every slot already holds a non-matching copy
        p = f"aether/SAVES/T-301{'/0' + str(k) if k else ''}/GAMESAV_001.BUP"
        files.append(bup(p, "GAMESAV_001", 60, 20 + k, 110 + k))
    files.append(bup("satiator-saves/[SLOT17]/T-301/GAMESAV_001.BUP", "GAMESAV_001", 60, 90, 120))
    files.append(bup("SAVES/LATEST/GENSAVE_001.BUP", "GENSAVE_001", 70, 3000, 130))
    return {"runs": 2, "dirs": ["aether"], "files": files}


def case_gens() -> dict:
    """M6 Task 24b: the import writes ordinary saves into the destination's CURRENT set (savegen.h: its newest complete
    generation, else the folder itself), under new names. MK-81214[UT] has generations G00003 (complete: current) and
    G00004 (no DONE.TXT: ignored); the folder's own SCORCHER_02 is no longer current, so Kai's SCORCHER_02 is copied
    into G00003; SCORCHER_01 is there in the winning version (present), SCORCHER_03 in another one (conflict). Slot 3's
    own generation 03/G000/G00001 receives SLOTSAVE_03; CART and the quarantine stay at the folder. GS-9001 has
    G000/G00099 complete and G001/G00100 not: the save goes into G00099. T-123 has only an incomplete generation: into
    the folder itself."""
    U = "aether/SAVES/MK-81214[UT]"
    files = merge_files() + [
        bup(f"{U}/SCORCHER_02.BUP", "SCORCHER_02", 200, 1, 50),
        bup(f"{U}/G000/G00003/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 3, save_id=2),
        bup(f"{U}/G000/G00003/SCORCHER_03.BUP", "SCORCHER_03", 120, 81, 51),
        done(f"{U}/G000/G00003/DONE.TXT"),
        bup(f"{U}/G000/G00004/SCORCHER_02.BUP", "SCORCHER_02", 200, 70, 4),
        bup(f"{U}/03/G000/G00001/OLD_SLOT3_1.BUP", "OLD_SLOT3_1", 20, 1, 52),
        done(f"{U}/03/G000/G00001/DONE.TXT"),
        bup("aether/SAVES/GS-9001/G000/G00099/GS_OLD_0001.BUP", "GS_OLD_0001", 20, 1, 53),
        done("aether/SAVES/GS-9001/G000/G00099/DONE.TXT"),
        bup("aether/SAVES/GS-9001/G001/G00100/GS_NEW_0001.BUP", "GS_NEW_0001", 20, 2, 54),
        bup("aether/SAVES/T-123/G000/G00001/ABC_SAVE_01.BUP", "ABC_SAVE_01", 60, 1, 55),
    ]
    return {"runs": 2, "dirs": ["aether"], "files": files}


# --- Tidy cases (SPEC §5.17 Tidy saves), realized by realize.py and by the C# TempCard -----------------
def bkp(path: str, seq: int, recs: list[dict], cut: int = 0) -> dict:
    """A BRAM backup generation (pgsbak.h layout; realize.py builds it): recs are bup() dicts (their paths unused);
    cut: bytes cut off the end (a torn file)."""
    return {"path": path, "bkp": {"seq": seq, "date": "20261122-120000", "marker": "T-00001@U 0", "cut": cut,
                                  "recs": [{k: v for k, v in r.items() if k != "path"} for r in recs]}}


def tidy_gens() -> dict:
    """Every Tidy rule on one card (ops: preview, run, preview, run -- the second pair finds nothing)."""
    G = "aether/SAVES/T-00001[U]"

    def a1(p, **k): return bup(p, "A_SAV", 300, 10, 1, **k)       # A_SAV version 1
    def a2(p, **k): return bup(p, "A_SAV", 310, 20, 2, **k)       # version 2
    def a3(p): return bup(p, "A_SAV", 320, 30, 3)                  # version 3 (in an incomplete generation only)
    def cfg(p, **k): return bup(p, "B_CFG", 120, 5, 4, **k)       # never changes
    files = [
        a1(f"{G}/A_SAV.BUP"), cfg(f"{G}/B_CFG.BUP", save_id=7),     # pre-24b files (a save_id outside the record)
        a1(f"{G}/G000/G00001/A_SAV.BUP"), cfg(f"{G}/G000/G00001/B_CFG.BUP"), done(f"{G}/G000/G00001/DONE.TXT"),
        a2(f"{G}/G000/G00002/A_SAV.BUP"), cfg(f"{G}/G000/G00002/B_CFG.BUP"), done(f"{G}/G000/G00002/DONE.TXT"),
        {"path": f"{G}/G000/G00002/A_SAV.BUP.TMP", "raw": "00"},
        a2(f"{G}/G000/G00003/A_SAV.BUP"), cfg(f"{G}/G000/G00003/B_CFG.BUP"), done(f"{G}/G000/G00003/DONE.TXT"),
        a3(f"{G}/G000/G00004/A_SAV.BUP"), cfg(f"{G}/G000/G00004/B_CFG.BUP"),           # incomplete: not current
        cfg(f"{G}/CART/B_CFG.BUP"),                                                     # CART: never touched
        bup(f"{G}/A_SAV~1.BUP", "A_SAV", 300, 10, 9),                                   # same name and date, other data
        bup(f"{G}/ABCDEFGHIJKLMNOP.BUP", "B_CFG", 120, 5, 4),                           # a 20-byte name: left alone
        {"path": f"{G}/JUNK.BUP", "raw": "566d656d00"},                                  # does not decode
        # slot 1, with its own generation
        bup(f"{G}/01/S1_SAV.BUP", "S1_SAV", 40, 3, 11), bup(f"{G}/01/G000/G00001/S1_SAV.BUP", "S1_SAV", 40, 3, 11),
        done(f"{G}/01/G000/G00001/DONE.TXT"),
        # T-00002: shards; G000 holds only a duplicate of the current G00100 -> emptied, removed, then the shard; an
        # empty shard G002 on top (dirs). T-00003: G00098 holds a folder (never removed), G00097 a .BUP that does not
        # decode (kept, so its generation stays)
        bup("aether/SAVES/T-00002[U]/G000/G00099/C_SAV.BUP", "C_SAV", 50, 1, 20),
        done("aether/SAVES/T-00002[U]/G000/G00099/DONE.TXT"),
        bup("aether/SAVES/T-00002[U]/G001/G00100/C_SAV.BUP", "C_SAV", 50, 1, 20),
        done("aether/SAVES/T-00002[U]/G001/G00100/DONE.TXT"),
        {"path": "aether/SAVES/T-00003[U]/G000/G00098/SUB/X.TXT", "raw": "00"},
        bup("aether/SAVES/T-00003[U]/G000/G00098/D_SAV.BUP", "D_SAV", 50, 1, 21),
        {"path": "aether/SAVES/T-00003[U]/G000/G00097/BAD.BUP", "raw": "00" * 70},
        bup("aether/SAVES/T-00003[U]/G000/G00099/D_SAV.BUP", "D_SAV", 50, 1, 21),
        done("aether/SAVES/T-00003[U]/G000/G00099/DONE.TXT"),
        # _GENERAL with generations
        bup("aether/SAVES/_GENERAL/GENERAL_SAV.BUP", "GENERAL_SAV", 40, 1, 30),
        bup("aether/SAVES/_GENERAL/G000/G00001/GENERAL_SAV.BUP", "GENERAL_SAV", 40, 1, 30),
        done("aether/SAVES/_GENERAL/G000/G00001/DONE.TXT"),
        # never touched: the quarantine, IMPORT.TXT, Kai's and Rings' folders, a folder name too long for the table
        cfg("aether/SAVES/_QUARANTINE/IMPORT/T-00001[U]/B_CFG.BUP"),
        {"path": "aether/SAVES/IMPORT.TXT", "raw": b"kai=0\n".hex()},
        cfg("SAVES/BRAM/T-00001[U]/B_CFG.BUP"), cfg("satiator-saves/T-00001/B_CFG.BUP"),
        cfg("aether/SAVES/T-00004[U]-LONG-NAME-XX/B_CFG.BUP"),
        cfg("aether/SAVES/T-00004[U]-LONG-NAME-XX/G000/G00001/B_CFG.BUP"),
        # the BRAM backups: flat (before Task 24b) and in shards
        bkp("aether/SAVES/_BACKUP/00001-20261101-120000.BKP", 1, [a1("x"), cfg("x")]),      # covered: removed
        bkp("aether/SAVES/_BACKUP/00002.BKP", 2, [cfg("x"), bup("x", "Z_SAV", 30, 1, 40)]), # Z_SAV nowhere else
        bkp("aether/SAVES/_BACKUP/000/00003-20261102-120000.BKP", 3,
            [a2("x"), cfg("x"), bup("x", "GENERAL_SAV", 40, 1, 30)]),
        bkp("aether/SAVES/_BACKUP/000/00004.BKP", 4, [a2("x")], cut=10),                    # torn: kept
        bkp("aether/SAVES/_BACKUP/000/00005.BKP", 5, [a3("x")]),                            # A_SAV v3 kept in G00004
        bkp("aether/SAVES/_BACKUP/000/00006.BKP", 6, [bup("x", "S1_SAV", 40, 3, 11), bup("x", "C_SAV", 50, 1, 20)]),
        {"path": "aether/SAVES/_BACKUP/000/00007.TMP", "raw": "00"},
        bkp("aether/SAVES/_BACKUP/001/00100-20261120-120000.BKP", 100, [a2("x"), cfg("x")]),
        bkp("aether/SAVES/_BACKUP/003/00300.BKP", 300, [cfg("x")]),                         # the newest: never
        {"path": "aether/SAVES/_BACKUP/002/README", "raw": ""},
    ]
    return {"ops": "prpr", "dirs": ["aether/SAVES/_BACKUP/004", "aether/SAVES/T-00002[U]/G002"], "files": files}


def tidy_plain() -> dict:
    """A card from before Task 24b: no generations, so every save file is current and kept (two copies of a record
    under two names included); the flat backups go when the folders cover them, never the newest."""
    G = "aether/SAVES/MK-81214[UT]"
    files = [bup(f"{G}/SCORCHER_01.BUP", "SCORCHER_01", 300, 60, 3), bup(f"{G}/SCORCHER_01~1.BUP", "SCORCHER_01", 300, 60, 3),
             bup(f"{G}/03/SLOTSAVE_03.BUP", "SLOTSAVE_03", 50, 20, 14)]
    files += [bkp(f"aether/SAVES/_BACKUP/{n:05d}.BKP", n, [bup("x", "SCORCHER_01", 300, 60, 3)]) for n in (1, 2, 3)]
    files += [bkp("aether/SAVES/_BACKUP/00004-20261122-120000.BKP", 4, [bup("x", "SLOTSAVE_03", 50, 20, 14)])]
    return {"ops": "prr", "dirs": [], "files": files}


def tidy_busy(name: str) -> dict:
    files = tidy_plain()["files"]
    if name == "busy":
        files.append({"path": "aether/SAVES/JOURNAL.TXT", "raw": b"op=0".hex()})
    elif name == "busy_new":
        files.append({"path": "aether/SAVES/JOURNAL.TXT.NEW", "raw": b"op=0".hex()})
    else:                                                       # an empty journal: no swap pending
        files.append({"path": "aether/SAVES/JOURNAL.TXT", "raw": ""})
    return {"ops": "pr", "dirs": [], "files": files}


def tidy_random() -> dict:
    """A card shaped like the launcher's (test_pgsbak.c make_card): 12 games played in turn over 120 launches, slot 0
    and every 7th launch slot 1; each archive a new generation (some left without DONE.TXT, as a cut leaves one; now and
    then a jump into the next shard); a settings record that never changes and a save that changes on some launches;
    _GENERAL; a backup every launch, in shards (every 9th flat, as before Task 24b). Seeded: the same card every time."""
    st = [4242]

    def rnd(n: int) -> int:
        st[0] = (st[0] * 1103515245 + 12345) & 0xFFFFFFFF
        return (st[0] >> 16) % n

    files: list[dict] = []
    gen: dict[str, int] = {}
    ver = {g: 1 for g in range(12)}
    for launch in range(1, 121):
        game = rnd(12)
        slot = 1 if launch % 7 == 0 else 0
        d = f"aether/SAVES/T-{game:05d}[U]" + (f"/0{slot}" if slot else "")
        n = gen.get(d, 0) + 1 + (95 if rnd(40) == 0 else 0)
        gen[d] = n
        if rnd(2):
            ver[game] += 1
        gd = f"{d}/G{n // 100:03d}/G{n:05d}"
        recs = [bup(f"{gd}/C{game:05d}_CFG.BUP", f"C{game:05d}_CFG", 120, 50, game),
                bup(f"{gd}/G{game:05d}_SAV.BUP", f"G{game:05d}_SAV", 64 + (ver[game] * 37) % 900, 100000 + ver[game],
                    1000 + game * 50 + ver[game])]
        files += recs
        if rnd(15):
            files.append(done(f"{gd}/DONE.TXT"))
        if launch % 5 == 0:
            files.append(bup(f"aether/SAVES/_GENERAL/G000/G{launch // 5:05d}/GENERAL_SAV.BUP", "GENERAL_SAV", 40, 1, 7))
            files.append(done(f"aether/SAVES/_GENERAL/G000/G{launch // 5:05d}/DONE.TXT"))
        bpath = (f"aether/SAVES/_BACKUP/{launch:05d}.BKP" if launch % 9 == 0 else
                 f"aether/SAVES/_BACKUP/{launch // 100:03d}/{launch:05d}-20261122-120000.BKP")
        files.append(bkp(bpath, launch, recs + [bup("x", "GENERAL_SAV", 40, 1, 7)]))
    return {"ops": "prpr", "dirs": [], "files": files}


def tidy_nosaves() -> dict:
    """An Aether card with no /aether/SAVES at all (the journal check's FR_NO_PATH is absent):
    nothing to do, no error."""
    return {"ops": "pr", "dirs": ["aether"], "files": [{"path": "aether/LAUNCHER.BIN", "raw": "00"}]}


def tidy_alt() -> dict:
    """a generation may hold ALT/, alternate imports that are never loaded. M6 Task 24c
    (c1b784b): ALT/ is now deduped across generations too, but only against other ALT/ members -- G00001's own A_SAV
    and its ALT/A_SAV both duplicate the current G00002's, so both go, emptying G00001 (its ALT/ removed, then the
    generation itself); the current G00002's ALT/ is kept. A backup's record is covered by a kept ALT/ copy too, so
    00001 (Z_SAV, whose only copy is G00002's ALT/) and 00002 (A_SAV, covered by the current set) both go; only the
    newest (00003) is never removed."""
    G = "aether/SAVES/T-00001[U]"
    return {"ops": "prpr", "dirs": [], "files": [
        bup(f"{G}/G000/G00001/A_SAV.BUP", "A_SAV", 100, 10, 1),
        bup(f"{G}/G000/G00001/ALT/A_SAV.BUP", "A_SAV", 100, 10, 1),
        done(f"{G}/G000/G00001/DONE.TXT"),
        bup(f"{G}/G000/G00002/A_SAV.BUP", "A_SAV", 100, 10, 1),
        bup(f"{G}/G000/G00002/ALT/A_SAV.BUP", "A_SAV", 100, 10, 1),
        bup(f"{G}/G000/G00002/ALT/Z_SAV.BUP", "Z_SAV", 30, 5, 9),
        done(f"{G}/G000/G00002/DONE.TXT"),
        bkp("aether/SAVES/_BACKUP/000/00001.BKP", 1, [bup("x", "Z_SAV", 30, 5, 9)]),
        bkp("aether/SAVES/_BACKUP/000/00002.BKP", 2, [bup("x", "A_SAV", 100, 10, 1)]),
        bkp("aether/SAVES/_BACKUP/000/00003.BKP", 3, [bup("x", "A_SAV", 100, 10, 1)]),
    ]}


def tidy_cases() -> dict:
    return {"gens": tidy_gens(), "plain": tidy_plain(), "busy": tidy_busy("busy"),
            "busy_new": tidy_busy("busy_new"), "journal_empty": tidy_busy("empty"),
            "random": tidy_random(), "nosaves": tidy_nosaves(), "alt": tidy_alt()}


def match_real() -> dict:
    from omsat import gamesdb, match
    recs, _genres, aliases = gamesdb.read_gamesdb((REPO / "build/db/GAMES.DB").read_bytes())
    keys = sorted(r.key for r in recs)
    m = match.Matcher(keys, aliases)
    rows = []
    for fx in ("real_headers", "xband_headers"):
        for e in json.loads((REPO / f"contract/fixtures/{fx}.json").read_text(encoding="utf-8"))["entries"]:
            raw = bytes.fromhex(e["raw_hex"])
            prod, area = raw[0x20:0x2A].decode("ascii", "replace"), raw[0x40:0x50].decode("ascii", "replace")
            r = m.match(prod, area.strip())
            rows.append({"fixture": fx, "file": e["file"], "product": prod, "area": area.strip(), "key": r.key,
                         "how": r.how, "candidates": r.candidates})
    return {"source": "omsat.match over build/db/GAMES.DB (mkcases.py --match-real)", "keys": keys,
            "aliases": dict(sorted(aliases.items())), "rows": rows}


FILL_DATES = {"GS-9001": 19950402, "T-8113H": 19960115, "610-6342": 19951201, "T-4507G": 19971218, "NOPE-1": 19990101}


def gamesdb_fill() -> dict:
    """omsat.gamesdb.fill_dates on contract/fixtures/tiny.games.db (GS-9001 already dated: kept; 610-6342 is an alias
    of GS-7103; NOPE-1 is no key): the result's length, SHA-256 and the number of records filled."""
    import hashlib
    from omsat import gamesdb
    out, n = gamesdb.fill_dates((REPO / "contract/fixtures/tiny.games.db").read_bytes(), FILL_DATES)
    return {"source": "omsat.gamesdb.fill_dates (mkcases.py --gamesdb-fill)", "dates": FILL_DATES, "filled": n,
            "len": len(out), "sha256": hashlib.sha256(out).hexdigest()}


ART_PATHS = ["Ä", "日本/RPG", "a b/c-d_e", "x" * 255]


def artpak_expect() -> dict:
    """omsat.artpak on ART.PAK's container rules (SPEC §5.2): folder_art_key over every folder path of
    contract/fixtures/scan_tree.json plus a few non-ASCII and edge paths; and tiny.art.pak's entries as read_artpak
    decodes them (fields, and the SHA-256 of each blob get_blob returns)."""
    import hashlib
    from omsat import artpak
    tree = json.loads((REPO / "contract/fixtures/scan_tree.json").read_text())["tree"]
    paths = sorted(p for p in tree if p) + ART_PATHS
    data = (REPO / "contract/fixtures/tiny.art.pak").read_bytes()
    ents = []
    for e in artpak.read_artpak(data):
        row = {k: v for k, v in e.items() if not k.startswith("_")}
        for kind, flag in (("thumb", 1), ("hero", 2), ("jp", 4)):
            row[kind + "_sha256"] = hashlib.sha256(artpak.get_blob(data, e, kind)).hexdigest() if e["flags"] & flag else ""
        ents.append(row)
    return {"source": "omsat.artpak (mkcases.py --artpak)", "folder_keys": {p: artpak.folder_art_key(p) for p in paths},
            "tiny_entries": ents}


def main(argv: list[str]) -> int:
    if argv == ["--bup-corpus"]:
        p = REPO / "manager/golden/expect/bup_corpus.json"
        p.write_text(json.dumps(bup_corpus(), indent=1, sort_keys=True) + "\n", newline="\n")
        print(p)
        return 0
    if argv == ["--gamesdb-fill"]:
        p = REPO / "manager/golden/expect/gamesdb_fill.json"
        p.write_text(json.dumps(gamesdb_fill(), indent=1, sort_keys=True) + "\n", newline="\n")
        print(p)
        return 0
    if argv == ["--artpak"]:
        p = REPO / "manager/golden/expect/artpak.json"
        p.write_text(json.dumps(artpak_expect(), indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
        print(p)
        return 0
    if argv == ["--match-real"]:
        p = REPO / "manager/golden/expect/match_real.json"
        p.write_text(json.dumps(match_real(), indent=1, sort_keys=True) + "\n", newline="\n")
        print(p)
        return 0
    if argv == ["--cases"]:
        d = REPO / "manager/golden/cases"
        d.mkdir(exist_ok=True)
        for name, fn in (("corpus", case_corpus), ("merge", case_merge), ("existing", case_existing),
                         ("rerun", case_rerun), ("cart", case_cart), ("sticky", case_sticky), ("gens", case_gens),
                         ("highslot", case_highslot)):
            (d / f"{name}.json").write_text(json.dumps(fn(), indent=1, sort_keys=True) + "\n", newline="\n")
            print(d / f"{name}.json")
        return 0
    if argv == ["--tidy-cases"]:
        d = REPO / "manager/golden/tidy_cases"
        d.mkdir(exist_ok=True)
        for name, spec in tidy_cases().items():
            text = (json.dumps(spec, sort_keys=True, separators=(",", ":")) if name == "random"   # generated: compact
                    else json.dumps(spec, indent=1, sort_keys=True))
            (d / f"{name}.json").write_text(text + "\n", newline="\n")
            print(d / f"{name}.json")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
