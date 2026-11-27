// Aether Card Manager, GPL-3.0-or-later.
// Pillow's Lanczos resize, reproduced so the Card Manager's covers are byte-identical to tools/omsat's (which resize
// with PIL Image.LANCZOS). A port of Pillow 12.2.0 src/libImaging/Resample.c (precompute_coeffs,
// normalize_coeffs_8bpc, ImagingResampleHorizontal/Vertical_8bpc, ImagingResampleInner: PRECISION_BITS 22, the clip8
// table) and of src/PIL/Image.py resize's RGBA handling (premultiplied through Convert.c rgbA2rgba / rgba2rgbA).
// Pillow: Copyright (c) 1997-2011 Secret Labs AB, (c) 1995-2011 Fredrik Lundh and contributors, (c) 2010 Jeffrey A.
// Clark (Alex) and contributors; MIT-CMU (HPND). CREDITS.md credits it in the Pillow row ("Pillow 12.2.0 Lanczos
// resample and RGBA convert", added in 7a21938).
namespace VibeMenuCardManager.Core.Art;

public static class PilResample
{
    const int PrecisionBits = 32 - 8 - 2;

    static double Sinc(double x)
    {
        if (x == 0.0) return 1.0;
        x *= Math.PI;
        return Math.Sin(x) / x;
    }

    static double Lanczos(double x) => -3.0 <= x && x < 3.0 ? Sinc(x) * Sinc(x / 3) : 0.0;

    /// <summary>precompute_coeffs + normalize_coeffs_8bpc: per output pixel, its first input pixel, count and the
    /// fixed-point weights.</summary>
    static (int[] Bounds, int[] Kk, int KSize) Coeffs(int inSize, float in0, float in1, int outSize)
    {
        double scale = (double)(in1 - in0) / outSize, filterscale = scale < 1.0 ? 1.0 : scale;
        double support = 3.0 * filterscale;
        int ksize = (int)Math.Ceiling(support) * 2 + 1;
        var kk = new double[outSize * ksize];
        var bounds = new int[outSize * 2];
        for (int xx = 0; xx < outSize; xx++)
        {
            double center = in0 + (xx + 0.5) * scale, ww = 0.0, ss = 1.0 / filterscale;
            int xmin = (int)(center - support + 0.5);
            if (xmin < 0) xmin = 0;
            int xmax = (int)(center + support + 0.5);
            if (xmax > inSize) xmax = inSize;
            xmax -= xmin;
            for (int x = 0; x < xmax; x++)
            {
                double w = Lanczos((x + xmin - center + 0.5) * ss);
                kk[xx * ksize + x] = w;
                ww += w;
            }
            for (int x = 0; x < xmax; x++) if (ww != 0.0) kk[xx * ksize + x] /= ww;
            bounds[xx * 2] = xmin;
            bounds[xx * 2 + 1] = xmax;
        }
        var ik = new int[kk.Length];
        for (int i = 0; i < kk.Length; i++)
            ik[i] = kk[i] < 0 ? (int)(-0.5 + kk[i] * (1 << PrecisionBits)) : (int)(0.5 + kk[i] * (1 << PrecisionBits));
        return (bounds, ik, ksize);
    }

    /// <summary>clip8: the 22-bit fixed-point sum to a byte (Pillow's table covers -640..639; anything outside a
    /// byte saturates).</summary>
    static byte Clip8(int v)
    {
        int i = v >> PrecisionBits;
        return i < 0 ? (byte)0 : i > 255 ? (byte)255 : (byte)i;
    }

    /// <summary>ImagingResampleInner with LANCZOS on an 8-bit image of `bands` interleaved channels (1 = L, 4 = RGBA/RGBa),
    /// box = the whole image. The horizontal pass runs first, over the rows the vertical pass uses.</summary>
    public static byte[] Resize(byte[] src, int w, int h, int bands, int ow, int oh)
    {
        bool needH = ow != w, needV = oh != h;
        var (bh, kh, ksh) = Coeffs(w, 0f, w, ow);
        var (bv, kv, ksv) = Coeffs(h, 0f, h, oh);
        int yFirst = bv[0], yLast = bv[oh * 2 - 2] + bv[oh * 2 - 1];
        byte[] img = src;
        int iw = w, ih = h;
        if (needH)
        {
            for (int i = 0; i < oh; i++) bv[i * 2] -= yFirst;
            int th = yLast - yFirst;
            var t = new byte[ow * th * bands];
            for (int yy = 0; yy < th; yy++)
                for (int xx = 0; xx < ow; xx++)
                {
                    int xmin = bh[xx * 2], xmax = bh[xx * 2 + 1];
                    for (int c = 0; c < bands; c++)
                    {
                        int ss = 1 << (PrecisionBits - 1);
                        for (int x = 0; x < xmax; x++) ss += src[((yy + yFirst) * w + x + xmin) * bands + c] * kh[xx * ksh + x];
                        t[(yy * ow + xx) * bands + c] = Clip8(ss);
                    }
                }
            img = t; iw = ow; ih = th;
        }
        if (needV)
        {
            var o = new byte[iw * oh * bands];
            for (int yy = 0; yy < oh; yy++)
            {
                int ymin = bv[yy * 2], ymax = bv[yy * 2 + 1];
                for (int xx = 0; xx < iw; xx++)
                    for (int c = 0; c < bands; c++)
                    {
                        int ss = 1 << (PrecisionBits - 1);
                        for (int y = 0; y < ymax; y++) ss += img[((y + ymin) * iw + xx) * bands + c] * kv[yy * ksv + y];
                        o[(yy * iw + xx) * bands + c] = Clip8(ss);
                    }
            }
            img = o;
        }
        return ReferenceEquals(img, src) ? (byte[])src.Clone() : img;
    }

    /// <summary>Image.resize on an RGBA image: premultiply (rgbA2rgba), resample, unpremultiply (rgba2rgbA); the same
    /// size is a plain copy.</summary>
    public static byte[] ResizeRgba(byte[] rgba, int w, int h, int ow, int oh)
    {
        if (ow == w && oh == h) return (byte[])rgba.Clone();
        var pm = new byte[rgba.Length];
        for (int i = 0; i < rgba.Length; i += 4)
        {
            uint a = rgba[i + 3];
            for (int c = 0; c < 3; c++) { uint t = rgba[i + c] * a + 128; pm[i + c] = (byte)(((t >> 8) + t) >> 8); }
            pm[i + 3] = rgba[i + 3];
        }
        var r = Resize(pm, w, h, 4, ow, oh);
        for (int i = 0; i < r.Length; i += 4)
        {
            int a = r[i + 3];
            if (a == 255 || a == 0) continue;
            for (int c = 0; c < 3; c++) r[i + c] = (byte)Math.Clamp(255 * r[i + c] / a, 0, 255);
        }
        return r;
    }
}
