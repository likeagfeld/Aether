"""contract/SPEC.md §5.11 LOADBG.BIN: the loop layout (keyframe first, the wrap back to frame 0), the
reference decoder and checker (with mutations), the lossy conditional-replenishment encoder (error bound, periodic
loop), the scrim, and the shipped asset (menu/app/assets/LOADBG.BIN) against the menu's arena."""
import struct
from pathlib import Path

import pytest

from omsat import binfmt as bf
from omsat import loadbg as lb
from omsat.bootanim import decode_frame
from omsat.check import check_bytes

ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "menu" / "app" / "assets" / "LOADBG.BIN"
PAL = [0] + [(i * 11) & 0x7FFF for i in range(1, 256)]


def _frames(n=6, w=16, h=8):
    out = []
    for k in range(n):
        px = bytearray((x * 3 + y * 5) & 0xFF for y in range(8) for x in range(16))
        for y in range(h):
            px[y * w + (k * 2) % w] = 250
        out.append(bytes(px))
    return out


def test_layout_and_round_trip():
    fr = _frames()
    data = lb.encode(fr, fr[0], 16, 8, PAL, 2, 20261119)
    h, w, hh, scale, pal, ent, crcs = lb.parse(data)
    assert data[:8] == b"OMSATLBG" and h.count == 7 and (w, hh, scale) == (16, 8, 2)
    assert struct.unpack_from(">H", data, 64 + 6)[0] == lb.CODEC_LOOP
    assert decode_frame(bytes(16 * 8), ent[0]) == fr[0]           # entry 0: the keyframe, over zeros
    assert decode_frame(fr[5], ent[1]) == fr[0]                    # entry 1: frame 0 over frame N-1 (the wrap)
    assert decode_frame(fr[2], ent[4]) == fr[3]                    # entry f + 1: frame f over frame f - 1
    assert crcs[0] == crcs[1] == bf.crc32(fr[0])
    assert lb.decode_loop(16, 8, ent, crcs) == fr
    assert check_bytes(data) == []


def test_keyframe_first_for_streaming():
    fr = _frames()
    data = lb.encode(fr, fr[0], 16, 8, PAL, 2, 20261119)
    offs = [struct.unpack_from(">I", data, lb.INDEX_OFF + 12 * i)[0] for i in range(7)]
    assert offs == sorted(offs)                                    # the keyframe arrives first


def test_checker_mutations():
    fr = _frames()
    data = bytearray(lb.encode(fr, fr[0], 16, 8, PAL, 2, 20261119))
    _, _, _, _, _, ent, _ = lb.parse(bytes(data))
    off = struct.unpack_from(">I", data, lb.INDEX_OFF + 12 * 3)[0]
    bad = bytearray(data)
    bad[off + len(ent[3]) - 1] ^= 1                                # a frame byte: its CRC fails
    assert check_bytes(bytes(bad))
    bad = bytearray(data)
    struct.pack_into(">H", bad, 64 + 6, 2)                          # codec 2 is BOOTANIM's
    assert check_bytes(bytes(bad))
    bad = bytearray(data)
    struct.pack_into(">H", bad, 64 + 12, 3)                         # scale 3
    assert check_bytes(bytes(bad))
    bad = bytearray(data)
    struct.pack_into(">H", bad, lb.PAL_OFF + 2, 0x8001)             # palette bit 15 (HWF-30)
    assert check_bytes(bytes(bad))
    with pytest.raises(ValueError):
        lb.encode(fr, fr[1], 16, 8, PAL, 2, 20261119)              # frame 0 must be the keyframe


def test_scrim_is_dark_left_and_bottom():
    assert lb.scrim_alpha(0, 0) == pytest.approx(0.88)
    assert lb.scrim_alpha(250, 100) == pytest.approx(0.22)
    assert lb.scrim_alpha(250, 223) > lb.scrim_alpha(250, 160)
    assert max(lb.scrim_alpha(x, y) for x in range(0, 320, 7) for y in range(0, 224, 7)) <= 0.92


def test_lossy_loop_bound_and_period():
    np = pytest.importorskip("numpy")
    rng = np.random.default_rng(1)
    w, h, n = 16, 8, 10
    base = rng.uniform(0, 255, (w * h, 3))
    rgb = [np.clip(base + 6 * np.sin(k / n * 6.283) + rng.normal(0, 2, base.shape), 0, 255) for k in range(n)]
    palrgb = np.array([[i, (i * 7) % 256, (i * 13) % 256] for i in range(256)], float)
    frames, key, _ = lb.lossy_loop(rgb, palrgb, 28.0, width=w)
    assert frames[0] == key and len(frames) == n
    q = [lb.nearest(f, palrgb) for f in rgb]
    for f, r, qq in zip(frames[1:], rgb[1:], q[1:]):              # every kept pixel is within the threshold
        idx = np.frombuffer(f, np.uint8)
        kept = idx != qq
        assert (np.abs(palrgb[idx[kept]] - r[kept]).max(initial=0) <= 28.0)
    data = lb.encode(frames, key, w, h, [0] * 256, 2, 20261119)
    _, _, _, _, _, ent, crcs = lb.parse(data)
    assert lb.decode_loop(w, h, ent, crcs, passes=3) == frames    # periodic: the wrap lands on the keyframe


@pytest.mark.skipif(not ASSET.is_file(), reason="menu/app/assets/LOADBG.BIN not built")
def test_shipped_asset():
    data = ASSET.read_bytes()
    assert check_bytes(data) == []
    h, w, hh, scale, pal, ent, _ = lb.parse(data)
    assert (w, hh, scale, h.count) == (320, 224, 1, 64)            # round 2: full res, 63 loop frames + the keyframe
    assert struct.unpack_from(">H", data, 64 + 4)[0] == 30          # every 2nd frame of the 60 fps loop: 30 fps
    hw, total = lb.capacity(w, hh)
    assert len(data) <= total and h.data_off + len(ent[0]) <= hw   # the keyframe lies in the HWRAM part
    offs = [struct.unpack_from(">I", data, lb.INDEX_OFF + 12 * i)[0] for i in range(h.count)]
    assert max(len(ent[i]) for i in range(1, h.count) if offs[i] >= hw) <= lb.STAGE   # sound-RAM entries fit the stage
    assert not [i for i in range(1, h.count) if offs[i] < hw < offs[i] + len(ent[i])]  # none straddles the head


def test_capacity_and_mutations():
    hw, total = lb.capacity(320, 224)
    assert (hw, total) == (167_180, 167_180 + 360_448)
    fr = _frames()
    data = bytearray(lb.encode(fr, fr[0], 16, 8, PAL, 2, 20261119, fps=30))
    assert check_bytes(bytes(data)) == []
    struct.pack_into(">H", data, 64 + 4, 50)                       # fps 50: not 60 or 30
    assert check_bytes(bytes(data))
