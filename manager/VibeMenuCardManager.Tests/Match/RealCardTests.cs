// VibeMenu Card Manager, GPL-3.0-or-later.
// The user's real card (2026-11-24, "so many retail games listed as not in database", "entries not having a cover but
// they do"): its product codes, areas and paths only (Match/real_card.json), measured read-only. Over the DAT-built
// GAMES.DB every one of its non-exact codes finds the key omsat.cardscan finds, only the six known retail discs stay
// unmatched, and the bundled cover pack covers every matched game without a BOX.TGA except the three it has nothing for.
using System.Text.Json;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Tests.Match;

public class RealCardTests
{
    static readonly JsonElement Card = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/VibeMenuCardManager.Tests/Match/real_card.json"))).RootElement;

    static Matcher FullDb()
    {
        string p = Repo.P("build/db/GAMES.DB");
        Skip.IfNot(File.Exists(p), "build/db/GAMES.DB is not built");
        var db = GamesDb.Read(File.ReadAllBytes(p));
        Skip.IfNot(db.Records.Count >= 2112, "build/db/GAMES.DB was built without the libretro DAT");
        return new Matcher(db.Records.Select(r => r.Key), db.Aliases);
    }

    [SkippableFact]
    public void Every_real_retail_code_finds_its_key_and_only_the_six_known_discs_stay_unmatched()
    {
        var m = FullDb();
        var bad = new List<string>();
        foreach (var c in Card.GetProperty("non_exact_matches").EnumerateArray())
        {
            string product = c.GetProperty("product").GetString()!, area = c.GetProperty("area").GetString()!, want = c.GetProperty("key").GetString()!;
            if (m.Match(product, area).Key != want) bad.Add($"{product}/{area}: {m.Match(product, area).Key ?? "unmatched"}, expect {want}");
        }
        Assert.True(bad.Count == 0, string.Join("\n", bad));
        Assert.Equal(145, Card.GetProperty("non_exact_matches").GetArrayLength());
        var un = Card.GetProperty("retail_unmatched").EnumerateArray().ToList();
        Assert.Equal(6, un.Count);                    // two Playboy Karaoke (T-2305G, J), four SF3 translations (SF3TRANS)
        Assert.All(un, u => Assert.Null(m.Match(u.GetProperty("product").GetString()!, u.GetProperty("area").GetString()!).Key));
    }

    [SkippableFact]
    public void The_bundled_pack_covers_every_matched_game_without_a_box_tga_but_three()
    {
        string p = Repo.P("build/coverpack/COVERS.PAK");
        Skip.IfNot(File.Exists(p), "build/coverpack/COVERS.PAK is not built");
        using var pack = CoverPack.Open(p);
        var none = Card.GetProperty("no_cover_even_with_the_pack").EnumerateArray().Select(e => e.GetProperty("key").GetString()!).ToHashSet();
        Assert.Equal(["610-6732", "GS-9003", "HSS-0128"], none.Order(StringComparer.Ordinal));
        foreach (var k in Card.GetProperty("keys_without_box_tga").EnumerateArray().Select(e => e.GetString()!))
            Assert.True(none.Contains(k) ? pack.For(k).Count == 0 : pack.For(k).Count > 0, k);
    }
}
