from omsat import iphdr
from omsat.dump_headers import dump_dir, summarise


def test_dump_and_summary(tmp_path):
    (tmp_path / "a").mkdir()
    (tmp_path / "a" / "x.iso").write_bytes(iphdr.make_system_id(product="T-8113H-50", area="E", device="CD-1/1", title="A") + bytes(1792))
    (tmp_path / "y.iso").write_bytes(iphdr.make_system_id(product="T-8113H", area="U", device="CD-1/1", title="B") + bytes(1792))
    (tmp_path / "junk.iso").write_bytes(bytes(4096))
    entries, skipped = dump_dir(tmp_path)
    assert sorted(e["file"] for e in entries) == ["x.iso", "y.iso"] and skipped == 1
    assert all(len(e["raw_hex"]) == 512 for e in entries)
    s = summarise(entries)
    assert s == {"discs": 2, "pal_suffixed": 1, "area_E_only": 1, "area_E_only_with_suffix": 1, "product_len_10": 1}


import json
from pathlib import Path

import pytest

from omsat.gamesdb import read_gamesdb
from omsat.match import Matcher

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(not (ROOT / "contract/fixtures/real_headers.json").exists() or not (ROOT / "build/db/GAMES.DB").exists(),
                    reason="F-real dumps or build/db/GAMES.DB not present")
def test_real_headers_match_rate():
    real = json.loads((ROOT / "contract/fixtures/real_headers.json").read_text(encoding="utf-8"))["entries"]
    recs, _, aliases = read_gamesdb((ROOT / "build/db/GAMES.DB").read_bytes())
    m = Matcher([r.key for r in recs], aliases)
    results = [(e["file"], e["product"], m.match(e["product"], e["area"])) for e in real]
    unmatched = [(f, p, r.candidates) for f, p, r in results if r.key is None]
    print(f"matched {len(real) - len(unmatched)} of {len(real)}; unmatched: {unmatched}")
    assert len(real) > 0
