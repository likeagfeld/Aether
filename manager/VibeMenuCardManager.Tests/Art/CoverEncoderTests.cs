// VibeMenu Card Manager, GPL-3.0-or-later.
// The cover encoder against tools/omsat/artpak.py encode_cover (manager/golden/expect/art_covers.json, mkart.py):
// every case byte-identical -- the same-size and solid-color cases by SHA-256, the resampled ones (Lanczos, up and down,
// an RGBA disc on a transparent field) against the full Python output.
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core.Art;

namespace VibeMenuCardManager.Tests.Art;

public static class CoverCheck
{
    /// <summary>mkart.py's input images, by the same formulas.</summary>
    public static RgbaImage Make(JsonElement spec)
    {
        int w = spec.GetProperty("w").GetInt32(), h = spec.GetProperty("h").GetInt32();
        var px = new byte[w * h * 4];
        string kind = spec.GetProperty("kind").GetString()!;
        int[] rgb = kind == "solid" ? spec.GetProperty("rgb").EnumerateArray().Select(x => x.GetInt32()).ToArray() : [];
        double r = Math.Min(w, h) / 2.0 - 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int i = (y * w + x) * 4;
                (int R, int G, int B, int A) p = kind switch
                {
                    "gradient" => (x * 255 / Math.Max(1, w - 1), y * 255 / Math.Max(1, h - 1), 128, 255),
                    "solid" => (rgb[0], rgb[1], rgb[2], 255),
                    _ => Math.Pow(x - w / 2.0 + 0.5, 2) + Math.Pow(y - h / 2.0 + 0.5, 2) <= r * r
                        ? ((x * 7) & 255, (y * 5) & 255, 200, 255) : (0, 0, 0, 0),
                };
                (px[i], px[i + 1], px[i + 2], px[i + 3]) = ((byte)p.R, (byte)p.G, (byte)p.B, (byte)p.A);
            }
        return new RgbaImage(w, h, px);
    }

    public sealed record Case(string Name, string Part, JsonElement Spec, bool Square, int W, int H, bool Exact, string Sha, byte[]? Data);

    public static List<Case> Load()
    {
        var d = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/art_covers.json"))).RootElement;
        return d.GetProperty("covers").EnumerateArray().Select(c => new Case(c.GetProperty("name").GetString()!,
            c.GetProperty("part").GetString()!, c.GetProperty("spec"), c.GetProperty("layout").GetString() == "square",
            c.GetProperty("w").GetInt32(), c.GetProperty("h").GetInt32(), c.GetProperty("exact").GetBoolean(),
            c.GetProperty("sha256").GetString()!,
            c.TryGetProperty("data_zlib_b64", out var z) ? Inflate(Convert.FromBase64String(z.GetString()!)) : null)).ToList();
    }

    static byte[] Inflate(byte[] z)
    {
        using var s = new ZLibStream(new MemoryStream(z), CompressionMode.Decompress);
        using var o = new MemoryStream();
        s.CopyTo(o);
        return o.ToArray();
    }

    /// <summary>Each case encoded and compared; the mismatches, with the first differing pixel.</summary>
    public static List<string> Compare(IEnumerable<Case> cases, Func<RgbaImage, (int, int), byte[]> encode)
    {
        var bad = new List<string>();
        foreach (var c in cases)
        {
            var img = Make(c.Spec);
            if (CoverEncoder.IsSquare(img.W, img.H) != c.Square) { bad.Add($"{c.Name}: layout differs"); continue; }
            var b = encode(img, (c.W, c.H));
            if (Convert.ToHexStringLower(SHA256.HashData(b)) == c.Sha) continue;
            string where = "";
            if (c.Data != null)
                for (int i = 0; i < Math.Min(b.Length, c.Data.Length); i += 2)
                    if (b[i] != c.Data[i] || b[i + 1] != c.Data[i + 1])
                    { where = $" first at pixel ({i / 2 % c.W},{i / 2 / c.W}): {b[i]:X2}{b[i + 1]:X2} expect {c.Data[i]:X2}{c.Data[i + 1]:X2}"; break; }
            bad.Add($"{c.Name} {c.Part} {c.W}x{c.H}: differs{where}");
        }
        return bad;
    }
}

public class CoverEncoderTests
{
    [Fact]
    public void Every_cover_is_byte_identical_with_omsat_encode_cover()
    {
        var cases = CoverCheck.Load();
        Assert.Equal(21, cases.Count);                       // the exact count
        Assert.Contains(cases, c => !c.Exact);                   // the resampled cases are there
        var bad = CoverCheck.Compare(cases, CoverEncoder.Encode);
        Assert.True(bad.Count == 0, $"{bad.Count} of {cases.Count} differ:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void Layouts_match_omsat_layout_for()
    {
        var d = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/art_covers.json"))).RootElement;
        foreach (var c in d.GetProperty("layout_cases").EnumerateObject())
        {
            var wh = c.Name.Split('x').Select(int.Parse).ToArray();
            Assert.Equal(c.Value.GetString() == "square", CoverEncoder.IsSquare(wh[0], wh[1]));
        }
    }

    [Theory]
    [InlineData(255, 0, 0, 0x801F)]
    [InlineData(0, 255, 0, 0x83E0)]
    [InlineData(0, 0, 255, 0xFC00)]
    [InlineData(0, 0, 0, 0x8000)]
    [InlineData(7, 7, 7, 0x8000)]
    public void Rgb555_sets_bit_15_and_truncates(int r, int g, int b, int want) => Assert.Equal(want, CoverEncoder.Rgb555(r, g, b));

    [Fact]
    public void A_decoded_png_is_straight_rgba()
    {
        // a 2x1 PNG: opaque red, then half-transparent green (SkiaSharp encodes it, our decoder reads it back)
        var info = new SkiaSharp.SKImageInfo(2, 1, SkiaSharp.SKColorType.Rgba8888, SkiaSharp.SKAlphaType.Unpremul);
        using var bmp = new SkiaSharp.SKBitmap(info);
        bmp.SetPixel(0, 0, new SkiaSharp.SKColor(255, 0, 0, 255));
        bmp.SetPixel(1, 0, new SkiaSharp.SKColor(0, 255, 0, 128));
        using var png = bmp.Encode(SkiaSharp.SKEncodedImageFormat.Png, 100);
        var img = RgbaImage.Decode(png.ToArray());
        Assert.Equal((2, 1), (img.W, img.H));
        Assert.Equal(new byte[] { 255, 0, 0, 255 }, img.Rgba[..4]);
        Assert.Equal(128, img.Rgba[7]);
        Assert.Throws<FormatException>(() => RgbaImage.Decode([1, 2, 3]));
    }
}
