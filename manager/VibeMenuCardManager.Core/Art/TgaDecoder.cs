// Aether Card Manager, GPL-3.0-or-later.
// TGA covers (Satiator Rings' BOX.TGA, retrohead's box server) decoded exactly as Pillow 12.2.0 decodes them, so
// covers built from them are byte-identical with tools/omsat's (which open them with PIL and convert("RGBA")). A port of
// src/PIL/TgaImagePlugin.py and the unpackers it uses (Unpack.c: BGR, BGRA, BGRA;15Z, P with a BGR/BGRA/BGRA;15Z
// palette; TgaRleDecode.c). Covers SPEC §5.6's variants -- type 2 at 32, 24 and 16 bpp, type 1 (color-mapped) at
// 8 bpp, either origin, the image-ID field -- plus what Pillow also reads: RLE types 9 and 10, greyscale types 3 and 11,
// a right-to-left origin, and the TGA 2.0 footer's "no alpha" attribute for 32 bpp. Pillow behaviours kept on purpose:
// a 16 bpp pixel with bit 15 SET is transparent (BGRA;15Z: inverted alpha), 32 bpp keeps its alpha unless the 2.0
// extension says there is none, and 5-bit channels scale as v * 255 / 31.
namespace VibeMenuCardManager.Core.Art;

public static class TgaDecoder
{
    static byte B5(int v) => (byte)(v * 255 / 31);

    /// <summary>One pixel of `depth` bits at p as RGBA (Pillow's unpacker for the mode it opens the file in).</summary>
    static void Unpack(ReadOnlySpan<byte> s, int depth, bool grey, Span<byte> o)
    {
        switch (depth)
        {
            case 8: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;             // L (greyscale); P is mapped by the caller
            case 16 when grey: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;  // LA
            case 16:
                int v = s[0] | s[1] << 8;
                o[0] = B5((v >> 10) & 31); o[1] = B5((v >> 5) & 31); o[2] = B5(v & 31);
                o[3] = (byte)~((v >> 15) * 255);                              // BGRA;15Z: bit 15 set = transparent
                break;
            case 24: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; break;
            case 32: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; break;
        }
    }

    public static bool LooksLikeTga(ReadOnlySpan<byte> f) =>
        f.Length >= 18 && f[1] is 0 or 1 && f[2] is 1 or 2 or 3 or 9 or 10 or 11 && (f[12] | f[13] << 8) > 0 && (f[14] | f[15] << 8) > 0
        && f[16] is 1 or 8 or 16 or 24 or 32;

    public static RgbaImage Decode(byte[] f)
    {
        if (f.Length < 18) throw new FormatException("TGA: shorter than its header");
        int idLen = f[0], cmType = f[1], type = f[2], w = f[12] | f[13] << 8, h = f[14] | f[15] << 8, depth = f[16], flags = f[17];
        if (cmType is not (0 or 1) || w <= 0 || h <= 0 || depth is not (1 or 8 or 16 or 24 or 32)) throw new FormatException("not a TGA file");
        bool grey = type is 3 or 11, mapped = type is 1 or 9, truecolor = type is 2 or 10, rle = (type & 8) != 0;
        if (!grey && !mapped && !truecolor) throw new FormatException("unknown TGA mode");
        if (mapped && cmType == 0) grey = true;                               // Pillow: mode "L" without a colour map
        if (depth == 1 || (grey && depth is not (8 or 16)) || (mapped && depth != 8) || (truecolor && depth == 8))
            throw new FormatException($"TGA type {type} at {depth} bpp is not decoded");
        int orient = flags & 0x30;
        bool topDown = orient is 0x20 or 0x30, flipH = orient is 0x10 or 0x30;
        int pos = 18 + idLen;
        byte[]? pal = null;                                                   // RGBA, 256 entries
        if (cmType == 1)
        {
            int start = f[3] | f[4] << 8, size = f[5] | f[6] << 8, md = f[7];
            int eb = md switch { 16 => 2, 24 => 3, 32 => 4, _ => throw new FormatException("unknown TGA map depth") };
            pal = new byte[256 * 4];
            for (int i = 0; i < 256; i++) pal[i * 4 + 3] = 255;
            for (int i = 0; i < size && start + i < 256; i++)
                Unpack(f.AsSpan(pos + i * eb, eb), md, false, pal.AsSpan((start + i) * 4, 4));
            if (md == 24) for (int i = 0; i < 256; i++) pal[i * 4 + 3] = 255;
            pos += size * eb;
        }
        int bpp = (depth + 7) / 8, n = w * h;
        var raw = new byte[n * bpp];
        if (!rle)
        {
            if (pos + raw.Length > f.Length) throw new FormatException("TGA: the pixel data is truncated");
            Array.Copy(f, pos, raw, 0, raw.Length);
        }
        else
        {
            int o = 0;
            while (o < raw.Length)
            {
                if (pos >= f.Length) throw new FormatException("TGA: the RLE data is truncated");
                int c = f[pos++], k = ((c & 0x7F) + 1) * bpp;
                if (o + k > raw.Length) throw new FormatException("TGA: an RLE packet overruns the image");
                if ((c & 0x80) != 0)
                {
                    if (pos + bpp > f.Length) throw new FormatException("TGA: the RLE data is truncated");
                    for (int i = 0; i < k; i += bpp) Array.Copy(f, pos, raw, o + i, bpp);
                    pos += bpp;
                }
                else
                {
                    if (pos + k > f.Length) throw new FormatException("TGA: the RLE data is truncated");
                    Array.Copy(f, pos, raw, o, k);
                    pos += k;
                }
                o += k;
            }
        }
        var px = new byte[n * 4];
        for (int y = 0; y < h; y++)
        {
            int sy = topDown ? y : h - 1 - y;
            for (int x = 0; x < w; x++)
            {
                int sx = flipH ? w - 1 - x : x;
                var s = raw.AsSpan((sy * w + sx) * bpp, bpp);
                var d = px.AsSpan((y * w + x) * 4, 4);
                if (mapped && pal != null) pal.AsSpan(s[0] * 4, 4).CopyTo(d);
                else Unpack(s, depth, grey, d);
            }
        }
        if (truecolor && depth == 32 && NoAlphaFooter(f))                     // TGA 2.0: attributes type 0 = no alpha
            for (int i = 3; i < px.Length; i += 4) px[i] = 255;
        return new RgbaImage(w, h, px);
    }

    static bool NoAlphaFooter(byte[] f)
    {
        if (f.Length < 26 || !f.AsSpan(f.Length - 18).SequenceEqual("TRUEVISION-XFILE.\0"u8)) return false;
        long ext = BitConverter.ToUInt32(f, f.Length - 26);
        return ext != 0 && ext + 494 < f.Length && f[ext + 494] == 0;
    }
}
