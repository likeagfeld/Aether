// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Match;
using VibeMenuCardManager.Tests.Disc;
using VibeMenuCardManager.Tests.Match;

namespace VibeMenuCardManager.Tests.Mutation;

public class MatchMutationTests
{
    [Fact]
    public void A_wrong_expected_key_is_caught()
    {
        var (m, cases) = MatchCheck.Contract();
        cases[3] = cases[3] with { Key = "MK-81009" };          // MK-81009 on an E disc must be -50
        Assert.Single(MatchCheck.Compare(m, cases));
    }

    [Fact]
    public void A_matcher_without_the_area_filter_fails_the_contract()
    {
        var (_, cases) = MatchCheck.Contract();
        var d = System.Text.Json.JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/match_cases.json"))).RootElement;
        var broken = new Matcher(d.GetProperty("keys").EnumerateArray().Select(k => k.GetString()!),
            d.GetProperty("aliases").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!), noAreaFilter: true);
        var bad = MatchCheck.Compare(broken, cases);
        Assert.NotEmpty(bad);
        Assert.Contains(bad, b => b.StartsWith("MK-81009/E"));
    }

    [Fact]
    public void A_flipped_header_byte_is_caught()
    {
        var rows = HeaderCheck.Load();
        rows[7].Raw[0x22] ^= 0x01;                              // one product byte
        var bad = HeaderCheck.Compare(rows);
        Assert.Single(bad);
        Assert.Contains("product", bad[0]);
    }
}
