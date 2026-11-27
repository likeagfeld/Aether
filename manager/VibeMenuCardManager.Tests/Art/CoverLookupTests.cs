// VibeMenu Card Manager, GPL-3.0-or-later.
// The cover read order fix (coordinator fix round 2, diagnosed on a real card: ART.PAK held 43 of 44 games' covers,
// but the window only looked at BOX.TGA): CoverLookup reads ART.PAK's hero, else thumb, else nothing.
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Art;

public class CoverLookupTests
{
    static RgbaImage Solid(int w, int h, byte r, byte g, byte b)
    {
        var px = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
        return new RgbaImage(w, h, px);
    }

    static ArtPakData Pak(params ArtEntry[] entries) => ArtPak.Read(ArtPak.Write(entries, 20261123));

    [Fact]
    public void Finds_the_hero_for_the_games_own_disc()
    {
        var (sq, hero, _) = CoverEncoder.Cover(Solid(100, 100, 255, 0, 0));
        var data = Pak(new ArtEntry("T-00001", 1, sq, null, hero, null, 1));
        Assert.NotNull(CoverLookup.FromArtPak(data, "T-00001", 1));
    }

    [Fact]
    public void Falls_back_to_the_disc_0_applies_to_all_discs_entry()
    {
        var (sq, hero, _) = CoverEncoder.Cover(Solid(100, 100, 0, 255, 0));
        var data = Pak(new ArtEntry("T-00002", 0, sq, null, hero, null, 1));
        Assert.NotNull(CoverLookup.FromArtPak(data, "T-00002", 2));
    }

    [Fact]
    public void A_different_discs_entry_is_not_used_when_a_disc_0_entry_also_exists()
    {
        var (sq1, hero1, _) = CoverEncoder.Cover(Solid(100, 100, 255, 0, 0));   // disc 0: red
        var (sq2, hero2, _) = CoverEncoder.Cover(Solid(100, 100, 0, 255, 0));   // disc 1: green
        var data = Pak(new ArtEntry("T-00005", 0, sq1, null, hero1, null, 1), new ArtEntry("T-00005", 1, sq2, null, hero2, null, 2));
        var found = CoverLookup.FromArtPak(data, "T-00005", 1);
        Assert.NotNull(found);
        Assert.True(found!.Rgba[1] > 200 && found.Rgba[0] < 50, "disc 1's own green cover, not disc 0's red");
    }

    [Fact]
    public void HasCover_is_a_cheap_flags_only_check()
    {
        var (sq, hero, _) = CoverEncoder.Cover(Solid(100, 100, 0, 0, 255));
        var data = Pak(new ArtEntry("T-00003", 1, sq, null, hero, null, 1));
        Assert.True(CoverLookup.HasCover(data, "T-00003", 1));
        Assert.False(CoverLookup.HasCover(data, "T-99999", 1));
    }

    [Fact]
    public void Falls_back_to_thumb_when_there_is_no_hero()
    {
        var (sq, _, thumb) = CoverEncoder.Cover(Solid(60, 60, 1, 2, 3), thumb: true);
        var data = Pak(new ArtEntry("T-00004", 1, sq, thumb, null, null, 1));
        Assert.NotNull(CoverLookup.FromArtPak(data, "T-00004", 1));
        Assert.True(CoverLookup.HasCover(data, "T-00004", 1));
    }

    [Fact]
    public void Missing_pak_or_key_gives_null_and_false()
    {
        Assert.Null(CoverLookup.FromArtPak(null, "X", 1));
        Assert.False(CoverLookup.HasCover(null, "X", 1));
        Assert.Null(CoverLookup.FromArtPak(Pak(), "", 1));
    }
}
