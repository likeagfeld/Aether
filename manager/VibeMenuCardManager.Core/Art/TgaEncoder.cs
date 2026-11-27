// Aether Card Manager, GPL-3.0-or-later.
// Writing a game folder's BOX.TGA (contract/SPEC.md §5.6): uncompressed true-color (image type 2), top-left origin,
// 32 bpp with alpha. The menu accepts this exact variant ("32 bpp, alpha dropped"), so a cover the Card Manager writes
// is read back by the menu the same way Satiator Rings' own BOX.TGA files are.
namespace VibeMenuCardManager.Core.Art;

public static class TgaEncoder
{
    /// <summary>An 18-byte header (no image ID, no color map) plus BGRA8888 rows, top-left origin (flag bit 0x20 set).</summary>
    public static byte[] Encode(RgbaImage img)
    {
        var o = new byte[18 + img.W * img.H * 4];
        o[2] = 2;                                   // image type 2: uncompressed true-color
        o[12] = (byte)(img.W & 0xFF); o[13] = (byte)(img.W >> 8);
        o[14] = (byte)(img.H & 0xFF); o[15] = (byte)(img.H >> 8);
        o[16] = 32;                                  // bits per pixel
        o[17] = 0x28;                                // 8-bit alpha (0x08) + top-left origin (0x20)
        int p = 18;
        for (int i = 0; i < img.W * img.H; i++)
        {
            int s = i * 4;
            o[p++] = img.Rgba[s + 2]; o[p++] = img.Rgba[s + 1]; o[p++] = img.Rgba[s + 0]; o[p++] = img.Rgba[s + 3];
        }
        return o;
    }
}
