"""contract/SPEC.md §5.10 BOOTANIM.BIN: the packer, the reference decoder, the checker (with its mutation
demo), the paired reference chain and the stream-rate figures. The shipped asset is checked too: every frame decodes,
and against the source PNGs when build/bootanim/final/saturn_frames is present."""
import random
import struct
from pathlib import Path

import pytest

from omsat import bootanim as ba
from omsat import binfmt as bf
from omsat.check import check_bytes

ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "menu" / "app" / "assets" / "BOOTANIM.BIN"
FRAMES = ROOT / "build" / "bootanim" / "final" / "saturn_frames"


def _frames(n=8, w=16, h=8, seed=1):
    rng = random.Random(seed)
    out, cur = [], bytearray(w * h)
    for k in range(n):
        cur = bytearray(cur)
        for _ in range(rng.randrange(1, 20)):            # a few changed spans, a moving bar, runs and noise
            a, v = rng.randrange(w * h), rng.randrange(256)
            for j in range(a, min(w * h, a + rng.randrange(1, 9))):
                cur[j] = v
        row = k % h
        cur[row * w:(row + 1) * w] = bytes(range(k, k + w))
        out.append(bytes(cur[:w * h]))
    return out


def _pack(frames, w=16, h=8, chant=310):
    return ba.encode(frames, w, h, 60, chant, [0] + [(i * 7) & 0x7FFF for i in range(1, 256)], 20261118)


def test_round_trip_and_layout():
    fr = _frames()
    data = _pack(fr)
    h, a = ba.parse(data)
    assert data[:8] == b"OMSATBAN" and h.count == 8 and (a.width, a.height, a.fps, a.chant_x100) == (16, 8, 60, 310)
    assert h.table_off == 592 and h.table_size == 12 * 8 and h.sections[0] == (64, 16) and h.sections[1] == (80, 512)
    assert struct.unpack_from(">H", data, 64 + 6)[0] == ba.CODEC_LZ_REF2
    assert ba.decode_all(a) == fr
    assert check_bytes(data) == []


def test_reference_chain_is_paired():
    assert [ba.ref_of(i) for i in range(6)] == [-2, 0, 0, 2, 2, 4]
    fr = _frames(10)
    _, a = ba.parse(_pack(fr))
    black = bytes(16 * 8)
    prev = black                                          # the even frames alone: the 30 fps fallback chain
    for i in range(0, 10, 2):
        prev = ba.decode_frame(prev, a.frames[i])
        assert prev == fr[i]


def test_ops_cover_every_kind_and_skip_is_used():
    w = 16
    ref = bytes(range(64)) * 2
    cur = bytearray(ref)
    cur[10:20] = b"\x05" * 10                             # FILL
    cur[40:48] = bytes([200, 1, 77, 3, 9, 250, 6, 13])   # LIT
    cur[100:108] = cur[40:48]                             # COPY (the encoder only tries row/near distances, so 60 back)
    data = ba.encode_frame(ref, bytes(cur), w)
    kinds = set()
    p = 0
    while p < len(data):
        op, v = data[p] >> 6, data[p] & 63
        p += 1 + (v == 62) + 2 * (v == 63)
        n = v + 1 if v < 62 else None
        kinds.add(op)
        if op == ba.LIT:
            p += n if n else 0
        elif op == ba.FILL:
            p += 1
        elif op == ba.COPY:
            p += 2 if data[p] & 0x80 else 1
    assert ba.SKIP in kinds and ba.FILL in kinds and ba.LIT in kinds
    assert ba.decode_frame(ref, data) == bytes(cur)


def test_checker_mutation_demo():
    good = _pack(_frames())
    assert check_bytes(good) == []
    h, a = ba.parse(good)
    off, ln, _ = struct.unpack_from(">III", good, 592 + 12 * 3)
    m = bytearray(good); m[off + ln - 1] ^= 0x41           # a frame's last op byte: decode or CRC catches it
    assert check_bytes(bytes(m)), "frame byte flip not caught"
    m = bytearray(good); m[592 + 12 * 2 + 8] ^= 1          # a frame CRC in the index: the table CRC catches it
    assert check_bytes(bytes(m)), "index flip not caught"
    m = bytearray(good); struct.pack_into(">H", m, 64 + 6, 1)   # codec 1 (deflate, retired)
    assert check_bytes(bytes(m)), "unknown codec not caught"
    m = bytearray(good); struct.pack_into(">H", m, 64, 20)     # width not a multiple of 8
    assert check_bytes(bytes(m)), "bad width not caught"
    assert check_bytes(good[:-1]), "truncation not caught"


def test_encoder_refuses_ragged_frames():
    with pytest.raises(ValueError):
        ba.encode([bytes(16 * 8), bytes(16 * 8 - 1)], 16, 8, 60, 0, [0] * 256, 1)


def test_rates_report_window_and_fallback():
    _, a = ba.parse(_pack(_frames(70)))
    r = ba.rates(a)
    assert r["peak_kbs"] >= r["window_kbs"] >= r["avg_kbs"] > 0
    assert r["fallback_avg_kbs"] > 0


@pytest.mark.skipif(not ASSET.is_file(), reason="menu/app/assets/BOOTANIM.BIN not built")
def test_shipped_asset():
    data = ASSET.read_bytes()
    assert check_bytes(data) == []
    _, a = ba.parse(data)
    assert (len(a.frames), a.width, a.height, a.chant_x100) == (215, 176, 127, 13710)
    r = ba.rates(a)
    assert r["avg_kbs"] < 250 and r["window_kbs"] < 300     # sustained below a 2x CD (300 KB/s); peaks go to the ring
    if FRAMES.is_dir():
        _, _, pix, _ = ba.load_frames(FRAMES)
        assert ba.decode_all(a) == pix                     # byte-exact against the indexed PNGs
