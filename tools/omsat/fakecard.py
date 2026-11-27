"""Synthetic test card for the --app-fake build (plan D6): GAMES.DB, LIBRARY.IDX, FAVS/RECENT/SETTINGS and empty image
files, packed into one OMFAKEFS blob (menu/src/fakefs.h). Test-only; never installed on a card.
`python -m omsat.fakecard [--out PATH]`"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

from . import sound, theme
from .gamesdb import USER_TITLE, GameRecord, region_flags_from, write_gamesdb
from .libidx import Folder, LibEntry, card_fingerprint, read_libidx, write_libidx

MAGIC = b"OMFAKEFS"
ENT = 264
DATE = 20261118
MAX_BLOB = 164 * 1024        # plan D5 (Task 13c round 2: LOADBG.BIN comes from the fake disc, not the blob); Task 14/15:
                             # + the 51,200 B music excerpt (fake build only: --app-fake HWRAM headroom ~166 KB before);
                             # + VRTILES.BIN (13,940 B), the bytes that left the ELF's .rodata: net zero HWRAM
VRTILES = Path(__file__).resolve().parents[2] / "menu" / "app" / "assets" / "VRTILES.BIN"   # M6 C3 (SPEC §5.18)
OUT = Path(__file__).resolve().parents[2] / "build" / "fakecard" / "FAKECARD.BIN"
GENRES = ["Action", "Puzzle", "Racing", "RPG", "Shooter", "Sports"]
ADJ = ["Alpha", "Blue", "Cosmic", "Dark", "Electric", "Final", "Galaxy", "Hyper", "Iron", "Jet"]
NOUN = ["Racer", "Quest", "Blaster", "Puzzle"]
AREAS = ["J", "U", "E", "JTUE", "T", "B", "UE"]
# one long title, so tour step 05 (A-Z, first letter: this game is selected) shows the selected row's marquee
# and the details title's marquee (controller addendum 3) on the fake card.
LONG_TITLES = {20: "Alpha Blaster: The Director's Cut Special Edition"}
FOLDERS = [Folder("", 0xFFFF), Folder("Racing", 0), Folder("RPG", 0), Folder("Shooters", 0), Folder("Shooters/Classic", 3)]


def _games():
    out = []
    for i in range(40):
        title = f"{ADJ[i % 10]} {NOUN[i // 10]}"
        genre = 0xFF if i % 7 == 6 else i % 6
        folder = {2: 1, 3: 2, 4: 4 if i % 8 == 0 else 3}.get(genre, 0)
        slug = title.lower().replace(" ", "_")
        path = f"{slug}.iso" if folder == 0 else f"{FOLDERS[folder].path}/{title}/{slug}.cue"
        title = LONG_TITLES.get(i, title)                     # GAMES.DB title only: the path keeps the short name
        year = 0 if i % 9 == 8 else 1994 + i % 5
        out.append((f"FK-{i + 1:04d}", title, genre, AREAS[i % len(AREAS)], year, folder, path))
    return out


SCROLL_MARKER = "aether/FAKESCRIPT.SCROLL"   # selects the held-scroll pad script (menu/app/fake/fakepad.c)
LOADING_MARKER = "aether/FAKESCRIPT.LOADING"  # selects the launch / cancel pad script (fakepad.c)
SETTINGS_MARKER = "aether/FAKESCRIPT.SETTINGS"  # scrolls to Safe area, cycles overscan and Loader
SDHW_MARKER = "aether/FAKESCRIPT.SDHW"          # reads at the user's measured card rate (~245 KB/s, 4 us a byte)
TILES_MARKER = "aether/FAKESCRIPT.TILES"        # steps through the five virtual root folders' tiles
IDLE_MARKER = "aether/FAKESCRIPT.IDLE"          # the screensaver pad script (leave the pad, then A)
LOADER_MARKER = "aether/FAKESCRIPT.LOADER"      # launch two favorites, "CWX (auto)" then "JHL (database)"
LOADER_JHL_GAME = 5                                # Final Racer, the second favorite by title: hint JHL
GAME_MARKER = "aether/FAKESCRIPT.GAME"          # the per-game panel, its save and Forget
PGCYCLE_MARKER = "aether/FAKESCRIPT.PGCYCLE"    # A/B panel cycles (hardware report on 3c64908: the menu froze)
PGLONG_MARKER = "aether/FAKESCRIPT.PGLONG"      # with --pgload-script; first match wins
PGLOAD_MARKER = "aether/FAKESCRIPT.PGLOAD"      # the panel's read-ahead of LOADBG.BIN, then a launch
# game 0 ("Alpha Racer", the first Favorite) carries Golden Axe: The Duel's own compat bytes so the
# details card, the per-game panel's Pad row and the pre-launch warning can all be captured without a second game.
PADWARN_MARKER = "aether/FAKESCRIPT.PADWARN"
ABOUT_MARKER = "aether/FAKESCRIPT.ABOUT"        # Settings > About: Start, Down x14, A opens the page, B returns
TOOL_MARKER = "aether/FAKESCRIPT.TOOL"          # the panel's Cheats and Manage saves through the loading screen
RETURN_MARKER = "aether/FAKESCRIPT.RETURN"      # a tool's hand-off arms the return block, the menu starts again
RETURN_SCRIPTS = {"cheats": "aether/FAKESCRIPT.RETCH", "saves": "aether/FAKESCRIPT.RETSV",
                  "settings": "aether/FAKESCRIPT.RETSET"}
POPUP_SCRIPTS = {"cheats": "aether/FAKESCRIPT.POPCH", "saves": "aether/FAKESCRIPT.POPSV",
                 "settings": "aether/FAKESCRIPT.POPSET"}   # 2026-11-25: the in-menu popups (fakepad.c k_pop*)


# 2026-11-25 freeze hunt (hardware: the menu froze in the Cheats popup on Daytona USA and in Settings with About; no
# Mednafen repro): pad soaks as data (fakepad.c soak() reads FAKESCRIPT.SOAK), so they cost the menu almost no HWRAM.
# A row is (keys, frames held, frames to the next row, never 0); the rows repeat for as long as the emulator runs, each round
# starting and ending at the root of the list.
SOAK_MARKER = "aether/FAKESCRIPT.SOAK"
_K = {"R": 1 << 15, "L": 1 << 14, "D": 1 << 13, "U": 1 << 12, "S": 1 << 11, "A": 1 << 10, "C": 1 << 9, "B": 1 << 8,
      "Y": 1 << 5}                               # SGL SL_DEF.H PER_DGT_*
SOAKS = {
    # Favorites, game 0's panel, Down x5 to Cheats, A (the popup); A (toggle) and Down over 8 rows, back up, held
    # scrolls, Y (a built-in cheat's message), B (saves and closes), B (the panel closes), B (back to the root)
    "cheats": [("", 1, 60), ("A", 3, 120), ("A", 3, 60)] + [("D", 3, 15)] * 4 + [("D", 3, 120), ("A", 3, 120)]
              + [("A", 3, 20), ("D", 3, 20)] * 8 + [("U", 3, 20), ("A", 3, 20)] * 2
              + [("D", 60, 80), ("U", 60, 80), ("Y", 3, 40), ("B", 3, 60), ("B", 3, 60), ("B", 3, 60)],
    # Start (Settings); held scrolls, a fresh Up from Theme wraps to About, A, B, single steps, About again; B closes
    "settings": [("", 1, 60), ("S", 3, 60), ("D", 60, 80), ("U", 60, 80), ("U", 3, 20), ("A", 3, 60), ("B", 3, 20)]
                + [("U", 3, 15)] * 2 + [("D", 3, 15)] * 3 + [("U", 3, 15), ("A", 3, 40), ("B", 3, 20), ("D", 3, 15),
                                                             ("B", 3, 60)],
}


def soak_script(name: str) -> bytes:
    return b"".join(struct.pack(">HBB", sum(_K[k] for k in keys), hold, step)
                    for keys, hold, step in SOAKS[name]) + bytes(4)   # a row of 0 frames ends them


def popup_files(key: str, cheat_key: str = "", saves: bool = True) -> dict[str, bytes]:
    """The in-menu popups' captures: game 0's CHEATS.PAK record (two cheats of the user's, one on; two built in) and
    its card saves -- the folder's own set, a newer complete generation with an alternate, slot 3 -- plus the general
    set and another game's folder for Settings > Manage saves. Test data only (the card is never installed)."""
    from .bupfile import BupDir, encode
    from .cheatpak import Cheat, Record, write_pak
    on, user = 0x01, 0x02
    rec = Record(master_addr=0x06000914, master_value=0xC305, flags=0x02, location=0x060FF000, cache4=125,
                 cheats=[Cheat("Infinite HP", on | user, [(0x0607A2C0, 0x03E7, 1)]),
                         Cheat("Max Dyne after every battle", user, [(0x0607A2C4, 0x270F, 1), (0x0607A2C6, 0x0000, 1)]),
                         Cheat("Berserk gauge always full", on, [(0x0607B010, 0x00FF, 3)]),
                         Cheat("All items in the inventory", 0, [(0x0607C000, 0x0063, 3)])])
    if cheat_key:                                        # a real record from the bundled CHEATS.PAK (--soak-cheat)
        from .cheatpak import read_pak
        rec = read_pak((Path(__file__).resolve().parents[1] / "data" / "cheats" / "CHEATS.PAK").read_bytes())[cheat_key]
    files = {"aether/CHEATS.PAK": write_pak({key: rec})}
    if not saves:                                        # the cheats soak: CHEATS.PAK alone (the card blob is HWRAM)
        return files

    def save(path: str, name: str, comment: str, size: int, date: int) -> None:
        d = BupDir(name.encode(), comment.encode(), 1, date, size, (size + 63) // 64 + 1)
        files[path] = encode(d, bytes((i * 7 + date) & 0xFF for i in range(size)))
    gdir = "aether/SAVES/MK-81045[U]"
    m = 24210 * 1440                                     # minutes since 1980 (BupDate): late 2046 -- any fixed date
    save(f"{gdir}/PANZER_SYS.BUP", "PANZER_SYS", "SYSTEM", 100, m)
    save(f"{gdir}/G000/G00001/PANZER_01.BUP", "PANZER_01", "DISC1 CAMP", 700, m + 2000)
    save(f"{gdir}/G000/G00001/PANZER_02.BUP", "PANZER_02", "DISC3 TOWN", 700, m + 2100)
    save(f"{gdir}/G000/G00001/PANZER_SYS.BUP", "PANZER_SYS", "SYSTEM", 100, m + 2100)
    files[f"{gdir}/G000/G00001/DONE.TXT"] = b""
    save(f"{gdir}/G000/G00001/ALT/PANZER_01.BUP", "PANZER_01", "RINGS COPY", 700, m + 900)
    save(f"{gdir}/03/PANZER_01.BUP", "PANZER_01", "NEW GAME+", 700, m + 3000)
    save("aether/SAVES/_GENERAL/OPTIONS.BUP", "OPTIONS", "SETTINGS", 64, m)
    save("aether/SAVES/T-1234G[J]/GRANDIA_01.BUP", "GRANDIA_01", "DISC 1", 2000, m + 500)
    return files


LAUNCH_MARKER = "aether/FAKESCRIPT.LAUNCH"      # the launch panel (user decision 2026-11-23): Favorites, A, A
PAD3D_MARKER = "aether/FAKESCRIPT.PAD3D"        # menu/app/fake/fakepad.c om_fakepad_port1_id: port 1 reads 0x16
# tools/data/compat/controller_compat.json's row 1, as tools/omsat/compat.py's own NOTE_MAX=46 truncates
# it on a real card (contract/SPEC.md §5.3 "at most 46 bytes") -- this is the actual on-card text, not the full
# warning_text the research doc quotes.
PADWARN_NOTE = "Use a standard pad: the 3D pad sends it to the"
# the seeded PERGAME.INI (two sections, SPEC §5.14) and game 0's IP stand-in (its game id); the expected
# file after the GAME script is computed from these by omsat.pergame (expected_game_pergame)
PERGAME_SEED = "# seeded by omsat.fakecard\n[T-4507G@J]\ntitle=Seeded Two\nloader=jhl\n[GS-9001@J]\ntitle=Seeded One\n"
GAME0_NAME = "alpha_racer"   # 2026-11-23: game 0's list name, its image's file name (alpha_racer.iso, at the root)
GAME0_IP = {"maker": "SEGA TP T-81", "product": "MK-81045", "version": "V1.000", "release_date": "19950101",
            "device": "CD-1/1", "area": "U", "peripherals": "J", "title": "ALPHA RACER"}


def game0_ip(product: str = GAME0_IP["product"]) -> bytes:
    """a 2,048 B sector with game 0's System ID (ST-040-R4 §4.2), so the menu reads a game id from it"""
    from .iphdr import make_system_id
    return make_system_id(**dict(GAME0_IP, product=product)).ljust(2048, b"\0")


def expected_game_pergame() -> str:
    """PERGAME.INI after FAKESCRIPT.GAME: game 0 (MK-81045@U) saved with Loader CWX and Saves On, then forgotten from
    its own panel (2026-11-23: no Per-game list) -- by the PC rules (omsat.pergame), which the menu's writer must match
    byte for byte. Its title is the name the list shows (2026-11-23: the image's file name, alpha_racer.iso)"""
    from . import pergame
    g = pergame.Game("MK-81045@U", title=GAME0_NAME, loader="cwx", saves="on")
    return pergame.delete(pergame.put(PERGAME_SEED, g), "MK-81045@U")


# a corrupt seed (an embedded NUL: the menu reads it as empty, backs it up as
# PERGAME.BAK at the first save, then writes a clean file) and an oversize one (16,385 B: never read, saves refused)
PERGAME_CORRUPT = b"# seeded by omsat.fakecard: corrupt\n[T-4507G@J]\ntitle=Seeded Two\nloader=j\0hl\n"


def pergame_oversize() -> bytes:
    head = PERGAME_SEED.encode("ascii")
    body = (b"#" * 99 + b"\n") * ((16385 - len(head)) // 100)
    out = head + body
    return out + b"#" * (16385 - len(out) - 1) + b"\n"


def expected_corrupt_first_save() -> str:
    """the clean file the corrupt-seed run's first save writes: game 0's section alone"""
    from . import pergame
    return pergame.put("", pergame.Game("MK-81045@U", title=GAME0_NAME, loader="cwx", saves="on"))
FW_MARKER = "aether/FAKESCRIPT.FW"              # the firmware string (READFAIL = s_get_fw_version fails)
SLOWBOOT_MARKER = "aether/FAKESCRIPT.SLOWBOOT"  # each start-up step held ~0.7 s (interstitial captures)
# ONE --diag flag for the fake build's diagnostics. Each mode is one marker file on the card:
#   lines   - 13d: the developer boot lines and the tour counters on NBG0 (main.c, folders.c; off by default)
#   page1   - 16a: the diag-combo pad script (hold L+R, press Start), then idle on the diagnostics page 1
#   page2   - 16a: the combo, then R once: idle on page 2 (the boot timings)
#   dismiss - 16a: the combo, then B: back in the Folders view (the exit path)
#   scroll  - 2026-11-19 hardware flicker: hold Down 4 s, Up 4 s, then the combo and R: page 2's NBG2/VDP1 counters
#   page3   - M6 C2: the combo, then R twice: idle on page 3 (the BIGLOGO line)
#   page4   - M6 Task 21 (the --sdtest disc): the combo, R three times, A, A: the SD stress page runs, then idles
DIAG_MARKERS = {"lines": "aether/FAKESCRIPT.DIAG", "page1": "aether/FAKESCRIPT.DIAG1",
                "page2": "aether/FAKESCRIPT.DIAG2", "dismiss": "aether/FAKESCRIPT.DIAG3",
                "scroll": "aether/FAKESCRIPT.DIAG4", "page3": "aether/FAKESCRIPT.DIAG5",   # page3: M6 C2
                "page4": "aether/FAKESCRIPT.DIAG6"}                                        # page4: M6 Task 21
DIAG_MARKER = DIAG_MARKERS["lines"]
STUB_BODY = 8 * 1024                            # read in 2 KB steps by the loading screen (the real body is ~77 KB)


def launcher_stub() -> bytes:
    """a LAUNCHER.BIN that passes the menu's header and CRC checks so the fake build's launch reaches
    "Starting game...". It is never run: the fake build stops at "would hand off now" (menu/views/folders.c)."""
    from .lnchhdr import make_header
    body = b"OMSAT FAKE LAUNCHER STUB - NEVER EXECUTED".ljust(STUB_BODY, b"\0")   # zeros: never run
    return make_header(body) + body


# the loading script's card dresses its third favorite as the approved mockup's game, with its US cover
# (tall) from the user's Rings cover project zip when it is on this PC (test-only; the card is never committed or
# installed). The TEST theme's 72 KB BG.BIN makes room for the cover: the loading script never visits the themes.
COVER_ZIP = Path(r"W:\SEGA SATURN RINGS Cover Project-20261117T130330Z-1-001.zip")
COVER_MEMBER = "SEGA SATURN RINGS Cover Project/US/Panzer Dragoon Saga (Disc 1) (USA).jpg"
MOCKUP_GAME = ("Panzer Dragoon Saga", "Team Andromeda", 1998, "RPG", "U", 1, 4)   # title maker year genre area disc/4


def _mockup_artpak(key: str) -> bytes | None:
    import io
    import zipfile
    from .artpak import ArtEntry, write_artpak
    if not COVER_ZIP.is_file():
        return None
    with zipfile.ZipFile(COVER_ZIP) as z:
        img = Image.open(io.BytesIO(z.read(COVER_MEMBER))).convert("RGB")
    return write_artpak([ArtEntry(key, 1, None, img, None, 0)], DATE)


def build(panel_alpha: int = 50, scroll: bool = False, stub: bool = False, loading: bool = False,
          music: bool = True, diag: str | None = None, slowboot: bool = False,
          sd_hw: bool = False, settings_script: bool = False, settings_ini: str | None = None,
          tiles: bool = False, vrtiles: bool = True, fw: str | None = None,
          recent: tuple = (7, 3, 20, 1), idle: bool = False, loader_script: bool = False,
          game_script: bool = False, pgload_script: bool = False, pergame_seed: str = "", pglong: bool = False,
          pgcycle: bool = False,
          pad_script: bool = False, pad3d: bool = False, about_script: bool = False,
          launch_script: bool = False, tool_script: bool = False, return_script: str = "",
          popup_script: str = "", soak: str = "", soak_cheat: str = "") -> dict[str, bytes]:
    if soak == "cheats" and not popup_script:    # the popups' card (game 0's CHEATS.PAK record, its stub)
        popup_script = "cheats"
    games = _games()
    if popup_script:                             # the launch panel's card with its stub (a failed popup hands off)
        tool_script = True
    if return_script:                            # as --tool-script, then the round trip back (fakepad.c k_ret*)
        tool_script = True
    if tool_script:                              # the launch panel's card, plus the stub so the loading screen ends
        launch_script = stub = True
    if loader_script or pgload_script:           # the --loading card (dressing, stub), its own pad script
        loading = stub = True
    recs = [GameRecord(k, t, maker="OMSAT TEST", genre=g, region_flags=region_flags_from(a), players=1, year=y)
            for k, t, g, a, y, _, _ in games]
    discs = {}
    if loading:
        t, mk, yr, gn, ar, d, dt = MOCKUP_GAME
        recs[0] = GameRecord(games[0][0], t, maker=mk, genre=GENRES.index(gn), region_flags=region_flags_from(ar),
                             players=1, year=yr, disc_count=dt)
        discs[games[0][0]] = (d, dt, ar)
    for i in LONG_TITLES:                        # 2026-11-23: the long title is the user's own (SPEC §5.3 0x35), so
        recs[i].user_flags = USER_TITLE          # the list shows it (and its marquee) instead of the folder's name
    if loader_script:                            # OM_LH_JHL (menu/src/gamesdb.h): Loader = Auto says "JHL (database)"
        recs[LOADER_JHL_GAME].loader_hint = 1
    if pad_script:                               # game 0's own compat row (0x01 OM_CF_3DPAD | 0x02
                                                  # OM_CF_STDPAD), the real dataset's note as it actually reads on
                                                  # a card (PADWARN_NOTE)
        recs[0].compat_flags = 0x03
        recs[0].compat_note = PADWARN_NOTE
    if popup_script:                             # game 0 dressed as the popups' mockup game (its cover below)
        t, mk, yr, gn, ar, d, dt = MOCKUP_GAME
        recs[0] = GameRecord(games[0][0], t, maker=mk, genre=GENRES.index(gn), region_flags=region_flags_from(ar),
                             players=1, year=yr)
    ents = sorted((LibEntry(p, k, area_flags=region_flags_from(discs[k][2] if k in discs else a),
                            image_type=2 if p.endswith(".iso") else 1, folder_id=f,
                            disc=discs[k][0] if k in discs else 1, disc_total=discs[k][1] if k in discs else 1)
                   for k, _, _, a, _, f, p in games), key=lambda e: (e.folder_id, e.path.encode()))
    files: dict[str, bytes] = {p: b"" for *_, p in games}
    if game_script or pgload_script or launch_script:   # M6 Task 18 / launch panel: game 0 has a System ID, so it
                                                          # has a game id (the panel bails to a direct launch without
                                                          # one, folders.c pg_open_selected)
        files[games[0][6]] = game0_ip("MK-81307" if pgload_script else GAME0_IP["product"])
    if game_script:
        files["aether/PERGAME.INI"] = {"corrupt": PERGAME_CORRUPT, "oversize": pergame_oversize()}.get(
            pergame_seed, PERGAME_SEED.encode("ascii"))
        files[GAME_MARKER] = b"per-game panel pad script\n"
    if pgload_script:
        files[PGLOAD_MARKER] = b"panel read-ahead, then launch pad script\n"
    if pgcycle:
        files[PGCYCLE_MARKER] = b"200 A/B launch panel open/close cycles pad script\n"
    if pglong:                                   # 15 s slow panel, the music's refill check
        files[PGLONG_MARKER] = b"15 s slow panel, refill check pad script\n"
    if pad_script:
        files[PADWARN_MARKER] = b"pre-launch pad warning pad script\n"
    if pad3d:
        files[PAD3D_MARKER] = b"port 1 reads the 3D Control Pad's analog-mode id (0x16, HWF-77)\n"
    if about_script:
        files[ABOUT_MARKER] = b"settings > about pad script\n"
    if return_script:
        files[RETURN_MARKER] = b"a tool's hand-off arms the return block, then the menu starts again\n"
        files[RETURN_SCRIPTS[return_script]] = f"{return_script}: hand off, return, reopen\n".encode()
    if popup_script:
        if not soak:
            files[POPUP_SCRIPTS[popup_script]] = f"{popup_script}: the in-menu popup pad script\n".encode()
        files.update(popup_files(games[0][0], soak_cheat, saves=not soak))
    if soak:
        files[SOAK_MARKER] = soak_script(soak)
    if tool_script:
        if not return_script and not popup_script:
            files[TOOL_MARKER] = b"launch panel tools pad script (Cheats, Manage saves)\n"
        art = _mockup_artpak(games[0][0])        # game 0's cover, so the eye's page shows one
        if art:
            files["aether/ART.PAK"] = art
    elif launch_script:
        files[LAUNCH_MARKER] = b"launch panel pad script (A opens it, A again starts it)\n"
    roots = sorted({p.split("/")[0] for p in files})
    files["aether/GAMES.DB"] = write_gamesdb(recs, GENRES, {}, DATE)
    files["aether/LIBRARY.IDX"] = write_libidx(ents, FOLDERS, card_fingerprint(roots), DATE)
    files["aether/FAVS.TXT"] = "".join(games[i][6] + "\n" for i in (0, 5, 12)).encode()
    files["aether/RECENT.TXT"] = "".join(games[i][6] + "\n" for i in recent).encode()   # --recent
    if vrtiles:                                  # --no-vrtiles = a card without it (the NO ART fallback)
        files["aether/VRTILES.BIN"] = VRTILES.read_bytes()
    # --settings-ini replaces the card's SETTINGS.INI (e.g. "overscan=24\ntheme=saturn"); default unchanged
    # the default card turns Last game off, so the frozen tour and the scripts start at the root as their
    # steps assume; a --settings-ini without reselect=off gets the default (on)
    files["aether/SETTINGS.INI"] = (settings_ini.rstrip("\n") + "\n").encode() if settings_ini else \
        b"theme=saturn\nclock=24\nreselect=off\n"
    test_bg = Image.new("RGB", (320, 224), (255, 0, 0))
    test_bg.paste((0, 0, 255), (160, 0, 320, 224))                   # left red, right blue: the CC gate's reference
    files["aether/THEMES/TEST/BG.BIN"] = theme.encode_bg(test_bg, DATE)
    if scroll:
        files[SCROLL_MARKER] = b"held-scroll pad script\n"
    if loading:
        if not pgload_script:                    # PGLOAD has its own marker (above)
            files[LOADER_MARKER if loader_script else LOADING_MARKER] = (b"loader pad script\n" if loader_script
                                                                        else b"loading-screen pad script\n")
        art = _mockup_artpak(games[0][0])
        if art:
            files["aether/ART.PAK"] = art
    if stub:
        files["aether/LAUNCHER.BIN"] = launcher_stub()
    # the streamed music's test excerpt (2 blocks, 0.371 s: per-block levels and L/R tones, sound.test_music).
    # review-task-19.md reclaim recommendation: was 3 blocks (51,200 B); fake-only, no host
    # test or fake-card capture asserts the block count (only tools/tests/test_sound.py exercises test_music()
    # directly, with its own default) -- 2 still shows a level change between blocks, just not the full 3-level
    # cycle, and frees ~17 KB of FAKECARD.BIN for --app-fake's HWRAM floor without touching any product format.
    if music:                                    # --no-music: every real card today (review-sound I1's precedence check)
        files["aether/MUSIC/BGM.MUS"] = sound.test_music(blocks=2)
    if diag:
        files[DIAG_MARKERS[diag]] = f"fake diagnostics: {diag}\n".encode()
    if slowboot:
        files[SLOWBOOT_MARKER] = b"start-up steps held for captures\n"
    if sd_hw:
        files[SDHW_MARKER] = b"card reads at ~245 KB/s\n"
    if settings_script:
        files[SETTINGS_MARKER] = b"settings scroll/overscan/loader pad script\n"
    if tiles:
        files[TILES_MARKER] = b"virtual root tiles pad script\n"
    if idle:
        files[IDLE_MARKER] = b"screensaver pad script\n"
    if fw is not None:                           # the fake s_get_fw_version's string (fakesat.c)
        files[FW_MARKER] = fw.encode("ascii")
    # Task 13c round 2: LOADBG.BIN (~500 KB) no longer fits the compiled-in card; the fake build reads it from its own
    # disc (tools/sbuild.py puts it in the fake ISO; menu/app/fake/fakesat.c om_fakesat_cd_file) and serves it.
    files["aether/THEMES/TEST/THEME.INI"] = theme.theme_ini("TEST", "OMSAT", text="FFFFFF", text_hi="FFFF00",
                                                              panel="00FF00", cursor="FF00FF",
                                                              panel_alpha=panel_alpha).encode()
    if loading or tool_script:                  # the cover's room in the blob
        del files["aether/THEMES/TEST/BG.BIN"], files["aether/THEMES/TEST/THEME.INI"]
    return files


def user_card(card_dir: Path, stage_dir: Path) -> dict[str, bytes]:
    """2026-11-20 hardware report: a real card's layout and values for the fake build's tour (step 08 opens the first
    card folder, step 10 sorts it by region). card_dir holds that card's LIBRARY.IDX and GAMES.DB as cardbuild wrote
    them; stage_dir holds each LIBRARY.IDX image path as a small stand-in (a cue and the first 2048 B sector with the
    disc's System ID), so the menu reads the real IP.BIN area, date and title. GAMES.DB is cut to the card's keys (the
    full one is ~168 KB, over MAX_BLOB); the tour's FAVS/RECENT stay empty."""
    from .gamesdb import read_gamesdb
    ents, folders, _ = read_libidx((Path(card_dir) / "LIBRARY.IDX").read_bytes())
    recs, genres, aliases = read_gamesdb((Path(card_dir) / "GAMES.DB").read_bytes())
    keys = {aliases.get(e.key, e.key) for e in ents if e.key}
    files: dict[str, bytes] = {}
    for e in ents:
        base = Path(stage_dir) / e.path
        files[e.path] = base.read_bytes()
        if e.path.lower().endswith(".cue"):                       # the cue's own FILE line names its stand-in bin
            for line in files[e.path].decode("ascii", "replace").splitlines():
                if line.strip().upper().startswith("FILE "):
                    name = line.split('"')[1]
                    files[e.path.rsplit("/", 1)[0] + "/" + name] = (base.parent / name).read_bytes()
    roots = sorted({p.split("/")[0] for p in files} | {"aether"})
    files["aether/LIBRARY.IDX"] = write_libidx(ents, folders, card_fingerprint(roots), DATE)
    files["aether/GAMES.DB"] = write_gamesdb([r for r in recs if r.key in keys], genres,
                                               {a: c for a, c in aliases.items() if c in keys}, DATE)
    files["aether/SETTINGS.INI"] = b"theme=saturn\nclock=24\n"
    files["aether/VRTILES.BIN"] = VRTILES.read_bytes()             # M6 C3
    return files


def pack(files: dict[str, bytes]) -> bytes:
    names = sorted(files)
    table_end = 16 + ENT * len(names)
    table, data = bytearray(), bytearray()
    for n in names:
        raw = n.encode("utf-8")
        if len(raw) > 255:
            raise ValueError(f"path over 255 bytes: {n}")
        table += raw.ljust(256, b"\0") + struct.pack(">II", table_end + len(data), len(files[n]))
        data += files[n] + b"\0" * (-len(files[n]) % 4)
    return MAGIC + struct.pack(">II", len(names), 0) + bytes(table) + bytes(data)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--panel-alpha", type=int, default=50)
    ap.add_argument("--scroll", action="store_true",
                    help="the held-scroll pad script instead of the frozen tour (review-13a I6)")
    ap.add_argument("--loading", action="store_true",
                    help="the launch / cancel pad script (Task 13c) instead of the frozen tour")
    ap.add_argument("--no-music", action="store_true", help="no aether/MUSIC/BGM.MUS (like every real card today)")
    ap.add_argument("--launcher-stub", action="store_true",
                    help="add a LAUNCHER.BIN that passes the CRC, so a launch reaches 'Starting game...' (Task 13c)")
    ap.add_argument("--diag",
                    nargs="?", const="lines", default=None, choices=sorted(DIAG_MARKERS),
                    help="fake diagnostics (Ruling M3-R45, one flag): bare or 'lines' = the developer boot lines and "
                         "tour counters on NBG0; 'page1' / 'page2' / 'dismiss' = the diag-combo pad script that opens "
                         "the hidden diagnostics screen and idles on page 1, on page 2, or back in the Folders view")
    ap.add_argument("--slowboot", action="store_true",
                    help="hold each start-up step ~0.7 s so the interstitial can be captured (Task 13d)")
    ap.add_argument("--sd-hw", action="store_true",
                    help="every card read at the user's measured SD rate (~245 KB/s, 4 us a byte) instead of instant "
                         "(or the loading script's 1 MB/s): hardware-like loading-screen timing")
    ap.add_argument("--settings-script", action="store_true",
                    help="the Settings pad script (M6 Task 4): scrolls to Safe area, cycles overscan and Loader, "
                         "instead of the frozen tour")
    ap.add_argument("--tiles", action="store_true",
                    help="M6 C3: rest on each of the five virtual root folders in turn (their art tiles), instead of "
                         "the frozen tour")
    ap.add_argument("--idle-script", action="store_true",
                    help="M6 Task 14: the screensaver pad script (no input for ~83 s, then one A that must only wake "
                         "it); give saver=1 with --settings-ini")
    ap.add_argument("--loader-script", action="store_true",
                    help="M6 Task 17: the --loading card with the LAUNCHER.BIN stub; Favorites, launch the third game "
                         "(loading screen: CWX (auto)), then the second, whose GAMES.DB hint is JHL (JHL (database))")
    ap.add_argument("--game-script", action="store_true",
                    help="M6 Task 18: the per-game panel pad script (C on the first favorite, its loader and saves, "
                         "the Per-game list, Forget); a seeded PERGAME.INI; use with --diag lines for the file's CRC")
    ap.add_argument("--pgload-script", action="store_true",
                    help="M6 Task 18: the --loading card; the dressed game's panel open 4 s, then its launch (the "
                         "loading screen's background read ahead); use with --sd-hw --diag lines")
    ap.add_argument("--pgcycle-script", action="store_true",
                    help="the --pgload-script card, then 200 cycles of A (launch panel) and B (close), 1 s apart")
    ap.add_argument("--pglong-script", action="store_true",
                    help="M6 Task 18b fix round: as --pgload-script, but the panel stays 15 s with 8 V-blank "
                         "iterations and SD stalls; its close prints MUSR <B/s> U<underruns> PASS|FAIL")
    ap.add_argument("--pergame-seed", default="", choices=["", "corrupt", "oversize"],
                    help="M6 Task 18 (review I2): with --game-script, a corrupt (embedded NUL) or oversize (16,385 B) "
                         "PERGAME.INI instead of the two-section seed")
    ap.add_argument("--expected-pergame", action="store_true",
                    help="M6 Task 18: print the PERGAME.INI the --game-script run must end with (length, CRC-32) "
                         "and exit")
    ap.add_argument("--recent", default=None,
                    help="M6 Task 13: RECENT.TXT's games by number, most recent first (e.g. 16,7: game 16 is in "
                         "Shooters/Classic); default 7,3,20,1")
    ap.add_argument("--fw", default=None,
                    help="M6 Task 12: the Satiator firmware string the fake build reads (e.g. v158; READFAIL = the "
                         "read fails); default: no marker, a passing v163")
    ap.add_argument("--no-vrtiles", action="store_true",
                    help="M6 C3: no aether/VRTILES.BIN (the virtual root folders show NO ART and a named message)")
    ap.add_argument("--settings-ini", default=None,
                    help="M6 Task 5: the card's SETTINGS.INI text instead of the default (\\n separates lines, "
                         "e.g. \"overscan=24\\ntheme=saturn\"); the frozen tour and the scripts are unchanged")
    ap.add_argument("--user-card", nargs=2, metavar=("CARD_DIR", "STAGE_DIR"),
                    help="a real card's LIBRARY.IDX/GAMES.DB and stand-in images instead of the synthetic games "
                         "(2026-11-20 sort report); the frozen tour, no music")
    ap.add_argument("--pad-script", action="store_true",
                    help="M6 Task 19: game 0 (the first favorite) carries Golden Axe: The Duel's compat flags/note "
                         "(0x03, PADWARN_NOTE); A on Favorites then A again attempts to launch it -- with --pad3d "
                         "the warning should show, without it the launch proceeds straight away")
    ap.add_argument("--pad3d", action="store_true",
                    help="M6 Task 19: port 1 reads the Sega 3D Control Pad's analog-mode id (0x16, HWF-77) instead "
                         "of whatever Mednafen's own SMPC emulation reports; menu/app/fake/fakepad.c om_fakepad_port1_id")
    ap.add_argument("--about-script", action="store_true",
                    help="Settings > About pad script: Start, Down x15 (clamps) to the last row (About), A opens the "
                         "page, B returns to Settings")
    ap.add_argument("--launch-script", action="store_true",
                    help="the launch panel pad script (user decision 2026-11-23): Favorites, A opens the panel on "
                         "Launch (row 0), A again starts it (double-tap); needs an IP.BIN stand-in like --game-script")
    ap.add_argument("--tool-script", action="store_true",
                    help="the launch panel's Cheats and Manage saves rows, each handed off through the loading "
                         "screen (the --launch-script card plus the LAUNCHER.BIN stub)")
    ap.add_argument("--return-script", default="", choices=["", "cheats", "saves", "settings"],
                    help="as --tool-script, but the hand-off arms the return block and starts the menu again: that "
                         "start skips the intro and reopens the list and the panel (Cheats, Manage saves, or Settings > "
                         "Manage saves)")
    ap.add_argument("--popup-script", default="", choices=["", "cheats", "saves", "settings"],
                    help="the in-menu popups (2026-11-25): Cheats or Manage saves from the launch panel, or Settings > "
                         "Manage saves, over the dimmed list (the --tool-script card plus CHEATS.PAK and card saves)")
    ap.add_argument("--soak", default="", choices=["", *SOAKS],
                    help="a pad soak that never ends (fakepad.c soak_tick): cheats = the Cheats popup (toggle every "
                         "row, scroll, save and close, reopen); settings = Settings held scrolls and About in and out")
    ap.add_argument("--soak-cheat", default="",
                    help="with --soak cheats: game 0's CHEATS.PAK record is this key's from tools/data/cheats/CHEATS.PAK "
                         "(e.g. MK81200-50, Daytona USA)")
    a = ap.parse_args(argv)
    if a.expected_pergame:
        import zlib
        t = expected_game_pergame().encode("ascii")
        print(f"PG LEN {len(t)} CRC {zlib.crc32(t):08X}")
        print(t.decode("ascii"), end="")
        c = expected_corrupt_first_save().encode("ascii")
        print(f"corrupt seed: BAK {len(PERGAME_CORRUPT)} {zlib.crc32(PERGAME_CORRUPT):08X}; first save PG LEN {len(c)} "
              f"CRC {zlib.crc32(c):08X}")
        o = pergame_oversize()
        print(f"oversize seed: {len(o)} B, CRC {zlib.crc32(o):08X} (never written)")
        return 0
    if a.user_card:
        files = user_card(Path(a.user_card[0]), Path(a.user_card[1]))
    else:
        files = build(a.panel_alpha, a.scroll, a.launcher_stub, a.loading, not a.no_music, a.diag, a.slowboot,
                       a.sd_hw, a.settings_script,
                       a.settings_ini.replace("\\n", "\n") if a.settings_ini is not None else None, a.tiles,
                       not a.no_vrtiles, a.fw,
                       tuple(int(v) for v in a.recent.split(",")) if a.recent else (7, 3, 20, 1),
                       a.idle_script, loader_script=a.loader_script, game_script=a.game_script,
                       pgload_script=a.pgload_script or a.pglong_script or a.pgcycle_script,
                       pgcycle=a.pgcycle_script, pergame_seed=a.pergame_seed,
                       pglong=a.pglong_script,
                       pad_script=a.pad_script, pad3d=a.pad3d, about_script=a.about_script,
                       launch_script=a.launch_script, tool_script=a.tool_script, return_script=a.return_script,
                       popup_script=a.popup_script, soak=a.soak, soak_cheat=a.soak_cheat)
    blob = pack(files)
    if len(blob) > MAX_BLOB:
        print(f"FAKECARD {len(blob)} B exceeds MAX_BLOB {MAX_BLOB} B (plan D5)")
        return 1
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_bytes(blob)
    print(f"FAKECARD: {len(blob)} B, {len(files)} files"
          f"{' (held-scroll script)' if a.scroll else ''}{' (loading script)' if a.loading else ''}"
          f"{' (LAUNCHER.BIN stub)' if a.launcher_stub else ''}{f' (diag {a.diag})' if a.diag else ''}"
          f"{' (slow boot)' if a.slowboot else ''}{' (SD at hardware rate)' if a.sd_hw else ''}"
          f"{' (settings script)' if a.settings_script else ''}{' (tiles script)' if a.tiles else ''}"
          f"{' (about script)' if a.about_script else ''}{' (launch script)' if a.launch_script else ''}"
          f"{' (no VRTILES.BIN)' if a.no_vrtiles else ''}{f' (fw {a.fw!r})' if a.fw is not None else ''}"
          f"{' (SETTINGS.INI ' + repr(a.settings_ini) + ')' if a.settings_ini else ''} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
