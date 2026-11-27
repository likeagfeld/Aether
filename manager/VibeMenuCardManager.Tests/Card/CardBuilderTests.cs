// VibeMenu Card Manager, GPL-3.0-or-later.
// The card build against tools/omsat/cardbuild.py on the fixture card (manager/golden/build_case, mkbuild.py): offline
// (RemoteArt over the committed cache, download off), no game data. LIBRARY.IDX, GAMES.DB and manifest.csv are
// byte-identical; ART.PAK is entry-for-entry and blob-for-blob identical except the JP-title bitmaps, which are held to
// TitleRenderer's measured bounds; the report's counts and per-title sources are equal.
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Art;

namespace VibeMenuCardManager.Tests.Card;

public static class BuildCheck
{
    public static string Case(string f) => Repo.P("manager/golden/build_case/" + f);
    public static JsonElement Expect() => JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/build.json"))).RootElement;

    public static BuildOptions Options(JsonElement x) => new(
        File.ReadAllBytes(Case("GAMES.DB")),
        JsonDocument.Parse(File.ReadAllText(Case("jp_titles.json"))).RootElement.EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!),
        CoversZip: Case("covers.zip"), Remote: new RemoteArt(Case("art_cache"), download: false),
        FolderArt: new Dictionary<string, string> { ["Misc"] = "folder_misc.png" }, FolderArtBase: Case(""),
        ArtPrefer: x.GetProperty("art_prefer").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!),
        LoaderCompatJson: Repo.P("tools/data/compat/loader_compat.json"),
        ControllerCompatJson: Repo.P("tools/data/compat/controller_compat.json"),
        SaveToolsJson: Repo.P("tools/data/compat/save_tools.json"));

    public static (TempCard Card, BuildResult R) Build(Func<BuildOptions, BuildOptions>? doctor = null)
    {
        var x = Expect();
        var t = new TempCard();
        t.Realize(JsonDocument.Parse(File.ReadAllText(Case("card.json"))).RootElement);
        var o = Options(x);
        return (t, CardBuilder.Build(Path.Combine(t.Dir, "card"), x.GetProperty("date").GetUInt32(), doctor?.Invoke(o) ?? o));
    }

    static string Sha(byte[] b) => Convert.ToHexStringLower(SHA256.HashData(b));

    public static List<string> Compare(BuildResult r, JsonElement x, TitleRenderer? tr)
    {
        var bad = new List<string>();
        var files = x.GetProperty("files");
        if (Sha(r.LibraryIdx) != files.GetProperty("LIBRARY.IDX").GetString()) bad.Add("LIBRARY.IDX differs");
        if (Sha(r.GamesDb) != files.GetProperty("GAMES.DB").GetString()) bad.Add("GAMES.DB differs");
        if (r.ManifestCsv != x.GetProperty("manifest_csv").GetString()) bad.Add("manifest.csv differs:\n" + r.ManifestCsv);
        var pak = ArtPak.Read(r.ArtPak);
        var want = x.GetProperty("art_entries").EnumerateArray().ToList();
        if (pak.Entries.Count != want.Count) bad.Add($"ART.PAK: {pak.Entries.Count} entries, expect {want.Count}");
        for (int i = 0; i < Math.Min(pak.Entries.Count, want.Count); i++)
        {
            var g = pak.Entries[i];
            var w = want[i];
            bool hasJp = (g.Flags & ArtPak.FJp) != 0;
            var f = new (string N, long V)[] { ("disc", g.Disc), ("flags", g.Flags), ("hero_w", g.HeroW), ("hero_h", g.HeroH),
                ("thumb_w", g.ThumbW), ("thumb_h", g.ThumbH), ("hero_sec", g.HeroSec), ("thumb_sec", g.ThumbSec), ("jp_sec", g.JpSec),
                ("jp_h", g.JpH), ("manifest_row", g.ManifestRow) };
            if (g.Key != w.GetProperty("key").GetString() || g.Disc != w.GetProperty("disc").GetInt32())
            {
                bad.Add($"entry {i}: {g.Key} disc {g.Disc}, expect {w.GetProperty("key").GetString()} disc {w.GetProperty("disc").GetInt32()}");
                continue;
            }
            foreach (var (n, v) in f) if (v != w.GetProperty(n).GetInt64()) bad.Add($"entry {i} {g.Key}: {n} {v} expect {w.GetProperty(n).GetInt64()}");
            foreach (var kind in new[] { "thumb", "hero" })
                if ((pak.Blob(g, kind) is byte[] b ? Sha(b) : "") != w.GetProperty(kind).GetString()) bad.Add($"entry {i} {g.Key}: {kind} differs");
            if (!hasJp && g.BlobCrc != w.GetProperty("blob_crc").GetUInt32()) bad.Add($"entry {i} {g.Key}: blob CRC differs");
            if (hasJp && !w.TryGetProperty("jp_data", out _)) { bad.Add($"entry {i} {g.Key}: a JP title the expect does not have"); continue; }
            if (hasJp)
            {
                if (Math.Abs(g.JpW - w.GetProperty("jp_w").GetInt32()) > 8) bad.Add($"entry {i} {g.Key}: JP width {g.JpW}");
                var s = TitleCheck.Similarity(pak.Blob(g, "jp")!, g.JpW, Convert.FromHexString(w.GetProperty("jp_data").GetString()!), w.GetProperty("jp_w").GetInt32());
                if (!TitleCheck.Close(s)) bad.Add($"entry {i} {g.Key}: JP title IoU {s.Iou:F3} diff {s.Diff:F2}");
            }
        }
        var rep = x.GetProperty("report");
        foreach (var k in new[] { "years_from_ipbin", "compat_applied", "covers", "folder_art", "art_entries", "check_errors", "per_title", "compat_notes", "scan", "remote" })
        {
            var a = r.Report[k];
            var e = JsonNode.Parse(rep.GetProperty(k).GetRawText());
            if (!JsonNode.DeepEquals(Normal(a), Normal(e))) bad.Add($"report.{k}: {a?.ToJsonString()} expect {e?.ToJsonString()}");
        }
        foreach (var k in new[] { "status", "rendered", "scaled", "cropped", "too_tall", "warning", "denominator" })
            if (!JsonNode.DeepEquals(r.Report["jp"]![k], rep.GetProperty("jp").TryGetProperty(k, out var ev) ? JsonNode.Parse(ev.GetRawText()) : null))
                bad.Add($"report.jp.{k}: {r.Report["jp"]![k]?.ToJsonString()}");
        return bad;
    }

    /// <summary>Objects compared without regard to member order (a JSON object is unordered).</summary>
    static JsonNode? Normal(JsonNode? n) => n switch
    {
        JsonObject o => new JsonObject(o.OrderBy(p => p.Key, StringComparer.Ordinal).Select(p => KeyValuePair.Create(p.Key, Normal(p.Value)))),
        JsonArray a => new JsonArray(a.Select(Normal).ToArray()),
        null => null,
        _ => JsonNode.Parse(n.ToJsonString()),
    };
}

public class CardBuilderTests
{
    [SkippableFact]
    public void The_fixture_card_builds_as_omsat_cardbuild_builds_it()
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout (the JP titles need it)");
        var x = BuildCheck.Expect();
        var (t, r) = BuildCheck.Build();
        using (t)
        {
            using var tr = new TitleRenderer();
            var bad = BuildCheck.Compare(r, x, tr);
            Assert.True(bad.Count == 0, string.Join("\n", bad));
            Assert.Equal(17, ArtPak.Read(r.ArtPak).Entries.Count);
        }
    }

    /// <summary>M6 Task 18d review (4cdfc7b, test_cardbuild.py test_jp_title_too_tall_is_dropped_and_reported): a title
    /// too tall even at 10 px gets no JP bitmap, never a clipped one, and the report names it with a warning. Here for
    /// real, not mocked: an 8-row bitmap cannot hold 10 px kanji.</summary>
    [SkippableFact]
    public void A_jp_title_too_tall_even_at_10_px_is_dropped_and_reported()
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout");
        var (t, r) = BuildCheck.Build(o => o with { JpHeight = 8 });
        using (t)
        {
            var jp = r.Report["jp"]!;
            int want = BuildCheck.Expect().GetProperty("report").GetProperty("jp").GetProperty("rendered").GetInt32();
            Assert.Equal(0, jp["rendered"]!.GetValue<int>());
            Assert.Equal(want, jp["too_tall"]!.AsArray().Count);                 // every title the 16-row build rendered
            Assert.Contains("even at 10 px", jp["warning"]!.GetValue<string>());
            Assert.DoesNotContain(ArtPak.Read(r.ArtPak).Entries, e => (e.Flags & ArtPak.FJp) != 0);
            Assert.Contains("taller than the 16-row bitmap", CardBuilder.JpWarning(r)!);
        }
        var (t2, r2) = BuildCheck.Build();
        using (t2)
        {
            Assert.Empty(r2.Report["jp"]!["too_tall"]!.AsArray());                // today's titles: none at 16 rows
            Assert.Null(r2.Report["jp"]!["warning"]);
            Assert.Null(CardBuilder.JpWarning(r2));
        }
    }

    /// <summary>The user's card (2026-11-24): Prepare wrote no cover for 139 matched images the Card Manager showed a
    /// pack cover for, so the menu showed none. The bundled COVERS.PAK is now a source after the card's own BOX.TGA and
    /// the zip: here the fixture's three uncovered games, one by a region fallback.</summary>
    [Fact]
    public void The_bundled_cover_pack_covers_a_matched_game_the_card_has_no_cover_for()
    {
        byte[] Tga(byte v) => TgaEncoder.Encode(new RgbaImage(8, 12, [.. Enumerable.Repeat(v, 8 * 12 * 4)]));
        using var pack = CoverPack.Read(CoverPackFixture.Build([
            new("T-1005A", "libretro", "tga", Tga(10), 8, 12),
            new("T-1005A", "coverproject", "tga", Tga(20), 8, 12),
            new("T-1006A", "coverproject", "tga", Tga(30), 8, 12, FallbackRegion: "U", FallbackFrom: "T-1006B"),
            new("GS-9001", "coverproject", "tga", Tga(40), 8, 12),       // Daytona has its own BOX.TGA: that wins
        ]));
        var (t, r) = BuildCheck.Build(o => o with { Remote = null, CoverPack = pack });
        using (t)
        {
            var rows = r.ManifestCsv.Split('\n').Select(l => l.Split(',')).Where(c => c.Length > 3).ToList();
            Assert.Equal("cover_pack", rows.Single(c => c[1] == "T-1005A")[3]);
            Assert.Equal("COVERS.PAK:T-1005A:coverproject", rows.Single(c => c[1] == "T-1005A")[4]);   // the pack's order
            Assert.Contains(r.ManifestCsv.Split('\n'), l => l.Contains(",T-1006A,0,\"cover_pack (fallback_region U, from T-1006B)\",", StringComparison.Ordinal));
            Assert.DoesNotContain(rows, c => c[1] == "GS-9001" && c[3] == "cover_pack");
            Assert.Equal(2, r.Report["covers"]!["cover_pack"]!.GetValue<int>());
            var pak = ArtPak.Read(r.ArtPak);
            Assert.True(CoverLookup.HasCover(pak, "T-1005A", 1));
        }
    }

    /// <summary>3aba482: a folder's only loose game (the folder is a folder row because it has a subfolder) takes the
    /// folder's BOX.TGA ahead of the cover zip; two loose games sharing it keep the zip first.</summary>
    [Theory]
    [InlineData(false, "user")]
    [InlineData(true, "coverproject")]
    public void A_folders_only_loose_game_takes_its_box_tga_first(bool secondLooseGame, string want)
    {
        using var t = new TempCard();
        string card = Path.Combine(t.Dir, "card"), solo = Path.Combine(card, "Racing");
        Directory.CreateDirectory(Path.Combine(solo, "Sub"));
        byte[] Iso(string product) => [.. VibeMenuCardManager.Core.Disc.SystemIdReader.Make(new Dictionary<string, string>
            { ["product"] = product, ["area"] = "JTUE", ["title"] = "X", ["device"] = "CD-1/1" }), .. new byte[2048]];
        File.WriteAllBytes(Path.Combine(solo, "Daytona USA (USA).iso"), Iso("GS-9001"));
        File.WriteAllBytes(Path.Combine(solo, "Sub", "vf2.iso"), Iso("GS-7103"));
        if (secondLooseGame) File.WriteAllBytes(Path.Combine(solo, "Alien.iso"), Iso("T-8113H"));
        File.WriteAllBytes(Path.Combine(solo, "BOX.TGA"), TgaEncoder.Encode(new RgbaImage(4, 6, [.. Enumerable.Repeat((byte)200, 4 * 6 * 4)])));
        var o = BuildCheck.Options(BuildCheck.Expect()) with { ArtPrefer = null, FolderArt = null, Remote = null };
        var r = CardBuilder.Build(card, 20261124, o);
        var row = r.ManifestCsv.Split('\n').Single(l => l.Split(',') is [_, "GS-9001", ..]).Split(',');
        Assert.Equal(want, row[3]);                     // manifest.csv: id,key,disc,source,source_file,transform
    }

    /// <summary>d51022a: a game with no cover of its own regions takes the same game's cover from US (then JP, then EU),
    /// marked in the manifest and the report -- here Misc/nocover.iso, made a Europe-only "Sample Game", finds the
    /// zip's "Sample Game (USA)".</summary>
    [Fact]
    public void A_game_with_no_cover_of_its_own_region_takes_the_same_games_US_cover()
    {
        var (t, r) = BuildCheck.Build(o =>
        {
            var db = GamesDb.Read(o.GamesDb);
            var rec = db.Records.Single(x => x.Key == "T-1005A");
            rec.TitleEn = "Sample Game";
            rec.RegionFlags = GamesDb.RegionFlagsFrom("E");
            return o with { GamesDb = GamesDb.Write(db.Records, db.Genres, db.Aliases, db.BuildDate), ArtPrefer = null };
        });
        using (t)
        {
            Assert.Equal(1, r.Report["covers"]!["region_fallback"]!.GetValue<int>());
            var per = r.Report["per_title"]!.AsArray().Single(x => (string)x!["key"]! == "T-1005A")!;
            Assert.Equal("region_fallback (fallback_region U, from T-1005A)", (string)per["source"]!);
            Assert.EndsWith("Sample Game (USA) [patched].jpg", (string)per["file"]!);
            Assert.Contains("region_fallback (fallback_region U, from T-1005A)", r.ManifestCsv);
        }
    }

    [Fact]
    public void The_build_reads_the_card_and_writes_nothing_on_it()
    {
        var (t, _) = BuildCheck.Build();
        using (t)
        {
            var spec = JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement;
            Assert.Equal(spec.GetProperty("files").GetArrayLength(), t.Manifest().Count(m => !m.Path.EndsWith('/')));
        }
    }

    [Fact]
    public void WriteTo_puts_the_five_files_in_the_out_folder()
    {
        var (t, r) = BuildCheck.Build();
        using (t)
        {
            string outDir = Path.Combine(t.Dir, "out");
            CardBuilder.WriteTo(outDir, r);
            Assert.Equal(["ART.PAK", "GAMES.DB", "LIBRARY.IDX", "manifest.csv", "report.json"],
                         Directory.GetFiles(outDir).Select(Path.GetFileName).Order(StringComparer.Ordinal));
            Assert.Equal(r.ArtPak, File.ReadAllBytes(Path.Combine(outDir, "ART.PAK")));
            Assert.Equal(r.ManifestCsv, Encoding.UTF8.GetString(File.ReadAllBytes(Path.Combine(outDir, "manifest.csv"))));
        }
    }
}
