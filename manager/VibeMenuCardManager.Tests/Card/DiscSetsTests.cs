// VibeMenu Card Manager, GPL-3.0-or-later.
// Multi-disc sets (SPEC §5.4 flags bits 2, 4-15) against contract/fixtures/discsets.json, the fixture
// tools/tests/test_discsets.py holds tools/omsat/discsets.py to.
using System.Text.Json;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class DiscSetsTests
{
    static List<JsonElement> Cases() =>
        JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/discsets.json"))).RootElement.GetProperty("cases").EnumerateArray().ToList();

    [Fact]
    public void Every_fixture_case_gets_the_set_and_alternate_bits_omsat_gives_it()
    {
        var cases = Cases();
        var got = DiscSets.Assign(cases.Select(c => (c.GetProperty("path").GetString()!, c.GetProperty("product").GetString()!,
            c.GetProperty("disc").GetInt32(), c.GetProperty("total").GetInt32())).ToList());
        for (int i = 0; i < cases.Count; i++)
        {
            Assert.True(cases[i].GetProperty("set").GetInt32() == got[i] >> 4, cases[i].GetProperty("path").GetString());
            Assert.True(cases[i].GetProperty("alt").GetBoolean() == ((got[i] & DiscSets.FlagAlt) != 0), cases[i].GetProperty("path").GetString());
        }
        Assert.Equal(41, got.Max() >> 4);   // incl. a Japanese and an emoji folder; a full-width digit is no token
    }

    [Theory]
    [InlineData("X (Disc 1)/X (Disc 1).cue", "X (Disc #)/X (Disc #).cue")]
    [InlineData("PhantasM (JP) (Disc 2)/PHANTASM_2.cue", "PhantasM (JP) (Disc #)/PHANTASM_#.cue")]
    [InlineData("S (Disk 3)/S_DISK3.CUE", "S (Disk #)/S_DISK#.CUE")]
    [InlineData("D (Disc II)/D (Disc II).cue", "D (Disc #)/D (Disc #).cue")]
    [InlineData("Mag Vol. 2/m.iso", "Mag Vol. #/m.iso")]
    [InlineData("Discworld/Disco.cue", "Discworld/Disco.cue")]
    [InlineData("ABCD1.cue", "ABCD1.cue")]
    public void Mask_matches_omsat(string path, string want) => Assert.Equal(want, DiscSets.Mask(path));

    [Fact]
    public void Stem_and_product_match_omsat()
    {
        Assert.Equal("C&C (USA) (Disc #", DiscSets.Stem("C&C (USA) (Disc 1) (GDI Disc)/x.cue"));
        Assert.Equal("Root/Plain.cue", DiscSets.Stem("Root/Plain.cue"));
        Assert.Equal("T-1234G", DiscSets.NormProduct("t-1234g v1.0"));
        Assert.Equal("MK_81", DiscSets.NormProduct("MK.81"));
    }

    [Fact]
    public void Library_index_keeps_the_bits_through_a_write_and_read()
    {
        var ents = new List<LibEntry> { new("A (Disc 1)/a.cue", Disc: 1, DiscTotal: 2, HasBox: true, Sets: 0x10),
                                         new("A (Disc 2)/a.cue", Disc: 2, DiscTotal: 2, Sets: 0x14) };
        var data = LibraryIndex.Write(ents, [new LibFolder("", 0xFFFF)], 0, 20261124);
        var back = LibraryIndex.Read(data).Entries;
        Assert.Equal([0x10, 0x14], back.Select(e => e.Sets));
        Assert.True(back[0].HasBox);
        Assert.Equal(new byte[] { 0x00, 0x11 }, data[(64 + 0x1A)..(64 + 0x1C)]);
    }
}
