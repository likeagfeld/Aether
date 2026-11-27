"""Product-key normalisation and the code forms of contract/SPEC.md §6. The single home of every rule that turns a
product code into a GAMES.DB key: the GAMES.DB build (coverproject, libretrodat), the compat data, the card scan and
the cheat keymap all reach a key through these functions and `omsat.match.Matcher`."""
from __future__ import annotations

import re

_PAREN = re.compile(r"\([^)]*\)")
_WS = re.compile(r"\s+")
_CODE_LIKE = re.compile(r"^[A-Z0-9][A-Z0-9-]*$")
_PAL = re.compile(r"-(50|18)$")
# §6 v3: a version that spilled from IP.BIN 0x2A into the product field: "T-29002G V", "GS-9055 V1", "MK-81029V1"
# (V after blanks, or V plus one digit right after a digit). No GAMES.DB key or libretro serial ends in V or V<digit>.
_VERSION_SPILL = re.compile(r"(\s+V\d?|(?<=\d)V\d)$")
_LEADING_ZEROS = re.compile(r"^([A-Z]+)0+(?=[1-9])")
# §6 v3: a -50/-18 suffix, or the same digits with the dash lost ("MK-8109150", "T-12003H50")
_ANY_SUFFIX = re.compile(r"-?(50|18)$")


def normalise(s: str) -> str:
    first = s.replace("\r", "\n").split("\n", 1)[0]
    t = _PAREN.sub("", first)
    t = _WS.sub("", t).upper()
    return t[:16]


def parse_code_cell(cell: str) -> tuple[str, list[str]]:
    lines = [ln for ln in str(cell).replace("\r", "\n").split("\n") if ln.strip()]
    key = normalise(lines[0])
    aliases: list[str] = []
    for ln in lines[1:]:
        for grp in re.findall(r"\(([^)]*)\)", ln) or [ln]:
            a = _WS.sub("", grp).upper()[:16]
            if a and a != key and _CODE_LIKE.match(a):
                aliases.append(a)
    return key, aliases


def is_pal_suffixed(key: str) -> bool:
    return bool(_PAL.search(key))


def cleanup(product: str) -> str:
    """SPEC.md §6 cleanup(): drop a version that spilled from IP.BIN 0x2A into the product field."""
    return _VERSION_SPILL.sub("", product.rstrip())


def split(k: str) -> tuple[str, str]:
    """SPEC.md §6 split(): (base, suffix), where suffix is "-50"/"-18" when `k` ends with it, else ""."""
    m = _PAL.search(k)
    return (k[: m.start()], k[m.start():]) if m else (k, "")


def suffix_base(k: str) -> str | None:
    """SPEC.md §6 v3 suffix_base(): `k` less a trailing -50/-18 or a dashless 50/18 ("MK-8109150" -> "MK-81091"),
    or None when `k` ends with neither or would leave fewer than 4 characters."""
    m = _ANY_SUFFIX.search(k)
    return k[: m.start()] if m and m.start() >= 4 else None


def skeleton(base: str) -> str:
    """SPEC.md §6 skeleton(): upper-case, drop "-", " " and "_", then drop zeros between the leading letters and the
    first non-zero digit."""
    s = base.upper().replace("-", "").replace(" ", "").replace("_", "")
    return _LEADING_ZEROS.sub(r"\1", s)


def skeleton_forms(base: str) -> set[str]:
    """SPEC.md §6 skeleton_forms(): the skeleton, plus it without a leading "MK" (Sega's US codes: "MK81045" ~ "81045")
    or a leading "T" before a digit (a third-party code written without its "T-": "12512H" ~ "T-12512H")."""
    sk = skeleton(base)
    if sk.startswith("MK"):
        return {sk, sk[2:]}
    if len(sk) > 1 and sk[0] == "T" and sk[1].isdigit():
        return {sk, sk[1:]}
    return {sk}
