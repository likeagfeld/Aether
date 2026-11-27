// VibeMenu Card Manager, GPL-3.0-or-later.
// The TGA decoder's Pillow behaviours, on hand-made files (the fixture card's seven BOX.TGA variants are checked byte
// for byte through the card build, CardBuilderTests). Each vector states what Pillow 12.2.0 gives for it.
using VibeMenuCardManager.Core.Art;

namespace VibeMenuCardManager.Tests.Art;

public class TgaDecoderTests
{
    static byte[] Header(int type, int w, int h, int depth, int flags, int idLen = 0, int cmLen = 0, int cmDepth = 0) =>
        [(byte)idLen, (byte)(cmLen > 0 ? 1 : 0), (byte)type, 0, 0, (byte)cmLen, (byte)(cmLen >> 8), (byte)cmDepth, 0, 0, 0, 0,
         (byte)w, (byte)(w >> 8), (byte)h, (byte)(h >> 8), (byte)depth, (byte)flags];

    [Fact]
    public void Sixteen_bpp_bit_15_set_is_transparent_and_channels_scale_by_255_over_31()
    {
        // BGRA;15Z: bit 15 set -> alpha 0, clear -> 255; 5-bit 31 -> 255, 16 -> 131 (16 * 255 / 31)
        byte[] f = [.. Header(2, 2, 1, 16, 0x20), 0x1F, 0x80, 0x00, 0x40];   // pixel 0: blue 31, bit15; pixel 1: red 16
        var img = TgaDecoder.Decode(f);
        Assert.Equal(new byte[] { 0, 0, 255, 0, 131, 0, 0, 255 }, img.Rgba);
    }

    [Fact]
    public void Bottom_left_origin_is_flipped_and_right_to_left_is_mirrored()
    {
        byte[] rows = [1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4];                  // 2x2, 24 bpp, stored rows bottom first
        var bl = TgaDecoder.Decode([.. Header(2, 2, 2, 24, 0x00), .. rows]);
        Assert.Equal(3, bl.Rgba[0]);                                         // the first stored row is the bottom one
        var tlr = TgaDecoder.Decode([.. Header(2, 2, 2, 24, 0x30), .. rows]);
        Assert.Equal(2, tlr.Rgba[0]);                                        // top-left origin, mirrored left-right
    }

    [Fact]
    public void An_image_id_field_is_skipped_and_the_palette_maps_type_1()
    {
        byte[] pal = [10, 20, 30, 40, 50, 60];                               // BGR entries 0 and 1
        var img = TgaDecoder.Decode([.. Header(1, 2, 1, 8, 0x20, idLen: 3, cmLen: 2, cmDepth: 24), 9, 9, 9, .. pal, 1, 0]);
        Assert.Equal(new byte[] { 60, 50, 40, 255, 30, 20, 10, 255 }, img.Rgba);
    }

    [Fact]
    public void RLE_runs_and_raw_packets_decode_and_an_overrun_is_refused()
    {
        byte[] body = [0x81, 5, 6, 7, 0x00, 8, 9, 10];                       // a run of 2, then 1 raw pixel (24 bpp)
        var img = TgaDecoder.Decode([.. Header(10, 3, 1, 24, 0x20), .. body]);
        Assert.Equal(new byte[] { 7, 6, 5, 255, 7, 6, 5, 255, 10, 9, 8, 255 }, img.Rgba);
        Assert.Throws<FormatException>(() => TgaDecoder.Decode([.. Header(10, 1, 1, 24, 0x20), 0x81, 5, 6, 7]));
    }

    [Fact]
    public void Thirty_two_bpp_keeps_alpha_unless_the_TGA_2_footer_says_there_is_none()
    {
        byte[] px = [1, 2, 3, 77];
        Assert.Equal(77, TgaDecoder.Decode([.. Header(2, 1, 1, 32, 0x28), .. px]).Rgba[3]);
        // a TGA 2.0 extension area at offset 22 whose attributes type (byte 494) is 0 = no alpha
        var ext = new byte[495];
        byte[] f = [.. Header(2, 1, 1, 32, 0x28), .. px, .. ext, .. BitConverter.GetBytes(22u), 0, 0, 0, 0, .. "TRUEVISION-XFILE.\0"u8.ToArray()];
        Assert.Equal(255, TgaDecoder.Decode(f).Rgba[3]);
    }

    [Fact]
    public void Truncated_or_unknown_files_are_format_errors()
    {
        Assert.Throws<FormatException>(() => TgaDecoder.Decode([.. Header(2, 4, 4, 24, 0x20), 1, 2, 3]));
        Assert.Throws<FormatException>(() => TgaDecoder.Decode([.. Header(5, 1, 1, 24, 0x20), 1, 2, 3]));
        Assert.Throws<FormatException>(() => TgaDecoder.Decode([1, 2, 3]));
        Assert.True(TgaDecoder.LooksLikeTga(Header(2, 1, 1, 24, 0)));
        Assert.False(TgaDecoder.LooksLikeTga(Header(2, 0, 1, 24, 0)));
    }
}
