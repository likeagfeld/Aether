// Aether Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Tests.Formats;

/// <summary>2026-11-25 hardware: the user's Mortal Kombat II (USA), region-patched to area J, black-screened under Auto
/// (CWX) -- Kai's T-8103H row is area U. contract/fixtures/compat_cases.json, the same fixture tools/tests/test_compat.py
/// runs: Kai's IP CRC first, then product + area, then the product alone; our override rows ahead of all.</summary>
public class CompatTests
{
    [Fact]
    public void Per_disc_rules_match_the_shared_fixture()
    {
        var spec = JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/compat_cases.json"))).RootElement;
        var keys = spec.GetProperty("keys").EnumerateArray().Select(k => k.GetString()!).ToList();
        string loader = Path.Combine(Path.GetTempPath(), $"compat_cases_{Guid.NewGuid():N}.json");
        File.WriteAllText(loader, "{\"games\": " + spec.GetProperty("games").GetRawText() + "}");
        try
        {
            var db = GamesDb.Write(keys.Select(k => new GameRecord { Key = k, TitleEn = k }).ToList(), [],
                                   new Dictionary<string, string>(), 20261125);
            var m = new Matcher(keys, new Dictionary<string, string>());
            var ents = new List<LibEntry>();
            var sids = new Dictionary<string, SystemId?>();
            var cases = spec.GetProperty("cases").EnumerateArray().ToList();
            foreach (var c in cases)
            {
                var s = SystemIdReader.Parse(Convert.FromHexString(c.GetProperty("raw_hex").GetString()!))!;
                string path = c.GetProperty("path").GetString()!;
                ents.Add(new LibEntry(path, m.Match(s.Product, s.Area).Key ?? "", AreaFlags: GamesDb.RegionFlagsFrom(s.Area),
                                      Sets: c.TryGetProperty("sets", out var sb) ? sb.GetInt32() : 0));
                sids[path] = s;
            }
            var report = new List<string>();
            var (outDb, _) = Compat.Fill(db, Compat.Load(loader, null), Compat.DiscsOf(ents, sids), report);
            var recs = GamesDb.Read(outDb).Records.ToDictionary(r => r.Key);
            for (int i = 0; i < cases.Count; i++)
            {
                var (c, e) = (cases[i], ents[i]);
                var r = recs[e.Key];
                int hint = r.LoaderHint != 0 && (r.CompatRegions == 0 || (r.CompatRegions & e.AreaFlags) != 0) ? r.LoaderHint : 0;
                Assert.Equal((c.GetProperty("expect_key").GetString(), c.GetProperty("expect_crc").GetString(), c.GetProperty("expect_hint").GetInt32()),
                             (e.Key, SystemIdReader.KaiIpCrc(sids[e.Path]!.Raw256).ToString("X8"), hint));
            }
            Assert.Equal(spec.GetProperty("expect_report").EnumerateArray().Select(x => x.GetString()!).ToList(), report);
        }
        finally { File.Delete(loader); }
    }

    [Fact]
    public void Same_product_keeps_pal_variants_apart()
    {
        Assert.True(Compat.SameProduct("T-8103H", "T-8103H  "));
        Assert.True(Compat.SameProduct("MK-81307", "81307"));
        Assert.False(Compat.SameProduct("T-8103H", "T-8103H-50"));
        Assert.False(Compat.SameProduct("T-8103H", "T-8107G"));
    }
}
