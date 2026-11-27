from omsat.coverproject import build

ROWS = [
    ("NTSC-U -COMBINED", "T-8113H", "Alien Trilogy", "First Person Shooter"),
    ("PAL -COMBINED", "T-8113H-50", "Alien Trilogy", "First Person Shooter"),
    ("PAL -COMBINED", "T-5025H-50 (EAE)", "FIFA 97", "Sport - Soccer"),
    ("PAL -COMBINED", "T-5025H-50 (EAI)", "FIFA 97", "Sport - Soccer"),
    ("NTSC-J -COMBINED", "GS-7103\n (610-6342)", "Virtua Fighter 2\nバーチャファイター2", "Fighting 3D"),
    ("BRAZIL -COMBINED", "191x56", "Alien Trilogy", "First Person Shooter"),
    ("NTSC-U -COMBINED", "GS-7103", "Virtua Fighter 2", "Fighting 3D"),
]


def test_build_merges_and_reports():
    recs, genres, aliases, jp, rep = build(ROWS)
    by = {r.key: r for r in recs}
    assert set(by) == {"T-8113H", "T-8113H-50", "T-5025H-50", "GS-7103", "191X56"}
    assert by["GS-7103"].region_flags == 0b0101          # J (bit0) | U (bit2), merged across sheets
    assert by["191X56"].region_flags == 0b10000          # Brazil bit4
    assert aliases == {"610-6342": "GS-7103"}
    assert jp == {"GS-7103": "バーチャファイター2"}
    assert genres[by["GS-7103"].genre] == "Fighting 3D"
    assert rep["rows_read"] == 7 and rep["unique_keys"] == 5
    assert rep["duplicate_rows_merged"] == 2 and rep["aliases"] == 1
