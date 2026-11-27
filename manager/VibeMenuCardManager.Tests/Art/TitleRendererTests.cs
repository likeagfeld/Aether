// VibeMenu Card Manager, GPL-3.0-or-later.
// The JP-title renderer against tools/omsat/jpfont.py render_jp (manager/golden/expect/art_titles.json, mkart.py) over 16
// titles. Not byte-identical (SkiaSharp + the static Noto Sans JP Regular against FreeType + the variable font at wght
// 400), so the bitmap check is a measured similarity. Re-measured after M6 Task 18d's vertical layout (c7ae17e: a fixed
// baseline per size, overhang shifted in), 2026-11-22, over the 14 Japanese titles:
//   right size:     IoU of inked pixels 0.547-0.944, mean |4-bit level difference| over inked pixels 1.73-5.50;
//   one size off:   IoU 0.172-0.594,                 mean difference 5.07-7.82.
// The one-size-off ranges now overlap the right ones: 14 and 13 px share baseline row 13, so the single kana "あ" one
// size off (IoU 0.594, diff 5.07) is as close as the hardest right render (真・女神転生, 0.547 / 5.50). The bounds stay
// IoU >= 0.52 and diff <= 6.0 for the right render (0.027 below its worst IoU: a failure after a SkiaSharp or font change
// means "re-measure"); a one-size-off render must fail them or be worse than the right render on both measures (per
// title). Latin-only titles (SEGA RALLY, Gun Griffon: Pillow's hinted Latin advances differ from the static CFF
// instance's by up to 4 px) are held to the size and width rules only.
using System.Text.Json;
using VibeMenuCardManager.Core.Art;

namespace VibeMenuCardManager.Tests.Art;

public static class TitleCheck
{
    public const double MinIou = 0.52, MaxDiff = 6.0;

    public sealed record Title(string Text, int? Px, bool Scaled, Dictionary<int, int> InkRight, int W, byte[] Data, bool Latin);

    public static List<Title> Load()
    {
        var d = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/art_titles.json"))).RootElement;
        return d.GetProperty("titles").EnumerateArray().Select(t =>
        {
            string text = t.GetProperty("text").GetString()!;
            return new Title(text, t.GetProperty("px").ValueKind == JsonValueKind.Number ? t.GetProperty("px").GetInt32() : null,
                t.GetProperty("scaled").GetBoolean(),
                t.GetProperty("ink_right").EnumerateObject().ToDictionary(p => int.Parse(p.Name), p => p.Value.GetInt32()),
                t.GetProperty("w").GetInt32(), Convert.FromHexString(t.GetProperty("data").GetString()!), text.All(c => c < 128));
        }).ToList();
    }

    /// <summary>(IoU of inked pixels, mean |level difference| over pixels inked in either) of two 4-bpp, 16-row bitmaps.</summary>
    public static (double Iou, double Diff) Similarity(byte[] a4, int wa, byte[] b4, int wb)
    {
        static int Get(byte[] d, int dw, int x, int y) => x >= dw ? 0 : (x % 2 == 0 ? d[(y * dw + x) / 2] >> 4 : d[(y * dw + x) / 2] & 15);
        int both = 0, any = 0;
        double diff = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < Math.Max(wa, wb); x++)
            {
                int a = Get(a4, wa, x, y), b = Get(b4, wb, x, y);
                if (a == 0 && b == 0) continue;
                any++;
                diff += Math.Abs(a - b);
                if (a > 0 && b > 0) both++;
            }
        return any == 0 ? (1, 0) : ((double)both / any, diff / any);
    }

    public static bool Close((double Iou, double Diff) s) => s.Iou >= MinIou && s.Diff <= MaxDiff;
}

public class TitleRendererTests
{
    static TitleRenderer Renderer()
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), $"Noto Sans JP is not at {TitleRenderer.DefaultFontPath} (an offline first build)");
        return new TitleRenderer();
    }

    [SkippableFact]
    public void Size_choice_and_width_follow_render_jp()
    {
        using var r = Renderer();
        var titles = TitleCheck.Load();
        Assert.Equal(16, titles.Count);
        foreach (var t in titles)
        {
            foreach (var (px, right) in t.InkRight)
                Assert.True(Math.Abs(r.InkRight(t.Text, px) - right) <= 4, $"{t.Text} at {px} px: ink right {r.InkRight(t.Text, px)}, Pillow {right}");
            var (title, px2, scaled) = r.RenderJp(t.Text);
            Assert.Equal(t.Scaled, scaled);
            // the size choice is asserted where Pillow's ink edge is clear of 160 by more than 4 px at the chosen size
            // and the next larger one: a borderline title may fall either side between two rasterizers
            bool clear = t.Px is not int p || (TitleRenderer.MaxW - t.InkRight[p] > 4 && (p == 14 || t.InkRight[p + 1] - TitleRenderer.MaxW > 4));
            if (clear) Assert.Equal(t.Px, px2);
            Assert.True(Math.Abs(title!.W - t.W) <= 8, $"{t.Text}: width {title.W}, Pillow {t.W}");
            Assert.Equal(0, title.W % 8);
            Assert.Equal(16, title.H);
            Assert.Equal(title.W / 2 * 16, title.Data.Length);
        }
    }

    [SkippableFact]
    public void Bitmaps_are_close_to_render_jp_and_a_one_size_off_render_is_not()
    {
        using var r = Renderer();
        var bad = new List<string>();
        int n = 0;
        foreach (var t in TitleCheck.Load().Where(t => !t.Latin))
        {
            var (title, _, _) = r.RenderJp(t.Text);
            var s = TitleCheck.Similarity(title!.Data, title.W, t.Data, t.W);
            if (!TitleCheck.Close(s)) bad.Add($"{t.Text}: IoU {s.Iou:F3}, diff {s.Diff:F2}");
            if (t.Px is int p)
            {
                var off = r.Render(t.Text, p == 10 ? 11 : p - 1)!;
                var so = TitleCheck.Similarity(off.Data, off.W, t.Data, t.W);
                if (TitleCheck.Close(so) && !(so.Iou < s.Iou && so.Diff > s.Diff))
                    bad.Add($"{t.Text}: the one-size-off render (IoU {so.Iou:F3}, diff {so.Diff:F2}) is as close as the right one (IoU {s.Iou:F3}, diff {s.Diff:F2})");
            }
            n++;
        }
        Assert.Equal(14, n);
        Assert.True(bad.Count == 0, string.Join("\n", bad));
    }

    [SkippableFact]
    public void Baseline_is_pillows_ascent()
    {
        using var r = Renderer();
        // Pillow getmetrics()[0] of the variable font at wght 400 (measured this session): 10:12 11:13 12:14 13:16 14:17
        Assert.Equal([12, 13, 14, 16, 17], new[] { 10, 11, 12, 13, 14 }.Select(r.Ascent));
    }

    [SkippableFact]
    public void Empty_or_inkless_text_gives_no_bitmap()
    {
        using var r = Renderer();
        Assert.Null(r.Render(" ", 14));
        Assert.Null(r.RenderJp("").Title);
    }

    [Fact]
    public void Pack4_puts_the_left_pixel_in_the_high_nibble()
    {
        Assert.Equal(new byte[] { 0xF1, 0x80 }, TitleRenderer.Pack4([0xFF, 0x1F, 0x80, 0x0F]));
    }

    [Fact]
    public void A_missing_font_is_a_named_error_not_a_fallback()
    {
        var e = Assert.Throws<FontMissingException>(() => new TitleRenderer(Path.Combine(Path.GetTempPath(), "no-such-font.otf")));
        Assert.Contains("Noto Sans JP is not at", e.Message);
    }
}
