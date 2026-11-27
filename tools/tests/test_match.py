from omsat.match import Matcher

KEYS = ["GS-9001", "T-8113H", "T-8113H-50", "T-8113H-18", "T-11304H-50", "MK-81009", "MK-81009-50",
        "191X56", "GS-7103"]
ALIASES = {"610-6342": "GS-7103"}
M = Matcher(KEYS, ALIASES)


def test_exact():
    r = M.match("GS-9001   ", "JTUE")
    assert (r.key, r.how) == ("GS-9001", "exact")


def test_alias():
    r = M.match("610-6342  ", "U")
    assert (r.key, r.how) == ("GS-7103", "alias")


def test_prefix_resolves_truncated_pal_code():
    # a 10-char field cannot hold T-11304H-50 (11 chars); a truncated "T-11304H-5" must still resolve
    r = M.match("T-11304H-5", "E")
    assert (r.key, r.how) == ("T-11304H-50", "prefix")


def test_area_preference_pal_vs_ntsc():
    # v2: area preference applies to exact hits (F-real: PAL discs carry the unsuffixed code)
    assert M.match("MK-81009  ", "U").key == "MK-81009"
    assert M.match("MK-81009  ", "E").key == "MK-81009-50"


def test_pal_tiebreak_prefers_50():
    # v2/R8: "T-8113H-" matches T-8113H, T-8113H-18 and T-8113H-50 by skeleton (not prefix: the
    # trailing "-" is stripped by skeleton()); area E prefers the suffixed pair, which ties on
    # length, but R8 then prefers the lone "-50" survivor over "-18".
    r = M.match("T-8113H-", "E")
    assert (r.key, r.how) == ("T-8113H-50", "skeleton")


def test_r8_pal_tiebreak_in_exact_branch():
    # brief-style inline matcher: a bare key plus both suffixed siblings is a clean 3-way exact-branch tie
    r = Matcher(["T-8113H", "T-8113H-18", "T-8113H-50"], {}).match("T-8113H   ", "E")
    assert (r.key, r.how) == ("T-8113H-50", "exact")


def test_unmatched():
    r = M.match("T-99999G  ", "J")
    assert r.key is None and r.how is None and r.candidates == []


def test_brazil_code_case_insensitive():
    assert M.match("191x56", "U").key == "191X56"


V2_KEYS = ["T-8113H", "T-8113H-50", "MK81282-50", "81001", "81045", "MK81045-50", "T-7004H", "GS-7004",
           "T-29002G", "T-4507G"]
M2 = Matcher(V2_KEYS, {})


def test_v2_exact_hit_upgrades_to_pal_for_e_only():
    assert (M2.match("T-8113H   ", "E").key, M2.match("T-8113H   ", "E").how) == ("T-8113H-50", "exact")
    assert M2.match("T-8113H   ", "U").key == "T-8113H"
    assert M2.match("T-8113H   ", "JTUE").key == "T-8113H"


def test_v2_skeleton_dash_and_mk_prefix():
    r = M2.match("MK-81282  ", "E")                  # Formula Karts (Europe), F-real
    assert (r.key, r.how) == ("MK81282-50", "skeleton")
    assert M2.match("MK-81001  ", "U").key == "81001"  # Ghen War (USA)
    assert M2.match("MK-81045  ", "UTJ").key == "81045"  # Golden Axe The Duel (USA): no E, so prefer unsuffixed


def test_v2_leading_zero():
    r = M2.match("T-07004H  ", "U")                  # Grand Slam (USA)
    assert (r.key, r.how) == ("T-7004H", "skeleton")


def test_v2_version_spill():
    r = M2.match("T-29002G V", "J")                  # Gals Panic SS (Japan)
    assert (r.key, r.how) == ("T-29002G", "exact")


def test_v2_still_unmatched():
    assert M2.match("T-99999G  ", "J").key is None
