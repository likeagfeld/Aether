# contract/fixtures/make_fixtures.py
"""Deterministically build the golden contract fixtures. `python make_fixtures.py` writes; `--check` compares."""
from __future__ import annotations

import json
import sys
from pathlib import Path

from PIL import Image

from omsat import artpak as ap, bootanim, iphdr, loadbg, sound, theme
from omsat.bupfile import BupDir, encode as encode_bup
from omsat.gamesdb import GameRecord, periph_flags_from, region_flags_from, write_gamesdb
from omsat.libidx import Folder, LibEntry, fingerprint, write_libidx
from omsat.match import Matcher

HERE = Path(__file__).parent
DATE = 20261117


def _tiny_loadbg() -> bytes:
    """SPEC §5.11: a 16x8 loop of 6 frames (a bar moving one pixel a frame) and its keyframe, shown 2x."""
    frames = []
    for k in range(6):
        px = bytearray((x * 3 + y * 5) & 0xFF for y in range(8) for x in range(16))
        for y in range(8):
            px[y * 16 + (k * 2) % 16] = 250
        frames.append(bytes(px))
    return loadbg.encode(frames, frames[0], 16, 8, [0] + [((i * 11) & 0x7FFF) for i in range(1, 256)], 2, DATE)


def _gradient(w: int, h: int) -> Image.Image:
    img = Image.new("RGB", (w, h))
    img.putdata([((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), 128) for y in range(h) for x in range(w)])
    return img


KEYS = ["GS-9001", "T-8113H", "T-8113H-50", "T-8113H-18", "T-11304H-50", "MK-81009", "MK-81009-50", "191X56", "GS-7103",
        "T-4507G", "MK81282-50", "81001", "T-7004H", "T-29002G",
        # Task 18e: the real miss forms measured on the user's card (codes only)
        "T-15909H", "T-15909H-18", "T-15909H-50", "MK81091-50", "T-12003H-50", "T-12003H-05", "81029", "MK81029-50",
        "GS-9055", "T-12512H", "T-12512H-50", "T-23201G", "T-8120H", "MK81005-50", "610-6803", "GS-9050"]
ALIASES = {"610-6342": "GS-7103"}
CASES = [("GS-9001   ", "JTUE", "GS-9001", "exact"), ("610-6342  ", "U", "GS-7103", "alias"),
         ("T-11304H-5", "E", "T-11304H-50", "prefix"), ("MK-81009  ", "E", "MK-81009-50", "exact"),
         ("T-8113H-", "E", "T-8113H-50", "skeleton"), ("T-99999G  ", "J", None, None),
         ("MK-8100", "E", "MK-81009-50", "prefix"), ("MK-8100", "U", "MK-81009", "prefix"),
         ("MK-8100", "JTUE", "MK-81009", "prefix"), ("191x56", "U", "191X56", "exact"),
         ("MK-81282  ", "E", "MK81282-50", "skeleton"), ("MK-81001  ", "U", "81001", "skeleton"),
         ("T-07004H  ", "U", "T-7004H", "skeleton"), ("T-29002G V", "J", "T-29002G", "exact"),
         # R8: a bare key plus both -18/-50 siblings ties in the exact branch; the -50 preference wins
         ("T-8113H   ", "E", "T-8113H-50", "exact"),
         # Empty-filter fallback (§6 choose() step 1): GS-9001 has no PAL variant, so the
         # area-preference filter would empty the candidate set for an E-only disc; the
         # unfiltered set is used instead and still resolves exactly.
         ("GS-9001   ", "E", "GS-9001", "exact"),
         # Task 18e (SPEC §6 v3), each a form read from a real disc's IP.BIN 0x20:
         # a dashless PAL suffix -- the base's family, then choose(); a US disc that carries "50" still picks the US key
         ("MK-8109150", "E", "MK81091-50", "suffix"), ("T-15909H50", "E", "T-15909H-50", "suffix"),
         ("T-15909H50", "U", "T-15909H", "suffix"), ("T-12003H50", "E", "T-12003H-50", "suffix"),
         ("MK_8100550", "E", "MK81005-50", "suffix"),
         # a suffixed code whose base is a key but whose suffixed form is not
         ("T-8120H-50", "E", "T-8120H", "suffix"),
         # a version that spilled into the product field with its digit ("MK-81029V1" + ".000" at 0x2A)
         ("MK-81029V1", "JTU", "81029", "skeleton"), ("GS-9055 V1", "JT", "GS-9055", "exact"),
         # a third-party code written without its leading "T-"
         ("12512H    ", "U", "T-12512H", "skeleton"), ("23201G    ", "J", "T-23201G", "skeleton"),
         ("6106803   ", "J", "610-6803", "skeleton"),
         # guards: a real key ending in 50 stays exact; an unknown dashless-50 code stays unmatched
         ("GS-9050   ", "J", "GS-9050", "exact"), ("T-9999950 ", "E", None, None)]


def build() -> dict[str, bytes]:
    out: dict[str, bytes] = {}
    out["tiny.art.pak"] = ap.write_artpak([
        ap.ArtEntry("GS-9001", 0, _gradient(80, 80), _gradient(160, 160), None, 1),
        ap.ArtEntry("T-8113H", 0, _gradient(64, 100), _gradient(128, 200), None, 2),
        ap.ArtEntry("T-4507G", 1, _gradient(80, 80), None, (16, 16, bytes(range(128))), 3),
        ap.ArtEntry("T-4507G", 2, _gradient(80, 80), None, None, 4),
        ap.ArtEntry(ap.folder_art_key("RPG"), 0, None, _gradient(160, 160), None, 5),
    ], DATE)
    out["tiny.games.db"] = write_gamesdb([
        # M6 Task 16 (SPEC §5.3 amendment): one compat row, on GS-9001 -- hint OM_LH_JHL(1), flags 3
        # (OM_CF_3DPAD | OM_CF_STDPAD), a note and a region mask of U only (bit2 = 0x04), independent of the
        # record's own (unrelated) JTUE region_flags. menu/host-tests/test_gamesdb.c checks these bytes directly.
        GameRecord("GS-9001", "Daytona USA", maker="SEGA", genre=0, region_flags=region_flags_from("JTUE"),
                   players=1, peripherals=periph_flags_from("JAS"), year=1995, date=19950401,
                   loader_hint=1, compat_flags=3, compat_note="JHL loader required", compat_regions=0x04),
        GameRecord("T-8113H", "Alien Trilogy", maker="Acclaim", genre=1, region_flags=region_flags_from("U"), players=1),
        GameRecord("T-4507G", "Grandia", title_rom="Grandia", maker="ESP", genre=2, disc_count=2,
                   region_flags=region_flags_from("J"), players=1),
        GameRecord("GS-7103", "Virtua Fighter 2", genre=3),
    ], ["Racing", "First Person Shooter", "RPG", "Fighting 3D"], ALIASES, DATE)
    out["tiny.library.idx"] = write_libidx([
        LibEntry("Racing/Daytona USA/daytona.cue", "GS-9001", area_flags=region_flags_from("JTUE"), folder_id=1, has_box=True),
        LibEntry("RPG/Grandia/disc1.cue", "T-4507G", disc=1, disc_total=2, folder_id=2),
        LibEntry("RPG/Grandia/disc2.cue", "T-4507G", disc=2, disc_total=2, folder_id=2),
        LibEntry("unknown.iso", "", image_type=2, title_override="MY HOMEBREW"),
    ], [Folder("", 0xFFFF), Folder("Racing", 0), Folder("RPG", 0)], fingerprint(["Racing", "RPG", "unknown.iso"]), DATE)
    out["sysid_gs9001.bin"] = iphdr.make_system_id(maker="SEGA ENTERPRISES", product="GS-9001", version="V1.000",
                                                   release_date="19950401", device="CD-1/1", area="JTUE",
                                                   peripherals="JAS", title="DAYTONA USA")
    out["sysid_pal_trunc.bin"] = iphdr.make_system_id(product="T-11304H-5", version="V1.000", device="CD-1/1",
                                                      area="E", title="3D LEMMINGS")
    out["tiny.bg.bin"] = theme.encode_bg(_gradient(320, 224), DATE)
    out["tiny.loadbg.bin"] = _tiny_loadbg()
    # SPEC §5.8 / §5.12 (Task 14/15): a two-clip SOUND.BIN (scroll + chant) and a two-block stereo MUSIC file
    out["tiny.sound.bin"] = sound.write_snd([sound.Clip([10, -10] * 50, 22050, 8, False, 0),
                                             sound.Clip([20, -20] * 30, 22050, 8, False, 4)], DATE)
    out["tiny.music.mus"] = sound.write_mus([(i * 7) % 2000 - 1000 for i in range(5000)],
                                            [1000 - (i * 3) % 2000 for i in range(5000)], 22050, 4500, -123, DATE)
    out["tiny.bootanim.bin"] = bootanim.encode(
        [bytes((x * 3 + y * 5 + k * 7 if (x + k) % 5 else 0) & 0xFF for y in range(8) for x in range(16)) for k in range(6)],
        16, 8, 60, 310, [0] + [((i * 7) & 0x7FFF) for i in range(1, 256)], DATE)
    m = Matcher(KEYS, ALIASES)
    cases = []
    for product, area, ek, eh in CASES:
        r = m.match(product, area)
        assert (r.key, r.how) == (ek, eh), (product, r)
        cases.append({"product": product, "area": area, "expect_key": ek, "expect_how": eh})
    out["match_cases.json"] = (json.dumps({"keys": KEYS, "aliases": ALIASES, "cases": cases}, indent=2) + "\n").encode()
    out.update(build_scan_fixtures(out["tiny.games.db"]))
    out["tiny.cheats.pak"] = _tiny_cheats()
    out["tiny.pergame.ini"] = _tiny_pergame()
    # SPEC §5.16 (M6 Task 22): one record, the values menu/host-tests/test_bupfile.c encodes and compares
    out["tiny.bup"] = encode_bup(BupDir(b"TESTSAVE01", b"VIBEMENU", 1, 0x01020304, 6, 1), b"ABCDEF")
    return out


def _tiny_cheats() -> bytes:
    """SPEC §5.15 (M6 Task 26b): three keys, two records -- GS-9001 and T-8113H-50 share one (a header row, a Kai
    cheat off, a Kai cheat on with two codes, a user-added cheat), T-8113H has its own (no master code)."""
    from omsat import cheatpak as cp
    a = cp.Record(0x06000914, 0xC305, cp.G_MASTER | cp.G_CWX, 0x06002800, 39, [
        cp.Cheat("PLAYER 1", cp.C_HEADER),
        cp.Cheat("UNLIMITED LIVES", 0, [(0x060DBCAA, 0x0003, 0x01)]),
        cp.Cheat("MAX MONEY", cp.C_ON, [(0x060DBCB0, 0xFFFF, 0x01), (0x060DBCB2, 0xFFFF, 0x01)]),
        cp.Cheat("MY CODE", cp.C_ON | cp.C_USER, [(0x06012345, 0x00FF, 0x03)])])
    b = cp.Record(0, 0, 0, 0, 0, [cp.Cheat("INVINCIBLE", 0, [(0x06000100, 0x0001, 0x0D)])])
    return cp.write_pak({"GS-9001": a, "T-8113H-50": a, "T-8113H": b}, DATE)


def _tiny_pergame() -> bytes:
    """SPEC §5.14 (M6 Task 15): two remembered games, LF line endings (the host test covers CRLF)."""
    return (
        "# header comment\n"
        "[MK-81045@U]\n"
        "title=Golden Axe - The Duel\n"
        "loader=jhl\n"
        "saves=on\n"
        "slot=3\n"
        "[T-4507G@J]\n"
        "loader=cwx\n"
        "slot=ask\n"
        "cheats=off\n"
    ).encode("ascii")


FP_CASES = [
    ["Racing", "RPG", "openmenu", "menu.bin", "autoboot.iso", "loose.iso"],
    [],
    ["b", "A", "System Volume Information", ".Trashes", "AutoBoot.cue", "satiator-rings", "Ä"],
    ["Games", "XBAND", "menu.bin", "REGION.TXT", "ar_backup.bin", "vibemenu"],   # Task 16: = ["Games", "XBAND"]
    # M6 Task 24c (Card Manager final review C1): a root carrying Kai's save tree must fingerprint as if it
    # were not there, the same as any other hidden root name -- expect == fingerprint(["Game1"]).
    ["SAVES", "Game1"],
    # the Aether rename (2026-11-25): the menu's folder now and before are both hidden, so a card that moved /vibemenu
    # to /aether keeps its fingerprint -- expect == fingerprint(["Games", "XBAND"]), as the vibemenu case above
    ["Games", "XBAND", "aether", "VIBEMENU"],
    ["Games", "XBAND", "Aether"],
    # 2026-11-24 (the lid swap): LAUNCHER.BIN's out.desc and out1..out9.desc at the root are hidden too -- expect ==
    # fingerprint(["Game1", "out0.desc", "out10.desc"])
    ["Game1", "out.desc", "OUT1.DESC", "out9.desc", "out0.desc", "out10.desc"],
]

SCAN_TREE = {
    "build_date": DATE,
    "tree": {
        # 2026-11-23: "satiator-rings-old" pins that the root's satiator-rings* / satiator-saves* names are prefixes
        "": {"dirs": ["Racing", "RPG", "openmenu", "Homebrew", "Empty", "Deep", "satiator-rings-old"],
             "files": ["menu.bin", "autoboot.iso", "loose.iso"]},
        "satiator-rings-old": {"dirs": [], "files": ["x.iso"]},
        "Racing": {"dirs": ["Daytona USA", "Sega Rally"], "files": []},
        # 2026-11-23 (clarification 7, cue dedupe): Daytona.ISO is named by daytona.cue's second FILE line (any case),
        # so it is part of the cue's game, not a second image: the folder stays a one-image game directory.
        "Racing/Daytona USA": {"dirs": [], "files": ["daytona.cue", "daytona.bin", "Daytona.ISO", "BOX.TGA"]},
        "Racing/Sega Rally": {"dirs": [], "files": ["rally.iso"]},
        "RPG": {"dirs": ["Grandia"], "files": []},
        "RPG/Grandia": {"dirs": [], "files": ["disc1.cue", "disc2.cue", "disc1.bin", "disc2.bin"]},
        # Non-root "openmenu" and non-root "autoboot.cue" pin that the §4 hidden set
        # (clarification 6) applies at the root only (clarification 7): both must survive
        # scanning here, unlike the root-level "openmenu" dir and "autoboot.iso" file above.
        # 2026-11-23 (Satiator Rings' model): Homebrew's BOX.TGA is its loose images' cover fallback (flag bit0); RPG/Grandia
        # (two images) is a folder row, not a game directory; Racing/Daytona USA and Racing/Sega Rally (one image each,
        # no subfolder) are single-game folders
        "Homebrew": {"dirs": ["openmenu"], "files": ["a.iso", "b.iso", "autoboot.cue", "BOX.TGA"]},
        "Homebrew/openmenu": {"dirs": [], "files": ["game.iso"]},
        "Empty": {"dirs": [], "files": []},
        "openmenu": {"dirs": [], "files": ["GAMES.DB"]},
        # Depth chain pinning clarification 7's depth <= 8 limit: Deep (depth 1) through
        # Deep/L2/.../L8 (depth 8) are entered as folder rows; Deep/.../L8/L9 (depth 9) is
        # not entered, and is counted in report["skipped_folders"].
        "Deep": {"dirs": ["L2"], "files": []},
        "Deep/L2": {"dirs": ["L3"], "files": []},
        "Deep/L2/L3": {"dirs": ["L4"], "files": []},
        "Deep/L2/L3/L4": {"dirs": ["L5"], "files": []},
        "Deep/L2/L3/L4/L5": {"dirs": ["L6"], "files": []},
        "Deep/L2/L3/L4/L5/L6": {"dirs": ["L7"], "files": []},
        "Deep/L2/L3/L4/L5/L6/L7": {"dirs": ["L8"], "files": []},
        "Deep/L2/L3/L4/L5/L6/L7/L8": {"dirs": ["L9"], "files": []},
        "Deep/L2/L3/L4/L5/L6/L7/L8/L9": {"dirs": [], "files": []},
    },
    # 2026-11-23: each .cue's FILE names (path components stripped), as a reader of the cue's first 4,096 bytes sees them
    "cues": {
        "Racing/Daytona USA/daytona.cue": ["daytona.bin", "daytona.iso"],
        "RPG/Grandia/disc1.cue": ["disc1.bin"],
        "RPG/Grandia/disc2.cue": ["disc2.bin"],
    },
    "sysids": {
        "loose.iso": {"product": "T-8113H", "area": "U", "device": "CD-1/1", "title": "ALIEN TRILOGY"},
        "Racing/Daytona USA/daytona.cue": {"product": "GS-9001", "area": "JTUE", "device": "CD-1/1", "title": "DAYTONA USA"},
        "Racing/Sega Rally/rally.iso": {"product": "GS-9047", "area": "JTUE", "device": "CD-1/1", "title": "SEGA RALLY"},
        "RPG/Grandia/disc1.cue": {"product": "T-4507G", "area": "J", "device": "CD-1/2", "title": "GRANDIA"},
        "RPG/Grandia/disc2.cue": {"product": "T-4507G", "area": "J", "device": "CD-2/2", "title": "GRANDIA"},
        "Homebrew/a.iso": {"product": "HB-0001", "area": "JTUE", "device": "CD-1/1", "title": "A"},
        "Homebrew/b.iso": {"product": "HB-0002", "area": "JTUE", "device": "CD-1/1", "title": "B"},
        "Homebrew/autoboot.cue": {"product": "HB-0003", "area": "JTUE", "device": "CD-1/1", "title": "AUTOBOOT"},
        "Homebrew/openmenu/game.iso": {"product": "HB-0004", "area": "JTUE", "device": "CD-1/1", "title": "OPENMENU"},
    },
}


def build_scan_fixtures(gdb_bytes: bytes) -> dict[str, bytes]:
    from omsat.cardscan import TreeSource, scan
    from omsat.gamesdb import read_gamesdb
    from omsat.libidx import card_fingerprint
    recs, _, aliases = read_gamesdb(gdb_bytes)
    r = scan(TreeSource(SCAN_TREE["tree"], SCAN_TREE["sysids"], SCAN_TREE["cues"]), [x.key for x in recs], aliases, DATE)
    fp = {"cases": [{"names": n, "expect": card_fingerprint(n)} for n in FP_CASES]}
    return {
        "fingerprint_cases.json": (json.dumps(fp, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
        "scan_tree.json": (json.dumps(SCAN_TREE, indent=2) + "\n").encode("utf-8"),
        "scan_tree.library.idx": r.data,
    }


def main() -> int:
    check = "--check" in sys.argv
    bad = 0
    for name, data in build().items():
        p = HERE / name
        if check:
            if not p.exists() or p.read_bytes() != data:
                print(f"DIFF {name}")
                bad += 1
        else:
            p.write_bytes(data)
            print(f"wrote {name} ({len(data)} B)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
