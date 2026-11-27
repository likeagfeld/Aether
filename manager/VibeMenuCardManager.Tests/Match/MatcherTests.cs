// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Tests.Match;

/// <summary>contract/SPEC.md §6 against its executable form, contract/fixtures/match_cases.json, and against
/// omsat.match on 51 real discs over a full GAMES.DB key set (manager/golden/expect/match_real.json).</summary>
public static class MatchCheck
{
    public sealed record Case(string Product, string Area, string? Key, string? How, List<string>? Candidates, string Label);

    public static (Matcher M, List<Case> Cases) Contract()
    {
        var d = JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/match_cases.json"))).RootElement;
        var m = new Matcher(d.GetProperty("keys").EnumerateArray().Select(k => k.GetString()!),
            d.GetProperty("aliases").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!));
        var cases = d.GetProperty("cases").EnumerateArray().Select(c => new Case(c.GetProperty("product").GetString()!,
            c.GetProperty("area").GetString()!, Str(c.GetProperty("expect_key")), Str(c.GetProperty("expect_how")), null,
            c.GetProperty("product").GetString()!.Trim() + "/" + c.GetProperty("area").GetString())).ToList();
        return (m, cases);
    }

    public static (Matcher M, List<Case> Cases) Real()
    {
        var d = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/match_real.json"))).RootElement;
        var m = new Matcher(d.GetProperty("keys").EnumerateArray().Select(k => k.GetString()!),
            d.GetProperty("aliases").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!));
        var cases = d.GetProperty("rows").EnumerateArray().Select(c => new Case(c.GetProperty("product").GetString()!,
            c.GetProperty("area").GetString()!, Str(c.GetProperty("key")), Str(c.GetProperty("how")),
            c.GetProperty("candidates").EnumerateArray().Select(x => x.GetString()!).ToList(),
            c.GetProperty("file").GetString()!)).ToList();
        return (m, cases);
    }

    static string? Str(JsonElement e) => e.ValueKind == JsonValueKind.Null ? null : e.GetString();

    public static List<string> Compare(Matcher m, IEnumerable<Case> cases)
    {
        var bad = new List<string>();
        foreach (var c in cases)
        {
            var r = m.Match(c.Product, c.Area);
            if (r.Key != c.Key || r.How != c.How || (c.Candidates != null && !r.Candidates.SequenceEqual(c.Candidates)))
                bad.Add($"{c.Label}: got {r.Key ?? "-"}/{r.How ?? "-"} [{string.Join(",", r.Candidates)}] expect {c.Key ?? "-"}/{c.How ?? "-"}" +
                        (c.Candidates != null ? $" [{string.Join(",", c.Candidates)}]" : ""));
        }
        return bad;
    }
}

public class MatcherTests
{
    [Fact]
    public void Every_contract_case_reproduces()
    {
        var (m, cases) = MatchCheck.Contract();
        Assert.Equal(29, cases.Count);   // 16 v2 cases + 13 for the v3 rules (448a49e)
        var bad = MatchCheck.Compare(m, cases);
        Assert.True(bad.Count == 0, $"{bad.Count} of {cases.Count} cases differ:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void Real_discs_match_as_omsat_does_over_the_full_key_set()
    {
        var (m, cases) = MatchCheck.Real();
        Assert.Equal(51, cases.Count);                 // 36 retail discs + 15 XBAND discs
        var bad = MatchCheck.Compare(m, cases);
        Assert.True(bad.Count == 0, $"{bad.Count} of {cases.Count} discs differ:\n" + string.Join("\n", bad));
    }

    [Theory]
    [InlineData("GS-7103\n (610-6342)", "GS-7103")]
    [InlineData("t-123 (EAE) x", "T-123X")]
    [InlineData("ABCDEFGHIJKLMNOPQRST", "ABCDEFGHIJKLMNOP")]
    public void Normalise(string s, string want) => Assert.Equal(want, Keys.Normalise(s));

    [Theory]
    [InlineData("T-07004H", "T7004H")]
    [InlineData("T-7004H", "T7004H")]
    [InlineData("MK-81045", "MK81045")]
    [InlineData("GS-9001", "GS9001")]
    public void Skeleton(string s, string want) => Assert.Equal(want, Matcher.Skeleton(s));
}
