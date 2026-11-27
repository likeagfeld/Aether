"""Index the Rings Cover Project zip (docs/research/research_rings_cover_project.md: JAPAN/ png, US/ and EU/ jpg,
Redump-style names) and extract single members by name. The 1.6 GB archive is never extracted wholesale:
zipfile reads only the central directory plus the members asked for. Credit: Felinni Marinas and the Rings
Cover Project contributors (CREDITS.md)."""
from __future__ import annotations

import io
import re
import zipfile
from pathlib import PurePosixPath

from PIL import Image

# DEVIATION (genuine bug, found on the real 1.6 GB archive): an earlier version matched
# `n.startswith(("JAPAN/", "US/", "EU/"))`, which assumes the region directories sit at the zip
# root. The real Rings Cover Project zip wraps everything in a top-level
# "SEGA SATURN RINGS Cover Project/" directory, so the region name is the member's *parent*
# directory, not necessarily its first path component. Matching on the parent directory name
# handles both the real archive and the root-level layout the unit tests use.
REGION_NAMES = {"JAPAN", "US", "EU"}
_REGION_OF_DIR = {"JAPAN": "(japan)", "US": "(usa)", "EU": "(europe)"}
_EXT = (".png", ".jpg", ".jpeg")
_BRACKETS = re.compile(r"\s*\[[^\]]*\]")
_LAST_PAREN = re.compile(r"\s*\([^)]*\)$")
_DISC = re.compile(r"\(disc (\d+)\)", re.I)


def norm_stem(name: str) -> str:
    stem = PurePosixPath(name).name
    for ext in _EXT + (".cue", ".iso"):
        if stem.lower().endswith(ext):
            stem = stem[: -len(ext)]
            break
    stem = _BRACKETS.sub("", stem)
    return " ".join(stem.lower().split())


class CoverZip:
    def __init__(self, path):
        self.zf = zipfile.ZipFile(path)
        self.index: dict[str, list[str]] = {}
        for n in self.zf.namelist():
            parts = n.split("/")
            if len(parts) >= 2 and parts[-2] in REGION_NAMES and n.lower().endswith(_EXT):
                self.index.setdefault(norm_stem(n), []).append(n)
        self.indexed = sum(len(v) for v in self.index.values())
        self.ambiguous = 0
        self.resolved = 0

    def _resolve(self, hits: list[str]) -> str | None:
        """one name under several region directories (measured on the real archive: 31 names, every one a
        "(Europe)" file in both EU/ and US/, byte-identical copies). The copy whose directory is the name's own region
        wins; otherwise, when every copy has the same CRC, any copy is the cover; otherwise it stays ambiguous."""
        for h in hits:
            parts = h.split("/")
            if _REGION_OF_DIR.get(parts[-2], "-") in parts[-1].lower():
                return h
        if len({self.zf.getinfo(h).CRC for h in hits}) == 1:
            return sorted(hits)[0]
        return None

    def find(self, image_stem: str) -> str | None:
        s = norm_stem(image_stem)
        while s:
            hits = self.index.get(s)
            if hits:
                if len(hits) == 1:
                    return hits[0]
                pick = self._resolve(hits)
                if pick:
                    self.resolved += 1
                    return pick
                self.ambiguous += 1
                return None
            if s.count("(") <= 1:
                return None            # never drop the region group: "gex" must not match "Gex (USA)"
            nxt = _LAST_PAREN.sub("", s)
            if nxt == s:
                return None
            s = nxt
        return None

    def image(self, member: str) -> Image.Image:
        with self.zf.open(member) as fh:
            return Image.open(io.BytesIO(fh.read())).convert("RGBA")

    @staticmethod
    def disc_of(member: str) -> int:
        m = _DISC.search(member)
        return int(m.group(1)) if m else 0

    def close(self) -> None:
        """release the underlying zip handle. The real archive is 1.6 GB;
        leaving it open for the life of the process is wasteful and, on some platforms, leaks
        a file descriptor."""
        self.zf.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
