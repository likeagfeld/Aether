"""Multi-disc sets (contract/SPEC.md §5.4 flags bits 2 and 4-15): which images of a card are the discs of one game.

Run by the card builders after the scan (cardbuild.py here, CardBuilder.cs in the Card Manager; the console's own scan
does not group). The menu reads the result at launch: LAUNCHER.BIN writes out1..outN.desc for the set, so opening and
closing the console's lid swaps discs in game (Satiator firmware 161+). contract/fixtures/discsets.json gates every
implementation. The rule, step by step:

1. Candidates: images whose IP.BIN says CD-n/m with 2 <= m <= 9 and 1 <= n <= m, and whose product number (gameid.h's
   rule: up to the first byte <= ' ', a-z upper-cased, anything outside 0-9 A-Z _ - as '_') is not empty. They are
   bucketed by (product, m). The disc number is always IP's n, never the file name's.
2. In a bucket, images cluster by their signature: the path with every disc number masked to '#' (`mask`). The stem is
   the masked path cut just after its first disc token (`stem`): `Command & Conquer (USA) (Disc #`.
3. Clusters, largest first (then by signature, byte order), join sets: a cluster joins the first set with the same
   stem (`stem`) whose disc numbers it does not share; else, when it has fewer distinct discs than m, the same-stem set
   whose signature shares the longest prefix with its own (the first on a tie), as alternates; else it starts a set.
4. Within a set, the first image added for a disc number is that disc's primary; any later one is an alternate (bit 2).
5. A set with at least 2 distinct disc numbers gets an id, 1.. in the order of its first image in LIBRARY.IDX order,
   at most 4095 (bits 4-15); every other image has 0.
"""
from __future__ import annotations

_WORDS = ("volume", "vol.", "vol", "disc", "disk", "cd")      # longest first where one is a prefix of another
_ROMAN = "ivxIVX"
SET_MAX = 4095
FLAG_ALT = 0x4


def _alnum(c: str) -> bool:
    return c.isascii() and c.isalnum()


def _token(s: str, i: int):
    """(end of the word and its spaces, end of the number) for a disc token at i, else None."""
    if i and _alnum(s[i - 1]):
        return None
    low = "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s[i:i + 6])   # ASCII only
    for w in _WORDS:
        if low.startswith(w):
            j = i + len(w)
            while j < len(s) and s[j] == " ":
                j += 1
            k = j
            while k < len(s) and s[k].isascii() and s[k].isdigit():
                k += 1
            if not 1 <= k - j <= 2:
                k = j
                while k < len(s) and s[k] in _ROMAN:
                    k += 1
                if not 1 <= k - j <= 4:
                    return None
            if k < len(s) and _alnum(s[k]):
                return None
            return j, k
    return None


def _underscore(s: str, i: int):
    """'_' + 1-2 digits + '.' + 1-4 letters/digits ending the path (PHANTASM_1.cue): the digits' span, else None."""
    if s[i] != "_":
        return None
    j = k = i + 1
    while k < len(s) and s[k].isascii() and s[k].isdigit():
        k += 1
    if not 1 <= k - j <= 2 or k >= len(s) or s[k] != ".":
        return None
    ext = s[k + 1:]
    if not 1 <= len(ext) <= 4 or not all(_alnum(c) for c in ext):
        return None
    return j, k


def _scan(s: str, first_only: bool = False):
    """The masked string, and the end of the first token (or -1)."""
    out, i, first = [], 0, -1
    while i < len(s):
        t = _token(s, i) or _underscore(s, i)
        if t:
            out.append(s[i:t[0]] + "#")
            if first < 0:
                first = len("".join(out))
                if first_only:
                    return "".join(out), first
            i = t[1]
        else:
            out.append(s[i])
            i += 1
    return "".join(out), first


def mask(path: str) -> str:
    return _scan(path)[0]


def stem(path: str) -> str:
    """The masked path cut just after its first disc token (the whole masked path when it has none)."""
    m, first = _scan(path, first_only=True)
    return m if first < 0 else m[:first]


def norm_product(raw: str) -> str:
    out = []
    for c in raw:
        if c <= " ":
            break
        c = c.upper() if "a" <= c <= "z" else c
        out.append(c if ("0" <= c <= "9" or "A" <= c <= "Z" or c in "_-") else "_")
    return "".join(out[:10])


def _lcp(a: bytes, b: bytes) -> int:
    n = 0
    while n < min(len(a), len(b)) and a[n] == b[n]:
        n += 1
    return n


def assign(items) -> list[int]:
    """items: (path, product, disc, total) per image, in LIBRARY.IDX order. Returns each image's flags bits (set id << 4
    | FLAG_ALT), 0 for an image in no set."""
    buckets: dict[tuple[str, int], list[int]] = {}
    for i, (path, product, disc, total) in enumerate(items):
        p = norm_product(product or "")
        if p and 2 <= total <= 9 and 1 <= disc <= total:
            buckets.setdefault((p, total), []).append(i)
    sets = []                                    # each: [first index, stem, sig bytes, {disc: [members]}]
    for (_, total), idx in buckets.items():
        clusters: dict[str, list[int]] = {}
        for i in idx:
            clusters.setdefault(mask(items[i][0]), []).append(i)
        gs = []
        for sig, members in sorted(clusters.items(), key=lambda kv: (-len(kv[1]), kv[0].encode("utf-8"))):
            nums = {items[i][2] for i in members}
            st, sb = stem(items[members[0]][0]), sig.encode("utf-8")
            same = [g for g in gs if g[1] == st]
            tgt = next((g for g in same if not nums & g[3].keys()), None)
            if tgt is None and len(nums) < total and same:
                best = -1
                for g in same:
                    n = _lcp(g[2], sb)
                    if n > best:
                        best, tgt = n, g
            if tgt is None:
                tgt = [members[0], st, sb, {}]
                gs.append(tgt)
            for i in members:
                tgt[3].setdefault(items[i][2], []).append(i)
                tgt[0] = min(tgt[0], i)
        sets += gs
    flags = [0] * len(items)
    sid = 0
    for g in sorted((g for g in sets if len(g[3]) >= 2), key=lambda g: g[0]):
        if sid == SET_MAX:
            break
        sid += 1
        for members in g[3].values():
            for k, i in enumerate(members):
                flags[i] = sid << 4 | (FLAG_ALT if k else 0)
    return flags


def apply(res, build_date: int) -> int:
    """Sets a cardscan.ScanResult's entries' `sets` bits and rewrites its LIBRARY.IDX bytes; returns the set count."""
    from .libidx import write_libidx
    flags = assign([(e.path, res.products.get(e.path, ""), e.disc, e.disc_total) for e in res.entries])
    for e, f in zip(res.entries, flags):
        e.sets = f
    res.data = write_libidx(res.entries, res.folders, res.fingerprint, build_date)
    return max(flags, default=0) >> 4
