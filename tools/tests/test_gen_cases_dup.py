"""M6 Task 23 fold-in (Task 22 re-review Minor): gen_cases.py's KB_DUP table must carry every member of a duplicate
group, however large, and say how wide its rows are, so test_bupfile.c's loop never reads past a row."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "menu" / "host-tests"))
sys.path.insert(0, str(ROOT / "tools"))
import gen_cases  # noqa: E402
from omsat import bupfile  # noqa: E402


def _rows(n):
    out = []
    for i in range(n):
        h = bupfile.encode(bupfile.BupDir(name=b"SAMENAME_01", date=100 + i, datasize=4), b"abcd")[:64]
        out.append({"path": f"f/COPY{i}.BUP", "len": 68, "hdr": h.hex()})
    return out


def test_five_way_duplicate_keeps_every_member():
    o = []
    gen_cases.bup_cases(o, _rows(5))
    text = "".join(o)
    width = int(re.search(r"#define KB_DUP_W (\d+)u", text).group(1))
    row = re.search(r"KB_DUP\[\] = \{\n  \{ \{ ([^}]*) \}, (\d+)u, (\d+) \}", text)
    members = [int(x) for x in row.group(1).split(",")]
    assert width >= 5 and len(members) == width
    assert sorted(m for m in members if m >= 0) == [0, 1, 2, 3, 4]
    assert int(row.group(2)) == 5 and int(row.group(3)) == 4        # the latest date wins


def test_corpus_rows_unchanged_width_at_least_4():
    o = []
    gen_cases.bup_cases(o)
    assert int(re.search(r"#define KB_DUP_W (\d+)u", "".join(o)).group(1)) >= 4
