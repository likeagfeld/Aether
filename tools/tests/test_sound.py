"""contract/SPEC.md §5.8 SOUND.BIN (OMSATSND) and §5.12 MUSIC (OMSATMUS), Task 14/15: pitch, layout, the 65,535-sample
ceiling (HWF-37), checker mutations, the committed bank reproducing exactly, the music packer's gain / limiter / planar
blocks, and the fake card's test excerpt."""
import math
import struct
import wave

import numpy as np
import pytest

from omsat import binfmt as bf
from omsat import sound
from omsat.check import check_bytes


def test_pitch_matches_hwf36():
    assert sound.pitch(44100) == (0, 0)
    assert sound.pitch(22050) == (-1, 0)
    assert sound.pitch(11025) == (-2, 0)
    o, f = sound.pitch(32000)
    assert o == -1 and abs(2 ** o * (1024 + f) / 1024 * 44100 - 32000) < 25


def test_layout_and_reader():
    data = sound.write_snd([sound.Clip([0, 100, -100, 32767], 22050, 16, False, 1),
                            sound.Clip([1, -1, 2], 11025, 8, False, 3)], 20261118)
    clips = sound.read_snd(data)
    assert [c["id"] for c in clips] == [1, 3]
    assert clips[0]["len"] == 8 and clips[0]["flags"] == sound.F16 and clips[0]["off"] % 2048 == 0
    assert clips[1]["len"] == 3 and clips[1]["oct"] == -2
    h_data_off, = struct.unpack_from(">I", data, 0x18)
    raw = data[h_data_off + clips[0]["off"]:h_data_off + clips[0]["off"] + 8]
    assert raw == struct.pack(">4h", 0, 100, -100, 32767)                 # big-endian signed 16-bit
    assert data[h_data_off + clips[1]["off"]:][:3] == bytes([1, 0xFF, 2])  # two's-complement 8-bit (HWF-34)
    assert check_bytes(data) == []


def test_too_long_is_a_named_error():
    with pytest.raises(sound.TooLong, match="E_TOO_LONG"):
        sound.write_snd([sound.Clip([0] * 65536, 22050, 8, False, 0)])


def test_checker_mutation_demo():
    good = sound.write_snd([sound.Clip([5] * 100, 22050, 8, False, 0)])
    assert check_bytes(good) == []
    flip = bytearray(good); flip[64 + 9] ^= 1
    assert check_bytes(bytes(flip)), "table CRC not caught"
    many = bytearray(good)
    struct.pack_into(">I", many, 64 + 8, 70000)                            # samples over 65,535
    struct.pack_into(">I", many, 0x24, bf.crc32(bytes(many[64:64 + 24])))
    assert any("65535" in e or "samples" in e for e in check_bytes(bytes(many)))
    assert any("loop" in e for e in check_bytes(sound.write_snd([sound.Clip([1] * 10, 22050, 8, True, 0)])))
    assert any("duplicate" in e for e in check_bytes(sound.write_snd([sound.Clip([1] * 10, 22050, 8, False, 0)] * 2)))
    big = [sound.Clip([1] * 65535, 22050, 8, False, i) for i in range(2)]   # 131,070 B of SFX > the 64 KB region
    assert any("SFX region" in e for e in check_bytes(sound.write_snd(big)))


def test_bank_is_reproducible_and_fits():
    if not sound.SFX_DIR.is_dir():
        pytest.skip("build/sfx/audio not on this PC")
    data = sound.bank()
    assert sound.BANK.read_bytes() == data, "menu/app/assets/SOUND.BIN is stale: python -m omsat.sound bank"
    clips = {c["id"]: c for c in sound.read_snd(data)}
    assert sorted(clips) == [0, 1, 2, 3, 4]
    assert all(c["rate"] == 22050 and c["flags"] == 0 for c in clips.values())
    assert clips[3]["samples"] == 10340 and clips[4]["samples"] == 36221          # scream, chant
    assert sum(c["len"] for i, c in clips.items() if i != 4) <= sound.SFX_MAX
    assert len(data) <= 64 * 2048                                                  # om_audio_boot's 64-sector cap
    assert check_bytes(data) == []


def test_music_layout_and_checker():
    left = list(range(-3000, 2000))
    right = [-v for v in left]
    data = sound.write_mus(left, right, 22050, 4500, -123)
    m = sound.read_mus(data)
    assert m["blocks"] == 2 and m["frames"] == 5000 and m["start_frame"] == 4500 and m["gain_cdb"] == -123
    assert m["data_off"] == 2048 and len(data) == 2048 + 2 * 16384
    assert struct.unpack_from(">h", data, 2048)[0] == -3000                       # block 0, L, frame 0
    assert struct.unpack_from(">h", data, 2048 + 8192)[0] == 3000                 # block 0, R: planar
    assert struct.unpack_from(">h", data, 2048 + 16384 + 2 * 903)[0] == -3000 + 4999          # block 1, L, frame 4999
    assert struct.unpack_from(">h", data, 2048 + 16384 + 2 * 905)[0] == 0         # padded with silence
    assert check_bytes(data) == []
    head_only = data[:2048]
    assert sound.check_mus(head_only, len(data)) == []                             # the menu reads 2 KB only
    assert any("past the end" in e for e in sound.check_mus(head_only, len(data) - 1))
    bad = bytearray(data); bad[64 + 5] = 1                                         # mono
    struct.pack_into(">I", bad, 0x24, bf.crc32(bytes(bad[64:96])))
    assert any("2 ch" in e for e in check_bytes(bytes(bad)))
    crc = bytearray(data); crc[70] ^= 1
    assert check_bytes(bytes(crc))


def test_test_music_is_the_acceptance_instrument():
    data = sound.test_music()
    m = sound.read_mus(data)
    assert m["blocks"] == 3 and len(data) == 51200 <= 60 * 1024                  # overrides: <= 60 KB
    blk = np.frombuffer(data[2048:], dtype=">i2").reshape(3, 2, 4096).astype(float)
    peaks = [20 * math.log10(np.max(np.abs(blk[b, 0])) / 32767) for b in range(3)]
    assert [round(p) for p in peaks] == [-6, -12, -18]                            # one level per block
    for ch, cyc in ((0, 82), (1, 123)):                                           # whole cycles: the loop is seamless
        spec = np.abs(np.fft.rfft(blk[0, ch]))
        assert int(np.argmax(spec)) == cyc
    assert abs(blk[2, 0, -1] - blk[0, 0, 0] * 0.25) < 600                         # the wrap continues the sine


def _wav(path, seconds, rate=22050, amp=0.05, burst=None):
    n = int(seconds * rate)
    t = np.arange(n) / rate
    x = amp * np.sin(2 * np.pi * 220 * t)
    if burst:
        a, b = (int(v * rate) for v in burst)
        x[a:b] = 0.99 * np.sign(np.sin(2 * np.pi * 220 * t[a:b]))
    y = np.stack([x, -x], axis=1)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes((y * 32767).astype("<i2").tobytes())


def test_music_from_wav_gain_limiter_and_offset(tmp_path):
    src, out = tmp_path / "m.wav", tmp_path / "m.mus"
    _wav(src, 3.0)                                                                # a quiet sine: -29 dBFS mean
    res = sound.music_from_wav(str(src), str(out), start_s=1.5, mean_db=-14.0, chunk_s=1, log=lambda *_: None)
    assert abs(res["src_mean_db"] - 20 * math.log10(0.05 / math.sqrt(2))) < 0.1
    assert abs(res["out_mean_db"] - (-14.0)) < 0.1 and res["limited_blocks"] == 0
    loud, lout = tmp_path / "l.wav", tmp_path / "l.mus"
    _wav(loud, 3.0, burst=(1.0, 1.05))                                            # + one full-scale burst
    lres = sound.music_from_wav(str(loud), str(lout), mean_db=-14.0, chunk_s=1, log=lambda *_: None)
    assert lres["limited_blocks"] > 0 and lres["clipped_samples"] == 0 and lres["out_peak_db"] <= -0.9
    data = out.read_bytes()
    m = sound.read_mus(data)
    assert m["start_frame"] == int(1.5 * 22050) and m["frames"] == 3 * 22050 and m["blocks"] == math.ceil(66150 / 4096)
    assert check_bytes(data) == []
    blk = np.frombuffer(data[2048:], dtype=">i2").reshape(m["blocks"], 2, 4096)
    assert np.array_equal(blk[0, 0, :100], -blk[0, 1, :100])                       # L and R kept apart
    with pytest.raises(ValueError):
        mono = tmp_path / "mono.wav"
        with wave.open(str(mono), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b"\0\0" * 10)
        sound.music_from_wav(str(mono), str(tmp_path / "x.mus"), log=lambda *_: None)


def test_music_trim_start_and_music_trim(tmp_path):   # user decision 2026-11-23: the music starts at the mix's 5:00
    src = tmp_path / "m.wav"
    _wav(src, 3.0)
    full, cut = tmp_path / "f.mus", tmp_path / "c.mus"
    sound.music_from_wav(str(src), str(full), mean_db=-14.0, chunk_s=1, log=lambda *_: None)
    assert sound.read_mus(full.read_bytes())["start_frame"] == 0                  # start 0 is the default
    res = sound.music_from_wav(str(src), str(cut), trim_s=1.0, mean_db=-14.0, chunk_s=1, log=lambda *_: None)
    m = sound.read_mus(cut.read_bytes())
    assert res["frames"] == m["frames"] == 2 * 22050 and m["start_frame"] == 0
    assert check_bytes(cut.read_bytes()) == []
    fd = full.read_bytes()
    mf = sound.read_mus(fd)
    out = tmp_path / "t.mus"
    r = sound.music_trim(str(full), str(out), 1.0)
    td = out.read_bytes()
    mt = sound.read_mus(td)
    k = r["blocks_cut"]
    assert k == int(1.0 * 22050) // 4096 == 5
    assert mt["blocks"] == mf["blocks"] - k and mt["frames"] == mf["frames"] - k * 4096 and mt["start_frame"] == 0
    assert mt["gain_cdb"] == mf["gain_cdb"] and check_bytes(td) == []
    assert td[2048:] == fd[2048 + k * 4096 * 4:]                                  # whole blocks, bytes kept
    with pytest.raises(ValueError):
        sound.music_trim(str(full), str(tmp_path / "x.mus"), 10.0)               # past the end


def test_menu_music_fades_in_over_3_s():   # user decision 2026-11-23: every start or resume after boot
    src = (sound.ROOT / "menu" / "app" / "audio.c").read_text(encoding="utf-8")
    assert "mus_key_on(s_fade_in_long ? s_vps * 3u : s_vps / 4u)" in src
    assert src.count("s_fade_in_long = 1") >= 3          # menu start, Music on, the resume after a cancelled launch
