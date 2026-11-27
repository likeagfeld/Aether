"""CHEATS.PAK (contract/SPEC.md §5.15): format, checks, the card-update merge and the card writers."""
from __future__ import annotations

import struct
from pathlib import Path

import pytest

from omsat import cheatpak as cp
from omsat.binfmt import FormatError, crc32
from omsat.check import check_bytes

FIX = Path(__file__).resolve().parents[2] / "contract" / "fixtures"


def rec(*cheats, loc=0x06002800):
    return cp.Record(0x06000914, 0xC305, cp.G_MASTER | cp.G_CWX, loc, 39, list(cheats))


def kai(name, on=False, codes=((0x06010000, 1, 1),)):
    return cp.Cheat(name, cp.C_ON if on else 0, list(codes))


def user(name, codes=((0x06030000, 7, 3),)):
    return cp.Cheat(name, cp.C_ON | cp.C_USER, list(codes))


def test_fixture_reads_back():
    recs = cp.read_pak((FIX / "tiny.cheats.pak").read_bytes())
    assert sorted(recs) == ["GS-9001", "T-8113H", "T-8113H-50"]
    a = recs["GS-9001"]
    assert [(c.name, c.flags) for c in a.cheats] == [("PLAYER 1", cp.C_HEADER), ("UNLIMITED LIVES", 0),
                                                     ("MAX MONEY", cp.C_ON), ("MY CODE", cp.C_ON | cp.C_USER)]
    assert recs["T-8113H-50"] == a
    assert check_bytes((FIX / "tiny.cheats.pak").read_bytes()) == []


def test_round_trip_and_sharing():
    a, b = rec(kai("A", True), kai("B")), rec(kai("C"), loc=0x060FF000)
    data = cp.write_pak({"K2": a, "K1": a, "K3": b})
    idx = cp.read_index(data)
    assert [k for k, _, _ in idx] == ["K1", "K2", "K3"]              # sorted by key bytes
    assert idx[0][1] == idx[1][1] != idx[2][1]                       # identical records stored once
    assert cp.read_pak(data) == {"K1": a, "K2": a, "K3": b}
    assert all(o % 4 == 0 and n % 4 == 0 for _, o, n in idx)


def test_rejects():
    data = bytearray(cp.write_pak({"K1": rec(kai("A")), "K2": rec(kai("B"))}))
    assert check_bytes(bytes(data)) == []
    bad = bytearray(data); bad[-6] ^= 1
    assert "record CRC" in check_bytes(bytes(bad))[0]
    bad = bytearray(data); bad[64 + 3] ^= 1
    assert "table CRC" in check_bytes(bytes(bad))[0]
    assert check_bytes(bytes(data[:-8]))                             # truncated
    assert check_bytes(bytes(data[:100]))
    bad = bytearray(data); bad[64:88], bad[88:112] = data[88:112], data[64:88]
    struct.pack_into(">I", bad, 0x24, crc32(bytes(bad[64:112])))
    assert "sorted" in check_bytes(bytes(bad))[0]                   # an unsorted index
    with pytest.raises(FormatError):
        cp.encode_record(rec(cp.Cheat("H", cp.C_HEADER | cp.C_ON)))  # a header row may not be on
    with pytest.raises(FormatError):
        cp.encode_record(rec(cp.Cheat("x" * 31, 0, [(0, 0, 0)])))
    with pytest.raises(FormatError):
        cp.encode_record(rec(*[kai(f"C{i}") for i in range(65)]))
    with pytest.raises(FormatError):
        cp.write_pak({f"K{i:05d}": rec(cp.Cheat(f"N{i}", 0, [(i, 0, 0)] * 255)) for i in range(60)})   # > MAX_FILE


def test_merge_keeps_the_users_choices():
    old = cp.write_pak({"K1": rec(kai("Lives", True), kai("Lives", False), kai("Gone", True), user("Mine")),
                        "OLDONLY": rec(user("Kept"))})
    new = cp.write_pak({"K1": rec(cp.Cheat("Group", cp.C_HEADER), kai("Lives"), kai("Lives", True),
                                  kai("Fresh", True)),
                        "NEWONLY": rec(kai("X"))})
    out, rep = cp.merge(old, new)
    r = cp.read_pak(out)
    k1 = r["K1"]
    # the first "Lives" was on, the second off, by position among same-named Kai cheats; new cheats keep Kai's default
    assert [(c.name, bool(c.flags & cp.C_ON)) for c in k1.cheats] == [
        ("Group", False), ("Lives", True), ("Lives", False), ("Fresh", True), ("Mine", True)]
    assert k1.cheats[-1].flags & cp.C_USER
    assert r["OLDONLY"].cheats[0].name == "Kept"                     # a game only on the card is kept
    assert "NEWONLY" in r
    assert rep["merged"] == 1 and rep["kept_card_only"] == 1
    again, _ = cp.merge(out, new)
    assert again == out                                             # merging again changes nothing
    twice, _ = cp.merge(out, out)
    assert cp.read_pak(twice) == r                                  # user cheats are never doubled


def test_merge_with_no_or_a_corrupt_card_copy():
    new = (FIX / "tiny.cheats.pak").read_bytes()
    assert cp.merge(None, new)[0] == new
    out, rep = cp.merge(b"OMSATCHT garbage", new)
    assert out == new and rep["old_error"]


def test_merge_that_does_not_fit_keeps_the_card_record():
    old = cp.write_pak({"K1": rec(*[user(f"U{i}") for i in range(40)])})
    new = cp.write_pak({"K1": rec(*[kai(f"C{i}") for i in range(30)])})
    out, rep = cp.merge(old, new)
    assert rep["kept_unfitted"] == ["K1"]
    assert cp.read_pak(out) == cp.read_pak(old)


def test_install_writes_then_replaces(tmp_path):
    vm = tmp_path / "aether"
    new = (FIX / "tiny.cheats.pak").read_bytes()
    rep = cp.install(vm, new)
    assert (vm / "CHEATS.PAK").read_bytes() == new and not (vm / "CHEATS.PAK.NEW").exists()
    assert rep["games"] == 3
    recs = cp.read_pak(new)
    recs["T-8113H"].cheats[0].flags |= cp.C_ON                       # the user turns a cheat on, on the console
    (vm / "CHEATS.PAK").write_bytes(cp.write_pak(recs))
    cp.install(vm, new)
    assert cp.read_pak((vm / "CHEATS.PAK").read_bytes())["T-8113H"].cheats[0].flags & cp.C_ON
    # an interrupted console replace: CHEATS.PAK gone, .NEW holds the latest -- it is the copy merged from
    (vm / "CHEATS.PAK").rename(vm / "CHEATS.PAK.NEW")
    cp.install(vm, new)
    assert cp.read_pak((vm / "CHEATS.PAK").read_bytes())["T-8113H"].cheats[0].flags & cp.C_ON
    (vm / "CHEATS.PAK").write_bytes(b"not a pak")
    rep = cp.install(vm, new)
    assert rep["old_error"] and (vm / "CHEATS.BAK").read_bytes() == b"not a pak"
    assert (vm / "CHEATS.PAK").read_bytes() == new


def test_cardbuild_copies_and_merges(tmp_path):
    """`python -m omsat.cardbuild` puts the repo's CHEATS.PAK in --out, merged with the one already there."""
    from omsat import cardbuild, kaicheats as kc
    from omsat.gamesdb import GameRecord, region_flags_from, write_gamesdb
    from omsat.iphdr import make_system_id

    card = tmp_path / "card"
    (card / "Action" / "Alien").mkdir(parents=True)
    (card / "Action" / "Alien" / "game.iso").write_bytes(
        make_system_id(product="T-8113H", version="V1.000", device="CD-1/1", area="U", title="ALIEN") + b"\0" * 2048)
    gamesdb = write_gamesdb([GameRecord("T-8113H", "Alien", genre=0, region_flags=region_flags_from("U"))],
                            ["Action"], {}, 20261120)
    out = tmp_path / "out"
    rep = cardbuild.build(card, gamesdb, {}, None, None, False, out, 20261120, compat=False)
    shipped = kc.PAK_PATH.read_bytes()
    assert (out / "CHEATS.PAK").read_bytes() == shipped and rep["check_errors"] == {}
    recs = cp.read_pak(shipped)
    key = sorted(recs)[0]
    recs[key].cheats.append(user("My own"))
    (out / "CHEATS.PAK").write_bytes(cp.write_pak(recs))
    rep = cardbuild.build(card, gamesdb, {}, None, None, False, out, 20261120, compat=False)
    assert cp.read_pak((out / "CHEATS.PAK").read_bytes())[key].cheats[-1].name == "My own"
    assert rep["cheats"]["merged"] == len(recs)
    rep = cardbuild.build(card, gamesdb, {}, None, None, False, tmp_path / "o2", 20261120, compat=False, cheats=False)
    assert not (tmp_path / "o2" / "CHEATS.PAK").exists() and "cheats" not in rep


def test_install_merges_the_card_copy(tmp_path):
    """omsat.install (and so cardput) puts CHEATS.PAK in its transactional plan merged, never copied over."""
    from omsat import install as inst
    import shutil
    data = tmp_path / "data"
    data.mkdir()
    for n in ("GAMES.DB", "ART.PAK", "LIBRARY.IDX"):
        shutil.copy(FIX / {"GAMES.DB": "tiny.games.db", "ART.PAK": "tiny.art.pak", "LIBRARY.IDX": "tiny.library.idx"}[n],
                    data / n)
    target = tmp_path / "sd"
    (target / "aether").mkdir(parents=True)
    new = (FIX / "tiny.cheats.pak").read_bytes()
    recs = cp.read_pak(new)
    recs["T-8113H"].cheats.append(user("Card only"))
    (target / "aether" / "CHEATS.PAK").write_bytes(cp.write_pak(recs))
    pak = tmp_path / "CHEATS.PAK"
    pak.write_bytes(new)
    launcher = Path(inst.__file__).resolve().parents[2] / "menu" / "launcher" / "out" / "LAUNCHER.BIN"
    if not launcher.is_file():
        pytest.skip("menu/launcher/out/LAUNCHER.BIN not built")
    iso = tmp_path / "autoboot.iso"
    iso.write_bytes(b"\0" * 2048)
    rep = inst.install(target, iso, launcher, data, cheats=pak)
    assert "CHEATS.PAK" in rep["copied"]
    got = cp.read_pak((target / "aether" / "CHEATS.PAK").read_bytes())
    assert got["T-8113H"].cheats[-1].name == "Card only"
