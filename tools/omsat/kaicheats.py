"""PSKai v6.545's cheat database, converted once into this repo (contract/SPEC.md §5.15).

The data: `tools/data/cheats/kai_cheats.txt`, the dataset in the public v6.545 release's `SatCom_src_20240114.7z`
(SHA-256 28675b8d...; 122 games, 428 cheats, 502 codes, CodeGen generation date 2016/08/02 -- the user's choice until
universal cheats work), converted once by `python -m omsat.kaicheats convert DIR`. The 447-game tables of the full
source tree (generation date 2024/01/13; its hand-reviewed keymap is kept as `kai_keymap_2024.json`) convert and pack
the same way, by path. Both are cafe-alpha's, credited in CREDITS.md. Nothing here reads the upstream files any more,
and nothing is downloaded.

Kai's per-code `cmd` byte (`docs/research/m6-research.md` §6): 0x00 one-shot write at boot, 0x01 continuous 16-bit
write, 0x03 continuous 8-bit write, 0x0D enable/condition code. A few codes carry 0x08 or 0x0F; they are stored
verbatim and the cheat engine decides which it runs.

`python -m omsat.kaicheats map [--gamesdb GAMES.DB]` rebuilds `tools/data/cheats/kai_keymap.json`: for each Kai game,
the GAMES.DB product key(s) it serves, `how` (`exact`, `fuzzy:<score>`, `unmatched`), its `location`, `jhl_only`
(PSKai's autoset.c forces it to JHL, cross-referenced by IP CRC through `tools/data/compat/loader_compat.json`) and
`igr_conflict` (`location` inside Aether's in-game-reset stub). `OVERRIDES` holds the hand-reviewed corrections.

`python -m omsat.kaicheats pak` rebuilds the committed `tools/data/cheats/CHEATS.PAK` (omsat.cheatpak) from the text
source and the keymap; `cardbuild` copies it to the card, merged with the card's own copy.
"""
from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from .gamesdb import REGION_BITS, read_gamesdb

ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = ROOT / "tools" / "data" / "cheats" / "kai_cheats.txt"
PAK_PATH = ROOT / "tools" / "data" / "cheats" / "CHEATS.PAK"
KEYMAP_PATH = ROOT / "tools" / "data" / "cheats" / "kai_keymap.json"
LOADER_COMPAT_PATH = ROOT / "tools" / "data" / "compat" / "loader_compat.json"

# Aether's in-game-reset stub (menu/launcher/igr.h OM_IGR_STUB/OM_IGR_STUB_END), the measured link
# range from task-20a-report.md section 3. It occupies the same Kai-documented cheat-handler location most of
# this database's games use (0x06002800); the full Kai budget at that location runs to 0x06002D48.
IGR_STUB_START = 0x06002800
IGR_STUB_END = 0x06002BC4

CT_PARAM_BF_DEFAULT_ON = 0x01
CT_PARAM_BF_GREYOUT = 0x80     # a sub-menu header in Kai's menu: no codes of its own (tested: every such row)

OM_CH_MAX_CHEATS = 64
OM_CH_MAX_CODES = 256


# --------------------------------------------------------------------------------------------------------------
# Source data model (pseudosat_kai/cheat_codes/cheats_codes.h: ct_code_t / ct_cheat_t / ct_game_t)
# --------------------------------------------------------------------------------------------------------------


@dataclass
class KaiCode:
    addr: int
    value: int
    cmd: int


@dataclass
class KaiCheat:
    name: str
    first_code: int
    num_codes: int
    flags: int


@dataclass
class KaiGame:
    name: str
    ip_crc: list[int]
    master_addr: int
    master_opc: int
    num_cheats: int
    cfg_location: int
    unused_u8: int
    cache_4_capacity: int
    first_cheat: int


@dataclass
class KaiDB:
    games: list[KaiGame]
    cheats: list[KaiCheat]
    codes: list[KaiCode]

    def cheats_for(self, g: KaiGame) -> list[KaiCheat]:
        return self.cheats[g.first_cheat:g.first_cheat + g.num_cheats]

    def codes_for(self, c: KaiCheat) -> list[KaiCode]:
        return self.codes[c.first_code:c.first_code + c.num_codes]


_DEFS_RE = re.compile(r"#define CT_(GAMECNT|CHEATCNT|CODECNT)\s+(\d+)")
_CODE_RE = re.compile(r"\{\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*0\s*\}")
_CHEAT_RE = re.compile(r'\{"((?:[^"\\]|\\.)*)",\s*(\d+),\s*(\d+),\s*(0x[0-9A-Fa-f]+|\d+)\}')
_GAME_RE = re.compile(
    r'\{"((?:[^"\\]|\\.)*)",\s*'
    r"\{([^}]*)\},\s*"
    r"(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(\d+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*([0-9/]+),\s*(\d+)\s*\}",
    re.S,
)


def load_c_tables(src: Path) -> KaiDB:
    """The one-time conversion's input (M6 Task 26b): PSKai's three CodeGen data files
    (`pseudosat_kai/cheat_codes/cheats_full_data_{defs.h,ram.c,rom.c}`) in a directory the caller names -- this repo
    no longer carries them. Parses them with simple regexes (they are machine-generated C initializer lists in a
    very regular shape; a full C parser would be overkill and would risk silently accepting a shape change). Counts
    are cross-checked against `cheats_full_data_defs.h`'s own `CT_*CNT` constants, same as the brief's Step 1 test."""
    defs_txt = (src / "cheats_full_data_defs.h").read_text(encoding="utf-8")
    defs = {k: int(v) for k, v in _DEFS_RE.findall(defs_txt)}

    rom_txt = (src / "cheats_full_data_rom.c").read_text(encoding="utf-8")
    codes = [KaiCode(int(a, 16), int(v, 16), int(c, 16)) for a, v, c in _CODE_RE.findall(rom_txt)]
    if len(codes) != defs.get("CODECNT"):
        raise ValueError(f"parsed {len(codes)} codes, CT_CODECNT says {defs.get('CODECNT')}")

    ram_txt = (src / "cheats_full_data_ram.c").read_text(encoding="utf-8")
    i_cheats = ram_txt.index("_ct_cheats_data")
    i_games = ram_txt.index("_ct_games_data")
    cheats = [KaiCheat(_unescape(n), int(fc), int(nc), int(fl, 16) if fl.startswith("0x") else int(fl))
              for n, fc, nc, fl in _CHEAT_RE.findall(ram_txt[i_cheats:i_games])]
    if len(cheats) != defs.get("CHEATCNT"):
        raise ValueError(f"parsed {len(cheats)} cheats, CT_CHEATCNT says {defs.get('CHEATCNT')}")

    games = []
    for name, crcs, maddr, mopc, ncheat, loc, unused, cache_expr, fcid in _GAME_RE.findall(ram_txt[i_games:]):
        ip_crc = [int(x.strip(), 16) for x in crcs.split(",") if x.strip()]
        # cache_expr is a literal C source expression, e.g. "156/4" (measured: always "/4" in this dataset,
        # matching cheats_codes.h's "Unit: 4 codes (32 bytes)" comment) -- the struct field is the compiler's
        # *computed* integer-division result, not the numerator alone (fix round 1: an earlier version took
        # just the numerator, e.g. 156 instead of 39, silently inflating every game's reported cache capacity).
        cache_parts = cache_expr.split("/")
        cap4 = int(cache_parts[0]) // int(cache_parts[1]) if len(cache_parts) == 2 else int(cache_parts[0])
        games.append(KaiGame(_unescape(name), ip_crc, int(maddr, 16), int(mopc, 16), int(ncheat), int(loc, 16),
                              int(unused, 16), cap4, int(fcid)))
    if len(games) != defs.get("GAMECNT"):
        raise ValueError(f"parsed {len(games)} games, CT_GAMECNT says {defs.get('GAMECNT')}")

    return KaiDB(games, cheats, codes)



def _unescape(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


# --------------------------------------------------------------------------------------------------------------
# The committed text source (tools/data/cheats/kai_cheats.txt): every Kai game, matched or not
# --------------------------------------------------------------------------------------------------------------

def source_header(db: KaiDB, generated: str) -> str:
    return (
        "# Cheat database from Pseudo Saturn Kai v6.545 by cafe-alpha (https://ppcenter.webou.net/pskai/): its CodeGen\n"
        f"# tables, generation date {generated}: {len(db.games)} games, {len(db.cheats)} cheats, {len(db.codes)} codes.\n"
        "# Credited in CREDITS.md; the PSKai source tree is GPL-2.0 (LICENSES/GPL-2.0.txt).\n"
        "# Converted once by tools/omsat/kaicheats.py; CHEATS.PAK is built from this file and a keymap.\n"
        "# [game]: name, crc (Kai's 8 IP CRCs), master=addr:opcode, location, cache (4-code units), unused;\n"
        "# cheat=<Kai flags, hex>:<name> starts a cheat and its code=<addr>:<value>:<cmd> lines follow.\n"
    )


def generation_date(src: Path) -> str:
    m = re.search(r"Generation date : ([0-9/]+)", (src / "cheats_full_data_defs.h").read_text(encoding="utf-8"))
    return m.group(1) if m else "unknown"


def dump_text(db: KaiDB, generated: str) -> str:
    out = [source_header(db, generated)]
    for g in db.games:
        out.append("[game]\n")
        out.append(f"name={g.name}\n")
        out.append("crc=" + ",".join(f"{c:08X}" for c in g.ip_crc) + "\n")
        out.append(f"master={g.master_addr:08X}:{g.master_opc:04X}\n")
        out.append(f"location={g.cfg_location:08X}\n")
        out.append(f"cache={g.cache_4_capacity}\n")
        if g.unused_u8:
            out.append(f"unused={g.unused_u8:02X}\n")
        for ch in db.cheats_for(g):
            out.append(f"cheat={ch.flags:02X}:{ch.name}\n")
            for c in db.codes_for(ch):
                out.append(f"code={c.addr:08X}:{c.value:04X}:{c.cmd:02X}\n")
    return "".join(out)


def parse_text(text: str) -> KaiDB:
    games: list[KaiGame] = []
    cheats: list[KaiCheat] = []
    codes: list[KaiCode] = []
    g = ch = None
    for i, line in enumerate(text.split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        if line == "[game]":
            g = KaiGame("", [], 0, 0, 0, 0, 0, 0, len(cheats))
            games.append(g)
            ch = None
            continue
        key, sep, val = line.partition("=")
        if not sep or g is None:
            raise ValueError(f"kai_cheats.txt line {i}: {line!r}")
        if key == "name":
            g.name = val
        elif key == "crc":
            g.ip_crc = [int(x, 16) for x in val.split(",")]
        elif key == "master":
            a, v = val.split(":")
            g.master_addr, g.master_opc = int(a, 16), int(v, 16)
        elif key == "location":
            g.cfg_location = int(val, 16)
        elif key == "cache":
            g.cache_4_capacity = int(val)
        elif key == "unused":
            g.unused_u8 = int(val, 16)
        elif key == "cheat":
            fl, _, name = val.partition(":")
            ch = KaiCheat(name, len(codes), 0, int(fl, 16))
            cheats.append(ch)
            g.num_cheats += 1
        elif key == "code":
            if ch is None:
                raise ValueError(f"kai_cheats.txt line {i}: code before any cheat")
            a, v, c = val.split(":")
            codes.append(KaiCode(int(a, 16), int(v, 16), int(c, 16)))
            ch.num_codes += 1
        else:
            raise ValueError(f"kai_cheats.txt line {i}: unknown key {key!r}")
    return KaiDB(games, cheats, codes)


def load_db(path: Path | None = None) -> KaiDB:
    return parse_text((path or SOURCE_PATH).read_text(encoding="ascii"))


def fits_om_ch_file(cheats: list[KaiCheat], total_codes: int) -> bool:
    return len(cheats) <= OM_CH_MAX_CHEATS and total_codes <= OM_CH_MAX_CODES


# --------------------------------------------------------------------------------------------------------------
# Title matching (Step 5): Kai game name -> GAMES.DB product key(s)
# --------------------------------------------------------------------------------------------------------------

_REGION_TOKEN = {"USA": "U", "JPN": "J", "JAP": "J", "UK": "E", "EUR": "E", "FRA": "E", "GER": "E", "ESP": "E", "ITA": "E"}
_TAG_RE = re.compile(r"^(.*?)\s*-\s*([A-Za-z0-9/.,& ]+)$")
_BRACKET_RE = re.compile(r"\s*\[[^\]]*\]\s*")
_DISC_RE = re.compile(r"\s*\(disc \d+\)\s*", re.I)
_PUNCT_RE = re.compile(r"[^a-z0-9]+")
_LEADING_THE_RE = re.compile(r"^the\s+")
_SPECIAL_BUILD_RE = re.compile(r"\b(demo|prototype)s?\b", re.I)

# Hand-reviewed corrections (Step 5: "review every fuzzy row by hand ... fix or mark unmatched"), found by checking
# each automatic fuzzy/ambiguous match's actual GAMES.DB title text. An empty list forces "unmatched" (either the
# automatic match was a different game entirely, or the title is genuinely absent from this GAMES.DB snapshot).
OVERRIDES: dict[str, list[str]] = {
    # Coincidental word-overlap fuzzy matches that pointed at the wrong game entirely:
    "Assault Suit Leynos 2": ["T-2501G"],              # not "Assault Rigs"
    "Farland Story": ["T-32505G"],                     # not "Farland Saga" (a different, later game)
    "Gulliver Boy": ["T-14303G"],                       # not "Linkle Liver Story"
    "Pretty Fighter X": ["T-15001G"],                   # not "Pocket Fighter"
    "Revolution X - USA": ["T-8107H"],                  # not "World Evolution Soccer"
    "Vandal Hearts": ["T-9526G"],                       # not "Steam-Heart's"
    "Virtua Racing - USA": ["T-4801H"],                 # not "Virtual Casino"
    "Advanced World War": ["GS-9087"],                  # not "Rampage World Tour"
    "Blue Seed": ["GS-9014"],                           # not "Cyber Speedway"
    "Dragon Force II": ["GS-9184"],                     # not "Dragon Force" (the first game)
    "Magic School Lunar!": ["T-27902G"],                # "Mahou Gakuen Lunar!" = "Magic School Lunar!"; not "Magic Carpet"
    # "Exhumed" (EUR title) and "PowerSlave" (USA title) are the same game but different GAMES.DB keys/regions:
    "Exhumed / PowerSlave - EUR": ["81084-50"],
    "Exhumed / PowerSlave - USA": ["T-13205H"],
    # Not present in this project's GAMES.DB snapshot (checked by substring search; genuinely absent, not a typo):
    "Legend of Heroes III: The Moonlight Witch": [],    # only "I & II" is in GAMES.DB
    "Linda Cube": [],
    "CYBERSLED - USA": [],
    "Legend of the Swordsmaster": [],
    # Review fix round 1 (CRITICAL): "Mega Man 8 - USA" fuzzy-matched (score 0.86) to T-1219H, which is actually
    # "Mega Man X4" (region4=U) -- a completely different game. GAMES.DB's real "Mega Man 8" USA entry is
    # T-1216H, title_en "Mega Man 8 - Anniversary Collector's Edition" (region4=U). Source: `build/db/GAMES.DB`
    # via `read_gamesdb`, cross-checked by direct query this session (no title_rom difference). Bonus fixes
    # found during the same audit (not flagged by the review, but the same root cause): "Mega Man 8 - JPN" and
    # "Mega Man X4 - JPN" were unmatched because GAMES.DB catalogs the JP releases under their JP titles
    # ("Rockman", not "Mega Man"), which the automatic matcher's exact-title path never tries without a
    # cross-reference; corrected here the same way, by direct GAMES.DB lookup:
    "Mega Man 8 - USA": ["T-1216H"],       # title_en "Mega Man 8 - Anniversary Collector's Edition", region4=U
    "Mega Man 8 - JPN": ["T-1214G"],       # title_en "Rockman 8 ~Metal Heroes~", region1=J
    "Mega Man X4 - JPN": ["T-1221G"],      # title_en "Rockman X4", region1=J
    # Special-build discs (promo/demo) that do not share the retail release's product code or, in all likelihood,
    # its cheat addresses (different IP header/build):
    "Christmas NiGHTS into Dreams - EUR/USA": [],
    "Christmas NiGHTS into Dreams - JPN": [],
    "Shining Force III sc1 - JPN [v1.006]": [],         # JP-only scenario 2/3 SKUs are not the retail Scenario 1 key
    "Shining Force III sc2 - JPN [v1.003]": [],
    "Shining Force III sc3 - JPN": [],
    # Second review pass -- more coincidental word-overlap fuzzy matches:
    "Arcade Gears Vol. 3: Wonder 3": ["T-26107G"],      # not "Game-Ware Vol.3"
    "Chaos Seed": ["T-30902G"],                         # not "Darkseed"
    "Fist of the Northstar": ["T-20601G"],              # "Hokuto no Ken"; not "Jewels of the Oracle"
    "Gekirindan": ["T-7008G"],                          # not "Grid Run"
    "Initial D": ["T-25503G"],                          # not "Winning Post 2 Final '97"
    "Mobile Suit Gundam Side Story Vol. 3": ["T-13313G"],   # "Gaiden III"; not Vol. 1's own key (T-13307G)
    "Mobile Suit Zeta Gundam Vol. 1": ["T-13315G"],     # "Zenpen Zeta no Kodou"; not "Sega Ages Vol.1"
    "Mobile Suit Zeta Gundam Vol. 2": ["T-13320G"],     # "Kouhen Sora wo Kakeru"
    "Samurai Shodown III: Blades of Blood": ["T-3106G"],    # "Samurai Spirits Zankurou Musouken"; not "Taikou Risshiden II"
    "Tour Party": ["T-10312G"],                         # not "Choro Q Park"
    # Not present in this project's GAMES.DB snapshot (checked by substring search):
    "Puzzle & Action: Treasure Hunt": [],               # not "Elevator Action2 Returns" (word-overlap on "action")
    "Samurai Shodown RPG": [],                          # not "Yoshimura Shougi"
    "Shin-Oh-Ken / Ragnagard": [],                      # not "Shin Theme Park"
    "Tamagotchi Park": [],                              # not "Theme Park" (word-overlap on "Park")
    "Warriors of Fate": [],                             # not "MechWarrior 2" (word-overlap on "Warrior")
    # PSKai's own utility entry, not a game a card would ever carry a cheat-relevant copy of:
    "Saturn Save Data Manager": [],                     # not "Saturn Bomberman" (word-overlap on "Saturn")
    "Convenience Store 2": [],                          # not "Soviet Strike" (character-level coincidence, no shared words)
    # --- the 2016/08/02 dataset (the public v6.545 release's 122 games): every fuzzy and unmatched row
    # reviewed by hand against GAMES.DB's titles and regions. Ambiguous titles (two plausible releases) stay unmatched:
    # a wrong match pokes another game's addresses.
    "ASTAL - JAP": ["GS-9019"],                         # "Kisuishou Densetsu Astal"
    "BLUE SEED - JAP": ["GS-9014"],
    "CLASSMATE IF - JAP": [],                           # not in GAMES.DB (not "Assault Rigs")
    "CLOCKWORK KNIGHT -  JAP": ["GS-9004"],             # "~Pepperouchau no Daibouken Joukan~"
    "CLOCKWORK KNIGHT 2 - JAP": ["GS-9029"],            # "~Pepperouchau no Daibouken Gekan~"
    "CYBERSLED - USA": [],
    "F1 CHALLENGE -  UK": [],                           # GAMES.DB has only the US disc (MK-81206)
    "LAST GLADIATORS - JAP": ["T-18901G"],              # "Digital Pinball Last Gladiators" (not Ver.9.7)
    "NIGHT WARRIORS - USA": ["T-1208H"],
    "OUTLAWS OF THE LOST DYNASTY - JAP": [],
    "PRETTY FIGHTER X - JAP": ["T-15001G"],             # not "Pocket Fighter"
    "SLAM-N-JAM - USA": ["T-15902H"],                   # not "Space Jam"
    "VAMPIRE HUNTER - JAP": ["T-1202G"],                # not "Bakuretsu Hunter"
    "SHINOBI - JAP": ["GS-9010"],                       # "Shin Shinobi Den"
    "SHINOBI - USA": ["T-2301H"],                       # "Shinobi Legions"
    "STREET FIGHTER MOVIE - JAP": ["T-1201G"],          # "Real Battle on Film"; not the anime "Street Fighter II Movie"
    "STREET FIGHTER MOVIE - USA": ["T-8105H"],
    "SLAMDUNK - JAP": ["T-13301G"],
    "PARODIUS - JAP": [],                               # Gokujou Deluxe Pack or Jikkyou Oshaberi: ambiguous
    "VIRTUA RACING - USA": ["T-4801H"],                 # not "Virtual Casino"
    "VIRTUA RACING -  UK": ["T-4801H-50"],
    "VIRTUA FIGHTER REMIX - USA": [],                   # two US discs (81023 purple, 81028 blue): ambiguous
    "HI-OCTANE - USA": ["T-5002H"],
    "WINGARMS - JAP": ["GS-9038"],
    "REVOLUTION X - USA": ["T-8107H"],
    "MEGAMAN X3 - JAP": ["T-1210G"],                    # "Rockman X3"
    "DAYTONA USA - USA": ["81200"],                     # 81606 is a Virtua Cop / VF2 serial; Daytona's disc reads 81200
    "SEGA RALLY - USA": ["81207"],                      # 80012 is an alias of 81207 (GAMES.DB, 448a49e); the disc reads MK-81207
    "SEGA RALLY -  UK": ["MK81207-50"],
    "CORPSE KILLER - USA": ["T-16201H"],                # "Graveyard Edition", the Saturn release; not "Crypt Killer"
    "NBA JAM TE - USA": ["T-8102H"],                    # "Tournament Edition"; not "NBA Jam Extreme"
    "RED HEAT - JAP": [],                               # not in GAMES.DB (not "Winter Heat")
    "NFL QUARTERBACK CLUB - USA": [],                   # '96 or '97: ambiguous
    "NHL QUARTERBACK CLUB - USA": [],                   # no such game; a mislabelled entry
    "PANZER DRAGOON 2 - USA": ["81022"],                # "Panzer Dragoon Zwei"
    "GREATEST NINE - JAP": [],                          # the 1995 game or '96: ambiguous
    "IDOL JANSI SUCHIE (PS) - JAP": [],                 # "(PS)": Special? ambiguous, not "Suchie-Pai II"
}


def _norm(s: str) -> str:
    s = _PUNCT_RE.sub(" ", s.lower()).strip()
    s = re.sub(r"\s+", " ", s)
    return _LEADING_THE_RE.sub("", s)


def _split_tag(name: str) -> tuple[str, str]:
    """Returns (base_title, region_letters). region_letters is "" when `name` has no recognized trailing
    " - REGION" tag (most of the 2024 set's 447 names have none at all; the ones that do use "USA"/"JPN"/"EUR" and
    combinations like "EUR/USA", sometimes mixed with unrelated tokens like "Demo"/"Prototypes"/"Vol. 7")."""
    m = _TAG_RE.match(name)
    if not m:
        return name, ""
    base, tag = m.group(1), m.group(2)
    letters = set()
    for tok in re.split(r"[/,]", tag):
        tok = tok.strip().upper()
        if tok in _REGION_TOKEN:
            letters.add(_REGION_TOKEN[tok])
    return (base, "".join(sorted(letters))) if letters else (name, "")


def _clean_base(base: str) -> str:
    base = _DISC_RE.sub(" ", base)
    base = _BRACKET_RE.sub(" ", base)
    return base.strip()


@dataclass
class MatchResult:
    products: list[str]
    how: str


def build_title_index(recs) -> dict[str, list]:
    idx: dict[str, list] = {}
    for r in recs:
        idx.setdefault(_norm(r.title_en), []).append(r)
        if r.title_rom and r.title_rom != r.title_en:
            idx.setdefault(_norm(r.title_rom), []).append(r)
    return idx


def _region_filter(cands, letters: str):
    """Filters `cands` to those whose region_flags overlap `letters`. An explicit region tag that matches none of
    the title-exact candidates is reported as such (empty) rather than silently falling back to the unfiltered,
    possibly-wrong-region set (fix round 1: the prototype used to fall back here, which matched "ASTAL - JPN" to
    a US/Brazil key because no JP "Astal" key exists in this GAMES.DB snapshot)."""
    if not letters:
        return cands
    bits = sum(1 << REGION_BITS[c] for c in letters if c in REGION_BITS)
    return [c for c in cands if c.region_flags & bits]


def match_game(name: str, idx: dict[str, list], all_norms: list[str]) -> MatchResult:
    if name in OVERRIDES:
        ov = OVERRIDES[name]
        return MatchResult(list(ov), "exact" if ov else "unmatched")

    base, letters = _split_tag(name)
    base = _clean_base(base)
    # review finding: the special-build check used to run on the whole raw `name`, which wrongly
    # force-unmatched legitimate multi-region entries whose *tag* happens to also cover demo/prototype builds
    # (e.g. "Burning Rangers - EUR/JPN/USA/Demo/Prototypes" -- a real retail-game entry, not a demo disc). Running
    # it on `base` only (after `-` tag splitting) still correctly excludes genuine demo discs, whose "Demo"/
    # "Prototype" word is part of the title itself, not the tag (e.g. "Panzer Dragoon Demo - USA").
    if _SPECIAL_BUILD_RE.search(base):
        return MatchResult([], "unmatched")
    nb = _norm(base)
    cands = idx.get(nb, [])
    if letters and cands:
        cands = _region_filter(cands, letters)
    if cands:
        return MatchResult(sorted({c.key for c in cands}), "exact")

    best = difflib.get_close_matches(nb, all_norms, n=1, cutoff=0.5)
    if not best:
        return MatchResult([], "unmatched")
    score = difflib.SequenceMatcher(None, nb, best[0]).ratio()
    fc = idx[best[0]]
    if letters:
        fc = _region_filter(fc, letters)
    if not fc:
        return MatchResult([], "unmatched")
    return MatchResult(sorted({c.key for c in fc}), f"fuzzy:{score:.2f}")


def _claim_rank(how: str) -> float:
    """Higher wins. "exact" outranks every "fuzzy:<score>"; among fuzzy claims, the higher score wins."""
    return 2.0 if how == "exact" else float(how.split(":", 1)[1])


def _resolve_collisions(rows: list[dict]) -> dict:
    """Review fix round 1 (CRITICAL finding): two different Kai game entries can independently match the same
    GAMES.DB product (e.g. a wrong fuzzy match landing on the same key as a different game's correct exact
    match, or two genuinely-the-same-retail-game catalog entries such as a version-patch pair). Silently
    keeping whichever one happened to iterate first (the old per-game-file seeder's `dest.exists()`
    skip) could seed one game's cheat codes onto a completely different game. This resolves every such
    collision deterministically instead: the claim with the strictly highest `_claim_rank` wins and keeps the
    product in its own row; every other claim has the product removed from its row's `products` (so it is
    never seeded for them). A genuine tie at the top rank (e.g. two "exact" claims, as with two version-patch
    entries for the same title) resolves to *no* winner -- the product is removed from every claimant's row,
    never seeded for any of them, until a human disambiguates (an `OVERRIDES` entry). Returns the `collisions`
    dict: `{product: {"claims": [{"kai_name", "how"}, ...], "resolved": <kai_name> | None}}`, for every product
    claimed by more than one distinct Kai game name (regardless of whether it was resolved)."""
    claimants: dict[str, list[int]] = {}
    for i, row in enumerate(rows):
        for p in row["products"]:
            claimants.setdefault(p, []).append(i)

    collisions: dict[str, dict] = {}
    for product, idxs in claimants.items():
        names = {rows[i]["kai_name"] for i in idxs}
        if len(names) < 2:
            continue
        ranked = sorted(idxs, key=lambda i: _claim_rank(rows[i]["how"]), reverse=True)
        top_rank = _claim_rank(rows[ranked[0]]["how"])
        tied = [i for i in idxs if _claim_rank(rows[i]["how"]) == top_rank]
        winner = ranked[0] if len(tied) == 1 else None
        collisions[product] = {
            "claims": [{"kai_name": rows[i]["kai_name"], "how": rows[i]["how"]} for i in idxs],
            "resolved": rows[winner]["kai_name"] if winner is not None else None,
        }
        for i in idxs:
            if i != winner:
                rows[i]["products"] = [p for p in rows[i]["products"] if p != product]
    return collisions


def build_keymap(db: KaiDB, gamesdb_bytes: bytes, loader_compat: dict[str, dict] | None = None) -> dict:
    recs, _, _ = read_gamesdb(gamesdb_bytes)
    idx = build_title_index(recs)
    all_norms = sorted(idx.keys())
    crc_to_loader = {}
    if loader_compat:
        for g in loader_compat.get("games", []):
            crc = g.get("ip_crc")
            if crc:
                crc_to_loader[int(crc, 16)] = g.get("loader")

    rows = []
    counts = {"exact": 0, "fuzzy": 0, "unmatched": 0, "products": 0, "jhl_only": 0, "igr_conflict": 0}
    for g in db.games:
        mr = match_game(g.name, idx, all_norms)
        how = "unmatched" if mr.how == "unmatched" else ("fuzzy" if mr.how.startswith("fuzzy") else "exact")
        counts[how] += 1
        jhl_only = any(crc_to_loader.get(c) == "jhl" for c in g.ip_crc)
        igr_conflict = IGR_STUB_START <= g.cfg_location < IGR_STUB_END
        if jhl_only:
            counts["jhl_only"] += 1
        if igr_conflict:
            counts["igr_conflict"] += 1
        rows.append({
            "kai_name": g.name,
            "products": mr.products,
            "how": mr.how,
            "location": f"{g.cfg_location:08X}",
            "jhl_only": jhl_only,
            "igr_conflict": igr_conflict,
            "num_cheats": g.num_cheats,
        })
    collisions = _resolve_collisions(rows)
    counts["products"] = sum(len(row["products"]) for row in rows)
    counts["collisions"] = len(collisions)
    counts["collisions_unresolved"] = sum(1 for c in collisions.values() if c["resolved"] is None)
    return {"counts": counts, "games": rows, "collisions": collisions}


# --------------------------------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------------------------------


def load_loader_compat() -> dict | None:
    return json.loads(LOADER_COMPAT_PATH.read_text(encoding="utf-8")) if LOADER_COMPAT_PATH.is_file() else None


def cmd_map(argv) -> int:
    db = load_db(Path(argv.source))
    gamesdb_path = Path(argv.gamesdb) if argv.gamesdb else ROOT / "build" / "db" / "GAMES.DB"
    km = build_keymap(db, gamesdb_path.read_bytes(), load_loader_compat())
    out = Path(argv.keymap)
    out.write_text(json.dumps(km, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(km["counts"], indent=2))
    return 0


def cmd_convert(argv) -> int:
    src = Path(argv.src)
    db = load_c_tables(src)
    out = Path(argv.out)
    out.write_text(dump_text(db, generation_date(src)), encoding="ascii", newline="\n")
    print(f"{out}: {len(db.games)} games, {len(db.cheats)} cheats, {len(db.codes)} codes")
    return 0


def cmd_pak(argv) -> int:
    from .cheatpak import build_from_source
    data = build_from_source(Path(argv.source), Path(argv.keymap))
    Path(argv.out).write_bytes(data)
    print(f"{argv.out}: {len(data)} B")
    return 0


def main(argv=None) -> int:
    """Each subcommand defaults to the shipped dataset (SOURCE_PATH / KEYMAP_PATH / PAK_PATH); pass the paths of
    another conversion (e.g. the 447-game 2024 tables) to build from it instead."""
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("map", help="text source + GAMES.DB -> keymap")
    m.add_argument("--source", default=str(SOURCE_PATH))
    m.add_argument("--keymap", default=str(KEYMAP_PATH))
    m.add_argument("--gamesdb")
    c = sub.add_parser("convert", help="one-time: PSKai's CodeGen tables in SRC -> a text source")
    c.add_argument("src")
    c.add_argument("--out", default=str(SOURCE_PATH))
    p = sub.add_parser("pak", help="text source + keymap -> CHEATS.PAK")
    p.add_argument("--source", default=str(SOURCE_PATH))
    p.add_argument("--keymap", default=str(KEYMAP_PATH))
    p.add_argument("--out", default=str(PAK_PATH))
    a = ap.parse_args(argv)
    return {"map": cmd_map, "convert": cmd_convert, "pak": cmd_pak}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
