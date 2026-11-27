"""Cover fallbacks for `omsat.cardbuild` beyond the Cover Project zip and BOX.TGA:

  zip by title   -- the Rings Cover Project zip again, looked up by the GAMES.DB title plus its region name
                    ("Die Hard Arcade (USA)"): patched images (XBAND builds) carry their own cue names;
  retrohead      -- the Satiator Rings box server, files-ds-scene.net/retrohead/satiator/box/2d/<product>.TGA
                    (docs/research/research_retrohead_boxart.md: 64x100 or 80x80, 24 bpp; MK- prefixed product
                    numbers for Sega's US codes). 2d/ only: flat fronts, like every other cover (3d/ is a box render);
  libretro       -- libretro-thumbnails Sega_-_Saturn Named_Boxarts/<Title> (<Region>).png;
  generated      -- a title card drawn here, the last resort, always flagged in the report.

Every download goes through a cache directory (hits and misses both), so a rerun is offline and gives the same bytes.
Nothing here writes outside that cache. Credits: CREDITS.md (retrohead, libretro-thumbnails)."""
from __future__ import annotations

import hashlib
import io
import re
import urllib.parse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

RETROHEAD = "https://files-ds-scene.net/retrohead/satiator/box/2d/"
LIBRETRO = "https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Saturn/master/Named_Boxarts/"
REGION_NAME = (("U", "USA"), ("J", "Japan"), ("E", "Europe"), ("B", "Brazil"), ("T", "Taiwan"))
REGION_BITS = {"J": 0, "T": 1, "U": 2, "E": 3, "B": 4}   # omsat.gamesdb.REGION_BITS
# A retrohead 2d/ cover that is the wrong release for its product number is not a cover for that game (checked by eye
# against the libretro scan of the same release; the entry says what was seen). The chain falls through to libretro.
RETROHEAD_REJECT = {
    "MK-81070": "2d/ holds the PAL Saturn Bomberman front (black spine) for the US product number; 3d/ and libretro "
                "show the US release (white spine, NetLink Playable) -- checked 2026-11-19",
}
_LIBRETRO_BAD = re.compile(r'[&*/:`<>?\\|"]')            # libretro-thumbnails replaces these with "_"


def region_names(region_flags: int) -> list[str]:
    return [name for code, name in REGION_NAME if region_flags & (1 << REGION_BITS[code])]


def zip_title_stem(title: str, region: str) -> str:
    """The Cover Project's file stem for a title: apostrophes are "_" there ("Herc_s Adventures (USA)")."""
    return f"{title.replace(chr(39), '_')} ({region})"


def retrohead_names(key: str) -> list[str]:
    """Product numbers as retrohead names them: Sega's US numbers carry an MK- prefix GAMES.DB keys drop."""
    names = [key]
    if key.isdigit() or re.fullmatch(r"\d+-\d+", key):
        names.append("MK-" + key)
    return names


def _fetch(url: str) -> bytes | None:
    import urllib.error
    import urllib.request
    req = urllib.request.Request(url, headers={"User-Agent": "Aether-omsat/0.1 (cover fallback)"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


class RemoteArt:
    """Cached downloads. download=False: the cache only (a cache miss is "not found")."""

    def __init__(self, cache: Path, download: bool = True, fetch=_fetch):
        self.cache, self.download, self.fetch = Path(cache), download, fetch
        self.log: list[str] = []

    def get(self, url: str) -> bytes | None:
        h = hashlib.sha256(url.encode("utf-8")).hexdigest()[:24]
        hit, miss = self.cache / (h + ".bin"), self.cache / (h + ".miss")
        if hit.is_file():
            return hit.read_bytes()
        if miss.is_file() or not self.download:
            return None
        data = self.fetch(url)
        self.cache.mkdir(parents=True, exist_ok=True)
        (self.cache / (h + ".url")).write_text(url + "\n", encoding="utf-8")
        if data is None:
            miss.write_bytes(b"")
            self.log.append(f"404 {url}")
            return None
        hit.write_bytes(data)
        self.log.append(f"200 {url} ({len(data)} B)")
        return data

    def retrohead(self, key: str):
        for name in retrohead_names(key):
            url = RETROHEAD + urllib.parse.quote(name + ".TGA")
            data = self.get(url)
            if data is None:
                continue
            if name in RETROHEAD_REJECT:
                return None, f"{url} REJECTED: {RETROHEAD_REJECT[name]}"
            return Image.open(io.BytesIO(data)).convert("RGBA"), url
        return None, None

    def libretro(self, title: str, region_flags: int):
        for region in region_names(region_flags):
            name = _LIBRETRO_BAD.sub("_", f"{title} ({region})") + ".png"
            url = LIBRETRO + urllib.parse.quote(name)
            data = self.get(url)
            if data is not None:
                return Image.open(io.BytesIO(data)).convert("RGBA"), url
        return None, None


def _font(font_path, px: int):
    if font_path:
        try:
            f = ImageFont.truetype(str(font_path), px)
            try:
                f.set_variation_by_axes([600])
            except Exception:
                pass
            return f
        except OSError:
            pass
    return ImageFont.load_default()


def _wrap(draw: ImageDraw.ImageDraw, text: str, font, width: int) -> list[str]:
    lines, cur = [], ""
    for word in text.split():
        t = (cur + " " + word).strip()
        if draw.textlength(t, font=font) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


def _backdrop(w: int, h: int, top=(16, 28, 64), bottom=(4, 8, 22)) -> Image.Image:
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        for x in range(w):
            px[x, y] = c
    return img


def title_card(title: str, square: bool, font_path=None) -> Image.Image:
    """The last-resort cover: the title on the menu's navy, 4x the hero size so the encoder's downscale is clean."""
    w, h = (640, 640) if square else (512, 800)
    img = _backdrop(w, h)
    d = ImageDraw.Draw(img)
    d.rectangle([12, 12, w - 13, h - 13], outline=(120, 180, 240), width=4)
    font = _font(font_path, 64)
    lines = _wrap(d, title, font, w - 96)[:5]
    lh = 80
    y = (h - lh * len(lines)) // 2
    for ln in lines:
        d.text(((w - d.textlength(ln, font=font)) / 2, y), ln, font=font, fill=(232, 238, 248))
        y += lh
    return img.convert("RGBA")


def folder_card(name: str, font_path=None) -> Image.Image:
    """Folder art when the card has none: a folder outline in the menu's accent over its navy, the folder's
    name below it. Square, 640x640 (the encoder fits it to 160x160)."""
    w = h = 640
    img = _backdrop(w, h, (18, 34, 78), (6, 10, 28))
    d = ImageDraw.Draw(img)
    acc, hi = (120, 180, 240), (170, 225, 255)
    # the folder: a tab and a body, rounded, outlined with a soft fill
    d.rounded_rectangle([150, 150, 300, 210], radius=18, fill=(40, 70, 130), outline=acc, width=8)
    d.rounded_rectangle([140, 185, 500, 420], radius=24, fill=(48, 84, 150), outline=acc, width=8)
    d.line([160, 240, 480, 240], fill=hi, width=6)
    font = _font(font_path, 88)
    tw = d.textlength(name, font=font)
    while tw > w - 80 and getattr(font, "size", 0) > 32:
        font = _font(font_path, font.size - 8)
        tw = d.textlength(name, font=font)
    d.text(((w - tw) / 2, 460), name, font=font, fill=(232, 238, 248))
    return img.convert("RGBA")
