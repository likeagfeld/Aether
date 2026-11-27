"""the compat seed files parse, use only known values, and carry their source."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
D = ROOT / "tools" / "data" / "compat"


def _load(name):
    return json.loads((D / name).read_text(encoding="utf-8"))


def test_loader_compat_shape():
    j = _load("loader_compat.json")
    assert j["source"].startswith("https://ppcenter.webou.net/pskai/compat")
    assert j["games"], "seeded from Kai's list: at least the JHL-only titles"
    for g in j["games"]:
        assert g["loader"] in ("jhl", "cwx", "bad") and g["region"] in tuple("JTUEB*")
        assert 1 <= len(g["product"]) <= 10 and g["note"]
    assert {g["product"] for g in j["games"] if g["loader"] == "jhl"}, "no JHL-only title found"


def test_netlink_and_tools_shape():
    n = _load("netlink_saves.json")
    assert n["source"] and all(1 <= len(p["bup_name_prefix"]) <= 11 for p in n["patterns"])
    t = _load("save_tools.json")
    assert len(t["tools"]) >= 4
    for x in t["tools"]:
        # title_substring is IP-title text (upper case) or "" when the title is generic; codes go in product_codes
        assert x["title_substring"] == "" or x["title_substring"].isupper()
        assert all(isinstance(c, str) and 1 <= len(c) <= 10 and " " not in c for c in x["product_codes"])
        assert x["title_substring"] or x["product_codes"], x["name"]
        assert x["title_substring"] not in {c for y in t["tools"] for c in y["product_codes"]}, "a code in the title field"
