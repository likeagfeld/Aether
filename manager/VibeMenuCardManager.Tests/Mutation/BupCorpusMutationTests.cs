// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos (design spec §9): corrupt the corpus and the real comparison must go RED. A demo that passes on a
// corrupted input would mean the check cannot fail.
using VibeMenuCardManager.Tests.Saves;

namespace VibeMenuCardManager.Tests.Mutation;

public class BupCorpusMutationTests
{
    [Fact]
    public void Lowering_DAYTONA__00s_date_flips_the_prefer_check_red()
    {
        var rows = CorpusCheck.LoadRows();
        var (_, pairs) = CorpusCheck.LoadExpect();
        var i = rows.FindIndex(r => r.Path == "vbt/20200620/DAYTONA__00.BUP");
        var h = (byte[])rows[i].Hdr.Clone();
        h[0x29] = 0x00;                                  // date 0x00D0D84C -> 0x0000D84C, below Daytona__01's 0x00CDE683
        rows[i] = rows[i] with { Hdr = h };
        var bad = CorpusCheck.ComparePrefer(rows, pairs);
        Assert.Equal(2, bad.Count);                      // the pair, in both orders
        Assert.All(bad, m => Assert.Contains("DAYTONA__00.BUP", m));
    }

    [Fact]
    public void A_truncated_length_flips_the_decode_check_red()
    {
        var rows = CorpusCheck.LoadRows();
        var (expect, _) = CorpusCheck.LoadExpect();
        rows[0] = rows[0] with { Len = 63 };
        var bad = CorpusCheck.CompareDecode(rows, expect);
        Assert.Single(bad);
        Assert.Contains(rows[0].Path, bad[0]);
    }

    [Fact]
    public void A_flipped_name_byte_flips_the_decode_check_red()
    {
        var rows = CorpusCheck.LoadRows();
        var (expect, _) = CorpusCheck.LoadExpect();
        var h = (byte[])rows[5].Hdr.Clone();
        h[0x10] ^= 0x01;
        rows[5] = rows[5] with { Hdr = h };
        Assert.Single(CorpusCheck.CompareDecode(rows, expect));
    }
}
