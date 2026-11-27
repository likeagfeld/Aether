"""save tools pause per-game saves. tools/data/compat/save_tools.json feeds GAMES.DB's compat bit 7
(OM_CF_SAVETOOL, SPEC §5.3) through compat.load, and the menu's launch-time IP match (menu/src/loader.c
om_savetool_ip) carries the same product numbers and title substrings -- this test keeps the two in step."""
import json
import re
from pathlib import Path

from omsat import compat, gamesdb
from omsat.check import check_bytes

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "data" / "compat" / "save_tools.json"


def _db(keys):
    recs = [gamesdb.GameRecord(key=k, title_en=k, region_flags=4) for k in sorted(keys)]
    return gamesdb.write_gamesdb(recs, [], {}, 20261120)


def test_save_tool_products_set_bit7(tmp_path):
    lj = tmp_path / "l.json"
    lj.write_text(json.dumps({"source": "s", "games": [{"product": "SGC", "region": "U", "loader": "jhl"}]}))
    rows = compat.load(lj, None, save_tools_json=TOOLS)
    assert compat.CF_SAVETOOL == 0x80
    for p in ("SGC", "SATAKORE", "T-060680", "610-6276"):
        assert rows[p].flags & compat.CF_SAVETOOL, p
    assert rows["SGC"].hint == 1                                    # the loader row's own data is kept
    data, n = compat.fill_compat(_db(["SGC", "GS-9001"]), rows)
    assert n == 1 and check_bytes(data) == []
    recs = {r.key: r for r in gamesdb.read_gamesdb(data)[0]}
    assert recs["SGC"].compat_flags & compat.CF_SAVETOOL
    assert not recs["GS-9001"].compat_flags & compat.CF_SAVETOOL


def test_no_save_tools_file_is_a_noop(tmp_path):
    lj = tmp_path / "l.json"
    lj.write_text(json.dumps({"source": "s", "games": []}))
    assert compat.load(lj, None, save_tools_json=tmp_path / "absent.json") == {}


def test_menu_tables_match_the_json():
    tools = json.loads(TOOLS.read_text(encoding="utf-8"))["tools"]
    src = (ROOT / "menu" / "src" / "loader.c").read_text(encoding="utf-8")
    prod = re.search(r"k_tool_prod\[\] = \{([^}]*)\}", src).group(1)
    title = re.search(r"k_tool_title\[\] = \{([^}]*)\}", src).group(1)
    c_prod = set(re.findall(r'"([^"]*)"', prod))
    c_title = set(re.findall(r'"([^"]*)"', title))
    assert c_prod == {p for t in tools for p in t["product_codes"]}
    assert c_title == {t["title_substring"] for t in tools if t["title_substring"]}
    assert all(t == t.upper() for t in c_title)                     # the menu upper-cases the IP title only
    assert re.search(r"#define OM_CF_SAVETOOL 0x80u", (ROOT / "menu" / "src" / "gamesdb.h").read_text())
