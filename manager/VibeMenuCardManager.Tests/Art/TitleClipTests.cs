// VibeMenu Card Manager, GPL-3.0-or-later.
// M6 Task 18d on the C# side: no JP-title ink may fall outside the bitmap (contract/SPEC.md §5.2 vertical layout). The
// same oracle as tools/tests/test_jpfont.py: the text at the size RenderJp chose, drawn on a tall canvas (64 rows,
// Pillow's "la" anchor at row 24) and quantized to the same 4 bpp (scaled titles resampled to 160 as RenderJp does);
// the count of non-zero nibbles cannot change under an integer vertical shift (checked first), so reference minus
// bitmap is the ink that was clipped. The same cases: the worst titles of build/db/jp_titles.json, every title there,
// a kanji title centred in the title box, the shift rule under the old baselines, and a 12-row bitmap stepping down.
using VibeMenuCardManager.Core.Art;

namespace VibeMenuCardManager.Tests.Art;

public class TitleClipTests
{
    const int RefH = 64, RefY = 24;

    // test_jpfont.py WORST
    static readonly string[] Worst =
    [
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
    ];

    static TitleRenderer Renderer()
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), $"Noto Sans JP is not at {TitleRenderer.DefaultFontPath}");
        return new TitleRenderer();
    }

    static int Nibbles(byte[] g) => g.Count(v => v >> 4 != 0);

    static int BitmapNibbles(Core.Formats.ArtJpTitle t) => t.Data.Sum(b => (b >> 4 != 0 ? 1 : 0) + ((b & 15) != 0 ? 1 : 0));

    /// <summary>test_jpfont.py _reference: the text at px on a 2048 x 64 canvas, "la" anchor at row y; cropped to the ink
    /// rounded up to 8, or (scaled) to the ink and resampled to 160 wide, rows kept.</summary>
    static byte[] Reference(TitleRenderer r, string text, int px, bool scaled, int y = RefY)
    {
        const int W = 2048;
        var im = r.Raster(text, px, W, RefH, y + r.Ascent(px));
        int right = 0;
        for (int i = 0; i < im.Length; i++) if (im[i] != 0) right = Math.Max(right, i % W + 1);
        int cw = scaled ? right : (right + 7) / 8 * 8;
        var c = new byte[cw * RefH];
        for (int row = 0; row < RefH; row++) Array.Copy(im, row * W, c, row * cw, cw);
        return scaled ? PilResample.Resize(c, cw, RefH, 1, TitleRenderer.MaxW, RefH) : c;
    }

    static int Clipped(TitleRenderer r, string text, IReadOnlyDictionary<int, int>? baselines = null)
    {
        var (t, px, scaled) = r.RenderJp(text, TitleRenderer.Height, baselines ?? TitleRenderer.Baseline);
        Assert.NotNull(t);
        Assert.True(t.H == 16 && t.W % 8 == 0 && t.W >= 8 && t.W <= TitleRenderer.MaxW && t.Data.Length == t.W / 2 * t.H);
        return Nibbles(Reference(r, text, px ?? 10, scaled)) - BitmapNibbles(t);
    }

    [SkippableFact]
    public void The_reference_is_shift_invariant()
    {
        using var r = Renderer();
        foreach (var t in Worst)
            foreach (int px in new[] { 14, 10 })
                Assert.Equal(Nibbles(Reference(r, t, px, false)), Nibbles(Reference(r, t, px, false, RefY + 3)));
    }

    [SkippableFact]
    public void The_worst_case_titles_lose_no_ink()
    {
        using var r = Renderer();
        var bad = Worst.Select(t => (t, Lost: Clipped(r, t))).Where(x => x.Lost != 0).Select(x => $"{x.t}: {x.Lost} ink px clipped").ToList();
        Assert.True(bad.Count == 0, string.Join("\n", bad));
    }

    [SkippableFact]
    public void Every_jp_title_loses_no_ink()
    {
        string src = Repo.P("build/db/jp_titles.json");
        Skip.IfNot(File.Exists(src), $"{src} not built (python -m omsat.coverproject)");
        using var r = Renderer();
        var titles = System.Text.Json.JsonDocument.Parse(File.ReadAllText(src)).RootElement.EnumerateObject()
                           .Select(p => (p.Name, Text: p.Value.GetString() ?? "")).Where(x => x.Text != "").ToList();
        var bad = new List<string>();
        long total = 0;
        foreach (var (key, text) in titles)
        {
            if (r.RenderJp(text).Title == null) continue;          // no ink at all (none in the corpus; never clipped)
            int lost = Clipped(r, text);
            total += lost;
            if (lost != 0) bad.Add(key);
        }
        Assert.True(titles.Count > 1000);
        Assert.True(bad.Count == 0, $"{bad.Count} of {titles.Count} JP titles clipped, {total} ink px; first: {string.Join(", ", bad.Take(8))}");
    }

    [SkippableFact]
    public void A_kanji_title_is_centred_in_the_title_box()
    {
        using var r = Renderer();
        var t = r.RenderJp("電脳戦機バーチャロン").Title!;
        var rows = Enumerable.Range(0, t.H).Where(y => t.Data.Skip(y * (t.W / 2)).Take(t.W / 2).Any(b => b != 0)).ToList();
        Assert.True(rows[0] >= 0 && rows[^1] <= 15);
        Assert.True(Math.Abs((rows[0] + rows[^1] + 1) / 2.0 - 8) <= 1.5, string.Join(",", rows));
    }

    [SkippableFact]
    public void Overhanging_ink_is_shifted_in_not_clipped()
    {
        // the old placement's baselines (the font's ascent): every worst-case title must still lose no ink
        using var r = Renderer();
        var old = new Dictionary<int, int> { [14] = 17, [13] = 16, [12] = 14, [11] = 13, [10] = 12 };
        var bad = Worst.Where(t => Clipped(r, t, old) != 0).ToList();
        Assert.True(bad.Count == 0, string.Join("\n", bad));
    }

    [SkippableFact]
    public void Ink_taller_than_the_bitmap_takes_a_smaller_size()
    {
        using var r = Renderer();
        Assert.Equal(14, r.RenderJp("電脳戦機").Px);
        var (t, px, scaled) = r.RenderJp("電脳戦機", 12, TitleRenderer.Baseline);
        Assert.False(scaled);
        Assert.True(t!.H == 12 && px < 14, $"px {px}");
        var refImg = r.Raster("電脳戦機", px!.Value, 1024, RefH, RefY + r.Ascent(px.Value));
        Assert.Equal(Nibbles(refImg), BitmapNibbles(t));
    }

    /// <summary>test_jpfont.py test_too_tall_even_at_10_px_is_dropped_not_clipped (4cdfc7b): an 8-row bitmap cannot hold
    /// 10 px kanji, so a short title (the unscaled sizes all skip) and an over-long one (the scaled path) both throw
    /// TitleTooTallException -- no bitmap, never a clipped or squashed one; 12 rows hold it.</summary>
    [SkippableFact]
    public void Too_tall_even_at_10_px_is_dropped_not_clipped()
    {
        using var r = Renderer();
        foreach (var t in new[] { "電脳戦機", new string('あ', 60) })
            Assert.Throws<TitleTooTallException>(() => r.RenderJp(t, 8, TitleRenderer.Baseline));
        Assert.NotNull(r.RenderJp("電脳戦機", 12, TitleRenderer.Baseline).Title);
    }

    [SkippableFact]
    public void No_ink_is_none_not_too_tall()
    {
        using var r = Renderer();
        Assert.Equal((null, null, false), (r.RenderJp("　").Title, r.RenderJp("　").Px, r.RenderJp("　").Scaled));   // an ideographic space
    }

    /// <summary>SPEC §5.2 step 2: B = base(px) + floor((H - 16) / 2) -- a floor, so an odd H below 16 rounds down (H 13:
    /// -2, where C#'s integer division would give -1). The ink of a 13-row render sits one row higher than a truncating
    /// baseline would put it, unless the shift rule moved it.</summary>
    [SkippableFact]
    public void The_baseline_offset_is_a_floor()
    {
        using var r = Renderer();
        var t13 = r.RenderJp("あ", 13, TitleRenderer.Baseline).Title!;
        var refImg = r.Raster("あ", 14, t13.W, 13 + 64, 32 + 13 - 2);                      // base 13, floor(-3 / 2) = -2
        var want = new byte[t13.W * 13];
        Array.Copy(refImg, 32 * t13.W, want, 0, want.Length);
        Assert.Equal(TitleRenderer.Pack4(want), t13.Data);
    }

    /// <summary>Mutation: the pre-18d placement (baseline at the ascent, no shift) clips -- the oracle sees it.</summary>
    [SkippableFact]
    public void Mutation_the_old_placement_clips_and_the_oracle_sees_it()
    {
        using var r = Renderer();
        var t = r.Raster("電脳戦機バーチャロン", 14, 1024, 16, r.Ascent(14));      // the old 16-row render at 14 px
        Assert.True(Nibbles(Reference(r, "電脳戦機バーチャロン", 14, false)) - Nibbles(t) > 0);
    }
}
