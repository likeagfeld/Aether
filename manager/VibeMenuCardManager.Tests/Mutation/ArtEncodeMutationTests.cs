// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos for the cover encoder and the title renderer: each broken variant must fail the
// same comparison the real tests use. (A wrong font weight is no longer a reachable mutation: the font is the static
// Regular instance, pinned by SHA-256 in Directory.Build.targets.)
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Tests.Art;

namespace VibeMenuCardManager.Tests.Mutation;

public class ArtEncodeMutationTests
{
    [Fact]
    public void Dropping_bit_15_fails_the_cover_golden()
    {
        var cases = CoverCheck.Load();
        var bad = CoverCheck.Compare(cases, (img, size) =>
        {
            var b = CoverEncoder.Encode(img, size);
            for (int i = 0; i < b.Length; i += 2) b[i] &= 0x7F;
            return b;
        });
        Assert.Equal(cases.Count, bad.Count);                     // every case has opaque pixels
    }

    [Fact]
    public void An_alpha_threshold_of_127_fails_the_rgba_case()
    {
        var bad = CoverCheck.Compare(CoverCheck.Load(), (img, size) => CoverEncoder.Encode(img, size, alphaThreshold: 127));
        Assert.NotEmpty(bad);
        Assert.All(bad, b => Assert.StartsWith("disc_rgba", b));  // only the case with partly transparent edges
    }

    [Fact]
    public void One_channel_off_by_one_level_is_caught()
    {
        var cases = CoverCheck.Load();
        var bad = CoverCheck.Compare(cases, (img, size) =>
        {
            var b = CoverEncoder.Encode(img, size);
            for (int i = 0; i < b.Length; i += 2)
                if ((b[i] & 0x80) != 0) { b[i + 1] ^= 0x01; break; }      // the lowest red bit of one pixel
            return b;
        });
        Assert.Equal(cases.Count, bad.Count);
    }

    /// <summary>The nibble order is gated exactly, by Pack4's vector: measured this session, the title similarity bound
    /// is blind to a swap (adjacent pixels are alike, so a swapped render stayed within it for some of the 14 titles), so it is not
    /// what catches this mutation.</summary>
    [Fact]
    public void Swapped_nibbles_fail_the_pack4_vector()
    {
        byte[] g = [0xFF, 0x1F, 0x80, 0x0F];
        Assert.Equal(new byte[] { 0xF1, 0x80 }, TitleRenderer.Pack4(g));
        Assert.NotEqual(new byte[] { 0xF1, 0x80 }, TitleRenderer.Pack4(g, nibbleSwap: true));
    }
}
