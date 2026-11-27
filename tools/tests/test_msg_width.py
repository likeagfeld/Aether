"""Message widths. om_fatal lines go to the 40-column NBG0 text layer (HWF-24), where om_text_put truncates at 40, so a
longer literal silently loses its tail: the 40-column rule still binds them. Since Task 13a, om_folders_status lines go
to the status toast, set in the proportional small face in the footer strip (menu/src/layout.h TOAST): a line wider than
the strip marquees (controller addendum 3) and is never abbreviated, so the gate there is the toast's 63-byte buffer
(menu/src/anim.h OM_TOAST_MSG) plus a measured report of which literals will scroll. Lines with an rc go through
om_msg_rc, whose suffix the toast keeps whole at the right of the strip."""
import re
from pathlib import Path

from omsat import uifont

MENU = Path(__file__).resolve().parents[2] / "menu"
CALL = re.compile(r'\b(om_fatal|om_folders_status)\(\s*"((?:[^"\\]|\\.)*)"')
TOAST_W = 310 - 10 + 1          # layout.h TOAST box
TOAST_MSG = 64                  # anim.h OM_TOAST_MSG (63 bytes + NUL)


def _literals():
    for p in sorted(MENU.rglob("*.c")):
        if "host-tests" in p.parts or "spikes" in p.parts or "out" in p.parts:
            continue
        for m in CALL.finditer(p.read_text(encoding="utf-8", errors="replace")):
            yield p.relative_to(MENU).as_posix(), m.group(1), m.group(2)


def test_there_are_literals_to_check():
    assert sum(1 for _, fn, _ in _literals() if fn == "om_fatal") >= 2, "gate would pass vacuously"
    assert sum(1 for _, fn, _ in _literals() if fn == "om_folders_status") >= 3, "gate would pass vacuously"


def test_fatal_literals_fit_40_columns():
    long = [(p, s, len(s)) for p, fn, s in _literals() if fn == "om_fatal" and len(s) > 40]
    assert not long, f"over 40 columns: {long}"


def test_status_literals_fit_the_toast_buffer_and_are_measured():
    m = uifont.load_metrics()
    rows = [(p, s, uifont.measure(m["small"], s)) for p, fn, s in _literals() if fn == "om_folders_status"]
    assert all(len(s.encode("utf-8")) < TOAST_MSG for _, s, _ in rows)
    assert all("…" not in s for _, s, _ in rows)            # never an ellipsis (addendum 3)
    # every literal status line in the menu today fits the strip without scrolling
    wide = [(p, s, w) for p, s, w in rows if w > TOAST_W]
    assert not wide, f"would marquee: {wide}"


# B2 (hardware 2026-11-23: text cut off on the import saves screens). The launcher's import screen (lseq.c
# om_run_import, its progress line pgs_say) is the 40 x 28 VDP2 text layer with no safe area of its own, so every line
# keeps a one-cell margin each side (x >= 1, x + length <= 39) and stays above the bottom two rows (row <= 25). The
# menu's picker (folders.c import_pick, import_request) is drawn through the safe-area inset, at most 3 cells
# (text.c), which leaves 34 columns and rows 0-21: every line fits those.
LSEQ = MENU / "launcher" / "lseq.c"
FOLD = MENU / "views" / "folders.c"
STR = r'"((?:[^"\\]|\\.)*)"'


def _body(path, head):
    s = path.read_text(encoding="utf-8")
    i = s.index(head)
    j = s.index("\n}\n", i)
    return s[i:j]


def _ival(expr, consts):
    expr = expr.strip()
    return int(consts.get(expr, expr), 0)


def test_launcher_import_screen_fits_with_a_margin():
    body = _body(LSEQ, "static int om_run_import(") + _body(LSEQ, "static void pgs_say(")
    consts = dict(re.findall(r"#define (\w+)\s+(\d+)", LSEQ.read_text(encoding="utf-8")))
    puts = re.findall(r"om_txt_puts\(\s*([\w ]+),\s*([\w ]+),\s*" + STR, body)
    counts = re.findall(r"put_(?:count|row)\(\s*(\d+),\s*" + STR, body)   # put_row: label, count at columns 31-35
    assert len(puts) >= 4 and len(counts) >= 6, "gate would pass vacuously"
    bad = [(x, y, s) for x, y, s in puts if s.strip() and (_ival(x, consts) < 1 or _ival(x, consts) + len(s) > 39
                                                           or _ival(y, consts) > 25)]
    bad += [(1, y, s) for y, s in counts if 1 + len(s) + 5 > 39 or int(y) > 25]
    assert not bad, f"import screen lines past the margin or the bottom rows: {bad}"
    assert "om_txt_puts(0," not in _body(LSEQ, "static int om_run_import("), "a line at column 0"


def test_launcher_import_notices_fit_with_a_margin():
    s = (MENU / "launcher" / "pgsbak.c").read_text(encoding="utf-8")
    long = [t for t in re.findall(r'strcpy\(notice,\s*' + STR, s) if len(t) > 38]
    assert not long, f"import notices over 38 columns: {long}"


def test_menu_import_picker_fits_the_largest_inset():
    body = _body(FOLD, "static OM_COLD int import_pick(") + _body(FOLD, "static OM_COLD void import_request(")
    puts = re.findall(r"om_text_put\(\s*(\d+),\s*([\w +()]+?),\s*" + STR, body)
    assert len(puts) >= 5, "gate would pass vacuously"
    bad = [(x, y, t) for x, y, t in puts if int(x) + len(t) > 34 or (y.strip().isdigit() and int(y) > 21)]
    assert not bad, f"picker lines past 34 columns or row 21: {bad}"
    vis = int(re.search(r"#define PICK_VIS\s+(\d+)", FOLD.read_text(encoding="utf-8")).group(1))
    assert 4 + vis - 1 < 21, f"PICK_VIS {vis}: the list runs into the footer row or past row 21"
    assert "k < 24 && title[k]" not in body, "titles cut at 24 characters with room to spare"


def test_in_game_reset_is_the_shown_name():   # user decision 2026-11-23: "Soft reset" is shown as "In Game Reset"
    ov = (MENU / "views" / "ovdraw.c").read_text(encoding="utf-8")
    ls = LSEQ.read_text(encoding="utf-8")
    assert '[OM_OVR_IGR] = "In Game Reset"' in ov
    assert '"IN GAME RESET: ON"' in ls and '"IN GAME RESET: OFF"' in ls
    assert '"Soft reset"' not in ov and '"SOFT RESET' not in ls
