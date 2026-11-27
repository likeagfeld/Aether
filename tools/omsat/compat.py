"""loader-hint and controller compat rows for GAMES.DB bytes 0x2E-0x3F (contract/SPEC.md §5.3).

Sources: Kai's compatibility page + PSKai v6.545 `autoset.c` (`tools/data/compat/loader_compat.json`) give
the loader hint and the region mask; the controller-compat dataset
(`tools/data/compat/controller_compat.json`) gives the pad/stick/multitap warning note and flags.
a loader JSON row's own `"igr": "bad"` sets `OM_CF_NOIGR` (CF_NOIGR below) -- menu/src/loader.c's
`om_igr_want` forces the in-game reset off for that key regardless of the global or per-game setting.

review finding Important 2: the real controller-compat dataset's `confidence` vocabulary is
`confirmed` / `multiple reports` / `single report` -- it never says `likely`. `_CONFIDENT` is `confirmed` and
`multiple reports` (two independent corroborating sources, e.g. Shinobi Legions); `single report` (one source,
possibly internally inconsistent, e.g. Street Fighter Alpha 2) is explicitly excluded -- not confident enough to
show the user as a warning. Dropped rows and rows whose `peripheral` text matches none of the known hazards (the
bit1-only fallback) are named in the optional `report` list `load`/`load_controller` can append to, so cardbuild's
own report records them (the brief's own rule for unrecognized peripheral text).

`load()` builds the key -> row map cardbuild needs; `fill_compat()` is cardbuild's writer step, patching matching
GAMES.DB records like `gamesdb.fill_dates`. A GAMES.DB without this task's data reads back as all-zero bytes
0x2E-0x3F, which is "no compat data" -- unaffected readers (and old cards) are unaffected.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from . import binfmt as bf
from .gamesdb import REGION_BITS, ascii_fold, read_gamesdb, write_gamesdb
from .iphdr import kai_ip_crc
from .keys import cleanup, normalise, skeleton_forms, split
from .match import Matcher

_HINTS = {"jhl": 1, "cwx": 2, "bad": 3}
_CONFIDENT = {"confirmed", "multiple reports"}
NOTE_MAX = 46
# gamesdb.h OM_CF_NOIGR -- a loader JSON row with "igr": "bad" forces the in-game reset off for that
# key (menu/src/loader.c om_igr_want). No titles are known-bad yet; this is the plumbing for when one is found.
CF_NOIGR = 0x20
# gamesdb.h OM_CF_SAVETOOL -- a save tool (tools/data/compat/save_tools.json: Save Game Copier, the Dezaemon
# 2 managers, Rockin'-B's manager, the FDD Operator); its launch pauses per-game saves (menu/src/loader.c om_saves_want)
CF_SAVETOOL = 0x80


# 2026-11-25: loader_compat.json rows whose "source" starts with this are our own hardware findings (NiGHTS): they
# apply to every disc of their product, ahead of Kai's rows
OVERRIDE_SOURCE = "Aether hardware test"
_HINT_NAMES = {1: "JHL", 2: "CWX", 3: "bad"}


@dataclass
class LoaderRow:
    """One loader_compat.json row as the per-disc rules see it (fill_compat's `discs`)."""
    product: str
    area: str
    hint: int
    crc: int | None
    override: bool


@dataclass
class Disc:
    """One card image for fill_compat's per-disc rules: its LIBRARY.IDX key and area bits, its IP area text and
    product, Kai's IP CRC (iphdr.kai_ip_crc), and whether discsets put it in a set of 2 or more discs."""
    path: str
    key: str
    area_flags: int
    area: str
    product: str
    crc: int
    in_set: bool = False


def discs_of(entries, sysids) -> list[Disc]:
    """The scan's final entries (after discsets) that have a key and a readable System ID."""
    return [Disc(e.path, e.key, e.area_flags, sysids[e.path].area, sysids[e.path].product, kai_ip_crc(sysids[e.path].raw),
                 e.sets >> 4 != 0)
            for e in entries if e.key and sysids.get(e.path) is not None]


def same_product(a: str, b: str) -> bool:
    """Two product codes name the same product (keys.py rules): equal after cleanup + normalise, or the same -50/-18
    suffix (none counts) and an overlapping skeleton form ("MK-81307" ~ "81307"). "T-8103H" and "T-8103H-50" differ."""
    x, y = normalise(cleanup(a)), normalise(cleanup(b))
    if not x or not y:
        return False
    if x == y:
        return True
    (bx, sx), (by, sy) = split(x), split(y)
    return sx == sy and bool(skeleton_forms(bx) & skeleton_forms(by))


@dataclass
class CompatRow:
    hint: int = 0
    flags: int = 0
    note: str = ""
    regions: int = 0
    # 2026-11-24: each source row's (area, hint, flags, regions); fill_compat resolves the product through the §6
    # matcher once per area, so each lands on the record a disc of that area matches. Empty: the aggregate above
    # applies to every record the product resolves to.
    parts: list = field(default_factory=list)
    src: list = field(default_factory=list)       # 2026-11-25: the LoaderRows behind it (fill_compat's per-disc rules)


def _region_mask(area: str) -> int:
    """GAMES.DB region bits (J0 T1 U2 E3 B4); "*" or empty = 0 (applies to every region)."""
    if not area or area == "*":
        return 0
    return sum(1 << REGION_BITS[c] for c in set(area.upper()) if c in REGION_BITS)


def load_controller(rows: list[dict], report: list[str] | None = None) -> dict[str, CompatRow]:
    """Maps each confident row (controller_compat.json layout: product_codes, title, peripheral, symptom,
    warning_text, confidence, sources) to compat flags/note, keyed by each of its product codes, normalised the
    same way the matcher does. "3D Control Pad" -> bit0 (OM_CF_3DPAD) + bit1 (OM_CF_STDPAD, a standard pad works);
    "multitap" -> bit2 (OM_CF_MULTITAP); "stick" -> bit3 (OM_CF_STICK); any other peripheral text -> bit1 only (the
    specific hazard is unknown, but a standard pad is the safe fallback). `report`, if given, gets one line per
    row dropped for low confidence and one per row whose peripheral text matched none of the known hazards."""
    out: dict[str, CompatRow] = {}
    for row in rows:
        title = row.get("title", "?")
        confidence = row.get("confidence")
        if confidence not in _CONFIDENT:
            if report is not None:
                report.append(f"{title}: confidence {confidence!r} not used (dropped, no compat row)")
            continue
        peripheral = (row.get("peripheral") or "").lower()
        if "3d control pad" in peripheral:
            flags = 0x01 | 0x02
        elif "multitap" in peripheral:
            flags = 0x04
        elif "stick" in peripheral:
            flags = 0x08
        else:
            flags = 0x02
            if report is not None:
                report.append(f"{title}: unrecognized peripheral {row.get('peripheral')!r}, using bit1 only")
        note = ascii_fold(row.get("warning_text", ""))[:NOTE_MAX]
        for code in row.get("product_codes", []):
            out[normalise(code)] = CompatRow(flags=flags, note=note)
    return out


def load(loader_json: Path, controller_json: Path | None, report: list[str] | None = None,
         save_tools_json: Path | None = None) -> dict[str, CompatRow]:
    """Reads the loader JSON (Task 2's layout: a top-level "games" list of {product, area/region, loader, ...})
    and, if it exists, the controller JSON (a plain list, controller_compat.json's layout). A missing or absent
    controller_json is a no-op for the controller half -- the loader hints still load. `report`, if given, is
    passed through to `load_controller` (see there)."""
    rows: dict[str, CompatRow] = {}
    data = json.loads(Path(loader_json).read_text(encoding="utf-8"))
    for g in data.get("games", []):
        key = normalise(g["product"])
        row = rows.setdefault(key, CompatRow())
        area = g.get("area") or g.get("region", "")
        hint, flags = _HINTS.get(g.get("loader", ""), 0), CF_NOIGR if g.get("igr") == "bad" else 0
        row.hint = hint or row.hint
        row.regions |= _region_mask(area)
        row.flags |= flags
        row.parts.append(("" if area == "*" else area, hint, flags, _region_mask(area)))
        crc = g.get("ip_crc")
        row.src.append(LoaderRow(g["product"], area, hint, int(crc, 16) if crc else None,
                                 str(g.get("source", "")).startswith(OVERRIDE_SOURCE)))
    if save_tools_json is not None and Path(save_tools_json).is_file():   # by product number
        for t in json.loads(Path(save_tools_json).read_text(encoding="utf-8")).get("tools", []):
            for p in t.get("product_codes", []):
                rows.setdefault(normalise(p), CompatRow()).flags |= CF_SAVETOOL
    if controller_json is not None:
        p = Path(controller_json)
        if p.is_file():
            crows = json.loads(p.read_text(encoding="utf-8"))
            for key, c in load_controller(crows, report=report).items():
                row = rows.setdefault(key, CompatRow())
                row.flags |= c.flags
                if not row.note:
                    row.note = c.note
    return rows


_ALL_AREAS = ("J", "T", "U", "E", "")


def resolve(m: Matcher, product: str, area: str) -> set[str]:
    """The GAMES.DB keys a disc with this product code resolves to (contract/SPEC.md §6 -- the matcher the card scan
    and the menu use: exact, alias, skeleton, prefix, with the MK- prefix and -50/-18 suffix forms), for this area;
    with no area, for any area (a row that applies to every release of the product)."""
    return {k for a in ((area,) if area else _ALL_AREAS) for k in (m.match(product, a).key,) if k}


def _effect(t: CompatRow | None, area: int) -> int:
    """The hint the menu reads for a disc of these area bits (menu/src/gamesdb.c om_gdb_compat's region-mask gate)."""
    return t.hint if t is not None and t.hint and (t.regions == 0 or t.regions & area) else 0


def _one(rows: list[LoaderRow]) -> LoaderRow | None:
    """The first row, when every row agrees on a non-zero hint."""
    return rows[0] if rows and rows[0].hint and len({r.hint for r in rows}) == 1 else None


def decide(d: Disc, src: list[LoaderRow], current: int) -> tuple[int, str | None, LoaderRow | None]:
    """2026-11-25 (Mortal Kombat II USA region-patched to area J: Kai's T-8103H row is area U, so Auto fell back to
    CWX and the disc black-screened). A disc's loader hint, by the first rule that gives one: (1) our own override rows
    of its product, any area; (2) Kai's own method, a row whose ip_crc is the disc's IP CRC, whatever its product or
    area; (3) the row its product and area reach today (`current`, the record-level fill); (4) the product alone,
    any area, when every row of that product (not its -50/-18 variants) agrees; (5) 2026-11-27, a disc of a multi-disc
    set (discsets) gets JHL unless a row of its product or its IP CRC says CWX: the in-game-reset stub hands a game's
    lid-exit disc swap (SYS_EXECDMP) to the BIOS CD player only under JHL (Enemy Zero, Policenauts on hardware).
    Returns (hint, how, row)."""
    r = _one([s for s in src if s.override and same_product(s.product, d.product)])
    if r:
        return r.hint, "override", r
    r = _one([s for s in src if not s.override and s.crc is not None and s.crc == d.crc])
    if r:
        return r.hint, "crc", r
    if current:
        return current, "exact", None
    r = _one([s for s in src if not s.override and same_product(s.product, d.product)])
    if r:
        return r.hint, "product", r
    if d.in_set and not any(s.hint == _HINTS["cwx"] and (same_product(s.product, d.product) or s.crc == d.crc)
                            for s in src):
        return _HINTS["jhl"], "set", None
    return 0, None, None


def _fold(got: dict[str, CompatRow], src: list[LoaderRow], d: Disc, report: list[str] | None) -> None:
    """One disc's decide() onto its record: the hint set (if the record has none or the same one) and the record's
    region mask widened by the disc's area bits, so the menu's gate passes this disc. A record already holding a
    different hint is left as it is (one hint per record) and the conflict named."""
    t = got.get(d.key)
    cur = _effect(t, d.area_flags)
    hint, how, row = decide(d, src, cur)
    if how == "crc" and not same_product(row.product, d.product) and report is not None:
        report.append(f"{d.path}: IP CRC {d.crc:08X} is Kai's row for {row.product}, the disc says {d.product}")
    if not hint or hint == cur:
        return
    why = {"override": "by our hardware test", "crc": f"by IP CRC {d.crc:08X}", "product": "by product only",
           "set": "multi-disc set"}[how]
    src_of = f" ({row.product}, area {row.area})" if row else ""
    if t is not None and t.hint and t.hint != hint:
        if report is not None:
            report.append(f"{d.path}: {_HINT_NAMES[hint]} {why}{src_of} not applied, "
                          f"{d.key} already has {_HINT_NAMES[t.hint]}")
        return
    if t is None:
        t = got[d.key] = CompatRow(regions=d.area_flags)
    t.hint = hint
    # a disc with no area letters (a patched header: debug menus, XBAND builds) passes the menu's gate only with mask 0
    t.regions = t.regions | d.area_flags if t.regions and d.area_flags else 0
    if report is not None:
        report.append(f"{d.path}: multi-disc set → JHL, disc area {d.area or '-'}" if how == "set" else
                      f"{d.path}: {_HINT_NAMES[hint]} matched {why}{src_of}, disc area {d.area or '-'}")


def fill_compat(data: bytes, rows: dict[str, CompatRow], discs: list[Disc] = (),
                report: list[str] | None = None) -> tuple[bytes, int]:
    """Patches each GAMES.DB record a row resolves to (`resolve`: the record a disc of that product and area matches,
    never only a byte-identical key -- 2026-11-24, "MK-81307" is GAMES.DB's "81307" and "MK81307-50") with its loader
    hint, compat flags, note and region mask (like `gamesdb.fill_dates`: build date preserved, nothing else touched).
    Then each of the card's `discs` (discs_of) goes through `decide`'s rule order, folded onto its record (`_fold`);
    each hint that changes a disc's Auto is named in `report`. Products with no record on this card are ignored.
    Returns (bytes, records patched); with nothing to patch the input bytes come back unchanged."""
    recs, genres, aliases = read_gamesdb(data)
    m = Matcher([r.key for r in recs], aliases)
    got: dict[str, CompatRow] = {}
    for product, row in rows.items():
        parts = row.parts or [("", row.hint, row.flags, row.regions)]
        for area, hint, flags, regions in parts:
            for k in resolve(m, product, area):
                t = got.setdefault(k, CompatRow(regions=-1))
                t.hint = hint or t.hint
                t.flags |= flags | (row.flags if not row.parts else 0)
                t.note = t.note or row.note
                t.regions = regions if t.regions == -1 else (0 if 0 in (t.regions, regions) else t.regions | regions)
        if row.parts and (row.flags or row.note):        # controller/save-tool flags merged onto a loader row
            for k in resolve(m, product, ""):
                t = got.setdefault(k, CompatRow(regions=0))
                t.flags |= row.flags
                t.note = t.note or row.note
    src = [s for row in rows.values() for s in row.src]
    for d in discs:
        _fold(got, src, d, report)
    n = 0
    for r in recs:
        row = got.get(r.key)
        if row is None:
            continue
        r.loader_hint, r.compat_flags, r.compat_note = row.hint, row.flags, row.note
        r.compat_regions = max(row.regions, 0)
        n += 1
    if not n:
        return data, 0
    build_date = bf.validate_common(data, bf.MAGIC_GDB).build_date
    return write_gamesdb(recs, genres, aliases, build_date), n
