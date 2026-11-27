import pytest

from omsat import jpfont

NOTO = jpfont.DEFAULT


def _skip_if_no_font():
    if not (NOTO.exists() and jpfont.usable(NOTO)):
        pytest.skip(f"Noto Sans JP not cached at {NOTO}; run the real cardbuild smoke "
                    f"build (or python -c \"from omsat.jpfont import ensure_font; ensure_font()\") first")


def test_download_failure_is_reported_not_raised(tmp_path):
    def boom(url):
        raise OSError("offline")
    p, why = jpfont.ensure_font(None, True, fetch=boom, default=tmp_path / "f.ttf")
    assert p is None and "download failed" in why and "offline" in why


def test_garbage_download_is_rejected(tmp_path):
    p, why = jpfont.ensure_font(None, True, fetch=lambda url: b"not a font", default=tmp_path / "f.ttf")
    assert p is None and why == "downloaded file is not a usable font"


def test_no_download_when_disabled(tmp_path):
    p, why = jpfont.ensure_font(None, False, default=tmp_path / "missing.ttf")
    assert p is None and "missing" in why


# --- Fix round 1/2 (Ruling M2-R6, revised: over-long JP titles are scaled to the menu's actual
# 160px JP panel (OM_JP_CANVAS_W), never cropped and never scaled to the contract's 320px cap) --


def _test_only_crop_at_160(text, f):
    """TEST ONLY: the old, wrong over-long behaviour (crop at 160 instead of resample), with the current vertical
    layout (jpfont._place), so the comparison isolates the horizontal rule"""
    img, right = jpfont._place(text, f, 10, 1024, 16)
    return 160, 16, jpfont._pack_4bpp(img.crop((0, 0, 160, 16)))


def test_overlong_title_is_scaled_to_160_not_cropped():
    assert jpfont.OM_JP_CANVAS_W == 160          # pins the constant itself against drift
    _skip_if_no_font()
    long_text = "あ" * 60      # "a" x60 in hiragana: far wider than 160px even at 10px
    result, scaled = jpfont.render_jp(long_text, NOTO)
    assert result is not None and scaled is True
    w, h, data = result
    assert w == jpfont.OM_JP_CANVAS_W == 160 and w % 8 == 0 and h == 16
    assert len(data) == w // 2 * h

    # the right-most 8px column (last 4 bytes of each row, 2px/byte) must carry ink: the title's
    # end is not cut off.
    row_bytes = w // 2
    last_col = [data[y * row_bytes + row_bytes - 4: y * row_bytes + row_bytes] for y in range(h)]
    assert any(any(b for b in row) for row in last_col)

    # not simply the first 160px of an uncropped render: a naive crop-at-160 (the old, wrong
    # behaviour) must produce different bytes, because it would cut the glyphs off mid-row
    # instead of shrinking them to fit.
    f = jpfont._font(NOTO, 10)
    naive = _test_only_crop_at_160(long_text, f)
    assert naive != (w, h, data)


def test_title_that_fits_is_unscaled():
    _skip_if_no_font()
    result, scaled = jpfont.render_jp("日本", NOTO)      # short title, fits easily
    assert result is not None and scaled is False
    w, h, data = result
    assert w <= jpfont.OM_JP_CANVAS_W and w % 8 == 0


# --- M6 Task 18d: no JP-title ink may fall outside the 16-row bitmap (2026-11-22 review: at 14 px Pillow's default
# "la" anchor put the baseline at row 17 of a 16-row bitmap, so rows 16-18 -- the bottom of most ideographs -- were
# cut off; the user sees titles with their bottoms missing). Oracle, independent of jpfont's own placement: the same
# text at the size render_jp chose, drawn on a tall canvas, quantized to the same 4 bpp; the count of non-zero nibbles
# cannot change under an integer vertical shift, so (reference count - bitmap count) is the ink that was clipped.

REF_H, REF_Y = 64, 24
# The worst cases from the 1,270 titles of build/db/jp_titles.json (measured 2026-11-22): the lowest ink (full-width
# lowercase descenders, bottom row 21 at 14 px under the old placement), the highest ink (top row 4), plain kanji and
# kana, and over-long titles (the scaled path).
WORST = [
    "結婚　〜Ｍａｒｒｉａｇｅ〜",
    "卒業Ⅲ　Ｗｅｄｄｉｎｇ　Ｂｅｌｌ",
    "サクラ大戦　（特別限定版　Ａ　ｔｙｐｅ）",
    "ブレインバトル　Ｑ",
    "タイトーチェイスＨ．Ｑ．＋Ｓ．Ｃ．Ｉ．",
    "日本語",
    "電脳戦機バーチャロン",
    "ときめきメモリアル　〜ｆｏｒｅｖｅｒ　ｗｉｔｈ　ｙｏｕ〜",
    "機動戦艦ナデシコ　Ｔｈｅ　ｂｌａｎｋ　ｏｆ　３ｙｅａｒｓ",
    "Ｔｈｅ　Ｌｅｇｅｎｄ　ｏｆ　Ｈｅｒｏｅｓ　Ⅰ＆Ⅱ　英雄伝説",
]


def _nibbles(img):
    return sum(1 for p in img.getdata() if p >> 4)


def _render_with_px(text, monkeypatch):
    used = []
    real = jpfont._font

    def spy(path, px):
        used.append(px)
        return real(path, px)
    monkeypatch.setattr(jpfont, "_font", spy)
    try:
        result, scaled = jpfont.render_jp(text, NOTO)
    finally:
        monkeypatch.setattr(jpfont, "_font", real)
    return result, scaled, used[-1]


def _reference(text, px, scaled, y=REF_Y):
    from PIL import Image, ImageDraw
    f = jpfont._font(NOTO, px)
    im = Image.new("L", (2048, REF_H))
    ImageDraw.Draw(im).text((0, y), text, fill=255, font=f)
    bb = im.getbbox()
    if scaled:        # Pillow skips the vertical pass when the height is unchanged: rows stay independent
        return im.crop((0, 0, bb[2], REF_H)).resize((jpfont.OM_JP_CANVAS_W, REF_H), Image.LANCZOS)
    return im.crop((0, 0, (bb[2] + 7) // 8 * 8, REF_H))


def _bitmap_nibbles(result):
    w, h, data = result
    return sum((b >> 4 != 0) + (b & 15 != 0) for b in data)


def _clipped(text, monkeypatch):
    result, scaled, px = _render_with_px(text, monkeypatch)
    assert result is not None
    w, h, data = result
    assert h == 16 and w % 8 == 0 and 8 <= w <= jpfont.OM_JP_CANVAS_W and len(data) == w // 2 * h
    return _nibbles(_reference(text, px, scaled)) - _bitmap_nibbles(result), px, scaled


def test_reference_is_shift_invariant():
    """the oracle's premise, measured rather than assumed: an integer vertical shift keeps the nibble count"""
    _skip_if_no_font()
    for t in WORST:
        for px in (14, 10):
            assert _nibbles(_reference(t, px, False)) == _nibbles(_reference(t, px, False, y=REF_Y + 3))


def test_worst_case_titles_lose_no_ink(monkeypatch):
    _skip_if_no_font()
    bad = []
    for t in WORST:
        lost, px, scaled = _clipped(t, monkeypatch)
        if lost:
            bad.append(f"{ascii(t)} at {px} px{' scaled' if scaled else ''}: {lost} ink px clipped")
    assert not bad, f"{len(bad)} of {len(WORST)} worst-case JP titles clipped: " + "; ".join(bad)


def test_every_jp_title_loses_no_ink(monkeypatch):
    import json
    from pathlib import Path
    _skip_if_no_font()
    src = Path(__file__).resolve().parents[2] / "build" / "db" / "jp_titles.json"
    if not src.is_file():
        pytest.skip(f"{src} not built (python -m omsat.coverproject)")
    titles = json.loads(src.read_text(encoding="utf-8"))
    bad, total = [], 0
    for key, t in sorted(titles.items()):
        lost, px, scaled = _clipped(t, monkeypatch)
        total += lost
        if lost:
            bad.append(key)
    assert not bad, (f"{len(bad)} of {len(titles)} JP titles clipped, {total} ink px lost in all "
                     f"(the bottoms of the glyphs cut off on the details card); first: {bad[:8]}")


def test_kanji_title_is_centred_in_the_title_box():
    """the bitmap is drawn at DET_TITLE's y0 - 1 (folders.c draw_details), 16 rows: 170..185, the box's text middle
    OM_DET_TITLE_MID 178 = bitmap row 8. A plain kanji/kana title's ink is centred there (within 1.5 rows)."""
    _skip_if_no_font()
    w, h, data = jpfont.render_jp("電脳戦機バーチャロン", NOTO)[0]
    rows = [y for y in range(h) if any(data[y * (w // 2):(y + 1) * (w // 2)])]
    assert rows[0] >= 0 and rows[-1] <= 15
    assert abs((rows[0] + rows[-1] + 1) / 2 - 8) <= 1.5, rows


def test_overhanging_ink_is_shifted_in_not_clipped(monkeypatch):
    """no title in the corpus overhangs at the shipped baselines, so the shift rule is exercised here: with the old
    placement's baselines (the font's ascent, 17..12 px rows), every worst-case title must still lose no ink"""
    _skip_if_no_font()
    monkeypatch.setattr(jpfont, "BASELINE", {14: 17, 13: 16, 12: 14, 11: 13, 10: 12})
    bad = [t for t in WORST if _clipped(t, monkeypatch)[0]]
    assert not bad, f"{len(bad)} of {len(WORST)} clipped with the shift rule: {[ascii(t) for t in bad]}"


def test_ink_taller_than_the_bitmap_takes_a_smaller_size(monkeypatch):
    """a 12-row bitmap cannot hold 14 px kanji (13-14 rows of ink): render_jp must step down, never clip"""
    _skip_if_no_font()
    result, scaled, px = _render_with_px("電脳戦機", monkeypatch)
    assert px == 14 and not scaled
    used = []
    real = jpfont._font
    monkeypatch.setattr(jpfont, "_font", lambda p, s: used.append(s) or real(p, s))
    w, h, data = jpfont.render_jp("電脳戦機", NOTO, height=12)[0]
    assert h == 12 and used[-1] < 14
    from PIL import Image, ImageDraw
    ref = Image.new("L", (1024, REF_H))
    ImageDraw.Draw(ref).text((0, REF_Y), "電脳戦機", fill=255, font=real(NOTO, used[-1]))
    assert _nibbles(ref) == _bitmap_nibbles((w, h, data))


def test_too_tall_even_at_10_px_is_dropped_not_clipped():
    """SPEC §5.2's corner case, synthetic (no real title reaches it in a 16-row bitmap): an 8-row bitmap cannot hold
    10 px kanji (about 10 rows of ink), so render_jp raises TooTall -- no bitmap, never a clipped or squashed one.
    The same for a short title (the unscaled sizes all skip) and an over-long one (the scaled path)."""
    _skip_if_no_font()
    for t in ("電脳戦機", "あ" * 60):
        with pytest.raises(jpfont.TooTall):
            jpfont.render_jp(t, NOTO, height=8)
    assert jpfont.render_jp("電脳戦機", NOTO, height=12)[0] is not None     # 10 px ink fits 12 rows


def test_no_ink_is_none_not_too_tall():
    _skip_if_no_font()
    assert jpfont.render_jp("　", NOTO) == (None, False)                     # an ideographic space: no ink
