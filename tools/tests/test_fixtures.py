# tools/tests/test_fixtures.py
import json
import subprocess
import sys
from pathlib import Path

from omsat.check import check_bytes
from omsat.match import Matcher

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"


def test_fixtures_are_reproducible():
    r = subprocess.run([sys.executable, str(FIX / "make_fixtures.py"), "--check"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_fixtures_validate():
    for name in ("tiny.art.pak", "tiny.games.db", "tiny.library.idx", "tiny.bg.bin", "tiny.bootanim.bin",
                 "tiny.loadbg.bin", "tiny.sound.bin", "tiny.music.mus", "tiny.cheats.pak"):
        assert check_bytes((FIX / name).read_bytes()) == [], name


def test_match_cases():
    spec = json.loads((FIX / "match_cases.json").read_text())
    m = Matcher(spec["keys"], spec["aliases"])
    for c in spec["cases"]:
        r = m.match(c["product"], c["area"])
        assert (r.key, r.how) == (c["expect_key"], c["expect_how"]), c
