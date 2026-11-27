"""BOOTANIM.BIN, the boot animation streamed from the menu disc (contract/SPEC.md §5.10). Big-endian.

Layout: the §5.1 common header (magic OMSATBAN), a 16-byte info block, the 256-color RGB555 palette, the frame
index (offset, length, CRC-32 of the decoded pixels per frame), then the frames back to back.

Codec 2 (ruling in task-13b-report.md): byte-oriented ops decoded IN PLACE over a reference frame. Even
frame 2k refers to frame 2k-2 and odd frame 2k+1 to frame 2k (frame -2 is all index 0, i.e. black), so the even
frames alone form a chain the menu can decode at 30 fps when the CD falls behind. Ops (see SPEC §5.10):
  00LLLLLL SKIP n        the reference pixels stay (the inter-frame delta; free on the SH-2)
  01LLLLLL LIT n, bytes  n new pixels
  10LLLLLL COPY n, dist  n pixels from dist back in this frame (dist <= position; overlapping, forward)
  11LLLLLL FILL n, byte  n copies of one pixel
L < 62: n = L + 1; L = 62: n = 63 + next byte; L = 63: n = next u16.
dist: b < 0x80: b + 1; else ((b & 0x7F) << 8 | next) + 129.
The encoder is an optimal parse (dynamic programming over byte cost) against skip runs, fill runs and back-references
at a fixed set of distances (neighbours in the row and the rows above).

`python -m omsat.bootanim pack --frames DIR --palette palette.bin --chant chant_offset.txt --out BOOTANIM.BIN`
`python -m omsat.bootanim check BOOTANIM.BIN [--frames DIR]` decodes every frame, checks its CRC-32 and, with
--frames, compares every frame byte for byte with the indexed PNG it came from."""
from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from . import binfmt as bf

MAGIC = b"OMSATBAN"
INFO_OFF, INFO_SIZE = 64, 16
PAL_OFF, PAL_SIZE = INFO_OFF + INFO_SIZE, 512
INDEX_OFF = PAL_OFF + PAL_SIZE
ENTRY = 12
CODEC_LZ_REF2 = 2
_INFO = struct.Struct(">HHHHIHH")           # width, height, fps, codec, chant_start_x100, flags, reserved
assert _INFO.size == INFO_SIZE
# Menu limits (menu/src/bootanim.h): the decode buffers, the ring and the CD-read cap. A writer must stay inside them.
MAX_W, MAX_H = 176, 128
MAX_FRAMES = 512
MAX_FRAME_BYTES = 16384
MAX_FILE = 4 * 1024 * 1024


@dataclass(frozen=True)
class Anim:
    width: int
    height: int
    fps: int
    chant_x100: int
    palette: list
    frames: list                             # compressed frame bytes
    crcs: list


def ref_of(i: int) -> int:
    """The frame index frame i's dictionary comes from; -2 = the all-black frame."""
    return i - 2 if i % 2 == 0 else i - 1


SKIP, LIT, COPY, FILL = 0, 1, 2, 3
MAX_N = 65535
MAX_DIST = 32896


def _len_bytes(op: int, n: int) -> bytes:
    if not 1 <= n <= MAX_N:
        raise ValueError(n)
    if n <= 62:
        return bytes([(op << 6) | (n - 1)])
    if n <= 318:
        return bytes([(op << 6) | 62, n - 63])
    return bytes([(op << 6) | 63]) + struct.pack(">H", n)


def _hdr_cost(n: int) -> int:
    return 1 if n <= 62 else (2 if n <= 318 else 3)


def _dist_bytes(d: int) -> bytes:
    if d <= 128:
        return bytes([d - 1])
    d -= 129
    return bytes([0x80 | (d >> 8), d & 0xFF])


def decode_frame(ref: bytes, data: bytes) -> bytes:
    """Reference decoder (menu/src/bootanim.c's om_ba_decode follows it op for op)."""
    out, n, p, pos = bytearray(ref), len(ref), 0, 0
    while pos < n:
        if p >= len(data):
            raise bf.FormatError(f"frame ops end at pixel {pos} of {n}")
        b = data[p]
        p += 1
        op, v = b >> 6, b & 63
        if v < 62:
            ln = v + 1
        elif v == 62:
            ln = 63 + data[p]
            p += 1
        else:
            ln = struct.unpack_from(">H", data, p)[0]
            p += 2
        if ln == 0 or pos + ln > n:
            raise bf.FormatError(f"op at pixel {pos}: length {ln} overruns the frame")
        if op == LIT:
            if p + ln > len(data):
                raise bf.FormatError(f"literal at pixel {pos} runs past the frame data")
            out[pos:pos + ln] = data[p:p + ln]
            p += ln
        elif op == FILL:
            out[pos:pos + ln] = bytes([data[p]]) * ln
            p += 1
        elif op == COPY:
            d = data[p]
            p += 1
            if d & 0x80:
                d = (((d & 0x7F) << 8) | data[p]) + 129
                p += 1
            else:
                d += 1
            if d > pos:
                raise bf.FormatError(f"copy at pixel {pos} reaches {d} back")
            for k in range(ln):
                out[pos + k] = out[pos + k - d]
        pos += ln
    if p != len(data):
        raise bf.FormatError(f"{len(data) - p} bytes after the last op")
    return bytes(out)


def _runs(eq) -> list:
    """r[i] = how many consecutive True start at i."""
    r = [0] * (len(eq) + 1)
    for i in range(len(eq) - 1, -1, -1):
        r[i] = r[i + 1] + 1 if eq[i] else 0
    return r


def encode_frame(ref: bytes, cur: bytes, width: int) -> bytes:
    """Optimal parse by byte cost. f[i]: cheapest encoding of cur[i:] with no literal run open; g[i]: with one open."""
    n = len(cur)
    dists = sorted({1, 2, 3, 4, 6, 8, width - 2, width - 1, width, width + 1, width + 2, 2 * width - 1,
                    2 * width, 2 * width + 1, 3 * width, 4 * width} - {0})
    skip = _runs([cur[i] == ref[i] for i in range(n)])
    fill = _runs([i + 1 < n and cur[i + 1] == cur[i] for i in range(n)])
    cps = [(d, 1 if d <= 128 else 2, _runs([i >= d and cur[i] == cur[i - d] for i in range(n)])) for d in dists]
    big = 1 << 30
    f, g = [big] * (n + 1), [big] * (n + 1)
    fc, gc = [None] * (n + 1), [False] * (n + 1)
    f[n] = g[n] = 0
    for i in range(n - 1, -1, -1):
        best, ch = big, None
        run = skip[i]
        if run:
            for ln in {run, min(run, 62), min(run, 318), min(run, MAX_N)}:
                c = _hdr_cost(ln) + f[i + ln]
                if c < best:
                    best, ch = c, (SKIP, ln, 0)
        run = fill[i] + 1
        if run >= 3:
            for ln in {run, min(run, 62), min(run, 318), min(run, MAX_N)}:
                c = _hdr_cost(ln) + 1 + f[i + ln]
                if c < best:
                    best, ch = c, (FILL, ln, 0)
        for d, dc, runs in cps:
            run = runs[i]
            if run >= 2:
                for ln in {run, min(run, 62), min(run, 318), min(run, MAX_N)}:
                    if ln >= 2:
                        c = _hdr_cost(ln) + dc + f[i + ln]
                        if c < best:
                            best, ch = c, (COPY, ln, d)
        cont = 1 + g[i + 1]
        if cont < best:
            g[i], gc[i] = cont, True
        else:
            g[i], gc[i] = best, False
        if 1 + cont < best:
            best, ch = 1 + cont, (LIT, 0, 0)
        f[i], fc[i] = best, ch
    out, i = bytearray(), 0
    while i < n:
        op, ln, d = fc[i]
        if op == LIT:
            j = i + 1
            while j < n and gc[j]:
                j += 1
            k = i
            while k < j:
                m = min(j - k, MAX_N)
                out += _len_bytes(LIT, m) + cur[k:k + m]
                k += m
            i = j
            continue
        out += _len_bytes(op, ln)
        if op == FILL:
            out.append(cur[i])
        elif op == COPY:
            out += _dist_bytes(d)
        i += ln
    data = bytes(out)
    if decode_frame(ref, data) != cur:
        raise AssertionError("bootanim encoder: frame does not round-trip")
    return data


def encode(pixels: list[bytes], width: int, height: int, fps: int, chant_x100: int, palette: list[int],
           build_date: int) -> bytes:
    n = width * height
    if not pixels or any(len(p) != n for p in pixels):
        raise ValueError(f"every frame must be {width}x{height} = {n} B")
    black = bytes(n)
    frames = [encode_frame(pixels[r] if (r := ref_of(i)) >= 0 else black, p, width) for i, p in enumerate(pixels)]
    crcs = [bf.crc32(p) for p in pixels]
    index_size = ENTRY * len(frames)
    data_off = bf.align(INDEX_OFF + index_size, 4)
    off, index = data_off, b""
    for f, c in zip(frames, crcs):
        index += struct.pack(">III", off, len(f), c)
        off += len(f)
    h = bf.Header(magic=MAGIC, count=len(frames), table_off=INDEX_OFF, table_size=index_size, data_off=data_off,
                  data_size=off - data_off, build_date=build_date, table_crc=bf.crc32(index),
                  sections=((INFO_OFF, INFO_SIZE), (PAL_OFF, PAL_SIZE), (0, 0)))
    info = _INFO.pack(width, height, fps, CODEC_LZ_REF2, chant_x100, 0, 0)
    pal = b"".join(struct.pack(">H", c) for c in palette)
    body = bf.pack_header(h) + info + pal + index
    return body + bytes(data_off - len(body)) + b"".join(frames)


def parse(data: bytes) -> tuple[bf.Header, Anim]:
    h = bf.validate_common(data, MAGIC)
    if h.sections[0] != (INFO_OFF, INFO_SIZE) or h.sections[1] != (PAL_OFF, PAL_SIZE) or h.sections[2] != (0, 0):
        raise bf.FormatError("section table must be (64,16) info, (80,512) palette, (0,0)")
    if h.table_off != INDEX_OFF or h.table_size != ENTRY * h.count:
        raise bf.FormatError("frame index must sit at 592 and be 12 B per frame")
    w, hh, fps, codec, chant, _flags, _res = _INFO.unpack_from(data, INFO_OFF)
    if codec != CODEC_LZ_REF2:
        raise bf.FormatError(f"codec {codec} unknown (2 = byte LZ in place, reference frame i-2 / i-1)")
    pal = list(struct.unpack_from(">256H", data, PAL_OFF))
    frames, crcs = [], []
    for i in range(h.count):
        off, ln, crc = struct.unpack_from(">III", data, INDEX_OFF + ENTRY * i)
        if off < h.data_off or off + ln > h.data_off + h.data_size or off + ln > len(data):
            raise bf.FormatError(f"frame {i}: range {off}+{ln} outside the data section")
        frames.append(data[off:off + ln])
        crcs.append(crc)
    return h, Anim(w, hh, fps, chant, pal, frames, crcs)


def decode_all(a: Anim) -> list[bytes]:
    n = a.width * a.height
    black, out = bytes(n), []
    for i, f in enumerate(a.frames):
        r = ref_of(i)
        p = decode_frame(out[r] if r >= 0 else black, f)
        if bf.crc32(p) != a.crcs[i]:
            raise bf.FormatError(f"frame {i}: decoded CRC-32 {bf.crc32(p):08x} != index {a.crcs[i]:08x}")
        out.append(p)
    return out


def check_bootanim(data: bytes) -> list[str]:
    h, a = parse(data)
    errs = []
    if a.width % 8 or not 8 <= a.width <= MAX_W or not 1 <= a.height <= MAX_H:
        errs.append(f"size {a.width}x{a.height}: width a multiple of 8 up to {MAX_W}, height 1..{MAX_H}")
    if not 1 <= h.count <= MAX_FRAMES:
        errs.append(f"{h.count} frames (1..{MAX_FRAMES})")
    if a.fps != 60:
        errs.append(f"fps {a.fps} (the menu locks to the 60 Hz V-blank)")
    if not 0 <= a.chant_x100 < 100 * h.count:
        errs.append(f"chant start {a.chant_x100 / 100} is not inside the {h.count} frames")
    if any(c & 0x8000 for c in a.palette):
        errs.append("palette entry with bit15 set (VDP2 CRAM mode 0 is 15-bit, HWF-30)")
    big = [i for i, f in enumerate(a.frames) if len(f) > MAX_FRAME_BYTES]
    if big:
        errs.append(f"frames {big[:5]} exceed {MAX_FRAME_BYTES} B (the menu's per-frame cap)")
    if len(data) > MAX_FILE:
        errs.append(f"file is {len(data)} B > {MAX_FILE}")
    if errs:
        return errs
    try:
        decode_all(a)
    except (bf.FormatError, IndexError, struct.error) as e:
        errs.append(f"{type(e).__name__}: {e}")
    return errs


def rates(a: Anim) -> dict:
    """Stream rate at the file's fps, in KB/s (1 KB = 1024 B): average, per-frame peak, peak over any 1 s window."""
    sizes = [len(f) for f in a.frames]
    even = sizes[0::2]
    win = max(sum(sizes[i:i + a.fps]) for i in range(max(1, len(sizes) - a.fps + 1)))
    return {"total": sum(sizes), "avg_kbs": sum(sizes) / len(sizes) * a.fps / 1024,
            "peak_kbs": max(sizes) * a.fps / 1024, "peak_frame": sizes.index(max(sizes)), "window_kbs": win / 1024,
            "fallback_avg_kbs": sum(even) / len(even) * a.fps / 2 / 1024,
            "fallback_peak_kbs": max(even) * a.fps / 2 / 1024}


def load_frames(d: Path) -> tuple[int, int, list[bytes], list[int] | None]:
    from PIL import Image
    files = sorted(d.glob("frame_*.png"))
    if not files:
        raise SystemExit(f"bootanim: no frame_*.png in {d}")
    pix, size, pal0 = [], None, None
    for f in files:
        im = Image.open(f)
        if im.mode != "P":
            raise SystemExit(f"bootanim: {f.name} is {im.mode}, not an indexed PNG")
        if size is None:
            size, pal0 = im.size, im.getpalette()
        elif im.size != size or im.getpalette() != pal0:
            raise SystemExit(f"bootanim: {f.name} differs in size or palette from {files[0].name}")
        pix.append(im.tobytes())
    return size[0], size[1], pix, pal0


def read_chant(path: Path) -> int:
    m = re.search(r"^chant_start_frames\s*=\s*([0-9.]+)", path.read_text(encoding="utf-8"), re.MULTILINE)
    if not m:
        raise SystemExit(f"bootanim: no chant_start_frames in {path}")
    return round(float(m.group(1)) * 100)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m omsat.bootanim")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--frames", required=True, type=Path)
    p.add_argument("--palette", required=True, type=Path)
    p.add_argument("--chant", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--date", type=int, default=20261118)
    c = sub.add_parser("check")
    c.add_argument("file", type=Path)
    c.add_argument("--frames", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "pack":
        w, h, pix, _ = load_frames(a.frames)
        raw = a.palette.read_bytes()
        if len(raw) != PAL_SIZE:
            raise SystemExit(f"bootanim: {a.palette} is {len(raw)} B, want {PAL_SIZE}")
        data = encode(pix, w, h, 60, read_chant(a.chant), list(struct.unpack(">256H", raw)), a.date)
        errs = check_bootanim(data)
        if errs:
            raise SystemExit("bootanim: packed file fails its own check: " + "; ".join(errs))
        a.out.write_bytes(data)
        r = rates(parse(data)[1])
        print(f"bootanim: {a.out} {len(data)} B, {len(pix)} frames {w}x{h}; {r['avg_kbs']:.1f} KB/s average, "
              f"{r['peak_kbs']:.1f} KB/s peak (frame {r['peak_frame']}), {r['window_kbs']:.1f} KB/s peak 1 s; 30 fps fallback {r['fallback_avg_kbs']:.1f} "
              f"average, {r['fallback_peak_kbs']:.1f} peak")
        return 0
    data = a.file.read_bytes()
    errs = check_bootanim(data)
    if not errs and a.frames:
        _, anim = parse(data)
        w, h, pix, _ = load_frames(a.frames)
        got = decode_all(anim)
        if (w, h) != (anim.width, anim.height) or len(pix) != len(got):
            errs.append(f"frames dir is {len(pix)} x {w}x{h}, file is {len(got)} x {anim.width}x{anim.height}")
        else:
            errs += [f"frame {i} differs from its PNG" for i, (x, y) in enumerate(zip(pix, got)) if x != y]
    if errs:
        print(f"FAIL {a.file}")
        for e in errs:
            print(f"  - {e}")
        return 1
    r = rates(parse(data)[1])
    print(f"PASS {a.file}: {r['avg_kbs']:.1f} KB/s average, {r['peak_kbs']:.1f} KB/s peak"
          + (" (every frame byte-exact against its PNG)" if a.frames else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
