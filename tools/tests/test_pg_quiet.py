"""M6 Task 24 fold-in: the static panel's quiet counter (menu/views/folders.c s_pg_quiet) takes
the same path in the fake build as in --app. An `#ifdef OMSAT_FAKECARD` line between `if (p || held) ...;` and its
`else` made that `else` bind to the fake-only `if`, so a held button reset nothing in the fake build. The counter's
if/else must be one unbroken statement, with any fake-only line outside it."""
import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "menu" / "views" / "folders.c"


def test_quiet_counter_if_else_is_not_split_by_the_preprocessor():
    s = SRC.read_text(encoding="utf-8")
    m = re.search(r"if \(p \|\| held\) s_pg_quiet = 0;(.*?)else if \(s_pg_quiet < 255u\) s_pg_quiet\+\+;", s, re.S)
    assert m, "the quiet counter's if/else was not found"
    between = m.group(1)
    assert "#if" not in between and "if (" not in between, f"the else binds elsewhere:{between!r}"
