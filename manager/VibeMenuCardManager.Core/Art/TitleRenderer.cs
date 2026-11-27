// Aether Card Manager, GPL-3.0-or-later.
// JP-title bitmaps for ART.PAK (contract/SPEC.md §5.2: 4 bpp, high nibble = left pixel, row stride w/2, width a multiple
// of 8, height 16 here) rendered from Noto Sans JP (Google, SIL OFL 1.1; Directory.Build.targets fetches it at build
// time). A port of tools/omsat/jpfont.py render_jp and _place (M6 Task 18d, c7ae17e; contract/SPEC.md §5.2 "Vertical
// layout"):
// - the largest size of 14..10 px whose ink fits OM_JP_CANVAS_W (160, the menu's JP panel) and is at most 16 rows tall,
//   else 10 px resampled horizontally (Lanczos, never cropped) to exactly 160 (Ruling M2-R6);
// - the text drawn at x 0 with its baseline at a fixed row per size (Pillow's "ls" anchor; BASELINE: 14/13/12 px row 13,
//   11/10 px row 12) on a canvas 32 rows taller above and below; ink that still overhangs the 16 rows is shifted in by
//   the fewest rows, never cut;
// - the width is the ink's right edge rounded up to 8;
// - a title whose ink is taller than the bitmap even at 10 px gets no bitmap: TitleTooTallException (4cdfc7b; the
//   builder reports it in jp.too_tall), never a clipped or squashed one.
// The six steps of SPEC §5.2 "Vertical layout" (4cdfc7b) map to RenderJp (1 width, 4 too tall, 6 width out) and Place
// (2 baseline, B = base(px) + floor((H - 16) / 2); 3 ink rows on the padded canvas; 5 shift).
// Glyphs are drawn as filled outlines (glyph paths + DrawPath): measured this session, a path fill's coverage matches
// FreeType's (Pillow's) within 2 % of ink, where SkiaSharp's DrawText was 36 % heavier. Each glyph sits at a whole-pixel
// pen position (advances rounded, as FreeType's hinted advances are), which brought the long titles closer. Not byte-identical with
// Pillow (different rasterizers, hinting, and the static Regular instance against Pillow's variable font at wght 400):
// TitleRendererTests compares within measured bounds.
// Before 18d the baseline sat at the font's ascent (row 17 at 14 px of a 16-row bitmap) and cut off the bottom of most
// ideographs, in Python and here alike; TitleRendererTests now proves no ink is clipped (the Python test's oracle).
using SkiaSharp;

namespace VibeMenuCardManager.Core.Art;

public sealed class FontMissingException(string path)
    : Exception($"Noto Sans JP is not at {path}: build the Card Manager online once (Directory.Build.targets fetches it), or pass the font's path");

/// <summary>jpfont.py TooTall: the title's ink is taller than the bitmap even at 10 px (SPEC §5.2): no JP bitmap.</summary>
public sealed class TitleTooTallException(int height) : Exception($"JP title ink is over {height} rows even at 10 px");

public sealed class TitleRenderer : IDisposable
{
    public const int MaxW = 160;          // menu/src/omlayout.h OM_JP_CANVAS_W (tools/omsat/jpfont.py OM_JP_CANVAS_W)
    public const int Height = 16;
    static readonly int[] Sizes = [14, 13, 12, 11, 10];
    /// <summary>jpfont.py BASELINE (SPEC §5.2 vertical layout): the baseline row of each size in a 16-row bitmap.</summary>
    public static readonly IReadOnlyDictionary<int, int> Baseline = new Dictionary<int, int> { [14] = 13, [13] = 13, [12] = 13, [11] = 12, [10] = 12 };
    const int Pad = 32;                   // jpfont.py _PAD: the canvas margin above and below the bitmap

    readonly SKTypeface _tf;

    public static string DefaultFontPath => Path.Combine(AppContext.BaseDirectory, "fonts", "NotoSansJP-Regular.otf");

    public TitleRenderer(string? fontPath = null)
    {
        string p = fontPath ?? DefaultFontPath;
        if (!File.Exists(p)) throw new FontMissingException(p);
        _tf = SKTypeface.FromFile(p) ?? throw new FontMissingException(p);
    }

    public void Dispose() => _tf.Dispose();

    SKFont Font(int px) => new(_tf, px) { Edging = SKFontEdging.Antialias, Hinting = SKFontHinting.Normal, Subpixel = false };

    /// <summary>Pillow's ascent for the "la" anchor: the font's ascender in pixels, rounded up.</summary>
    public int Ascent(int px)
    {
        using var f = Font(px);
        f.GetFontMetrics(out var m);
        return (int)MathF.Ceiling(-m.Ascent);
    }

    /// <summary>The text's outline at px, each glyph placed at a whole-pixel pen position (FreeType's hinted advances,
    /// which Pillow's basic layout adds up: each advance rounded to a pixel), baseline at row y (default: Ascent(px),
    /// Pillow's "la" anchor, which getbbox measures with).</summary>
    SKPath TextPath(SKFont f, string text, int px, float? baseline = null)
    {
        var glyphs = f.GetGlyphs(text);
        var widths = f.GetGlyphWidths(glyphs);
        var path = new SKPath();
        float x = 0, y = baseline ?? Ascent(px);
        for (int i = 0; i < glyphs.Length; i++)
        {
            using var g = f.GetGlyphPath(glyphs[i]);
            if (g != null)
            {
                g.Transform(SKMatrix.CreateTranslation(x, y));
                path.AddPath(g);
            }
            x += MathF.Round(widths[i], MidpointRounding.AwayFromZero);
        }
        return path;
    }

    /// <summary>The ink's right edge at px (Pillow's getbbox(text)[2]): the outline's tight bounds, rounded up.</summary>
    public int InkRight(string text, int px)
    {
        using var f = Font(px);
        using var path = TextPath(f, text, px);
        return path.IsEmpty ? 0 : (int)MathF.Ceiling(path.TightBounds.Right);
    }

    /// <summary>The text at px as 8-bit coverage, w columns by rows rows, its baseline at row baseline.</summary>
    internal byte[] Raster(string text, int px, int w, int rows, float baseline)
    {
        var info = new SKImageInfo(w, rows, SKColorType.Gray8, SKAlphaType.Opaque);
        using var bmp = new SKBitmap(info);
        using (var c = new SKCanvas(bmp))
        {
            c.Clear(SKColors.Black);
            using var f = Font(px);
            using var path = TextPath(f, text, px, baseline);
            using var paint = new SKPaint { Color = SKColors.White, IsAntialias = true, Style = SKPaintStyle.Fill };
            c.DrawPath(path, paint);
        }
        var o = new byte[w * rows];
        for (int y = 0; y < rows; y++) System.Runtime.InteropServices.Marshal.Copy(bmp.GetPixels() + y * bmp.RowBytes, o, y * w, w);
        return o;
    }

    /// <summary>Pillow getbbox of an 8-bit image: (left, top, right, bottom) of its nonzero pixels, null when none.</summary>
    static (int L, int T, int R, int B)? Bbox(byte[] g, int w, int rows)
    {
        int l = w, t = rows, r = 0, b = 0;
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < w; x++)
                if (g[y * w + x] != 0) { l = Math.Min(l, x); t = Math.Min(t, y); r = Math.Max(r, x + 1); b = Math.Max(b, y + 1); }
        return r == 0 ? null : (l, t, r, b);
    }

    enum PlaceKind { Placed, NoInk, TooTall }

    /// <summary>jpfont.py _place: the text at px on a canvas `width` wide, baseline at baselines[px] + (height - 16) / 2;
    /// the height-row coverage and the ink's right edge; NoInk; or TooTall (its ink is taller than height). Ink that
    /// overhangs the top or bottom is shifted in by the minimum, never cut.</summary>
    (PlaceKind Kind, byte[] Img, int Right) Place(string text, int px, int width, int height, IReadOnlyDictionary<int, int> baselines)
    {
        int rows = height + 2 * Pad;
        var tall = Raster(text, px, width, rows, Pad + baselines[px] + (int)Math.Floor((height - 16) / 2.0));   // step 2: floor, as Python's //
        if (Bbox(tall, width, rows) is not var (_, bt, br, bb)) return (PlaceKind.NoInk, [], 0);
        int top = bt - Pad, bot = bb - Pad;
        if (bot - top > height) return (PlaceKind.TooTall, [], 0);
        int dy = top < 0 ? -top : bot > height ? height - bot : 0;
        var o = new byte[width * height];
        Array.Copy(tall, (Pad - dy) * width, o, 0, width * height);
        return (PlaceKind.Placed, o, br);
    }

    static byte[] Crop(byte[] g, int w, int newW, int rows)
    {
        var o = new byte[newW * rows];
        for (int y = 0; y < rows; y++) Array.Copy(g, y * w, o, y * newW, Math.Min(w, newW));
        return o;
    }

    /// <summary>4 bpp: the top nibble of each pixel, high nibble = left pixel.</summary>
    public static byte[] Pack4(byte[] g) => Pack4(g, nibbleSwap: false);

    internal static byte[] Pack4(byte[] g, bool nibbleSwap)
    {
        var o = new byte[g.Length / 2];
        for (int i = 0; i < o.Length; i++)
            o[i] = nibbleSwap ? (byte)((g[2 * i + 1] >> 4 << 4) | (g[2 * i] >> 4)) : (byte)((g[2 * i] >> 4 << 4) | (g[2 * i + 1] >> 4));
        return o;
    }

    /// <summary>The 8-bit coverage a 4-bpp bitmap stands for (level n -> n * 17), for tests and previews.</summary>
    public static byte[] Unpack4(byte[] d) => d.SelectMany(b => new[] { (byte)((b >> 4) * 17), (byte)((b & 15) * 17) }).ToArray();

    /// <summary>The title placed at px (the vertical layout, no size search): null when the text has no ink or its ink
    /// is taller than the bitmap. Tests use it for the one-size-off render.</summary>
    public Formats.ArtJpTitle? Render(string text, int px, int maxW = MaxW, int height = Height)
    {
        var (kind, img, right) = Place(text, px, 1024, height, Baseline);
        if (kind != PlaceKind.Placed) return null;
        int w = Math.Min(maxW, (right + 7) / 8 * 8);
        return new Formats.ArtJpTitle(w, height, Pack4(Crop(img, 1024, w, height)));
    }

    /// <summary>render_jp: the title, the px used (null when it was scaled) and whether it was scaled to MaxW; a title
    /// with no ink gives null; one too tall even at 10 px throws TitleTooTallException.</summary>
    public (Formats.ArtJpTitle? Title, int? Px, bool Scaled) RenderJp(string text) => RenderJp(text, Height, Baseline);

    /// <param name="height">the bitmap's rows (16 on the card; tests take 12, as test_jpfont.py does)</param>
    /// <param name="baselines">the baseline table (tests take the old placement's, to exercise the shift rule)</param>
    internal (Formats.ArtJpTitle? Title, int? Px, bool Scaled) RenderJp(string text, int height, IReadOnlyDictionary<int, int> baselines)
    {
        foreach (int px in Sizes)
        {
            if (InkRight(text, px) > MaxW) continue;
            var (kind, img, right) = Place(text, px, 1024, height, baselines);
            if (kind == PlaceKind.NoInk) return (null, null, false);
            if (kind == PlaceKind.TooTall) continue;
            int w = Math.Min(MaxW, (right + 7) / 8 * 8);
            return (new Formats.ArtJpTitle(w, height, Pack4(Crop(img, 1024, w, height))), px, false);
        }
        int bw = Math.Max(1, InkRight(text, 10));
        var (k2, img2, right2) = Place(text, 10, bw, height, baselines);
        if (k2 == PlaceKind.NoInk) return (null, null, false);
        if (k2 == PlaceKind.TooTall) throw new TitleTooTallException(height);
        var ink = Crop(img2, bw, right2, height);                 // trims only blank margin past the ink, never content
        var scaled = PilResample.Resize(ink, right2, height, 1, MaxW, height);
        return (new Formats.ArtJpTitle(MaxW, height, Pack4(scaled)), null, true);
    }
}
