"""/aether/USEREDIT.INI (Aether Card Manager's user metadata overrides; SPEC §5.3 byte 0x35): parse it and apply it
onto a GAMES.DB, as the Card Manager's UserEdits.Parse / UserEdits.Apply do, so `omsat.cardbuild` gives the same card.
One `[<GAMES.DB key>]` section per edited game with any of `title`, `genre`, `year`, `region` (letters of JTUEB) and
`players`. A record whose title the user set gets byte 0x35 bit0 (USER_TITLE), which the menu reads to show that title
instead of the game's folder or file name; every other record has the bit cleared."""
from __future__ import annotations

from .gamesdb import USER_TITLE, read_gamesdb, region_flags_from, write_gamesdb
from . import binfmt as bf

FIELDS = ("title", "genre", "year", "region", "players")


def parse(text: str) -> dict[str, dict]:
    out: dict[str, dict] = {}
    cur = None
    for raw in text.replace("\r\n", "\n").split("\n"):
        line = raw.strip()
        if not line or line[0] == "#":
            continue
        if line[0] == "[" and line.endswith("]"):
            key = line[1:-1].strip()
            cur = out.setdefault(key, {}) if key else None
            continue
        if cur is None or "=" not in line:
            continue
        k, v = line.split("=", 1)
        k, v = k.strip().lower(), v.strip()
        if k in ("year", "players"):
            try:
                cur[k] = int(v)
            except ValueError:
                pass
        elif k in FIELDS:
            cur[k] = v
    return {k: e for k, e in out.items() if any(e.get(f) for f in FIELDS)}


def apply(gamesdb: bytes, edits: dict[str, dict]) -> tuple[bytes, dict]:
    """Returns (GAMES.DB bytes, report). A genre name not in the genre table (compared case-insensitively) is
    appended. With no edits and no flag to clear, the input bytes come back unchanged."""
    recs, genres, aliases = read_gamesdb(gamesdb)
    by_key = {r.key: r for r in recs}
    changed = titles = 0

    def genre_id(name: str) -> int:
        for i, g in enumerate(genres):
            if g.lower() == name.lower():
                return i
        genres.append(name)
        return len(genres) - 1

    for r in recs:
        e = edits.get(r.key, {})
        before = (r.title_en, r.title_rom, r.genre, r.year, r.region_flags, r.players, r.user_flags)
        if e.get("title"):
            r.title_en = r.title_rom = e["title"]
            r.user_flags |= USER_TITLE
            titles += 1
        else:
            r.user_flags &= ~USER_TITLE
        if e.get("genre"):
            r.genre = genre_id(e["genre"])
        if e.get("year"):
            r.year = e["year"]
        if e.get("region"):
            r.region_flags = region_flags_from(e["region"])
        if e.get("players"):
            r.players = e["players"]
        changed += before != (r.title_en, r.title_rom, r.genre, r.year, r.region_flags, r.players, r.user_flags)
    report = {"records": sum(1 for k in edits if k in by_key), "titles": titles,
              "not_in_db": sorted(k for k in edits if k not in by_key)}
    if not changed:
        return gamesdb, report
    return write_gamesdb(recs, genres, aliases, bf.validate_common(gamesdb, bf.MAGIC_GDB).build_date), report
