"""PERGAME.INI (contract/SPEC.md §5.14): parse, dump (byte-exact round trip), game_id, dir_name, check.
`python -m omsat.pergame FILE` prints the checker's findings and exits 1 on any.

The same rules as menu/src/pergame.c and menu/src/gameid.c (the menu writes the file; the PC tools and, later, the
Card Manager read and write it too). The game id is Pseudo Saturn Kai's save-folder rule (cafe-alpha, GPL-2.0;
re-implemented from pseudo_bram.c, docs/research/m6-research.md §1.2)."""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field

MAX_BYTES = 16384                        # two copies fit the menu's LWRAM stage
MAX_SECTIONS = 512
MAX_LINE = 255
TITLE_MAX = 40
KEYS = ("title", "loader", "saves", "slot", "cheats", "igr")
VALUES = {"loader": ("auto", "cwx", "jhl"), "saves": ("default", "on", "off"), "cheats": ("default", "on", "off"),
          "igr": ("default", "on", "off"), "slot": tuple("0123456789") + ("ask",)}
DEFAULTS = {"title": "", "loader": "auto", "saves": "default", "slot": "0", "cheats": "default", "igr": "default"}
ID_RE = re.compile(r"^[A-Z0-9_-]{1,10}@[A-Z]{0,8}$")


def game_id(product: bytes, areas: bytes) -> str | None:
    """IP.BIN 0x20 (10 bytes) + 0x40 (10 bytes) -> "MK-81214@UT"; None for an empty product."""
    p = ""
    for c in product[:10]:
        if c <= 0x20:
            break
        if 0x61 <= c <= 0x7A:
            c -= 32
        ch = chr(c)
        p += ch if (ch.isascii() and (ch.isdigit() or "A" <= ch <= "Z" or ch in "-_")) else "_"
    if not p:
        return None
    letters = ""
    for c in areas[:10]:
        if c == 0 or len(letters) >= 8:
            break
        if 0x41 <= c <= 0x5A:
            letters += chr(c)
    return p + "@" + letters


def dir_name(gid: str) -> str | None:
    """"MK-81214@UT" -> "MK-81214[UT]"; "MK-81214@" -> "MK-81214"; None when gid is not a game id."""
    if not ID_RE.match(gid):
        return None
    p, r = gid.split("@")
    return f"{p}[{r}]" if r else p


@dataclass
class Game:
    id: str
    title: str = ""
    loader: str = "auto"
    saves: str = "default"
    slot: str = "0"
    cheats: str = "default"
    igr: str = "default"
    lines: list[str] = field(default_factory=list, compare=False, repr=False)

    def values(self) -> tuple:
        return tuple(getattr(self, k) for k in KEYS)


def _lines(text: str) -> list[str]:
    """Raw lines with their terminators (the last one may have none)."""
    return re.findall(r"[^\n]*\n|[^\n]+$", text)


def _content(raw: str) -> str:
    s = raw[:-1] if raw.endswith("\n") else raw
    return s[:-1] if s.endswith("\r") else s


def _term(raw: str) -> str:
    return "\r\n" if raw.endswith("\r\n") else "\n" if raw.endswith("\n") else ""


def _classify(raw: str):
    """-> ("blank"|"head"|"key"|"other", name-or-key, value)"""
    s = _content(raw).strip(" \t")
    if not s:
        return "blank", None, None
    if s[0] == "[":
        inner = s[1:].split("]", 1)[0]
        return "head", inner.strip(" \t"), None
    if s[0] != "#" and "=" in s:
        k, v = s.split("=", 1)
        return "key", k.strip(" \t"), v.strip(" \t")
    return "other", None, None


def _set(g: Game, key: str, val: str) -> None:
    if key == "title":
        g.title = val[:TITLE_MAX]
        return
    v = val.lower()
    if v in VALUES[key] and (key != "slot" or len(val) == 1 or v == "ask"):
        setattr(g, key, v)


def parse(text: str) -> list[Game]:
    games: list[Game] = []
    cur: Game | None = None
    seen: set[str] = set()
    for raw in _lines(text):
        kind, a, b = _classify(raw)
        if kind == "head":
            cur = Game(a)
            seen = set()
            games.append(cur)
        if cur is not None:
            cur.lines.append(raw)
            if kind == "key" and a in KEYS and a not in seen:
                seen.add(a)
                _set(cur, a, b)
    return games


def get(text: str, gid: str) -> Game | None:
    for g in parse(text):
        if g.id == gid:
            return g
    return None


def _value(g: Game, key: str) -> str | None:
    v = getattr(g, key)
    if key == "title":
        return v[:TITLE_MAX] or None
    return None if v == DEFAULTS[key] else v


def put(text: str, g: Game) -> str:
    """The menu's om_pergame_put: change g's first section in place, or append it."""
    if not ID_RE.match(g.id):
        raise ValueError(f"bad game id {g.id!r}")
    out: list[str] = []
    found = inside = False
    eol = "\n"
    written: set[str] = set()
    blanks: list[str] = []

    def joined() -> str:
        return "".join(out)

    def leave():
        for k in KEYS:
            v = _value(g, k)
            if k not in written and v is not None:
                if out and not joined().endswith("\n"):
                    out.append(eol)
                out.append(f"{k}={v}{eol}")
        out.extend(blanks)
        blanks.clear()

    count = 0
    for raw in _lines(text):
        kind, a, b = _classify(raw)
        if inside and kind == "head":
            leave()
            inside = False
        if kind == "head":
            count += 1
            if not found and a == g.id:
                found = inside = True
                eol = "\r\n" if raw.endswith("\r\n") else "\n"
        elif inside:
            if kind == "blank":
                blanks.append(raw)
                continue
            out.extend(blanks)
            blanks.clear()
            if kind == "key" and a in KEYS:
                v = _value(g, a)
                if a not in written and v is not None:
                    out.append(f"{a}={v}{_term(raw)}")
                written.add(a)
                continue
        out.append(raw)
    if inside:
        leave()
    if not found:
        if count >= MAX_SECTIONS:
            raise ValueError("more than 512 sections")
        if out and not joined().endswith("\n"):
            out.append("\n")
        out.append(f"[{g.id}]\n")
        out.extend(f"{k}={_value(g, k)}\n" for k in KEYS if _value(g, k) is not None)
    return joined()


def delete(text: str, gid: str) -> str:
    """The menu's om_pergame_del: drop every [gid] section (header through the line before the next '[')."""
    out, inside = [], False
    for raw in _lines(text):
        kind, a, _ = _classify(raw)
        if kind == "head":
            inside = a == gid
        if not inside:
            out.append(raw)
    return "".join(out)


def dump(games: list[Game], original: str = "") -> str:
    """original with the games' changes applied: removed sections dropped, changed ones rewritten in place, new ones
    appended in list order. Unchanged games keep their bytes, so dump(parse(t), t) == t."""
    old = {g.id: g for g in reversed(parse(original))}          # the first copy wins, as in the menu
    keep = {g.id for g in games}
    text = original
    for gid in list(old):
        if gid not in keep:
            text = delete(text, gid)
    for g in games:
        if g.id not in old or old[g.id].values() != g.values():
            text = put(text, g)
    return text


def check(text: str | bytes) -> list[str]:
    """Findings for a PERGAME.INI (empty = valid). Mirrors the menu's refusals plus the spec's value rules."""
    errs: list[str] = []
    data = text.encode("latin-1", "replace") if isinstance(text, str) else text
    if len(data) > MAX_BYTES:
        return [f"file is {len(data)} B, over the {MAX_BYTES} B limit (SPEC §5.14)"]
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as e:
        return [f"not ASCII: {e}"]
    if "\0" in text:
        errs.append("embedded NUL byte")
    names: set[str] = set()
    sections = 0
    sec = None
    seen: set[str] = set()
    for i, raw in enumerate(_lines(text), 1):
        if len(_content(raw)) > MAX_LINE:
            errs.append(f"line {i}: longer than {MAX_LINE} B")
        kind, a, b = _classify(raw)
        if kind == "head":
            sections += 1
            sec, seen = a, set()
            if not ID_RE.match(a):
                errs.append(f"line {i}: section [{a}] is not a game id (<product>@<area letters>)")
            if a in names:
                errs.append(f"line {i}: duplicate section [{a}]")
            names.add(a)
        elif sec is None:
            continue                                            # the global section is reserved and ignored
        elif kind == "other" and not _content(raw).strip(" \t").startswith("#"):
            errs.append(f"line {i}: not key=value")
        elif kind == "key" and a in KEYS:
            if a in seen:
                errs.append(f"line {i}: {a}= given twice in [{sec}]")
            seen.add(a)
            if a == "title":
                if len(b) > TITLE_MAX:
                    errs.append(f"line {i}: title longer than {TITLE_MAX} B")
            elif b.lower() not in VALUES[a]:
                errs.append(f"line {i}: {a}={b} is not one of {'/'.join(VALUES[a])}")
    if sections > MAX_SECTIONS:
        errs.append(f"{sections} sections, over the {MAX_SECTIONS} limit")
    return errs


def main(argv=None) -> int:
    rc = 0
    for path in argv if argv is not None else sys.argv[1:]:
        with open(path, "rb") as fh:
            errs = check(fh.read())
        print(("FAIL " if errs else "PASS ") + path)
        for m in errs:
            print(f"  - {m}")
        rc |= bool(errs)
    return rc


if __name__ == "__main__":
    sys.exit(main())
