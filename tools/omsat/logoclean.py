"""M6 Task 7 (P2): the clean Saturn logo -- an upscale, a matte cleanup and a box-filtered downsample of
assets/logo/saturn_logo.png (217 x 144 RGBA, SEGA artwork, credited in CREDITS.md), and a before/after review sheet.

Pipeline (M3.5 open decision 11; review round 2 added decontaminate, the bleed, the palette and the gate):
    decontaminate the source is 1-bit and its anti-aliasing was matted on WHITE: in the edge band only (opaque pixels
                  within 2 px of transparency, chosen from the mask) alpha is recovered from color and the white
                  un-premultiplied, or, where the logo itself is near-white, alpha comes from the shape and the color
                  stays; interior pixels keep their exact colors; transparent pixels take the nearest logo color
    upscale       Real-ESRGAN x4 (realesrgan-ncnn-vulkan, model realesrgan-x4plus-anime) on the RGB; the alpha is
                  upscaled with LANCZOS (Real-ESRGAN works on RGB). Without the tool or a Vulkan device: LANCZOS x4 on
                  both and an unsharp mask on the RGB. The method that ran is returned and written on the sheet.
    clean_matte   alpha remapped through smoothstep(0.35, 0.65), which tightens the soft fringe the 217 px source
                  carries; the RGB of every pixel that is not fully opaque is taken from the nearest fully opaque pixel
                  within 3 px (defringe: edges carry the logo's own color, not the old dark halo); fully transparent
                  pixels become RGB 0.
    render        LANCZOS (reducing_gap 3.0, premultiplied by Pillow) to the interstitial's 152 x 101 and the header's
                  40 x 25; alpha quantized to the 16 levels the assets carry (k x 17); alpha < 17 -> (0, 0, 0, 0).
    finalize      soft-edge pixels take the nearest opaque pixel's color; no edge pixel is left lighter than the
                  opaque pixels next to it (limit_edges); RGB555. The big logo is palette-reduced (palettize_big), the
                  loading screen gets its own 22 x 15 footer (render_footer), blended for LOADBG's dark scrim.
    gate          fringe_violations: no edge pixel, blended over black or the saturn theme's darkest blue, lighter than
                  the opaque interior within 3 px by more than 8 luma. --review exits 2 when it fails.
The menu blends each edge pixel with the backdrop at upload (menu/src/logoblend.c, M6 Task 6): VDP1 itself has only
1-bit transparency. The review sheet shows old and new exactly that way, over the screens they appear on.

`python -m omsat.logoclean --review [--out DIR] [--no-fetch]` writes DIR/{sheet.png, new_big.png, new_hdr.png,
new_foot.png, master.png, decontaminated.png, decontam_paths.png, REVIEW.txt} and touches no asset. `--apply [--out DIR]` copies DIR/master.png to
assets/logo/saturn_logo_clean.png and stops (the generators regenerate the assets). The upscaler lives in
build/tools/realesrgan/ (gitignored); nothing of it ships."""
from __future__ import annotations

import argparse
import datetime
import hashlib
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from collections import Counter, deque
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "assets" / "logo" / "saturn_logo.png"
CLEAN = ROOT / "assets" / "logo" / "saturn_logo_clean.png"
TOOL_DIR = ROOT / "build" / "tools" / "realesrgan"
REVIEW_DIR = ROOT / "build" / "logo_review"
ZIP_NAME = "realesrgan-ncnn-vulkan-20220424-windows.zip"
ZIP_URL = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/" + ZIP_NAME
# sha256sum of the release zip, first downloaded 2026-11-20
PINNED_SHA256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d"
MODEL = "realesrgan-x4plus-anime"
EXE = "realesrgan-ncnn-vulkan.exe"
BIG = (152, 101)              # tools/omsat/gen_biglogo.py W, H
HDR = (40, 25)                # tools/omsat/gen_logo.py W, H
FOOT = (22, 15)               # the loading screen's footer logo (menu/src/layout.h LD_LOGO); stored 24 wide (HWF-06)

# where each image sits on the 320 x 224 screen (menu/views/interstitial.c CX - 76, CY - 50; menu/src/layout.h LOGO and
# LD_LOGO)
BIG_AT = (84, 46)
HDR_AT = (10, 8)
FOOT_BOX = (16, 200, 37, 214)                 # the loading screen's footer logo box, 22 x 15 (its own asset since review 2;
                                              # before, the header texture scaled into it -- the sheet's "old" view)
# menu/src/theme.c k_builtin: the background gradient's top and bottom
THEMES = {"saturn": (0x1C3C8C, 0x0A1438), "aether": (0x202020, 0x383838)}


# --- the upscaler ---------------------------------------------------------------------------------------------------

def _sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(dest: Path = TOOL_DIR) -> Path | None:
    """realesrgan-ncnn-vulkan (pinned release zip and SHA-256) in dest, downloaded once; None on any failure."""
    try:
        dest.mkdir(parents=True, exist_ok=True)
        z = dest / ZIP_NAME
        if not z.is_file() or _sha256(z) != PINNED_SHA256:
            tmp = dest / (ZIP_NAME + ".part")
            with urllib.request.urlopen(ZIP_URL, timeout=120) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f)
            if _sha256(tmp) != PINNED_SHA256:
                tmp.unlink()
                return None
            tmp.replace(z)
        exe = dest / EXE
        if not exe.is_file() or not (dest / "models" / f"{MODEL}.param").is_file():
            with zipfile.ZipFile(z) as zf:
                zf.extractall(dest)
        return exe if exe.is_file() else None
    except Exception:                       # noqa: BLE001 -- any failure means the Lanczos path
        return None


def _lanczos_x4(img: Image.Image) -> Image.Image:
    return img.resize((img.width * 4, img.height * 4), Image.LANCZOS)


def upscale(img: Image.Image, tool: Path | None) -> tuple[Image.Image, str]:
    """x4 RGBA and the method that ran: "realesrgan" or "lanczos" (no tool, a non-zero exit, a timeout, no Vulkan)."""
    img = img.convert("RGBA")
    alpha = _lanczos_x4(img.getchannel("A"))
    rgb = img.convert("RGB")
    if tool is not None:
        try:
            with tempfile.TemporaryDirectory() as td:
                src, out = Path(td) / "in.png", Path(td) / "out.png"
                rgb.save(src)
                r = subprocess.run([str(tool), "-i", str(src), "-o", str(out), "-n", MODEL, "-s", "4"],
                                   cwd=str(Path(tool).parent), capture_output=True, timeout=120)
                if r.returncode == 0 and out.is_file():
                    with Image.open(out) as up:
                        up = up.convert("RGB")
                    if up.size == alpha.size:
                        up.putalpha(alpha)
                        return up, "realesrgan"
        except (OSError, subprocess.SubprocessError):
            pass
    up = _lanczos_x4(rgb).filter(ImageFilter.UnsharpMask(radius=2, percent=80, threshold=2))
    up.putalpha(alpha)
    return up, "lanczos"


# --- the matte and the sizes ----------------------------------------------------------------------------------------

def _smoothstep_lut(lo: float = 0.35, hi: float = 0.65) -> list[int]:
    lut = []
    for v in range(256):
        t = min(max((v / 255 - lo) / (hi - lo), 0.0), 1.0)
        lut.append(round(t * t * (3 - 2 * t) * 255))
    return lut


def clean_matte(img: Image.Image, radius: int = 3) -> Image.Image:
    """Alpha through smoothstep(0.35, 0.65); colors of not-fully-opaque pixels from the nearest opaque pixel within
    `radius` px (Euclidean, ties in a fixed scan order); fully transparent pixels -> (0, 0, 0, 0)."""
    img = img.convert("RGBA")
    w, h = img.size
    a = list(img.getchannel("A").point(_smoothstep_lut()).getdata())
    rgb = list(img.convert("RGB").getdata())
    offs = sorted(((dx * dx + dy * dy, dy, dx) for dy in range(-radius, radius + 1)
                   for dx in range(-radius, radius + 1) if 0 < dx * dx + dy * dy <= radius * radius))
    out = []
    for i, (c, al) in enumerate(zip(rgb, a)):
        if al == 0:
            out.append((0, 0, 0, 0))
            continue
        if al < 255:
            y, x = divmod(i, w)
            for _, dy, dx in offs:
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and a[yy * w + xx] == 255:
                    c = rgb[yy * w + xx]
                    break
        out.append((*c, al))
    res = Image.new("RGBA", (w, h))
    res.putdata(out)
    return res


def _quantize_alpha(im: Image.Image) -> Image.Image:
    px = []
    for r, g, b, a in im.getdata():
        a = round(a / 17) * 17
        px.append((r, g, b, a) if a >= 17 else (0, 0, 0, 0))
    res = Image.new("RGBA", im.size)
    res.putdata(px)
    return res


def render(master: Image.Image) -> tuple[Image.Image, Image.Image]:
    """(152 x 101, 40 x 25), LANCZOS with reducing_gap 3.0, alpha in 16 levels (k x 17), alpha < 17 transparent."""
    return render_size(master, BIG), render_size(master, HDR)


def render_size(master: Image.Image, size: tuple[int, int]) -> Image.Image:
    """One size: LANCZOS with reducing_gap 3.0, alpha in 16 levels (k x 17), alpha < 17 transparent."""
    return _quantize_alpha(master.convert("RGBA").resize(size, Image.LANCZOS, reducing_gap=3.0))


# --- the menu's view of an image (RGB555, Task 6's blend over the real backdrop) -------------------------------------

def _rgb555(rgb: int) -> int:                 # menu/src/theme.c om_rgb555
    return ((rgb >> 3) & 31) << 10 | ((rgb >> 11) & 31) << 5 | ((rgb >> 19) & 31)


def gradient(top: int, bottom: int, w: int = 320, h: int = 224) -> list[list[int]]:
    """menu/ui/vdp2theme.c om_v2_bg_gradient: RGB555 rows with its 4x4 ordered dither."""
    bayer = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    t5, b5 = _rgb555(top), _rgb555(bottom)
    rows = []
    for y in range(h):
        c16 = [(((t5 >> 5 * k) & 31) * 16 * (h - 1 - y) + ((b5 >> 5 * k) & 31) * 16 * y) // (h - 1) for k in range(3)]
        row = []
        for x in range(w):
            t = bayer[y & 3][x & 3]
            c = 0
            for k in range(3):
                v = (c16[k] >> 4) + (1 if (c16[k] & 15) > t else 0)
                c |= min(v, 31) << 5 * k
            row.append(c)
        rows.append(row)
    return rows


def loading_backdrop() -> list[list[int]]:
    """The loading screen's LOADBG.BIN keyframe (menu/app/assets/LOADBG.BIN, 320 x 224) as RGB555 rows."""
    from .bootanim import decode_frame
    from .loadbg import parse
    _h, w, hh, _scale, pal, frames, _crcs = parse((ROOT / "menu" / "app" / "assets" / "LOADBG.BIN").read_bytes())
    idx = decode_frame(bytes(w * hh), frames[0])
    return [[pal[idx[y * w + x]] & 0x7FFF for x in range(w)] for y in range(hh)]


def mix555(fg: int, bg: int, a: int) -> int:  # menu/src/logoblend.c om_mix555 (without bit 15)
    return sum(((((fg >> s) & 31) * a + ((bg >> s) & 31) * (255 - a) + 127) // 255) << s for s in (0, 5, 10))


def to555(im: Image.Image) -> list[tuple[int, int]]:
    """RGBA -> (RGB555, alpha) per pixel, as the generators store it (each channel >> 3)."""
    return [((b >> 3) << 10 | (g >> 3) << 5 | (r >> 3), a) for r, g, b, a in im.convert("RGBA").getdata()]


def upload(px: list[tuple[int, int]], w: int, at: tuple[int, int], back: list[list[int]]) -> list[int]:
    """What the menu writes to VDP1 VRAM: alpha 255 -> the color, 0 -> transparent (-1 here), else blended with the
    backdrop at the pixel's screen position (Task 6's rule; 1-bit transparency nowhere else)."""
    out = []
    for i, (c, a) in enumerate(px):
        y, x = divmod(i, w)
        out.append(-1 if a == 0 else c if a == 255 else mix555(c, back[at[1] + y][at[0] + x], a))
    return out


def composite(vram: list[int], w: int, h: int, box: tuple[int, int, int, int], back: list[list[int]],
              pad: int = 4) -> Image.Image:
    """The screen around box (x0, y0, x1, y1): the backdrop, then the sprite drawn there (scaled nearest-neighbor when
    the box is not w x h, as VDP1 does for the loading footer), transparent code shows the backdrop."""
    x0, y0, x1, y1 = box
    bw, bh = x1 - x0 + 1, y1 - y0 + 1
    X0, Y0 = max(x0 - pad, 0), max(y0 - pad, 0)
    X1, Y1 = min(x1 + pad, len(back[0]) - 1), min(y1 + pad, len(back) - 1)
    img = Image.new("RGB", (X1 - X0 + 1, Y1 - Y0 + 1))
    pix = []
    for y in range(Y0, Y1 + 1):
        for x in range(X0, X1 + 1):
            c = back[y][x]
            if x0 <= x <= x1 and y0 <= y <= y1:
                s = vram[((y - y0) * h // bh) * w + (x - x0) * w // bw]
                if s >= 0:
                    c = s
            pix.append(((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31))
    img.putdata(pix)
    return img




# --- color decontamination at the source's resolution -----------------------------------
# The source is 1-bit (alpha 0 / 255 only): its anti-aliasing was matted on WHITE, so the opaque pixels along the
# silhouette carry white mixed into their color (measured: 128 of 1,118 pixels one step from transparency are more than
# 8 luma lighter than the interior next to them, 70 of 1,127 at two steps; at three steps the excess is shading). Only
# that band, chosen geometrically from the mask, is changed. Interior pixels keep their exact colors, whites included.

WHITE = (255, 255, 255)
BAND = 2                      # source px: opaque pixels within 2 px (chessboard) of transparency form the edge band
# Below this distance from white (RGB length) a reference color cannot tell coverage from color (white tube ends next
# to the white matte): alpha then comes from the shape (the mask, smoothed by the x4 upscale and the smoothstep) and the
# color stays as observed. 64 = a quarter of the 0..255 range: below it the projection's error exceeds a 16-level step.
ALPHA_FROM_COLOR_MIN = 64
FRINGE_TOL = 8                # luma (Rec.601, 0..255): about one RGB555 step; the no-light-fringe gate's tolerance
FRINGE_RADIUS = 3
COLORS = 128                  # tools/omsat/gen_biglogo.py COLORS: the big logo keeps n <= COLORS - 1 entries
EDGE_PAIRS = 24               # (color, alpha) palette pairs for the big logo's soft edge


def _luma(c: tuple[int, int, int]) -> float:
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def _bfs(w: int, h: int, seeds: list[int]) -> tuple[list[int], list[int]]:
    """Chessboard distance to the nearest seed and that seed's index (-1, -1 when there are no seeds)."""
    dist, near = [-1] * (w * h), [-1] * (w * h)
    q = deque(seeds)
    for i in seeds:
        dist[i], near[i] = 0, i
    while q:
        i = q.popleft()
        y, x = divmod(i, w)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                X, Y = x + dx, y + dy
                if 0 <= X < w and 0 <= Y < h and dist[Y * w + X] < 0:
                    j = Y * w + X
                    dist[j], near[j] = dist[i] + 1, near[i]
                    q.append(j)
    return dist, near


def decontaminate(img: Image.Image, band: int = BAND) -> tuple[Image.Image, list[int]]:
    """-> (RGBA with soft edge alpha and the white matte removed, the path per pixel: 0 untouched, 1 alpha from color
    (un-premultiplied against white), 2 alpha from shape (color kept), 3 replaced by the interior reference).
    Transparent pixels take the nearest logo color (bleed), so no later resample pulls white in."""
    img = img.convert("RGBA")
    w, h = img.size
    n = w * h
    rgb = list(img.convert("RGB").getdata())
    opaque = [v >= 128 for v in img.getchannel("A").getdata()]
    dist, _ = _bfs(w, h, [i for i in range(n) if not opaque[i]])
    interior = [opaque[i] and (dist[i] < 0 or dist[i] > band) for i in range(n)]
    _, near = _bfs(w, h, [i for i in range(n) if interior[i]])
    out_rgb, out_a, path = list(rgb), [255 if o else 0 for o in opaque], [0] * n
    for i in range(n):
        if not opaque[i] or interior[i]:
            continue
        c = rgb[i]
        ref = rgb[near[i]] if near[i] >= 0 else c
        y, x = divmod(i, w)
        refs = [_luma(rgb[Y * w + X]) for Y in range(max(0, y - FRINGE_RADIUS), min(h, y + FRINGE_RADIUS + 1))
                for X in range(max(0, x - FRINGE_RADIUS), min(w, x + FRINGE_RADIUS + 1)) if interior[Y * w + X]]
        top = max(refs) if refs else _luma(ref)
        v = [ref[k] - WHITE[k] for k in range(3)]
        vv = sum(t * t for t in v)
        if vv >= ALPHA_FROM_COLOR_MIN ** 2:
            u = [c[k] - WHITE[k] for k in range(3)]
            al = min(max(sum(u[k] * v[k] for k in range(3)) / vv, 0.0), 1.0)
            a8 = round(al * 255)
            true = tuple(min(max(round(WHITE[k] + u[k] / al), 0), 255) for k in range(3)) if a8 else ref
            path[i] = 1
        else:
            a8, true, path[i] = 255, c, 2
        if _luma(true) > top + FRINGE_TOL:
            true, path[i] = ref, 3
        out_rgb[i], out_a[i] = true, a8
    _, nv = _bfs(w, h, [i for i in range(n) if out_a[i]])
    res = Image.new("RGBA", (w, h))
    res.putdata([(*(out_rgb[i] if out_a[i] or nv[i] < 0 else out_rgb[nv[i]]), out_a[i]) for i in range(n)])
    return res, path


def path_map(img: Image.Image, path: list[int]) -> Image.Image:
    """Debug: the path each band pixel took (blue = alpha from color, red = alpha from shape, yellow = replaced by the
    interior reference; the rest dimmed), at 4x."""
    cols = {1: (60, 140, 255), 2: (255, 60, 60), 3: (255, 220, 0)}
    base = img.convert("RGB").point(lambda v: v // 3)
    base.putdata([cols.get(p, c) for p, c in zip(path, base.getdata())])
    return base.resize((img.width * 4, img.height * 4), Image.NEAREST)


# --- the final assets (what the menu stores) ------------------------------------------------------------------------

def render_footer(master: Image.Image) -> Image.Image:
    """The loading screen's own footer logo, 22 x 15 (menu/src/layout.h LD_LOGO), from the master like the others."""
    return render_size(master, FOOT)


def _edge_depth(alpha: list[int], w: int, h: int) -> list[int]:
    """Chessboard distance from transparency or the image's border (the gate's notion of depth)."""
    dist, _ = _bfs(w, h, [i for i in range(w * h) if alpha[i] == 0 or i % w in (0, w - 1) or i // w in (0, h - 1)])
    return dist


def limit_edges(rgb: list[tuple[int, int, int]], alpha: list[int], w: int, h: int,
                radius: int = FRINGE_RADIUS) -> list[tuple[int, int, int]]:
    """No edge pixel lighter than the opaque pixels near it that lie at least as deep in the shape (the gate's
    reference): a pixel within `radius` of transparency whose luma exceeds that reference is scaled down to it (hue
    kept). Deepest first, repeated until nothing changes, so a run of equal-depth pixels settles together."""
    dist = _edge_depth(alpha, w, h)
    rgb = list(rgb)
    order = sorted((i for i in range(w * h) if alpha[i] and 0 <= dist[i] <= radius), key=lambda i: (-dist[i], i))
    for _ in range(8):
        changed = False
        for i in order:
            y, x = divmod(i, w)
            refs = [_luma(rgb[Y * w + X]) for Y in range(max(0, y - radius), min(h, y + radius + 1))
                    for X in range(max(0, x - radius), min(w, x + radius + 1))
                    if Y * w + X != i and alpha[Y * w + X] == 255 and dist[Y * w + X] >= dist[i]]
            lm = _luma(rgb[i])
            if refs and lm > max(refs) + 0.5:
                f = max(refs) / lm
                rgb[i] = tuple(int(v * f) for v in rgb[i])
                changed = True
        if not changed:
            break
    return rgb


def finalize(im: Image.Image) -> list[tuple[int, int]]:
    """(RGB555, alpha) per pixel as stored: every soft-edge pixel takes the color of the nearest opaque pixel (bleed: the
    edge carries the interior's color), no edge pixel stays lighter than the interior next to it (limit_edges), then
    RGB555."""
    im = im.convert("RGBA")
    w, h = im.size
    px = list(im.getdata())
    _, near = _bfs(w, h, [i for i, p in enumerate(px) if p[3] == 255])
    rgb = [px[near[i]][:3] if 0 < p[3] < 255 and near[i] >= 0 else p[:3] for i, p in enumerate(px)]
    alpha = [p[3] for p in px]
    rgb = limit_edges(rgb, alpha, w, h)
    return [((b >> 3) << 10 | (g >> 3) << 5 | (r >> 3), a) if a else (0, 0) for (r, g, b), a in zip(rgb, alpha)]


def _c8(c: int) -> tuple[int, int, int]:
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)


def palettize_big(px: list[tuple[int, int]], w: int = BIG[0], colors: int = COLORS, k: int = EDGE_PAIRS):
    """The big logo's palette (gen_biglogo v2): median cut over the opaque pixels (colors - 1 - k entries), then the k
    most frequent (color, alpha) edge pairs. An edge pixel takes whichever looks closest over a dark backdrop
    (premultiplied error): a pair whose color is not lighter than its own, or rounding to opaque (the nearest
    not-lighter entry), or to transparent; an opaque pixel near the edge never takes a lighter entry. -> (indices, palette words (bit 15 clear), alpha bytes, the pixels as shown (RGB555, alpha))."""
    op = [c for c, a in px if a == 255]
    src = Image.new("RGB", (len(op), 1))
    src.putdata([_c8(c) for c in op])
    q = src.quantize(colors - 1 - k, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qp = q.getpalette()
    used = sorted(set(q.tobytes()))
    words = list(dict.fromkeys((qp[3 * u + 2] >> 3) << 10 | (qp[3 * u + 1] >> 3) << 5 | (qp[3 * u] >> 3)
                               for u in used))
    rgbs = [_c8(c) for c in words]
    cache: dict = {}

    def nearest(c: int, not_lighter: bool) -> int:
        key = (c, not_lighter)
        if key not in cache:
            t = _c8(c)
            lim = _luma(t) + FRINGE_TOL / 2
            cand = [j for j in range(len(rgbs)) if not not_lighter or _luma(rgbs[j]) <= lim]
            cache[key] = (min(cand, key=lambda j: (sum((rgbs[j][m] - t[m]) ** 2 for m in range(3)), j)) if cand
                          else min(range(len(rgbs)), key=lambda j: _luma(rgbs[j])))
        return cache[key]

    depth = _edge_depth([a for _c, a in px], w, len(px) // w)
    pairs = Counter((nearest(c, True), a) for c, a in px if 0 < a < 255)
    chosen = [p for p, _ in sorted(pairs.items(), key=lambda kv: (-kv[1], kv[0]))[:k]]
    pal = words + [words[j] for j, _ in chosen]
    alpha = [255] * len(words) + [a for _, a in chosen]
    idx, shown = [], []
    for i, (c, a) in enumerate(px):
        if a == 0:
            idx.append(0)
            shown.append((0, 0))
        elif a == 255:
            j = nearest(c, 0 <= depth[i] <= FRINGE_RADIUS)      # near the edge: never a lighter entry
            idx.append(j + 1)
            shown.append((words[j], 255))
        else:
            # the option that looks closest over a dark backdrop (premultiplied error): an edge pair whose color is not
            # lighter, or rounding to opaque (the nearest not-lighter entry), or to transparent
            t = _c8(c)
            pm = [t[z] * a for z in range(3)]
            j0 = nearest(c, True)
            opts = [(sum((rgbs[j0][z] * 255 - pm[z]) ** 2 for z in range(3)), 0, j0 + 1, (words[j0], 255)),
                    (sum(v * v for v in pm), 1, 0, (0, 0))]
            for m, (j, pa) in enumerate(chosen):
                if _luma(rgbs[j]) <= _luma(t) + FRINGE_TOL / 2:
                    opts.append((sum((rgbs[j][z] * pa - pm[z]) ** 2 for z in range(3)), 2 + m, len(words) + m + 1,
                                 (words[j], pa)))
            _e, _k, ix, sh = min(opts)
            idx.append(ix)
            shown.append(sh)
    return bytes(idx), pal, alpha, shown


def final_assets(master: Image.Image) -> dict:
    """{"big", "hdr", "foot"} -> (w, h, pixels as the menu stores them: (RGB555, alpha)); the big logo palettized.
    "big_n": its palette size."""
    big, hdr = render(master)
    _i, pal, _a, shown = palettize_big(finalize(big))
    return {"big": (*BIG, shown), "hdr": (*HDR, finalize(hdr)), "foot": (22, 15, finalize(render_footer(master))),
            "big_n": len(pal), "big_edge_alpha": sorted({a for _c, a in shown if 0 < a < 255})}


def old_assets() -> dict:
    """The logo as it shipped before M6 Task 7 (1-bit, from the original saturn_logo.png), in final_assets' shape. The
    loading footer then was the header texture scaled by VDP1 ("foot" here is that header texture; the sheet scales
    it)."""
    from . import gen_biglogo, gen_logo
    big = gen_biglogo.v1_pixels(SRC)
    hdr = gen_logo.v1_pixels(SRC)
    conv = [[(c & 0x7FFF, 255) if c & 0x8000 else (0, 0) for c in ws] for ws in (big, hdr)]
    return {"big": (*BIG, conv[0]), "hdr": (*HDR, conv[1]), "foot": (*HDR, conv[1])}


# --- the gate: no edge pixel lighter than the opaque interior next to it --------------------------------------------

def fringe_violations(px: list[tuple[int, int]], w: int, h: int, back: int, radius: int = FRINGE_RADIUS,
                      tol: float = FRINGE_TOL) -> tuple[list[tuple[int, int, float]], int, int]:
    """The image blended over a flat backdrop (RGB555) by Task 6's rule. Checked: every pixel with alpha > 0 within
    `radius` px of a transparent pixel or the image's border. Its reference: the max luma of the opaque pixels within
    `radius` px that lie at least as deep inside the shape (as far from transparency) as it does, itself excluded: the
    interior next to it, and the run of the same edge it belongs to -- a tube's highlight along its rim passes, a
    speck that differs from both fails; none: skipped. A violation is luma > reference + tol, so white next to white passes and white next to
    blue fails. -> (violations (x, y, excess), checked, skipped)."""
    n = w * h
    comp = [None if a == 0 else c if a == 255 else mix555(c, back, a) for c, a in px]
    lum = [None if c is None else _luma(_c8(c)) for c in comp]
    dist, _ = _bfs(w, h, [i for i in range(n) if px[i][1] == 0 or i % w in (0, w - 1) or i // w in (0, h - 1)])
    bad, checked, skipped = [], 0, 0
    for i in range(n):
        if px[i][1] == 0 or dist[i] > radius:
            continue
        y, x = divmod(i, w)
        win = [Y * w + X for Y in range(max(0, y - radius), min(h, y + radius + 1))
               for X in range(max(0, x - radius), min(w, x + radius + 1)) if Y * w + X != i and px[Y * w + X][1] == 255]
        refs = [lum[j] for j in win if dist[j] >= dist[i]]
        if not refs:
            skipped += 1
            continue
        checked += 1
        if lum[i] > max(refs) + tol:
            bad.append((x, y, round(lum[i] - max(refs), 1)))
    return bad, checked, skipped


def loading_mean() -> int:
    """The mean RGB555 of LOADBG.BIN under the footer logo over all its frames: the backdrop the footer asset is blended
    for (measured: each channel stays within one RGB555 step there, about (1, 1, 3); the scrim keeps it flat)."""
    try:
        from .loadbg import decode_loop, parse
        _h, w, hh, _s, pal, frames, crcs = parse((ROOT / "menu" / "app" / "assets" / "LOADBG.BIN").read_bytes())
        fr = decode_loop(w, hh, frames, crcs, passes=1)
    except Exception:                         # noqa: BLE001 -- no LOADBG.BIN: the measured value
        return 3 << 10 | 1 << 5 | 1
    x0, y0, x1, y1 = FOOT_BOX
    s, cnt = [0, 0, 0], 0
    for f in fr:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                c = pal[f[y * w + x]]
                for k in range(3):
                    s[k] += (c >> 5 * k) & 31
                cnt += 1
    return sum(round(s[k] / cnt) << 5 * k for k in range(3))


def gate(assets: dict) -> tuple[int, list[str]]:
    """The gate over black and over the saturn theme's darkest blue (its gradient's bottom); the footer also over its
    LOADBG backdrop. -> (total violations, one line per (asset, backdrop))."""
    total, lines = 0, []
    for name in ("big", "hdr", "foot"):
        w, h, px = assets[name]
        backs = {"black": 0, "saturn darkest blue": _rgb555(THEMES["saturn"][1])}
        if name == "foot":
            backs["LOADBG mean"] = loading_mean()
        for bn, b in backs.items():
            bad, ch, sk = fringe_violations(px, w, h, b)
            total += len(bad)
            lines.append(f"{name} over {bn}: {len(bad)} violations, {ch} checked, {sk} skipped"
                         + (f"; worst {sorted(bad, key=lambda t: -t[2])[:3]}" if bad else ""))
    return total, lines


# --- the review sheet -----------------------------------------------------------------------------------------------

def _font(size: int):
    try:
        return ImageFont.load_default(size)
    except TypeError:                         # Pillow < 10.1: the fixed bitmap font
        return ImageFont.load_default()


def _zoom(im: Image.Image, k: int) -> Image.Image:
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def _backs() -> dict:
    backs = {n: gradient(*THEMES[n]) for n in THEMES}
    backs["black"] = [[0] * 320 for _ in range(224)]
    try:
        backs["loading"] = loading_backdrop()
    except Exception:                         # noqa: BLE001 -- no LOADBG.BIN: a dark stand-in
        backs["loading"] = [[loading_mean()] * 320 for _ in range(224)]
    return backs


def _views(assets: dict, backs: dict, new: bool) -> dict:
    """Each screen the logo appears on -> the composited native-size image (pad 4 px of backdrop around it)."""
    out = {}
    for name in ("saturn", "aether", "black"):
        back = backs[name]
        for key, at in (("big", BIG_AT), ("hdr", HDR_AT)):
            w, h, px = assets[key]
            out[(key, name)] = composite(upload(px, w, at, back), w, h, (at[0], at[1], at[0] + w - 1, at[1] + h - 1),
                                         back)
    w, h, px = assets["foot"]
    if new:                                   # its own 22 x 15 asset, blended for the LOADBG backdrop
        flat = [[loading_mean()] * 320 for _ in range(224)]
        out[("foot", "loading")] = composite(upload(px, w, FOOT_BOX[:2], flat), w, h, FOOT_BOX, backs["loading"])
    else:                                     # today: the header texture (blended for the header) scaled by VDP1
        out[("foot", "loading")] = composite(upload(px, w, HDR_AT, backs["saturn"]), w, h, FOOT_BOX, backs["loading"])
    return out


def _edge_boxes(px: list[tuple[int, int]], w: int, h: int, size: int) -> list[tuple[str, tuple[int, int, int, int]]]:
    """Crop boxes (in the padded composite, pad 4) around the mask's extremes: the left tube ends (upper and lower
    half), the right tube end, the sphere's top rim."""
    pts = [(i % w, i // w) for i, (_c, a) in enumerate(px) if a]

    def box(x, y):
        x0 = min(max(x + 4 - size // 2, 0), w + 8 - size)
        y0 = min(max(y + 4 - size // 2, 0), h + 8 - size)
        return (x0, y0, x0 + size, y0 + size)
    top = [p for p in pts if p[1] < h * 2 // 5]           # the upper tube runs right of the sphere
    bot = [p for p in pts if p[1] >= h * 7 // 10]         # the lower tube runs left of it
    mid = [p for p in pts if h * 2 // 5 <= p[1] < h * 7 // 10]
    return [("upper tube end (right)", box(*max(top))), ("lower tube end (left)", box(*min(bot))),
            ("ring, left loop", box(*min(mid))), ("sphere top rim", box(*min(pts, key=lambda p: (p[1], p[0]))))]


def review_sheet(old: dict, new: dict, method: str, gate_lines: list[str], backs: dict | None = None) -> Image.Image:
    """OLD (shipped) vs NEW (final, palette-reduced) rows, over each screen the logo appears on, at 1x, 4x, 8x, and
    16x crops of the edges over black (tube ends, sphere rim)."""
    backs = backs or _backs()
    vo, vn = _views(old, backs, False), _views(new, backs, True)
    f_h, f_t, f_s = _font(22), _font(16), _font(13)
    gap = 16
    cols = [("saturn theme", "saturn"), ("aether theme", "aether"), ("black", "black")]
    blocks = [
        ("1x (native size): big logo and header over each theme; the loading footer over LOADBG.BIN",
         [(f"big, {l}", ("big", k), 1) for l, k in cols] + [(f"header, {l}", ("hdr", k), 1) for l, k in cols]
         + [("loading footer", ("foot", "loading"), 1)]),
        ("Interstitial / boot: big logo 152 x 101, 4x nearest", [(l, ("big", k), 4) for l, k in cols]),
        ("Menu header: logo 40 x 25, 8x nearest", [(l, ("hdr", k), 8) for l, k in cols]),
        ("Loading screen footer, 22 x 15 at 8x and 16x. OLD: the header texture scaled by VDP1. NEW: its own asset, "
         f"blended for LOADBG's mean under it (RGB555 {_c8(loading_mean())})",
         [("8x", ("foot", "loading"), 8), ("16x", ("foot", "loading"), 16)]),
    ]
    rendered = [(t, [(l, _zoom(vo[k], z)) for l, k, z in items], [(l, _zoom(vn[k], z)) for l, k, z in items])
                for t, items in blocks]
    bw, bh, bpx = new["big"]
    crops = _edge_boxes(bpx, bw, bh, 16)
    rendered.append(("Big logo edges over BLACK, 16x (crops at the mask's extremes: the two tube ends, the ring, the sphere rim)",
                     [(l, _zoom(vo[("big", "black")].crop(b), 16)) for l, b in crops],
                     [(l, _zoom(vn[("big", "black")].crop(b), 16)) for l, b in crops]))
    rendered.append(("Big logo edges over the saturn theme, 16x",
                     [(l, _zoom(vo[("big", "saturn")].crop(b), 16)) for l, b in crops],
                     [(l, _zoom(vn[("big", "saturn")].crop(b), 16)) for l, b in crops]))
    hw_, hh_, hpx = new["hdr"]
    hcrops = _edge_boxes(hpx, hw_, hh_, 10)
    rendered.append(("Header logo edges over BLACK, 16x",
                     [(l, _zoom(vo[("hdr", "black")].crop(b), 16)) for l, b in hcrops],
                     [(l, _zoom(vn[("hdr", "black")].crop(b), 16)) for l, b in hcrops]))

    rowlab = 70
    width = max(rowlab + sum(im.width + gap for _, im in co) for _, co, _ in rendered) + gap
    width = max(width, 1500)
    head = 104 + 20 * len(gate_lines)
    height = head + sum(60 + 2 * (max(im.height for _, im in co) + 24 + gap) for _, co, _ in rendered)
    sheet = Image.new("RGB", (width, height), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    d.text((gap, 12), "Aether Saturn logo, review 2: OLD (shipped today) vs NEW (final assets, as the menu shows them)",
           (255, 255, 255), font=f_h)
    d.text((gap, 44), f"Upscale: {method}. NEW: white matte removed from the edge band only (alpha from color, or from "
           "shape where the logo is white), interior colors kept; edge colors bled from the interior; x4, smoothstep, "
           "LANCZOS down; big logo palette-reduced (<= 127 entries).", (220, 220, 220), font=f_t)
    d.text((gap, 68), f"No-light-fringe gate (an edge pixel may not be lighter than the opaque interior within "
           f"{FRINGE_RADIUS} px by more than {FRINGE_TOL} luma), NEW:", (220, 220, 220), font=f_t)
    for k, line in enumerate(gate_lines):
        d.text((gap + 20, 90 + 20 * k), line, (170, 255, 170) if " 0 violations" in line else (255, 120, 120),
               font=f_s)
    y = head
    for title, co, cn in rendered:
        d.text((gap, y), title, (255, 230, 140), font=f_t)
        y += 28
        rh = max(im.height for _, im in co + cn)
        for row, lab in ((co, "OLD"), (cn, "NEW")):
            d.text((gap, y + 24 + rh // 2 - 8), lab, (255, 255, 255), font=f_h)
            x = rowlab
            for cap, im in row:
                d.text((x, y), cap, (200, 200, 200), font=f_s)
                sheet.paste(im, (x, y + 20))
                x += max(im.width, 110) + gap
            y += rh + 24 + gap
        y += 20
    return sheet.crop((0, 0, width, y + 10))


def build(tool: Path | None) -> tuple[Image.Image, str, dict, list[int], Image.Image]:
    """source -> (master, method, final assets, decontamination paths, decontaminated source)."""
    with Image.open(SRC) as src:
        dec, path = decontaminate(src)
    up, method = upscale(dec, tool)
    master = clean_matte(up)
    return master, method, final_assets(master), path, dec


def _to_rgba(w: int, h: int, px: list[tuple[int, int]]) -> Image.Image:
    im = Image.new("RGBA", (w, h))
    im.putdata([(*_c8(c), a) for c, a in px])
    return im


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m omsat.logoclean", description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--review", action="store_true", help="build the review sheet; touches no asset")
    g.add_argument("--apply", action="store_true", help="copy OUT/master.png to assets/logo/saturn_logo_clean.png")
    ap.add_argument("--out", type=Path, default=REVIEW_DIR)
    ap.add_argument("--no-fetch", action="store_true", help="do not fetch Real-ESRGAN: the Lanczos path")
    a = ap.parse_args(argv)
    if a.apply:
        src = a.out / "master.png"
        if not src.is_file():
            print(f"logoclean: {src} not found; run --review first")
            return 1
        shutil.copyfile(src, CLEAN)
        print(f"wrote {CLEAN}")
        return 0
    a.out.mkdir(parents=True, exist_ok=True)
    tool = None if a.no_fetch else fetch()
    master, method, assets, path, dec = build(tool)
    total, lines = gate(assets)
    master.save(a.out / "master.png")
    dec.save(a.out / "decontaminated.png")
    with Image.open(SRC) as src:
        path_map(src, path).save(a.out / "decontam_paths.png")
    _to_rgba(*assets["big"]).save(a.out / "new_big.png")
    _to_rgba(*assets["hdr"]).save(a.out / "new_hdr.png")
    _to_rgba(*assets["foot"]).save(a.out / "new_foot.png")
    review_sheet(old_assets(), assets, method, lines).save(a.out / "sheet.png")
    tool_sha = _sha256(TOOL_DIR / ZIP_NAME) if method == "realesrgan" and (TOOL_DIR / ZIP_NAME).is_file() else "-"
    counts = Counter(p for p in path if p)
    (a.out / "REVIEW.txt").write_text(
        f"method: {method}\n"
        f"model: {MODEL if method == 'realesrgan' else '- (LANCZOS x4 + UnsharpMask(2, 80, 2))'}\n"
        f"tool: {ZIP_URL if method == 'realesrgan' else '-'}\n"
        f"tool sha256: {tool_sha}\n"
        f"source: {SRC.relative_to(ROOT).as_posix()} sha256 {_sha256(SRC)}\n"
        f"decontamination (source px): alpha from color {counts[1]}, alpha from shape {counts[2]}, "
        f"replaced by interior {counts[3]}\n"
        f"big logo palette: n = {assets['big_n']}, edge alpha levels {assets['big_edge_alpha']}\n"
        f"gate (radius {FRINGE_RADIUS}, tolerance {FRINGE_TOL} luma): {total} violations\n  " + "\n  ".join(lines) + "\n"
        f"date: {datetime.date.today().isoformat()}\n", encoding="utf-8")
    print(f"logoclean: {method}; gate {total} violations; wrote {a.out / 'sheet.png'}")
    return 0 if total == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
