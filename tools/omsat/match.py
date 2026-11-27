"""Product-code matcher, v3 (contract/SPEC.md §6; v1 clarifications 2 and 5 superseded). The code forms live in
`omsat.keys`, shared with every GAMES.DB key producer."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable, Mapping

from .keys import cleanup, is_pal_suffixed, normalise, skeleton, skeleton_forms, split, suffix_base

__all__ = ["Matcher", "MatchResult", "cleanup", "split", "skeleton", "skeleton_forms", "suffix_base"]


@dataclass
class MatchResult:
    key: str | None
    how: str | None
    candidates: list[str] = field(default_factory=list)


def _area_pref(area: str) -> str | None:
    a = set(area.upper())
    has_e = "E" in a
    has_other = bool(a & set("JTU"))
    if has_e and not has_other:
        return "pal"
    if not has_e:
        return "ntsc"
    return None


def _choose(cands: Iterable[str], area: str, how: str) -> MatchResult:
    """SPEC.md §6 choose(): area preference filter, then shortest key, then the R8 "-50"
    preference; exactly one survivor wins, otherwise unmatched.

    R8: a remaining tie whose survivors all carry a -50/-18 suffix, and which contains
    exactly one "-50" key, resolves to that "-50" key (real GAMES.DB bases that carry a
    bare key plus both PAL variants would otherwise go unmatched for an E-only disc).
    """
    cands = sorted(set(cands))
    pref = _area_pref(area)
    if pref == "pal":
        preferred = [c for c in cands if is_pal_suffixed(c)] or cands
    elif pref == "ntsc":
        preferred = [c for c in cands if not is_pal_suffixed(c)] or cands
    else:
        preferred = cands
    shortest = min(len(c) for c in preferred)
    best = [c for c in preferred if len(c) == shortest]
    if len(best) == 1:
        return MatchResult(best[0], how, cands)
    if all(is_pal_suffixed(c) for c in best):
        fifties = [c for c in best if c.endswith("-50")]
        if len(fifties) == 1:
            return MatchResult(fifties[0], how, cands)
    return MatchResult(None, None, best)


class Matcher:
    def __init__(self, keys: Iterable[str], aliases: Mapping[str, str]):
        self._keys = sorted(set(keys))
        self._keyset = set(self._keys)
        self._aliases = {normalise(a): c for a, c in aliases.items()}
        self._suffixed_by_base: dict[str, set[str]] = {}
        self._skel_map: dict[str, set[str]] = {}
        for x in self._keys:
            base, suffix = split(x)
            if suffix:
                self._suffixed_by_base.setdefault(base, set()).add(x)
            for f in skeleton_forms(base):
                self._skel_map.setdefault(f, set()).add(x)

    def _family(self, base: str) -> set[str]:
        out: set[str] = set()
        for f in skeleton_forms(base):
            out |= self._skel_map.get(f, set())
        return out

    def match(self, product: str, area: str) -> MatchResult:
        k = normalise(cleanup(product))
        if not k:
            return MatchResult(None, None, [])

        if k in self._keyset:
            cands = {k} | self._suffixed_by_base.get(k, set())
            return _choose(cands, area, "exact")

        if k in self._aliases and self._aliases[k] in self._keyset:
            c = self._aliases[k]
            cands = {c} | self._suffixed_by_base.get(c, set())
            return _choose(cands, area, "alias")

        skel_cands = self._family(k)
        if skel_cands:
            return _choose(skel_cands, area, "skeleton")

        base = suffix_base(k)
        if base is not None:
            fam = self._family(base)
            if fam:
                return _choose(fam, area, "suffix")

        prefix_cands = [x for x in self._keys if x.startswith(k)]
        if prefix_cands:
            return _choose(prefix_cands, area, "prefix")

        return MatchResult(None, None, [])
