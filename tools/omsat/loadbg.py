"""LOADBG.BIN, the loading screen's animated background (contract/SPEC.md §5.11). Big-endian.

It reuses BOOTANIM.BIN's layout and its byte ops (tools/omsat/bootanim.py, codec 2's SKIP / LIT / COPY / FILL) with
its own magic, OMSATLBG, and codec 3, a LOOP:
- index entry 0 is the keyframe: frame 0 decoded over an all-zero frame. It comes first, so the menu can show it as
  soon as its bytes have arrived;
- entries 1..N are the N frames of a seamless loop: entry k holds loop frame k-1, decoded in place over frame k-2,
  and entry 1 (frame 0) over frame N-1 (the wrap). After the keyframe the menu applies the entries of frames 1, 2, ...,
  N-1, 0, 1, ... one per V-blank, in one buffer, for as long as the loading lasts.
- info: u16 width, u16 height, u16 fps (60), u16 codec (3), u32 0, u16 scale (the menu shows each pixel scale x scale
  on NBG1), u16 0. Each index CRC-32 is of the decoded pixels of that frame (entry 0 and entry 1: frame 0's).

The encoder is LOSSY on purpose (task-13c-report.md): at 320x224 the lossless loop is 4.9 MB and at
160x112 1.2 MB, far beyond the 256 KB the menu can borrow, so a pixel keeps its previous value while that value is
within `threshold` (max RGB channel difference, 0-255) of the true color ("conditional replenishment"). The ops and the
decoder are codec 2's unchanged; only which pixels are sent differs. The loop is closed by iterating (up to 6 passes):
the lossy frame 0 reached from frame N-1 becomes the new keyframe and the loop is re-encoded from it; the wrap (entry 1)
then sends frame 0 exactly, so the loop is periodic, and costs about four ordinary frames instead of a keyframe.
Measured: 160x112, 126 frames, threshold 28 -> 147 KB; 24 -> 171 KB; 32 -> 130 KB; lossless -> 1.2 MB.

The frames (build/loadbg/lb_*.png, 320x224 RGB, Real-ESRGAN + RIFE, see build_loadbg_pipeline.py there) are not
darkened, so `pack` applies the mockup's scrim first (loading_mockup_render.py: dark at the left, where the text is,
and at the bottom), then scales to width x height and quantizes to one 256-color RGB555 palette (no dither: dither
noise would change every pixel every frame).

`python -m omsat.loadbg pack --frames build/loadbg --out menu/app/assets/LOADBG.BIN [--threshold 23 --step 2]`
`python -m omsat.loadbg check FILE` decodes the keyframe and two passes of the loop against the CRCs."""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from . import binfmt as bf
from .bootanim import ENTRY, INDEX_OFF, INFO_OFF, INFO_SIZE, PAL_OFF, PAL_SIZE, _INFO, decode_frame, encode_frame

MAGIC = b"OMSATLBG"
CODEC_LOOP = 3
MAX_W, MAX_H = 320, 224
MAX_FRAMES = 511                     # + the keyframe = 512 index entries
# Where the menu keeps the file (menu/views/loading.c, Task 13c rounds 2-3): the card-scan work area (262,068 B HWRAM)
# holds the frame on screen (w x h), a 16 KB staging buffer, the SCU DMA tables and the head of the file -- the keyframe
# must lie there,
# it is decoded in place -- and the rest goes to sound RAM idle during loading: the music region 0x10000-0x2FFFF and
# the unclaimed 0x40000-0x77FFF (ledger D3; the SFX region and PCM_Work are never touched). A loop entry kept in sound
# RAM is brought into the staging buffer by DMA before it is applied, so none of those may exceed it.
ARENA = 262_068
STAGE = 16_384                                       # a loop entry kept in sound RAM fits it
DMA_TBL = 12 * MAX_H + 24                            # Task 13c round 3: the SCU DMA tables, in the same area
TBL_ALIGN = 4096                                     # TechBulletin SCU No. 25: the table on a 4,096 B boundary
SNDRAM = 0x20000 + 0x38000                           # 360,448 B
MAX_ENTRY = 65_535                                   # the keyframe (the menu's OM_LBG_MAX_ENTRY)


def capacity(w: int, h: int) -> tuple[int, int]:
    """-> (bytes of the file in HWRAM, total bytes the menu can hold)."""
    hw = (ARENA - ((w * h + 3) & ~3) - STAGE - 8 - TBL_ALIGN - DMA_TBL - 8) & ~3   # worst-case alignment slack
    return hw, hw + SNDRAM
SCRIM_RGB = (4, 8, 20)               # loading_mockup_render.py's NBG2 scrim color


def scrim_alpha(x: float, y: float) -> float:
    """loading_mockup_render.py, at native 320x224: dark at the left (text column) and at the bottom (footer)."""
    a = 0.22 + 0.66 * max(0.0, 1 - x / 190) ** 1.2 + 0.35 * max(0.0, (y - 160) / 64)
    return min(0.92, a)


def encode(pixels: list[bytes], key: bytes, width: int, height: int, palette: list[int], scale: int,
           build_date: int, fps: int = 60) -> bytes:
    """pixels[i] = the decoded (lossy) frame i, pixels[0] == key. Entry 0 codes zeros -> key; entry i + 1 codes
    pixels[i-1] -> pixels[i], and entry 1 pixels[N-1] -> key."""
    n = width * height
    if not pixels or any(len(p) != n for p in pixels) or pixels[0] != key:
        raise ValueError("every frame must be width x height and frame 0 must be the keyframe")
    cnt = len(pixels)
    frames = [encode_frame(bytes(n), key, width)]
    frames += [encode_frame(pixels[i - 1] if i else pixels[cnt - 1], pixels[i], width) for i in range(cnt)]
    crcs = [bf.crc32(key)] + [bf.crc32(p) for p in pixels]
    index_size = ENTRY * len(frames)
    data_off = bf.align(INDEX_OFF + index_size, 4)
    off, index, blob = data_off, b"", b""
    hw = capacity(width, height)[0]
    for k, (f, c) in enumerate(zip(frames, crcs)):
        if k and off < hw < off + len(f):         # round 3: no loop entry straddles the HWRAM / sound-RAM boundary
            blob += bytes(hw - off)               # (the menu DMAs an entry from sound RAM whole): pad up to it
            off = hw
        index += struct.pack(">III", off, len(f), c)
        off += len(f)
        blob += f
    h = bf.Header(magic=MAGIC, count=len(frames), table_off=INDEX_OFF, table_size=index_size, data_off=data_off,
                  data_size=off - data_off, build_date=build_date, table_crc=bf.crc32(index),
                  sections=((INFO_OFF, INFO_SIZE), (PAL_OFF, PAL_SIZE), (0, 0)))
    info = _INFO.pack(width, height, fps, CODEC_LOOP, 0, scale, 0)
    pal = b"".join(struct.pack(">H", c) for c in palette)
    body = bf.pack_header(h) + info + pal + index
    return body + bytes(data_off - len(body)) + blob


def parse(data: bytes):
    """-> (header, width, height, scale, palette, entries (keyframe, then the N loop frames), crcs)."""
    h = bf.validate_common(data, MAGIC)
    if h.sections[0] != (INFO_OFF, INFO_SIZE) or h.sections[1] != (PAL_OFF, PAL_SIZE) or h.sections[2] != (0, 0):
        raise bf.FormatError("section table must be (64,16) info, (80,512) palette, (0,0)")
    if h.table_off != INDEX_OFF or h.table_size != ENTRY * h.count:
        raise bf.FormatError("frame index must sit at 592 and be 12 B per entry")
    w, hh, fps, codec, zero, scale, _res = _INFO.unpack_from(data, INFO_OFF)
    if codec != CODEC_LOOP:
        raise bf.FormatError(f"codec {codec} unknown (3 = byte ops in place, a loop with a keyframe)")
    if fps not in (60, 30) or zero != 0:
        raise bf.FormatError(f"fps {fps} / field 8 {zero}: must be 60 or 30 / 0")
    pal = list(struct.unpack_from(">256H", data, PAL_OFF))
    frames, crcs, prev_end = [], [], h.data_off
    for i in range(h.count):
        off, ln, crc = struct.unpack_from(">III", data, INDEX_OFF + ENTRY * i)
        if off < prev_end or ln == 0 or off + ln > h.data_off + h.data_size or off + ln > len(data):
            raise bf.FormatError(f"entry {i}: range {off}+{ln} outside the data section or out of order")
        prev_end = off + ln
        frames.append(data[off:off + ln])
        crcs.append(crc)
    return h, w, hh, scale, pal, frames, crcs


def decode_loop(w: int, h: int, frames: list[bytes], crcs: list[int], passes: int = 2) -> list[bytes]:
    """The menu's order: the keyframe, then frames 1..N-1, 0 per pass. Returns frames 0..N-1 of the first pass."""
    n = len(frames) - 1
    cur = decode_frame(bytes(w * h), frames[0])
    if bf.crc32(cur) != crcs[0]:
        raise bf.FormatError("keyframe: decoded CRC-32 does not match its index entry")
    out = [cur]
    for p in range(passes):
        for k in range(1, n + 1):
            i = k % n
            cur = decode_frame(cur, frames[i + 1])
            if bf.crc32(cur) != crcs[i + 1]:
                raise bf.FormatError(f"pass {p}, frame {i}: decoded CRC-32 does not match its index entry")
            if p == 0 and i:
                out.append(cur)
    return out


def check_loadbg(data: bytes) -> list[str]:
    try:
        h, w, hh, scale, pal, frames, _crcs = parse(data)
    except (bf.FormatError, struct.error) as e:
        return [f"{type(e).__name__}: {e}"]
    errs = []
    if w % 2 or not 2 <= w <= MAX_W or not 1 <= hh <= MAX_H:
        errs.append(f"size {w}x{hh}: even width up to {MAX_W}, height 1..{MAX_H}")
    if scale not in (1, 2) or w * scale > MAX_W or hh * scale > MAX_H:
        errs.append(f"scale {scale}: 1 or 2, and the shown image at most {MAX_W}x{MAX_H}")
    if not 2 <= h.count <= MAX_FRAMES + 1:
        errs.append(f"{h.count} entries (2..{MAX_FRAMES + 1}: the loop plus its keyframe)")
    if any(c & 0x8000 for c in pal):
        errs.append("palette entry with bit15 set (VDP2 CRAM mode 0 is 15-bit, HWF-30)")
    if len(frames[0]) > MAX_ENTRY:
        errs.append(f"keyframe is {len(frames[0])} B > {MAX_ENTRY} B")
    hw, total = capacity(w, hh)
    offs0 = [struct.unpack_from(">I", data, INDEX_OFF + ENTRY * i)[0] for i in range(h.count)]
    big = [i for i, f in enumerate(frames) if i and len(f) > (STAGE if offs0[i] >= hw else MAX_ENTRY)]
    if big:
        errs.append(f"loop entries {big[:5]} exceed the {STAGE} B staging buffer (kept in sound RAM) or {MAX_ENTRY} B")
    if len(data) > total:
        errs.append(f"file is {len(data)} B > {total} B (HWRAM {hw} B + sound RAM {SNDRAM} B for a {w}x{hh} frame)")
    if h.data_off + len(frames[0]) > hw:
        errs.append(f"the keyframe ends at {h.data_off + len(frames[0])} B, past the {hw} B kept in HWRAM")
    offs = [struct.unpack_from(">I", data, INDEX_OFF + ENTRY * i)[0] for i in range(h.count)]
    cross = [i for i in range(1, h.count) if offs[i] < hw < offs[i] + len(frames[i])]
    if cross:
        errs.append(f"loop entries {cross} straddle the {hw} B HWRAM / sound-RAM boundary")
    if errs:
        return errs
    try:
        decode_loop(w, hh, frames, _crcs)
    except (bf.FormatError, IndexError, struct.error) as e:
        errs.append(f"{type(e).__name__}: {e}")
    return errs


# --- the packer (Pillow + numpy, PC only) ----------------------------------------------------------------------------
def load_rgb(d: Path, width: int, height: int, scrim: bool = True, blur: float = 0.0):
    import numpy as np
    from PIL import Image
    files = sorted(d.glob("lb_*.png"))
    if not files:
        raise SystemExit(f"loadbg: no lb_*.png in {d}")
    out = []
    for f in files:
        im = np.asarray(Image.open(f).convert("RGB"), dtype=np.float64)
        H, W = im.shape[:2]
        if scrim:
            x = np.arange(W)[None, :] * (320.0 / W)
            y = np.arange(H)[:, None] * (224.0 / H)
            a = np.minimum(0.92, 0.22 + 0.66 * np.maximum(0, 1 - x / 190) ** 1.2 + 0.35 * np.maximum(0, (y - 160) / 64))
            im = im * (1 - a[..., None]) + np.array(SCRIM_RGB, dtype=np.float64) * a[..., None]
        img = Image.fromarray(np.uint8(np.clip(np.rint(im), 0, 255)))
        if img.size != (width, height):
            img = img.resize((width, height), Image.LANCZOS)
        if blur:
            from PIL import ImageFilter
            img = img.filter(ImageFilter.GaussianBlur(blur))
        out.append(np.asarray(img, dtype=np.float64).reshape(-1, 3))
    return out


def make_palette(rgb):
    """256 RGB555 colors (median cut over a sample of every 8th frame), as (rgb555 list, float RGB of each entry)."""
    import numpy as np
    from PIL import Image
    tile = np.concatenate(rgb[::8], 0)
    img = Image.fromarray(np.uint8(tile).reshape(-1, 1, 3))
    p = img.quantize(256, method=Image.Quantize.MEDIANCUT).getpalette()[:768]
    p += [0] * (768 - len(p))
    pal555, palrgb = [], []
    for i in range(256):
        r, g, b = (p[3 * i] >> 3, p[3 * i + 1] >> 3, p[3 * i + 2] >> 3)
        pal555.append((b << 10) | (g << 5) | r)
        palrgb.append((r * 255 / 31, g * 255 / 31, b * 255 / 31))
    return pal555, np.array(palrgb)


def nearest(rgb, palrgb):
    import numpy as np
    d = ((rgb[:, None, :] - palrgb[None, :, :]) ** 2).sum(2)
    return d.argmin(1).astype(np.uint8)


def lossy_loop(rgb, palrgb, threshold: float, passes: int = 6, block=(1, 1), width: int = 0):
    """-> (frames as bytes, keyframe, converged). Frame i keeps frame i-1's pixels of a block (bw x bh) while their mean
    error (max RGB channel difference) is within threshold; otherwise the block is sent."""
    import numpy as np
    q = [nearest(f, palrgb) for f in rgb]
    bw, bh = block
    w = width
    h = len(rgb[0]) // w

    def step(ref, i):
        d = np.abs(palrgb[ref] - rgb[i]).max(1)
        if bw * bh > 1:
            m = d.reshape(h // bh, bh, w // bw, bw).mean((1, 3)) > threshold
            upd = np.repeat(np.repeat(m, bh, 0), bw, 1).reshape(-1)
        else:
            upd = d > threshold
        return np.where(upd, q[i], ref).astype(np.uint8)

    key, converged = q[0], False
    for _ in range(passes):
        dec, cur = [key], key
        for i in range(1, len(rgb)):
            cur = step(cur, i)
            dec.append(cur)
        nk = step(cur, 0)
        if np.array_equal(nk, key):
            converged = True
            break
        key = nk
    if not converged:                     # the last pass from the last keyframe: the wrap is then sent exactly
        dec, cur = [key], key
        for i in range(1, len(rgb)):
            cur = step(cur, i)
            dec.append(cur)
    return [d.tobytes() for d in dec], key.tobytes(), converged


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m omsat.loadbg")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--frames", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--scale", type=int, default=1, help="the menu enlarges each pixel scale x scale (NBG1 zoom)")
    p.add_argument("--step", type=int, default=2, help="keep every step-th source frame (2: the 60 fps loop at 30 fps)")
    p.add_argument("--threshold", type=float, default=23.0)
    p.add_argument("--block", default="1x1", help="replenishment block, W x H pixels")
    p.add_argument("--blur", type=float, default=0.0, help="Gaussian blur radius after scaling")
    p.add_argument("--no-scrim", action="store_true")
    p.add_argument("--preview", type=Path, help="write the decoded loop as 320x224 PNGs here")
    p.add_argument("--date", type=int, default=20261119)
    c = sub.add_parser("check")
    c.add_argument("file", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "pack":
        w, h = 320 // a.scale, 224 // a.scale
        rgb = load_rgb(a.frames, w, h, not a.no_scrim, a.blur)[::a.step]
        pal555, palrgb = make_palette(rgb)
        bw, bh = (int(v) for v in a.block.lower().split("x"))
        frames, key, conv = lossy_loop(rgb, palrgb, a.threshold, block=(bw, bh), width=w)
        data = encode(frames, key, w, h, pal555, a.scale, a.date, 60 // a.step)
        errs = check_loadbg(data)
        if errs:
            raise SystemExit("loadbg: packed file fails its own check: " + "; ".join(errs))
        a.out.write_bytes(data)
        _, _, _, _, _, ent, _ = parse(data)
        sizes = [len(f) for f in ent]
        print(f"loadbg: {a.out} {len(data)} B (capacity {capacity(w, h)[1]} B), {len(frames)} frames {w}x{h} "
              f"x{a.scale} at {60 // a.step} fps, threshold {a.threshold}, "
              f"loop {'converged' if conv else 'not converged (the wrap is sent exactly)'}; keyframe {sizes[0]} B, "
              f"frames avg {sum(sizes[1:]) / len(frames):.0f} B, max {max(sizes[1:])} B "
              f"(entry {sizes.index(max(sizes[1:]))}), wrap {sizes[1]} B")
        if a.preview:
            import numpy as np
            from PIL import Image
            a.preview.mkdir(parents=True, exist_ok=True)
            for i, f in enumerate(frames):
                img = Image.fromarray(np.uint8(palrgb[np.frombuffer(f, np.uint8)].reshape(h, w, 3)))
                img.resize((w * a.scale, h * a.scale), Image.NEAREST).save(a.preview / f"lbp_{i:04d}.png")
        return 0
    data = a.file.read_bytes()
    errs = check_loadbg(data)
    if errs:
        print(f"FAIL {a.file}")
        for e in errs:
            print(f"  - {e}")
        return 1
    print(f"PASS {a.file}: {len(data)} B, keyframe and two passes of the loop match their CRC-32s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
