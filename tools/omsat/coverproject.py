"""Import the Rings Cover Project sheet (Felinni Marinas, 2021) into GAMES.DB, then the supplementary sources of
`omsat.dbextra` (libretro DAT, disc aliases, save tools; the sheet takes priority). Credit: CREDITS.md."""
from __future__ import annotations

import argparse
import json
import sys

from . import dbextra
from .gamesdb import GameRecord, region_flags_from, write_gamesdb
from .keys import parse_code_cell

SHEETS = {"NTSC-U -COMBINED": "U", "NTSC-J -COMBINED": "J", "PAL -COMBINED": "E", "BRAZIL -COMBINED": "B"}


def load_sheet_rows(xlsx_path: str):
    import openpyxl
    wb = openpyxl.load_workbook(xlsx_path, read_only=True)
    rows = []
    for name in wb.sheetnames:
        if name not in SHEETS:
            continue
        for r in wb[name].iter_rows(min_row=2, values_only=True):
            if r and r[0]:
                rows.append((name, str(r[0]), str(r[2] or ""), str(r[3] or "")))
    return rows


def build(rows, dat_text: str | None = None, disc_aliases: dict | None = None, save_tools: list | None = None):
    """The sheet's records; then, when any supplementary source is given, `dbextra.merge_all` (report key "extra")."""
    recs: dict[str, GameRecord] = {}
    genres: list[str] = []
    gindex: dict[str, int] = {}
    aliases: dict[str, str] = {}
    jp: dict[str, str] = {}
    merged = 0
    for sheet, code, title, genre in rows:
        key, als = parse_code_cell(code)
        lines = [ln.strip() for ln in title.replace("\r", "\n").split("\n") if ln.strip()]
        en = lines[0] if lines else key
        if genre and genre not in gindex:
            gindex[genre] = len(genres)
            genres.append(genre)
        flag = region_flags_from(SHEETS[sheet])
        if key in recs:
            recs[key].region_flags |= flag
            merged += 1
        else:
            recs[key] = GameRecord(key=key, title_en=en, genre=gindex.get(genre, 0xFF), region_flags=flag)
            if len(lines) > 1:
                jp[key] = lines[1]
        for a in als:
            aliases.setdefault(a, key)
    aliases = {a: c for a, c in aliases.items() if a not in recs}
    extra = None
    if dat_text is not None or disc_aliases is not None or save_tools is not None:
        extra = dbextra.merge_all(recs, aliases, jp, dat_text, disc_aliases or {}, save_tools or [])
        aliases = {a: c for a, c in aliases.items() if a not in recs}
    report = {"source": "Project Saturn-Rings (Stage 3).xlsx", "rows_read": len(rows), "unique_keys": len(recs),
              "duplicate_rows_merged": merged, "aliases": len(aliases), "genres": len(genres),
              "jp_titles": len(jp), "keys_over_10_chars": sum(len(k) > 10 for k in recs)}
    if extra is not None:
        report["records"] = len(recs)
        report["extra"] = extra
    return list(recs.values()), genres, aliases, jp, report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--xlsx", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--jp-titles", required=True)
    ap.add_argument("--date", type=int, default=20261117)
    ap.add_argument("--libretro-dat", help="libretro-database Sega - Saturn.dat (downloaded to this path, pinned, when "
                                           "missing); omit for the sheet alone")
    ap.add_argument("--disc-aliases", default=str(dbextra.DISC_ALIASES))
    ap.add_argument("--save-tools", default=str(dbextra.SAVE_TOOLS))
    a = ap.parse_args(argv)
    dat = dbextra.fetch_dat(a.libretro_dat).read_text(encoding="utf-8") if a.libretro_dat else None
    recs, genres, aliases, jp, rep = build(load_sheet_rows(a.xlsx), dat, dbextra.load_json(a.disc_aliases),
                                           dbextra.load_json(a.save_tools)["tools"])
    with open(a.out, "wb") as fh:
        fh.write(write_gamesdb(recs, genres, aliases, a.date))
    with open(a.report, "w", encoding="utf-8") as fh:
        json.dump(rep, fh, indent=2)
    with open(a.jp_titles, "w", encoding="utf-8") as fh:
        json.dump(jp, fh, ensure_ascii=False, indent=2)
    print(json.dumps(rep, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
