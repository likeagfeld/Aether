from pathlib import Path

import pytest

from omsat.hwf import check

DOC = """# facts
| ID | Fact | Value used | Citation | Status |
|---|---|---|---|---|
| HWF-01 | RGB555 | 0x8000 | per X | CONFIRMED |
| HWF-02 | thing | 1 | per Y | PENDING |
| HWF-03 | bios | 0x6000288 | unverified, from jhloader.c | UNVERIFIED-ACCEPTED |
"""


def _tree(tmp_path: Path, src: str) -> Path:
    (tmp_path / "docs" / "decisions").mkdir(parents=True)
    (tmp_path / "docs" / "decisions" / "m2-hw-facts.md").write_text(DOC, encoding="utf-8")
    (tmp_path / "menu" / "src").mkdir(parents=True)
    (tmp_path / "menu" / "src" / "a.c").write_text(src, encoding="utf-8")
    return tmp_path


def test_confirmed_and_accepted_tags_pass(tmp_path):
    root = _tree(tmp_path, "int a = 0x8000; /* HWF-01 */\nint b; /* HWF-03 */\n")
    assert check(root) == []


def test_pending_row_fails(tmp_path):
    root = _tree(tmp_path, "int x; /* HWF-02 */\n")
    errs = check(root)
    assert len(errs) == 1 and "HWF-02" in errs[0] and "PENDING" in errs[0]


def test_unknown_tag_fails(tmp_path):
    root = _tree(tmp_path, "int x; /* HWF-77 */\n")
    errs = check(root)
    assert len(errs) == 1 and "HWF-77" in errs[0]


def test_zero_tags_is_not_a_pass(tmp_path):
    root = _tree(tmp_path, "int x;\n")
    assert check(root) == ["no HWF tags found under menu/ (0 of 0) -- gate cannot pass vacuously"]


# --- fix round 1 (reproduced by reviewer) -------------------

DOC_DUP = """# facts
| ID | Fact | Value used | Citation | Status |
|---|---|---|---|---|
| HWF-01 | RGB555 | 0x8000 | per X | PENDING |
| HWF-01 | RGB555 shadow | 0x8000 | per X (shadow row) | CONFIRMED |
"""


def _tree_dup(tmp_path: Path, src: str) -> Path:
    (tmp_path / "docs" / "decisions").mkdir(parents=True)
    (tmp_path / "docs" / "decisions" / "m2-hw-facts.md").write_text(DOC_DUP, encoding="utf-8")
    (tmp_path / "menu" / "src").mkdir(parents=True)
    (tmp_path / "menu" / "src" / "a.c").write_text(src, encoding="utf-8")
    return tmp_path


def test_duplicate_row_id_fails(tmp_path):
    # A later CONFIRMED row for the same ID must not silently shadow an
    # earlier PENDING row via last-wins dict insertion.
    root = _tree_dup(tmp_path, "int a = 0x8000; /* HWF-01 */\n")
    errs = check(root)
    assert len(errs) == 1 and "HWF-01" in errs[0] and "duplicate" in errs[0].lower()


def test_near_miss_three_digit_tag_fails(tmp_path):
    # HWF-123 must not be silently read as HWF-12.
    root = _tree(tmp_path, "int x; /* HWF-123 */\n")
    errs = check(root)
    assert len(errs) == 1 and "HWF-123" in errs[0]


def test_near_miss_one_digit_tag_fails(tmp_path):
    # HWF-1 must not be silently invisible.
    root = _tree(tmp_path, "int x; /* HWF-1 */\n")
    errs = check(root)
    assert len(errs) == 1 and "HWF-1" in errs[0]


def test_near_miss_lowercase_tag_fails(tmp_path):
    # hwf-01 must not be silently invisible.
    root = _tree(tmp_path, "int x; /* hwf-01 */\n")
    errs = check(root)
    assert len(errs) == 1 and "hwf-01" in errs[0]


# --- Task 1 (M3) -----------------------------------------------------------

# range extended to 39 in fix round 1 (Controller Ruling M3-R5): HWF-39 (SCSP DSP RBP/RBL/MPRO)
# extended to 40 in Task 11 fix round 1: HWF-40 (persistent status + peripherals INTBACK)
M3_ROWS = [f"HWF-{n}" for n in range(27, 41)]


def test_m3_rows_present_and_settled():
    """every hardware fact M3 code depends on has a settled row before the code exists."""
    from omsat import hwf
    doc = hwf.ROOT / "docs" / "decisions" / "m2-hw-facts.md"
    if not doc.exists():
        pytest.skip("docs/decisions/ is not tracked; this check needs a local checkout that still has it")
    st = hwf.rows(doc)
    missing = [r for r in M3_ROWS if r not in st]
    assert not missing, f"M3 HW-fact rows missing: {missing}"
    unsettled = {r: st[r] for r in M3_ROWS if st[r] not in hwf.SETTLED}
    assert not unsettled, f"M3 HW-fact rows not settled: {unsettled}"
