"""Aether sound files (contract/SPEC.md §5.8 SOUND.BIN, §5.12 MUSIC .MUS). Big-endian, signed PCM, played straight
from sound RAM by SCSP slots the SH-2 programs directly (decision D2, docs/decisions/m3-sound.md; no SGL/SBL sound).

- SOUND.BIN (magic OMSATSND) is the SFX bank on the MENU DISC (both ISOs, tools/sbuild.py): the boot chant must be in
  sound RAM before the boot animation's frame 137, which plays before API mode (no SD yet), and the five clips
  (66.7 KB) do not fit the 24 KB HWRAM budget (Task 14/15 report). One row per clip; ids: 0 scroll, 1 select, 2 ring,
  3 scream, 4 chant. The menu copies the chant to the music region (free at boot) and the others to the SFX region.
- MUSIC (magic OMSATMUS, `/aether/MUSIC/BGM.MUS`) is the SD-streamed background music: stereo 16-bit, planar
  blocks of `block_frames` frames ([L block][R block]); the menu streams it into a 7-block ring per channel (§5.12).
  It carries the first-start offset (`start_frame`) and the packer's gain.

`python -m omsat.sound bank [--out menu/app/assets/SOUND.BIN]`            (from build/sfx/audio/*.pcm)
`python -m omsat.sound music IN.wav --out BGM.MUS [--trim-start 300] [--start 0] [--mean -14]`
`python -m omsat.sound music-trim IN.MUS OUT.MUS --seconds 300`              (cut whole blocks off an existing file)
`python -m omsat.sound test-music --out T.MUS`                             (the fake card's 3-block test excerpt)
`python -m omsat.sound info FILE`"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys
import wave
from dataclasses import dataclass
from pathlib import Path

from . import binfmt as bf

ROOT = Path(__file__).resolve().parents[2]
MAGIC = b"OMSATSND"
MAGIC_MUS = b"OMSATMUS"
ROW = struct.Struct(">IIIHbBHB5s")
assert ROW.size == 24
MUS_INFO = struct.Struct(">IHHIIIhbBH6s")
assert MUS_INFO.size == 32
F16, FLOOP = 1, 2
MAX_SAMPLES = 65535                 # SCSP LSA/LEA are 16-bit sample counts (HWF-37)
SFX_MAX = 0x10000                   # sound RAM 0x30000-0x3FFFF (ledger D3)
MUSIC_REGION = 0x20000              # 0x10000-0x2FFFF: the chant at boot, the music ring afterwards
MAX_CLIPS = 8
SLOT_RATE = 44100
SFX_IDS = {"scroll": 0, "select": 1, "ring": 2, "scream": 3, "chant": 4}
CHANT_ID = 4
DATE = 20261119
BANK = ROOT / "menu" / "app" / "assets" / "SOUND.BIN"
SFX_DIR = ROOT / "build" / "sfx" / "audio"
BLOCK_FRAMES = 4096                 # one SCSP CA unit (HWF-51): the ring's block
RING_BLOCKS = 7                     # per channel: 7 x 4096 x 2 B = 57,344 B + a 2 B guard in each 64 KB half
MUS_RATE = 22050
MUS_HEAD = 2048                     # header + info row, padded to one 2 KB read


class TooLong(ValueError):
    pass


def pitch(rate: int) -> tuple[int, int]:
    """(OCT, FNS) so that 2**OCT * (1024 + FNS) / 1024 * 44,100 Hz ~= rate (HWF-36)."""
    ratio = rate / SLOT_RATE
    o = math.floor(math.log2(ratio))
    f = round(1024 * (ratio / 2 ** o - 1))
    if f == 1024:
        o, f = o + 1, 0
    if not -8 <= o <= 7:
        raise ValueError(f"rate {rate} Hz is outside the SCSP pitch range")
    return o, f


@dataclass
class Clip:
    samples: list
    rate: int
    bits: int
    loop: bool
    cid: int


def _pcm(c: Clip) -> bytes:
    if c.bits == 16:
        return struct.pack(f">{len(c.samples)}h", *c.samples)
    return bytes(s & 0xFF for s in c.samples)


def write_snd(clips, build_date: int = DATE) -> bytes:
    table, data = bytearray(), bytearray()
    for c in clips:
        if not 1 <= len(c.samples) <= MAX_SAMPLES:
            raise TooLong(f"E_TOO_LONG: {len(c.samples)} samples; one SCSP clip holds 1..{MAX_SAMPLES} (HWF-37)")
        raw = _pcm(c)
        o, f = pitch(c.rate)
        off = len(data)
        data += raw + b"\0" * (bf.align(len(raw)) - len(raw))
        table += ROW.pack(off, len(raw), len(c.samples), c.rate, o, (F16 if c.bits == 16 else 0) | (FLOOP if c.loop else 0),
                          f, c.cid, b"\0" * 5)
    data_off = bf.align(bf.HEADER_SIZE + len(table))
    h = bf.Header(magic=MAGIC, count=len(clips), table_off=bf.HEADER_SIZE, table_size=len(table), data_off=data_off,
                  data_size=len(data), build_date=build_date, table_crc=bf.crc32(bytes(table)),
                  sections=((0, 0), (0, 0), (0, 0)))
    head = bf.pack_header(h) + bytes(table)
    return head + b"\0" * (data_off - len(head)) + bytes(data)


def read_snd(data: bytes) -> list[dict]:
    h = bf.validate_common(data, MAGIC)
    if h.table_size != ROW.size * h.count or h.data_off % bf.SECTOR:
        raise bf.FormatError("OMSATSND table size or data offset alignment wrong")
    out = []
    for i in range(h.count):
        off, ln, n, rate, o, flags, f, cid, _ = ROW.unpack_from(data, h.table_off + ROW.size * i)
        out.append(dict(off=off, len=ln, samples=n, rate=rate, oct=o, flags=flags, fns=f, id=cid,
                        data_size=h.data_size, data_off=h.data_off))
    return out


def check_snd(data: bytes) -> list[str]:
    errs, clips = [], read_snd(data)
    if not 1 <= len(clips) <= MAX_CLIPS:
        errs.append(f"{len(clips)} clips; 1..{MAX_CLIPS} allowed")
    for i, c in enumerate(clips):
        if c["off"] % bf.SECTOR or c["off"] + c["len"] > c["data_size"]:
            errs.append(f"clip {i}: data range/alignment")
        if not 1 <= c["samples"] <= MAX_SAMPLES:
            errs.append(f"clip {i}: {c['samples']} samples outside 1..65535 (HWF-37)")
        if c["len"] != c["samples"] * (2 if c["flags"] & F16 else 1):
            errs.append(f"clip {i}: length {c['len']} != samples x width")
        if c["fns"] > 1023 or not -8 <= c["oct"] <= 7 or (c["rate"] and pitch(c["rate"]) != (c["oct"], c["fns"])):
            errs.append(f"clip {i}: OCT/FNS do not match rate {c['rate']} (HWF-36)")
        if c["flags"] & FLOOP:
            errs.append(f"clip {i}: loop flag set; SOUND.BIN clips are one-shots")
    ids = [c["id"] for c in clips]
    if len(set(ids)) != len(ids):
        errs.append("duplicate clip ids")
    sfx = sum(bf.align(c["len"], 2) for c in clips if c["id"] != CHANT_ID)
    chant = sum(c["len"] for c in clips if c["id"] == CHANT_ID)
    if sfx > SFX_MAX:
        errs.append(f"SFX clips {sfx} B exceed the {SFX_MAX} B SFX region (ledger D3)")
    if chant > MUSIC_REGION:
        errs.append(f"chant {chant} B exceeds the {MUSIC_REGION} B music region (ledger D3)")
    return errs


def _pcm8_file(path: Path) -> list[int]:
    raw = path.read_bytes()
    return [b - 256 if b > 127 else b for b in raw]


def bank(sfx_dir: Path = SFX_DIR) -> bytes:
    """SOUND.BIN from the prepared Saturn-format clips (build/sfx/audio/*.pcm: 8-bit signed mono 22,050 Hz, raw;
    build/sfx/index.html). Ids by SFX_IDS."""
    clips = [Clip(_pcm8_file(sfx_dir / f"{name}.pcm"), 22050, 8, False, cid) for name, cid in SFX_IDS.items()]
    return write_snd(clips, DATE)


# --- MUSIC (OMSATMUS) ------------------------------------------------------------------------------------------------

def write_mus(left, right, rate: int = MUS_RATE, start_frame: int = 0, gain_cdb: int = 0,
              build_date: int = DATE, block_frames: int = BLOCK_FRAMES) -> bytes:
    """left/right: int16 sequences of equal length (numpy arrays or lists). The last block is padded with silence."""
    import numpy as np
    lft = np.asarray(left, dtype=np.int16)
    rgt = np.asarray(right, dtype=np.int16)
    if lft.shape != rgt.shape or lft.ndim != 1 or not len(lft):
        raise ValueError("left/right must be equal, non-empty 1-D sequences")
    nblk = -(-len(lft) // block_frames)
    pad = nblk * block_frames - len(lft)
    lft = np.concatenate([lft, np.zeros(pad, np.int16)]).reshape(nblk, block_frames)
    rgt = np.concatenate([rgt, np.zeros(pad, np.int16)]).reshape(nblk, block_frames)
    body = np.stack([lft, rgt], axis=1).astype(">i2").tobytes()
    return _mus_head(nblk, rate, len(left) if not hasattr(left, "shape") else left.shape[0], start_frame, gain_cdb,
                     build_date, block_frames) + body


def _mus_head(nblk: int, rate: int, frames: int, start_frame: int, gain_cdb: int, build_date: int,
              block_frames: int = BLOCK_FRAMES) -> bytes:
    o, f = pitch(rate)
    info = MUS_INFO.pack(rate, 2, 16, block_frames, frames, start_frame, gain_cdb, o, 0, f, b"\0" * 6)
    h = bf.Header(magic=MAGIC_MUS, count=nblk, table_off=bf.HEADER_SIZE, table_size=MUS_INFO.size, data_off=MUS_HEAD,
                  data_size=nblk * block_frames * 4, build_date=build_date, table_crc=bf.crc32(info),
                  sections=((0, 0), (0, 0), (0, 0)))
    head = bf.pack_header(h) + info
    return head + b"\0" * (MUS_HEAD - len(head))


def read_mus(data: bytes, file_size: int | None = None) -> dict:
    """Parses the 2 KB head; data may be the whole file or just its head (file_size then gives the real size)."""
    size = len(data) if file_size is None else file_size
    h = bf.unpack_header(data)
    if h.magic != MAGIC_MUS:
        raise bf.FormatError(f"magic: expected {MAGIC_MUS!r}, got {h.magic!r}")
    if h.version != 1 or h.header_size != bf.HEADER_SIZE:
        raise bf.FormatError("version / header size")
    if h.table_off != bf.HEADER_SIZE or h.table_size != MUS_INFO.size:
        raise bf.FormatError("OMSATMUS info row must be 32 B at offset 64")
    info = data[h.table_off:h.table_off + MUS_INFO.size]
    if bf.crc32(info) != h.table_crc:
        raise bf.FormatError("crc: info row")
    rate, ch, bits, bfr, frames, start, gain, o, _r, f, _ = MUS_INFO.unpack(info)
    return dict(blocks=h.count, data_off=h.data_off, data_size=h.data_size, rate=rate, channels=ch, bits=bits,
                block_frames=bfr, frames=frames, start_frame=start, gain_cdb=gain, oct=o, fns=f, file_size=size)


def check_mus(data: bytes, file_size: int | None = None) -> list[str]:
    errs = []
    m = read_mus(data, file_size)
    if m["channels"] != 2 or m["bits"] != 16:
        errs.append(f"{m['channels']} ch / {m['bits']} bit: the menu plays 2 ch 16-bit only (SPEC §5.12)")
    if m["block_frames"] != BLOCK_FRAMES:
        errs.append(f"block_frames {m['block_frames']} != {BLOCK_FRAMES} (one SCSP CA unit, HWF-51)")
    if m["data_off"] != MUS_HEAD:
        errs.append(f"data offset {m['data_off']} != {MUS_HEAD}")
    if m["blocks"] < 1 or m["data_size"] != m["blocks"] * m["block_frames"] * 4:
        errs.append("data size != blocks x block_frames x 4")
    if m["data_off"] + m["data_size"] > 0x7FFFFFFF:
        errs.append("file over 2 GB: the menu seeks with a signed 32-bit offset (SPEC §5.12)")
    if m["data_off"] + m["data_size"] > m["file_size"]:
        errs.append("data runs past the end of the file")
    if m["frames"] > m["blocks"] * m["block_frames"] or m["frames"] <= (m["blocks"] - 1) * m["block_frames"]:
        errs.append("frames does not match the block count")
    if m["start_frame"] >= max(1, m["frames"]):
        errs.append("start_frame past the end")
    if m["rate"] and (m["fns"] > 1023 or pitch(m["rate"]) != (m["oct"], m["fns"])):
        errs.append(f"OCT/FNS do not match rate {m['rate']} (HWF-36)")
    return errs


def check_any(data: bytes) -> list[str]:
    return check_mus(data) if data[:8] == MAGIC_MUS else check_snd(data)


def test_music(blocks: int = 3) -> bytes:
    """The fake card's excerpt (Task 14/15 acceptance instrument): `blocks` blocks (3 = 49,152 B of PCM, 0.557 s),
    a sine with a whole number of cycles per block in each channel (L 441.4 Hz = 82 cycles, R 662.1 Hz = 123 cycles,
    so the file loops seamlessly), at a different level per block (-6, -12, -18 dBFS peak). 3 blocks in a 7-block ring:
    a live stream repeats its level pattern every 3 blocks (0.557 s), a ring that never refills every 7 (1.300 s)."""
    import numpy as np
    n = np.arange(BLOCK_FRAMES)
    out_l, out_r = [], []
    for b in range(blocks):
        a = 32767 * 10 ** (-6 * (b % 3 + 1) / 20)
        out_l.append(np.round(a * np.sin(2 * np.pi * 82 * n / BLOCK_FRAMES)))
        out_r.append(np.round(a * np.sin(2 * np.pi * 123 * n / BLOCK_FRAMES)))
    left = np.concatenate(out_l).astype(np.int16)
    right = np.concatenate(out_r).astype(np.int16)
    return write_mus(left, right, MUS_RATE, 0, 0, DATE)


def _wav_frames(w: wave.Wave_read, n: int):
    import numpy as np
    raw = w.readframes(n)
    return np.frombuffer(raw, dtype="<i2").reshape(-1, w.getnchannels()).astype(np.float32) / 32768.0


def music_from_wav(path: str, out: str, start_s: float = 0.0, mean_db: float = -14.0, ceiling_db: float = -1.0,
                   chunk_s: int = 600, log=print, trim_s: float = 0.0) -> dict:
    """WAV (16-bit, stereo, MUS_RATE) -> OMSATMUS, streamed to disk. Gain = mean_db - the source's mean level (RMS
    over all samples, ffmpeg volumedetect's mean_volume), so the stored mean is mean_db dBFS; a block limiter (256-frame
    blocks, the gain cut to keep each block's peak at ceiling_db, the cut spread over +-2 blocks and interpolated) keeps
    the peaks. trim_s: the source's first seconds are left out (measured and packed from there on); start_s counts
    from the cut. Returns the measurements."""
    import numpy as np
    with wave.open(path, "rb") as w:
        ch, width, rate, nfr = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        if width != 2 or ch != 2 or rate != MUS_RATE:
            raise ValueError(f"{path}: need 16-bit stereo {MUS_RATE} Hz (got {width * 8}-bit {ch} ch {rate} Hz)")
        skip = int(trim_s * rate)
        if skip >= nfr:
            raise ValueError(f"{path}: --trim-start {trim_s} s is past the end ({nfr / rate:.0f} s)")
        nfr -= skip
        w.setpos(skip)
        acc, cnt, peak = 0.0, 0, 0.0
        step = chunk_s * rate
        for i in range(0, nfr, step):
            x = _wav_frames(w, min(step, nfr - i))
            acc += float(np.sum(x.astype(np.float64) ** 2))
            cnt += x.size
            peak = max(peak, float(np.max(np.abs(x))))
    src_mean = 10 * math.log10(acc / cnt)
    gain_db = mean_db - src_mean
    g = 10 ** (gain_db / 20)
    ceil = 10 ** (ceiling_db / 20)
    nblk = -(-nfr // BLOCK_FRAMES)
    start_frame = int(start_s * rate) % max(1, nfr)
    limited, out_acc, out_peak, clipped = 0, 0.0, 0.0, 0
    with wave.open(path, "rb") as w, open(out, "wb") as f:
        w.setpos(skip)
        f.write(_mus_head(nblk, rate, nfr, start_frame, int(round(gain_db * 100)), DATE))
        step = (chunk_s * rate) // BLOCK_FRAMES * BLOCK_FRAMES
        for i in range(0, nfr, step):
            x = _wav_frames(w, min(step, nfr - i))
            n = len(x)
            lb = 256
            nb = -(-n // lb)
            xp = np.zeros((nb * lb, 2), np.float32)
            xp[:n] = x
            bpk = np.max(np.abs(xp).reshape(nb, lb, 2), axis=(1, 2)) * g
            bg = np.where(bpk > ceil, ceil / np.maximum(bpk, 1e-9), 1.0)
            limited += int(np.sum(bg < 1.0))
            sm = bg.copy()
            for d in (1, 2):                       # spread each cut over +-2 blocks (attack before, release after)
                sm[d:] = np.minimum(sm[d:], bg[:-d])
                sm[:-d] = np.minimum(sm[:-d], bg[d:])
            centers = np.arange(nb) * lb + lb / 2
            gains = np.interp(np.arange(nb * lb), centers, sm)[:n].astype(np.float32)
            y = x * (g * gains)[:, None]
            yi = np.clip(np.round(y * 32767), -32768, 32767)
            clipped += int(np.sum(np.abs(yi) >= 32767))
            out_acc += float(np.sum((yi.astype(np.float64) / 32768) ** 2))
            out_peak = max(out_peak, float(np.max(np.abs(yi))) / 32768)
            yi = yi.astype(np.int16)
            m = -(-n // BLOCK_FRAMES)
            pad = m * BLOCK_FRAMES - n
            if pad:
                yi = np.concatenate([yi, np.zeros((pad, 2), np.int16)])
            blk = yi.reshape(m, BLOCK_FRAMES, 2).transpose(0, 2, 1)       # [block][ch][frame]
            f.write(blk.astype(">i2").tobytes())
            log(f"music: {min(i + step, nfr) / rate:8.0f} / {nfr / rate:.0f} s")
    res = dict(frames=nfr, blocks=nblk, seconds=nfr / rate, src_mean_db=src_mean, src_peak_db=20 * math.log10(peak),
               gain_db=gain_db, out_mean_db=10 * math.log10(out_acc / (nfr * 2)), out_peak_db=20 * math.log10(out_peak),
               limited_blocks=limited, total_blocks_256=-(-nfr // 256), clipped_samples=clipped,
               start_frame=start_frame, bytes=MUS_HEAD + nblk * BLOCK_FRAMES * 4)
    return res


def music_trim(src: str, out: str, seconds: float, start_frame: int = 0, chunk: int = 1 << 22) -> dict:
    """An existing OMSATMUS without its first whole blocks covering `seconds` (rounded down), streamed (a full mix is
    about 1 GB); the gain and date are kept, start_frame is set. The cut data is byte for byte the source's."""
    with open(src, "rb") as f:
        head = f.read(MUS_HEAD)
        size = os.fstat(f.fileno()).st_size
        errs = check_mus(head, size)
        if errs:
            raise ValueError(f"{src}: {errs}")
        m = read_mus(head, size)
        h = bf.unpack_header(head)
        k = int(seconds * m["rate"]) // m["block_frames"]
        if k >= m["blocks"]:
            raise ValueError(f"{src}: {seconds} s is past the end ({m['frames'] / m['rate']:.0f} s)")
        frames = m["frames"] - k * m["block_frames"]
        with open(out, "wb") as g:
            g.write(_mus_head(m["blocks"] - k, m["rate"], frames, start_frame, m["gain_cdb"], h.build_date,
                              m["block_frames"]))
            f.seek(m["data_off"] + k * m["block_frames"] * 4)
            while (b := f.read(chunk)):
                g.write(b)
    return dict(blocks_cut=k, seconds_cut=k * m["block_frames"] / m["rate"], blocks=m["blocks"] - k, frames=frames)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bank"); b.add_argument("--out", default=str(BANK)); b.add_argument("--src", default=str(SFX_DIR))
    m = sub.add_parser("music"); m.add_argument("wav"); m.add_argument("--out", required=True)
    m.add_argument("--start", type=float, default=0.0); m.add_argument("--mean", type=float, default=-14.0)
    m.add_argument("--trim-start", type=float, default=0.0)
    r = sub.add_parser("music-trim"); r.add_argument("src"); r.add_argument("out")
    r.add_argument("--seconds", type=float, required=True); r.add_argument("--start", type=int, default=0)
    t = sub.add_parser("test-music"); t.add_argument("--out", required=True)
    i = sub.add_parser("info"); i.add_argument("file")
    a = ap.parse_args(argv)
    if a.cmd == "info":
        with open(a.file, "rb") as f:
            head = f.read(MUS_HEAD)
        if head[:8] == MAGIC_MUS:
            size = Path(a.file).stat().st_size
            print(read_mus(head, size), check_mus(head, size))
        else:
            data = Path(a.file).read_bytes()
            for c in read_snd(data):
                print(c)
            print(check_snd(data))
        return 0
    if a.cmd == "music-trim":
        print("music-trim:", music_trim(a.src, a.out, a.seconds, a.start))
        with open(a.out, "rb") as f:
            errs = check_mus(f.read(MUS_HEAD), Path(a.out).stat().st_size)
        print("\n".join(errs) if errs else f"music-trim: OK -> {a.out}")
        return 1 if errs else 0
    if a.cmd == "music":
        res = music_from_wav(a.wav, a.out, a.start, a.mean, trim_s=a.trim_start)
        print("music:", {k: round(v, 2) if isinstance(v, float) else v for k, v in res.items()})
        with open(a.out, "rb") as f:
            errs = check_mus(f.read(MUS_HEAD), Path(a.out).stat().st_size)
        print("\n".join(errs) if errs else f"music: OK -> {a.out}")
        return 1 if errs else 0
    data = bank(Path(a.src)) if a.cmd == "bank" else test_music()
    errs = check_any(data)
    if errs:
        print("\n".join(errs))
        return 1
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_bytes(data)
    print(f"{a.cmd}: {len(data)} B -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
