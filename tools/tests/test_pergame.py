"""omsat.pergame reads/writes the same format the menu does (SPEC §5.14) and the checker rejects
bad files (mutation demo: each corruption must go RED)."""
from pathlib import Path

from omsat import check, pergame

ROOT = Path(__file__).resolve().parents[2]
FIX = ROOT / "contract" / "fixtures" / "tiny.pergame.ini"

K_IN = ("# header comment\r\n"
        "[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nloader=jhl\r\nsaves=on\r\nslot=3\r\nmine=keep\r\n"
        "[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n")


def test_fixture_roundtrip():
    text = FIX.read_text(encoding="ascii")
    games = pergame.parse(text)
    assert [g.id for g in games] == ["MK-81045@U", "T-4507G@J"]
    assert (games[0].title, games[0].loader, games[0].saves, games[0].slot) == ("Golden Axe - The Duel", "jhl", "on", "3")
    assert (games[1].loader, games[1].slot, games[1].cheats, games[1].igr) == ("cwx", "ask", "off", "default")
    assert pergame.dump(games, text) == text


def test_gameid():
    assert pergame.game_id(b"MK-81045  ", b"U         ") == "MK-81045@U"
    assert pergame.dir_name("MK-81045@U") == "MK-81045[U]"
    # Kai's rule (m6-research.md §1.2): every area letter, product cut at the first byte <= ' ', '.' -> '_'
    assert pergame.game_id(b"MK-81214  ", b"UT        ") == "MK-81214@UT"
    assert pergame.dir_name("MK-81214@UT") == "MK-81214[UT]"
    assert pergame.game_id(b"T-4507g   ", b"JTUBKAEL  ") == "T-4507G@JTUBKAEL"
    assert pergame.game_id(b"GS-9055 V1", b"J         ") == "GS-9055@J"
    assert pergame.game_id(b"T-1.2/3_X ", b"J         ") == "T-1_2_3_X@J"
    assert pergame.game_id(b"AB\x80C      ", b"J         ") == "AB_C@J"
    assert pergame.game_id(b"T-1       ", b"      E   ") == "T-1@E"
    assert pergame.game_id(b"T-1       ", b"1jE-      ") == "T-1@E"
    assert pergame.game_id(b"T-1       ", b"J\0U       ") == "T-1@J"
    assert pergame.game_id(b"T-1       ", b"JTUBKAELJT") == "T-1@JTUBKAEL"
    assert pergame.game_id(b"MK81045-50", b"          ") == "MK81045-50@"
    assert pergame.dir_name("MK81045-50@") == "MK81045-50"
    for bad in (b"          ", b" T-1      "):
        assert pergame.game_id(bad, b"J         ") is None
    for bad in ("NOAT", "@U", "A@1", "A@B@C", "a@U", "ABCDEFGHIJK@U", "A@JTUBKAELJ"):
        assert pergame.dir_name(bad) is None, bad


def test_put_and_delete_match_the_menu():
    """The same cases as menu/host-tests/test_pergame.c, so the PC tool and the menu write the same bytes."""
    g = pergame.get(K_IN, "MK-81045@U")
    g.loader = "cwx"
    assert pergame.put(K_IN, g) == K_IN.replace("loader=jhl", "loader=cwx")
    g.cheats, g.igr = "on", "off"
    assert "mine=keep\r\ncheats=on\r\nigr=off\r\n[T-4507G@J]\n" in pergame.put(K_IN, g)
    g = pergame.get(K_IN, "MK-81045@U")
    g.saves, g.slot, g.loader, g.igr = "default", "0", "auto", "default"
    out = pergame.put(K_IN, g)
    assert out == ("# header comment\r\n[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nmine=keep\r\n"
                   "[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n")
    assert "default" not in out
    for gid in ("MK-81045@U", "T-4507G@J"):
        assert pergame.put(K_IN, pergame.get(K_IN, gid)) == K_IN
    n = pergame.Game("GS-9001@J", saves="off")
    assert pergame.put(K_IN, n) == K_IN + "[GS-9001@J]\nsaves=off\n"
    n = pergame.Game("GS-9001@J", title="Daytona USA", loader="jhl", saves="on", slot="ask", cheats="off", igr="on")
    assert pergame.put("", n) == "[GS-9001@J]\ntitle=Daytona USA\nloader=jhl\nsaves=on\nslot=ask\ncheats=off\nigr=on\n"
    assert pergame.put("", pergame.Game("T-1@")) == "[T-1@]\n"
    assert pergame.put("[A@J]\nloader=jhl", pergame.Game("B@U", igr="on")) == "[A@J]\nloader=jhl\n[B@U]\nigr=on\n"
    assert pergame.put("[A@J]\nloader=jhl", pergame.Game("A@J", loader="jhl", saves="on")) == "[A@J]\nloader=jhl\nsaves=on\n"
    assert pergame.put("[A@J]\nloader=jhl", pergame.Game("A@J", loader="jhl")) == "[A@J]\nloader=jhl"
    assert (pergame.put("[A@J]\nloader=jhl\n# note\n\n\n[B@U]\n", pergame.Game("A@J", loader="jhl", saves="on"))
            == "[A@J]\nloader=jhl\n# note\nsaves=on\n\n\n[B@U]\n")
    assert pergame.put("[A@J]\nloader=jhl\nx=1\nloader=cwx\n", pergame.Game("A@J", loader="cwx")) == "[A@J]\nloader=cwx\nx=1\n"
    t = "[A@J]\ntitle=0123456789012345678901234567890123456789XYZ\n"
    assert pergame.get(t, "A@J").title == "0123456789012345678901234567890123456789"
    assert pergame.delete(K_IN, "MK-81045@U") == "# header comment\r\n[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n"
    assert pergame.delete(K_IN, "NONE@U") == K_IN
    d = "[A@J]\nloader=jhl\n[B@U]\n[A@J]\nloader=cwx\n"
    assert pergame.get(d, "A@J").loader == "jhl"
    assert pergame.delete(d, "A@J") == "[B@U]\n"
    b = pergame.get("[A@J]\nloader=fast\nslot=12\n", "A@J")
    assert (b.loader, b.slot) == ("auto", "0")


def test_dump_writes_changes():
    games = pergame.parse(K_IN)
    games[0].loader = "cwx"
    del games[1]
    games.append(pergame.Game("GS-9001@J", saves="off"))
    assert pergame.dump(games, K_IN) == ("# header comment\r\n[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\n"
                                         "loader=cwx\r\nsaves=on\r\nslot=3\r\nmine=keep\r\n[GS-9001@J]\nsaves=off\n")


def test_checker_mutations():
    good = FIX.read_text(encoding="ascii")
    assert pergame.check(good) == []
    many = "".join(f"[G{i:03d}@J]\n" for i in range(513))
    for bad in (good.replace("loader=jhl", "loader=fast"),
                good.replace("[MK-81045@U]", "[MK-81045]"),
                good.replace("[MK-81045@U]", "[MK.81045@U]"),
                good + "[MK-81045@U]\n",                    # duplicate section
                good.replace("slot=3", "slot=10"),
                good.replace("cheats=off", "cheats=default\ncheats=on"),   # a managed key twice
                good.replace("Golden Axe - The Duel", "G" * 41),          # title over 40 bytes
                good + "[A@J]\ntitle=" + "x" * 250 + "\n",                 # a line over 255 bytes
                good + "[A@J]\nloader\n",                                  # no '='
                many,                                                      # 513 sections
                "x" * 40000):
        assert pergame.check(bad), bad[:40]
    assert pergame.check(many[:-len("[G512@J]\n")]) == []                 # 512 is allowed


def test_omsat_check_dispatches_by_name(tmp_path):
    (tmp_path / "a").mkdir()
    (tmp_path / "b").mkdir()
    good = tmp_path / "a" / "PERGAME.INI"
    good.write_bytes(FIX.read_bytes())
    bad = tmp_path / "b" / "pergame.ini"
    bad.write_bytes(FIX.read_bytes().replace(b"loader=jhl", b"loader=fast"))
    assert check.main([str(good)]) == 0
    assert check.main([str(bad)]) == 1
    assert check.main([str(FIX)]) == 0                                    # *.pergame.ini too


def test_size_cap_is_16_kb():
    """PERGAME.INI is capped at 16,384 B, the same value as the menu's
    OM_PERGAME_MAX_BYTES (menu/src/pergame.h) and SPEC §5.14; one byte over is refused."""
    import re
    h = (Path(__file__).resolve().parents[2] / "menu" / "src" / "pergame.h").read_text()
    assert int(re.search(r"#define OM_PERGAME_MAX_BYTES (\d+)u", h).group(1)) == pergame.MAX_BYTES == 16384
    head = "[A@J]\n"
    body = ("#" * 99 + "\n") * ((16384 - len(head)) // 100)
    ok = head + body + "#" * (16384 - len(head) - len(body) - 1) + "\n"
    assert len(ok) == 16384 and pergame.check(ok) == []
    assert "over the 16384 B limit" in pergame.check(ok + "#")[0]
