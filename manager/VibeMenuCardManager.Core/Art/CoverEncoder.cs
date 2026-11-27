// Aether Card Manager, GPL-3.0-or-later.
// Covers for ART.PAK (contract/SPEC.md §5.2 blob encodings): the image fitted into the thumb or hero size with its
// aspect kept and transparent letterbox bars (openMenu stretches; design spec §8.3), RGB555 with bit 15 set on an opaque
// pixel, 0x0000 transparent. A port of tools/omsat/artpak.py layout_for / encode_cover / rgb555, byte-identical with it:
// the resize is Pillow's Lanczos (PilResample). Decoding (PNG, JPEG, WebP, GIF, BMP) is SkiaSharp's; TGA is not yet
// supported here (BOX.TGA: SPEC §5.6, a later task).
using SkiaSharp;

namespace VibeMenuCardManager.Core.Art;

/// <summary>An image as straight (unpremultiplied) RGBA8888 pixels, row-major from the top-left.</summary>
public sealed record RgbaImage(int W, int H, byte[] Rgba)
{
    /// <summary>Decode an image file's bytes (anything SkiaSharp decodes) as Pillow's convert("RGBA") sees it.</summary>
    public static RgbaImage Decode(byte[] file)
    {
        // TGA has no signature (Pillow tries it last): anything that is not one of SkiaSharp's formats and has a sound
        // TGA header is decoded as a TGA, Pillow's way (TgaDecoder)
        ReadOnlySpan<byte> f = file;
        bool known = f.StartsWith(new byte[] { 0x89, 0x50, 0x4E, 0x47 }) || f.StartsWith(new byte[] { 0xFF, 0xD8 }) || f.StartsWith("GIF8"u8) || f.StartsWith("BM"u8)
                     || f.StartsWith("RIFF"u8);
        if (!known && TgaDecoder.LooksLikeTga(f)) return TgaDecoder.Decode(file);
        return DecodeSkia(file);
    }

    /// <summary>Decoded straight into unpremultiplied RGBA8888 (a premultiplied decode, then unpremultiplied, rounds
    /// every partly transparent pixel: measured on this task's fixture, three PNG covers differed from Pillow's).</summary>
    static RgbaImage DecodeSkia(byte[] file)
    {
        using var codec = SKCodec.Create(new SKMemoryStream(file)) ?? throw new FormatException("not an image SkiaSharp can decode");
        var info = new SKImageInfo(codec.Info.Width, codec.Info.Height, SKColorType.Rgba8888, SKAlphaType.Unpremul);
        var px = new byte[info.Width * info.Height * 4];
        var h = System.Runtime.InteropServices.GCHandle.Alloc(px, System.Runtime.InteropServices.GCHandleType.Pinned);
        try
        {
            var r = codec.GetPixels(info, h.AddrOfPinnedObject());
            if (r is not (SKCodecResult.Success or SKCodecResult.IncompleteInput))
                throw new FormatException($"the image could not be decoded ({r})");
        }
        finally { h.Free(); }
        return new RgbaImage(info.Width, info.Height, px);
    }
}

public static class CoverEncoder
{
    public static readonly (int W, int H) ThumbSquare = (80, 80), ThumbTall = (64, 100), HeroSquare = (160, 160), HeroTall = (128, 200);

    /// <summary>layout_for: square when width / height >= 0.85 (JP covers), else tall (US/EU).</summary>
    public static bool IsSquare(int w, int h) => (double)w / h >= 0.85;

    /// <summary>rgb555: bit 15 set (opaque), B&lt;&lt;10 | G&lt;&lt;5 | R, each channel's top 5 bits.</summary>
    public static ushort Rgb555(int r, int g, int b) => (ushort)(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));

    /// <summary>encode_cover: fit (scale = min(tw/w, th/h), sizes rounded half to even and at least 1), Lanczos-resized,
    /// pasted centred ((tw-nw)//2, (th-nh)//2) on a transparent canvas; a pixel with alpha >= 128 is its RGB555, any
    /// other 0x0000. Big-endian u16 per pixel.</summary>
    public static byte[] Encode(RgbaImage img, (int W, int H) size) => Encode(img, size, alphaThreshold: 128);

    internal static byte[] Encode(RgbaImage img, (int W, int H) size, int alphaThreshold)
    {
        var (tw, th) = size;
        double scale = Math.Min((double)tw / img.W, (double)th / img.H);
        int nw = Math.Max(1, (int)Math.Round(img.W * scale, MidpointRounding.ToEven));
        int nh = Math.Max(1, (int)Math.Round(img.H * scale, MidpointRounding.ToEven));
        var fitted = PilResample.ResizeRgba(img.Rgba, img.W, img.H, nw, nh);
        int ox = (tw - nw) / 2, oy = (th - nh) / 2;                     // both >= 0: the fit never exceeds the canvas
        var o = new byte[tw * th * 2];
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
            {
                int i = (y * nw + x) * 4;
                if (fitted[i + 3] < alphaThreshold) continue;
                ushort v = Rgb555(fitted[i], fitted[i + 1], fitted[i + 2]);
                int p = ((y + oy) * tw + x + ox) * 2;
                o[p] = (byte)(v >> 8);
                o[p + 1] = (byte)v;
            }
        return o;
    }

    /// <summary>The hero (and, when wanted, the thumb) of an image in the layout its aspect gives.</summary>
    public static (bool Square, byte[] Hero, byte[]? Thumb) Cover(RgbaImage img, bool thumb = false)
    {
        bool sq = IsSquare(img.W, img.H);
        return (sq, Encode(img, sq ? HeroSquare : HeroTall), thumb ? Encode(img, sq ? ThumbSquare : ThumbTall) : null);
    }

    static byte C5(int v) => (byte)(v * 255 / 31);

    /// <summary>The inverse of Rgb555/Encode, for a live preview of what the console will actually show: 0x0000 is
    /// transparent, every other word's top 5 bits of each channel scaled back to 8 bits.</summary>
    public static RgbaImage DecodeRgb555(byte[] data, int w, int h)
    {
        var o = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++)
        {
            int v = (data[i * 2] << 8) | data[i * 2 + 1];
            if ((v & 0x8000) == 0) continue;                 // transparent
            o[i * 4] = C5(v & 31); o[i * 4 + 1] = C5((v >> 5) & 31); o[i * 4 + 2] = C5((v >> 10) & 31); o[i * 4 + 3] = 255;
        }
        return new RgbaImage(w, h, o);
    }

    /// <summary>A plain RGBA PNG of the image, unmodified (no fit/letterbox): what Save changes writes as a game's
    /// BOX.TGA source or a folder's FOLDER.PNG when the user supplies an image through the UI.</summary>
    public static byte[] EncodePng(RgbaImage img)
    {
        using var bmp = new SKBitmap(new SKImageInfo(img.W, img.H, SKColorType.Rgba8888, SKAlphaType.Unpremul));
        System.Runtime.InteropServices.Marshal.Copy(img.Rgba, 0, bmp.GetPixels(), img.Rgba.Length);
        using var image = SKImage.FromBitmap(bmp);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return data.ToArray();
    }
}
