"""GAMES.DB sources beyond the Cover Project sheet, merged by `omsat.coverproject` after the sheet:

  libretro DAT   -- libretro-database `metadat/redump/Sega - Saturn.dat` (CC-BY-SA-4.0, credited in CREDITS.md):
                    a record for every serial the sheet does not know, titled from the DAT's game name;
  disc aliases   -- tools/data/gamesdb/disc_aliases.json: product codes read from real discs' IP.BIN (or from Kai's
                    IP dumps) that differ from the catalogue number the sheet keys the game by;
  save tools     -- tools/data/compat/save_tools.json: a record per save-tool product code, so OM_CF_SAVETOOL lands;
  same disc      -- keys the DAT shows are one disc (identical track-1 size and CRC) with identical title and region
                    are one record when the code that disc's IP.BIN carries is measured; the others become aliases.

The sheet always wins: a DAT serial that the §6 matcher already resolves against the sheet adds nothing. Every key
goes through omsat.keys / omsat.match, the same functions the card scan, the compat data and the cheat keymap use."""
from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

from .gamesdb import GameRecord, region_flags_from
from .keys import normalise, split
from .match import Matcher

DAT_COMMIT = "c6f7d24ccfaa0c5b39014ba2bce9fb67ef3ae7e7"           # libretro-database, 2026-11-14 ("version 2026.11.14")
DAT_URL = ("https://raw.githubusercontent.com/libretro/libretro-database/" + DAT_COMMIT
           + "/metadat/redump/Sega%20-%20Saturn.dat")
DAT_SHA256 = "c35e633b092cb4c6954ff466a6f3ff53a81bcb087ad74a50e9fabf68c5443b49"
_DATA = Path(__file__).resolve().parents[1] / "data"
DEFAULT_DAT = Path(__file__).resolve().parents[2] / "build" / "thirdparty" / "libretro-saturn-redump.dat"   # a cache
DISC_ALIASES = _DATA / "gamesdb" / "disc_aliases.json"
SAVE_TOOLS = _DATA / "compat" / "save_tools.json"

# DAT region words -> ST-040-R4 §4.3 area symbols (Korea, Taiwan and Hong Kong are "T", Asia NTSC)
_AREA = {"japan": "J", "usa": "U", "brazil": "B", "korea": "T", "taiwan": "T", "hong kong": "T", "asia": "T",
         "europe": "E", "germany": "E", "france": "E", "spain": "E", "italy": "E", "finland": "E", "uk": "E",
         "netherlands": "E", "sweden": "E", "australia": "E"}
_GAME = re.compile(r"^game \((.*?)^\)", re.M | re.S)
_DISC_INDEX = re.compile(r"(-[A-Z])?-{1,2}\d$")                   # "T-21301G-0", "MK81076-50--1", "T-8106H-50-E-1"
_TRACK1 = re.compile(r'rom \( name "[^"]*\(Track 0?1\)\.bin" size (\d+) crc ([0-9A-Fa-f]{8})')
_ARTICLE = re.compile(r"^(.*?), (The|A|An)( - .*)?$")


@dataclass(frozen=True)
class DatGame:
    name: str
    area: str
    serial: str          # normalised, disc index removed
    track1: tuple[int, str] | None


def fetch_dat(dest: Path) -> Path:
    """Downloads the pinned DAT to `dest` (a cache: an existing file with the pinned hash is reused)."""
    dest = Path(dest)
    if dest.is_file() and hashlib.sha256(dest.read_bytes()).hexdigest() == DAT_SHA256:
        return dest
    import urllib.request
    req = urllib.request.Request(DAT_URL, headers={"User-Agent": "Aether-omsat/0.1 (GAMES.DB build)"})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    if hashlib.sha256(data).hexdigest() != DAT_SHA256:
        raise ValueError(f"{DAT_URL}: sha256 differs from the pinned {DAT_SHA256}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    return dest


_REGION_WORDS = set(_AREA) | {"world", "asia"}
_DROP_TAG = re.compile(r"^(Disc [0-9A-Z]+|Rev [0-9A-Z]+|R|RE|Made in .*|\d+[MS](, \d+[MS])*|[A-Z][a-z](,[A-Z][a-z])+)$")


def _drop_tag(tag: str) -> bool:
    """A tag that only says where or which pressing (region, languages, disc number, revision, ring code). Every other
    tag -- Demo, Proto, Mihonban, Satakore, Omake Disc -- names a different disc, so it stays in the title: the cheat
    keymap matches by title, and a demo disc must not take the full game's cheats."""
    return bool(_DROP_TAG.match(tag)) or all(w.strip().lower() in _REGION_WORDS for w in re.split(r"[,/]", tag))


def dat_title(name: str) -> str:
    """The DAT's game name less its region/language/disc/revision tags, with a No-Intro trailing article moved to the
    front ("Horde, The (USA)" -> "The Horde")."""
    m = re.match(r"^(.*?)((?:\s*[\(\[][^\)\]]*[\)\]])*)$", name)
    base, tags = m.group(1).strip(), re.findall(r"[\(\[]([^\)\]]*)[\)\]]", m.group(2))
    a = _ARTICLE.match(base)
    base = f"{a.group(2)} {a.group(1)}{a.group(3) or ''}" if a else base
    keep = [t for t in tags if not _drop_tag(t) and t != "!"]
    return " ".join([base] + [f"({t})" for t in keep])


def dat_area(region: str) -> str:
    return "".join(sorted({_AREA[w.strip().lower()] for w in re.split(r"[,/]", region) if w.strip().lower() in _AREA}))


def parse_dat(text: str) -> tuple[list[DatGame], dict]:
    games, no_serial, stripped = [], 0, 0
    for blk in _GAME.findall(text):
        name = re.search(r'^\tname "(.*)"', blk, re.M).group(1)
        reg = re.search(r'^\tregion "(.*)"', blk, re.M)
        ser = re.search(r'^\tserial "(.*)"', blk, re.M)
        if not ser or not ser.group(1).strip():
            no_serial += 1
            continue
        s = normalise(ser.group(1))
        if "(Disc " in name and _DISC_INDEX.search(s):
            s, stripped = _DISC_INDEX.sub("", s), stripped + 1
        t1 = _TRACK1.search(blk)
        games.append(DatGame(name, dat_area(reg.group(1) if reg else ""), s,
                             (int(t1.group(1)), t1.group(2).upper()) if t1 else None))
    return games, {"games_parsed": len(games) + no_serial, "no_serial": no_serial, "disc_index_stripped": stripped}


def _pref(g: DatGame) -> int:
    return 0 if "U" in g.area else 1 if "E" in g.area else 2


def merge_dat(recs: dict[str, GameRecord], aliases: dict[str, str], games: list[DatGame]) -> dict:
    """Adds a record for every DAT serial the sheet does not resolve (sheet priority). Returns report counts."""
    m = Matcher(list(recs), aliases)
    new: dict[str, list[DatGame]] = {}
    known = ambiguous = 0
    for g in games:
        r = m.match(g.serial, g.area)
        # the sheet has this very code (exact/alias/skeleton, or a "suffix" hit on the same -50/-18: the DAT's
        # "T-25414-H-50" is the sheet's T-25414H-50); any other "suffix" or "prefix" hit is only a relative
        # (T-5029H-50 is not T-5029H-18), so the DAT's own code is added
        if r.key and (r.how in ("exact", "alias", "skeleton")
                      or (r.how == "suffix" and any(split(c)[1][1:] == g.serial[-2:] for c in r.candidates))):
            known += 1
            continue
        if r.candidates:
            ambiguous += 1
        new.setdefault(g.serial, []).append(g)
    for key, gs in sorted(new.items()):
        gs = sorted(gs, key=lambda g: (_pref(g), g.name))      # the first disc names the game
        flags = 0
        for g in gs:
            flags |= region_flags_from(g.area)
        recs[key] = GameRecord(key=key, title_en=dat_title(gs[0].name), region_flags=flags)
    return {"dat_serials_resolved_by_sheet": known, "dat_serials_ambiguous_in_sheet": ambiguous,
            "dat_records_added": len(new)}


def collapse_same_disc(recs: dict[str, GameRecord], aliases: dict[str, str], games: list[DatGame],
                       keep: dict[str, str], jp: dict[str, str]) -> tuple[list[list[str]], list[list[str]]]:
    """Duplicate keys (Task 18e rule): keys whose DAT entries share one track-1 size+CRC are one disc. When their
    records also carry the same title and region and one of them is named in disc_aliases.json "same_disc_keep" (the
    code measured in that disc's IP.BIN, so the key a scan already gives it), they become that one record and every
    other key its alias. Without that measurement nothing is folded: the key a disc resolves to never changes, since
    per-game saves and settings are keyed by it. Returns (folded groups, same-disc groups left as they are)."""
    by_disc: dict[tuple[int, str], set[str]] = {}
    for g in games:
        if g.track1 and g.serial in recs:
            by_disc.setdefault(g.track1, set()).add(g.serial)
    groups, left = [], []
    for keys in by_disc.values():
        keys = {k for k in keys if k in recs}     # a key already folded into an earlier group stays folded
        if len(keys) < 2 or len({(recs[k].title_en, recs[k].region_flags) for k in keys}) != 1:
            continue
        ks = sorted(keys)
        kept = next((k for k in ks if k in keep), None)
        if kept is None:
            left.append(ks)
            continue
        for k in ks:
            if k != kept:
                del recs[k]
                jp.pop(k, None)
                aliases[k] = kept
        groups.append([kept] + [k for k in ks if k != kept])
    for a, c in list(aliases.items()):          # an alias that pointed at a dropped key follows it
        aliases[a] = aliases.get(c, c) if c not in recs else c
    return sorted(groups), sorted(left)


def apply_disc_aliases(recs: dict[str, GameRecord], aliases: dict[str, str], rows: list[dict]) -> list[str]:
    """IP.BIN product -> GAMES.DB key rows; a row whose product is already a record, or whose key is not, is an
    error (the build stops: a typo here would silently match nothing)."""
    errs = []
    for row in rows:
        p, k = normalise(row["product"]), row["key"]
        if p in recs:
            errs.append(f"disc alias {p}: already a GAMES.DB key")
        elif k not in recs:
            errs.append(f"disc alias {p} -> {k}: no such GAMES.DB key")
        else:
            aliases[p] = k
    return errs


def add_save_tools(recs: dict[str, GameRecord], aliases: dict[str, str], tools: list[dict]) -> list[str]:
    m = Matcher(list(recs), aliases)
    added = []
    for t in tools:
        for p in t.get("product_codes", []):
            k = normalise(p)
            if not m.match(k, "").key and k not in recs:
                recs[k] = GameRecord(key=k, title_en=t["name"])
                added.append(k)
    return added


def merge_all(recs: dict[str, GameRecord], aliases: dict[str, str], jp: dict[str, str], dat_text: str | None,
              disc_aliases: dict, save_tools: list[dict]) -> dict:
    """The whole supplementary merge, in order: DAT records, same-disc collapse, disc aliases, save tools."""
    rep: dict = {}
    games: list[DatGame] = []
    if dat_text is not None:
        games, rep["libretro_dat"] = parse_dat(dat_text)
        rep["libretro_dat"].update(merge_dat(recs, aliases, games))
        rep["libretro_dat"]["commit"] = DAT_COMMIT
    keep = dict(disc_aliases.get("same_disc_keep", {}))
    rep["same_disc_folded"], left = collapse_same_disc(recs, aliases, games, keep, jp)
    rep["same_disc_not_folded"] = left
    errs = apply_disc_aliases(recs, aliases, disc_aliases.get("aliases", []))
    if errs:
        raise ValueError("; ".join(errs))
    rep["disc_aliases"] = len(disc_aliases.get("aliases", []))
    rep["save_tool_records"] = add_save_tools(recs, aliases, save_tools)
    return rep


def load_json(p: Path) -> dict:
    return json.loads(Path(p).read_text(encoding="utf-8"))
