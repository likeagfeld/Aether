# tools/tests/test_discsets.py -- multi-disc sets (SPEC §5.4 flags bits 2, 4-15; tools/omsat/discsets.py)
import json
from pathlib import Path

from omsat import discsets
from omsat.cardscan import TreeSource, scan
from omsat.libidx import LibEntry, Folder, read_libidx, write_libidx

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"


def test_fixture():
    doc = json.loads((FIX / "discsets.json").read_text(encoding="utf-8"))
    cases = doc["cases"]
    got = discsets.assign([(c["path"], c["product"], c["disc"], c["total"]) for c in cases])
    for c, f in zip(cases, got):
        assert (f >> 4, bool(f & 4)) == (c["set"], c["alt"]), c["path"]


def test_fixture_outcomes_on_the_users_card():
    cases = json.loads((FIX / "discsets.json").read_text(encoding="utf-8"))["cases"]
    by = {}
    for c in cases:
        by.setdefault(c["set"], []).append(c)

    def set_of(fragment):
        hit = {c["set"] for c in cases if fragment in c["path"]}
        assert len(hit) == 1, (fragment, hit)
        return hit.pop()
    cc = set_of("Command & Conquer")                      # different suffixes per disc: one set
    assert cc and sorted(c["disc"] for c in by[cc]) == [1, 2]
    pn = set_of("POLICENAUTS")                            # the AI Dub disc 1 is an alternate
    assert sorted((c["disc"], c["alt"]) for c in by[pn]) == [(1, False), (1, True), (2, False), (3, False)]
    assert next(c for c in by[pn] if c["alt"])["path"].startswith("Policenauts (Disc 1) (English v1.01) (AI Dub")
    pds = {c["set"] for c in cases if "Panzer Dragoon Saga" in c["path"]}
    assert len(pds) == 3                                  # the root set (+ patched alternate) and two debug sets
    ez = set_of("Enemy Zero")                             # IP numbering wins over the names' Disc 0..3
    assert sorted(c["disc"] for c in by[ez]) == [1, 2, 3, 4]
    assert len({c["set"] for c in cases if "PhantasM" in c["path"]}) == 2
    assert set_of("D (USA) (Disc I") and set_of("Playboy - Karaoke")
    tower = [c for c in cases if "Tower of Doom (v1.0" in c["path"]]
    assert tower and tower[0]["set"] == 0                 # one disc of two, no partner of its own stem
    assert set_of("Homebrew/Alpha") == 0 and set_of("Ten (Disc") == 0 and set_of("Nocode") == 0


def test_mask_and_stem():
    assert discsets.mask("X (Disc 1)/X (Disc 1).cue") == "X (Disc #)/X (Disc #).cue"
    assert discsets.mask("PhantasM (JP) (Disc 2)/PHANTASM_2.cue") == "PhantasM (JP) (Disc #)/PHANTASM_#.cue"
    assert discsets.mask("S (Disk 3)/S_DISK3.CUE") == "S (Disk #)/S_DISK#.CUE"
    assert discsets.mask("D (Disc II)/D (Disc II).cue") == "D (Disc #)/D (Disc #).cue"
    assert discsets.mask("O - CD1.cue") == "O - CD#.cue"
    assert discsets.mask("Mag Vol. 2/m.iso") == "Mag Vol. #/m.iso"
    assert discsets.mask("Discworld/Disco.cue") == "Discworld/Disco.cue"
    assert discsets.mask("ABCD1.cue") == "ABCD1.cue"        # 'CD' inside a word is no token
    assert discsets.stem("C&C (USA) (Disc 1) (GDI Disc)/x.cue") == "C&C (USA) (Disc #"
    assert discsets.stem("Root/Plain.cue") == "Root/Plain.cue"
    assert discsets.norm_product("t-1234g v1.0") == "T-1234G"
    assert discsets.norm_product("MK.81") == "MK_81"


def test_libidx_round_trip_keeps_the_bits():
    ents = [LibEntry("A (Disc 1)/a.cue", disc=1, disc_total=2, has_box=True, sets=1 << 4),
            LibEntry("A (Disc 2)/a.cue", disc=2, disc_total=2, sets=1 << 4 | 4)]
    data = write_libidx(ents, [Folder("", 0xFFFF)], 0, 20261124)
    got, _, _ = read_libidx(data)
    assert [e.sets for e in got] == [0x10, 0x14] and got[0].has_box
    assert data[64 + 0x1A:64 + 0x1C] == b"\x00\x11"       # bit0 box, set 1 in bits 4-15


def test_apply_on_a_scan():
    tree = {"": {"dirs": ["G (Disc 1)", "G (Disc 2)", "One"], "files": []},
            "G (Disc 1)": {"dirs": [], "files": ["g.iso"]}, "G (Disc 2)": {"dirs": [], "files": ["g.iso"]},
            "One": {"dirs": [], "files": ["o.iso"]}}
    sids = {"G (Disc 1)/g.iso": {"product": "T-1G", "area": "J", "device": "CD-1/2", "title": "G"},
            "G (Disc 2)/g.iso": {"product": "T-1G", "area": "J", "device": "CD-2/2", "title": "G"},
            "One/o.iso": {"product": "T-2G", "area": "J", "device": "CD-1/1", "title": "O"}}
    res = scan(TreeSource(tree, sids, {}), [], {}, 20261124)
    plain = res.data
    assert discsets.apply(res, 20261124) == 1
    assert len(res.data) == len(plain)                    # the file size does not change
    got = {e.path: e.sets for e in read_libidx(res.data)[0]}
    assert got == {"G (Disc 1)/g.iso": 0x10, "G (Disc 2)/g.iso": 0x10, "One/o.iso": 0}
