from omsat.keys import normalise, parse_code_cell, is_pal_suffixed


def test_normalise_basic():
    assert normalise("gs-9001") == "GS-9001"
    assert normalise("  T-8113H-50 ") == "T-8113H-50"


def test_normalise_drops_annotations_and_later_lines():
    assert normalise("T-5025H-50 (EAE)") == "T-5025H-50"
    assert normalise("GS-7103\n (610-6342)") == "GS-7103"
    assert normalise("T-16104H-50 (FXY)") == "T-16104H-50"


def test_normalise_truncates_to_16():
    assert normalise("A" * 20) == "A" * 16


def test_parse_code_cell_aliases_on_later_lines():
    assert parse_code_cell("GS-7103\n (610-6342)") == ("GS-7103", ["610-6342"])
    assert parse_code_cell("T-25001G\n (T-2500IG)") == ("T-25001G", ["T-2500IG"])
    assert parse_code_cell("81048\n (81605)") == ("81048", ["81605"])
    assert parse_code_cell("T-5008H-50\n (T-5008-50)") == ("T-5008H-50", ["T-5008-50"])


def test_parse_code_cell_first_line_annotation_is_not_alias():
    assert parse_code_cell("T-5025H-50 (EAE)") == ("T-5025H-50", [])


def test_parse_code_cell_brazil_and_plain():
    assert parse_code_cell("191x56") == ("191X56", [])
    assert parse_code_cell("T-15906H") == ("T-15906H", [])


def test_is_pal_suffixed():
    assert is_pal_suffixed("T-8113H-50")
    assert is_pal_suffixed("T-8113H-18")
    assert not is_pal_suffixed("T-8113H")
    assert not is_pal_suffixed("12512H-5")


def test_cleanup_version_spill_forms():
    """SPEC §6 v3: a version from IP.BIN 0x2A spilled into the 10-char product field (forms read from real discs)."""
    from omsat.keys import cleanup
    assert cleanup("T-29002G V") == "T-29002G"
    assert cleanup("GS-9055 V1") == "GS-9055"
    assert cleanup("MK-81029V1") == "MK-81029"
    assert cleanup("T-1234V   ") == "T-1234V"          # no blank before V and no digit after: not a spill
    assert cleanup("GS-9001   ") == "GS-9001"


def test_suffix_base_and_forms():
    from omsat.keys import skeleton, skeleton_forms, suffix_base
    assert suffix_base("MK-8109150") == "MK-81091"
    assert suffix_base("T-12003H50") == "T-12003H"
    assert suffix_base("T-8120H-50") == "T-8120H"
    assert suffix_base("81218") is None                   # 3 characters left: a real key, not a suffix
    assert suffix_base("GS-50") is None                  # under 4 characters left
    assert suffix_base("T-8113H") is None
    assert skeleton("MK_8100550") == "MK8100550"
    assert skeleton_forms("T-12512H") == {"T12512H", "12512H"}
    assert skeleton_forms("MK-81045") == {"MK81045", "81045"}
    assert skeleton_forms("TX-1") == {"TX1"}             # "T" only before a digit
