"""M6 Task 17 carried ruling: Herc's Adventures retail (T-23001H) is Kai "JHL-only", so Loader = Auto must pick JHL
for it. The menu half (om_loader_pick + om_loader_db_hint over GAMES.DB bytes 0x2E/0x34) is host-tested in
menu/host-tests/test_loader.c; this is the data half: the shipped loader_compat.json, through cardbuild's compat step
(omsat.compat), puts hint OM_LH_JHL (1) and a region mask that includes U (the retail disc's area "U") on the
T-23001H record, and a J or E disc of the same key is not given the hint."""
import json
from pathlib import Path

from omsat import compat, gamesdb

ROOT = Path(__file__).resolve().parents[2]
LOADER = ROOT / "tools" / "data" / "compat" / "loader_compat.json"
U, J, E = 1 << 2, 1 << 0, 1 << 3                   # GAMES.DB region bits (gamesdb.REGION_BITS)


def test_herc_retail_is_jhl_on_its_us_disc():
    games = json.loads(LOADER.read_text(encoding="utf-8"))["games"]
    assert [g["loader"] for g in games if g["product"] == "T-23001H"] == ["jhl"]
    rows = compat.load(LOADER, None)
    db = gamesdb.write_gamesdb([gamesdb.GameRecord(key="T-23001H", title_en="Herc's Adventures", region_flags=U)],
                               [], {}, 20261121)
    data, n = compat.fill_compat(db, rows)
    assert n == 1
    rec = {r.key: r for r in gamesdb.read_gamesdb(data)[0]}["T-23001H"]
    assert rec.loader_hint == 1                      # OM_LH_JHL (menu/src/gamesdb.h)
    assert rec.compat_regions & U                    # om_gdb_compat: the mask intersects a U disc's bits
    assert not rec.compat_regions & (J | E)          # ... and not a J or E disc's
