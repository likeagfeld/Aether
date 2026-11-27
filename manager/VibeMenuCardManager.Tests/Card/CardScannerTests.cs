// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Card;

/// <summary>contract/fixtures/scan_tree.json as an in-memory card (tools/omsat/cardscan.py TreeSource).</summary>
public sealed class TreeSource(JsonElement tree, JsonElement sysids, JsonElement cues = default) : IDirSource
{
    public IReadOnlyList<(string Name, bool IsDir)> List(string path)
    {
        var n = tree.GetProperty(path);
        return [.. n.GetProperty("dirs").EnumerateArray().Select(d => (d.GetString()!, true)),
                .. n.GetProperty("files").EnumerateArray().Select(f => (f.GetString()!, false))];
    }

    public SystemId? SysId(string path) =>
        sysids.TryGetProperty(path, out var f)
            ? SystemIdReader.Parse(SystemIdReader.Make(f.EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!)))
            : null;

    /// <summary>The fixture's own "cues" map (path -> FILE names); matches tools/omsat/cardscan.py's TreeSource.cue_files.</summary>
    public IReadOnlyList<string> CueFiles(string path) =>
        cues.ValueKind == JsonValueKind.Object && cues.TryGetProperty(path, out var names)
            ? names.EnumerateArray().Select(x => x.GetString()!).ToList() : [];
}

public static class ScanFixture
{
    public static JsonElement Load() => JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/scan_tree.json"))).RootElement;

    public static (List<string> Keys, Dictionary<string, string> Aliases) Db()
    {
        var d = GamesDb.Read(File.ReadAllBytes(Repo.P("contract/fixtures/tiny.games.db")));
        return (d.Records.Select(r => r.Key).ToList(), d.Aliases);
    }

    /// <summary>The first differing byte of two files, as "offset (region)", or null when equal.</summary>
    public static string? FirstDiff(byte[] got, byte[] want)
    {
        int n = Math.Min(got.Length, want.Length);
        for (int i = 0; i < n; i++)
            if (got[i] != want[i]) return $"byte {i} (0x{i:X}): {got[i]:X2} != {want[i]:X2}" + (i < 64 ? " (header)" : "");
        return got.Length == want.Length ? null : $"length {got.Length} != {want.Length}";
    }

    /// <summary>The tree laid out on disk: .iso files hold the System ID; a .cue names the fixture's own "cues"
    /// entry's FILE names (the first one carries the real sysid bytes; the others are empty placeholders -- enough
    /// to exist so the scanner's dedup sees and folds them), falling back to a sidecar ".sid" convention when the
    /// cue has no "cues" entry.</summary>
    public static void Realize(JsonElement t, string dir)
    {
        var cues = t.TryGetProperty("cues", out var c) ? c : default;
        foreach (var node in t.GetProperty("tree").EnumerateObject())
        {
            string d = Path.Combine(dir, node.Name.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(d);
            foreach (var sub in node.Value.GetProperty("dirs").EnumerateArray()) Directory.CreateDirectory(Path.Combine(d, sub.GetString()!));
            foreach (var f in node.Value.GetProperty("files").EnumerateArray())
            {
                string name = f.GetString()!, rel = node.Name == "" ? name : node.Name + "/" + name, p = Path.Combine(d, name);
                byte[] sid = t.GetProperty("sysids").TryGetProperty(rel, out var s)
                    ? SystemIdReader.Make(s.EnumerateObject().ToDictionary(x => x.Name, x => x.Value.GetString()!)) : [];
                if (name.EndsWith(".cue", StringComparison.OrdinalIgnoreCase) && sid.Length > 0)
                {
                    List<string> names = cues.ValueKind == JsonValueKind.Object && cues.TryGetProperty(rel, out var cueNames)
                        ? cueNames.EnumerateArray().Select(x => x.GetString()!).ToList() : [name + ".sid"];
                    var text = string.Concat(names.Select(n => $"FILE \"{n}\" BINARY\n  TRACK 01 MODE1/2048\n"));
                    File.WriteAllText(p, text);
                    File.WriteAllBytes(Path.Combine(d, names[0]), sid);
                    foreach (var n in names.Skip(1))
                    {
                        string np = Path.Combine(d, n);
                        if (!File.Exists(np)) File.WriteAllBytes(np, []);
                    }
                }
                else if (!File.Exists(p)) File.WriteAllBytes(p, sid);
            }
        }
    }
}

public class CardScannerTests
{
    [Fact]
    public void Scan_of_scan_tree_writes_exactly_the_golden_library_idx()
    {
        var t = ScanFixture.Load();
        var (keys, aliases) = ScanFixture.Db();
        var r = CardScanner.Scan(new TreeSource(t.GetProperty("tree"), t.GetProperty("sysids"), t.GetProperty("cues")), keys, aliases, t.GetProperty("build_date").GetUInt32());
        var want = File.ReadAllBytes(Repo.P("contract/fixtures/scan_tree.library.idx"));
        Assert.Null(ScanFixture.FirstDiff(r.Data, want));
        Assert.Equal((9, 4, 1), (r.Entries.Count, r.Matched, r.SkippedFolders));       // test_cardscan.py's report
        // 2026-11-23 (Satiator Rings' model): RPG/Grandia (two images) is a folder row now, not absorbed into RPG;
        // Racing's own Daytona USA and Sega Rally (one image each) are single-game folders, so neither lists here
        Assert.Equal(["", "Deep", "Empty", "Homebrew", "RPG", "Racing", "Deep/L2", "RPG/Grandia", "Deep/L2/L3", "Deep/L2/L3/L4",
                      "Deep/L2/L3/L4/L5", "Deep/L2/L3/L4/L5/L6", "Deep/L2/L3/L4/L5/L6/L7", "Deep/L2/L3/L4/L5/L6/L7/L8"],
                     r.Folders.Select(f => f.Path));
    }

    [Fact]
    public void Scan_of_the_same_tree_on_disk_writes_the_same_library_idx()
    {
        var t = ScanFixture.Load();
        var (keys, aliases) = ScanFixture.Db();
        using var card = new TempCard();
        ScanFixture.Realize(t, card.Dir);
        var r = CardScanner.Scan(new FsSource(card.Dir), keys, aliases, t.GetProperty("build_date").GetUInt32());
        Assert.Null(ScanFixture.FirstDiff(r.Data, File.ReadAllBytes(Repo.P("contract/fixtures/scan_tree.library.idx"))));
    }

    [Fact]
    public void Tiny_library_idx_reads_and_writes_back_byte_for_byte()
    {
        var b = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.library.idx"));
        var d = LibraryIndex.Read(b);
        Assert.Equal(4, d.Entries.Count);
        Assert.Equal("MY HOMEBREW", d.Entries[3].TitleOverride);
        Assert.True(d.Entries[0].HasBox);
        Assert.Null(ScanFixture.FirstDiff(LibraryIndex.Write(d.Entries, d.Folders, d.Fingerprint, d.BuildDate), b));
    }

    [Fact]
    public void Every_fingerprint_case_reproduces()
    {
        var cases = JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/fingerprint_cases.json"))).RootElement.GetProperty("cases");
        int n = 0;
        foreach (var c in cases.EnumerateArray())
        {
            Assert.Equal(c.GetProperty("expect").GetUInt32(), LibraryIndex.CardFingerprint(c.GetProperty("names").EnumerateArray().Select(x => x.GetString()!)));
            n++;
        }
        Assert.True(n >= 3);
    }

    [Theory]
    [InlineData("OpenMenu", true, true)]
    [InlineData("autoboot.iso.bak-1", true, true)]
    [InlineData(".Trashes", false, true)]
    [InlineData("System Volume Information", false, true)]
    [InlineData("openmenu", false, false)]
    [InlineData("Racing", true, false)]
    [InlineData("VibeMenu", true, true)]
    [InlineData("vibemenu", false, false)]
    [InlineData("Aether", true, true)]                 // the Aether rename: the folder now; "vibemenu" stays hidden
    [InlineData("aether", false, false)]
    // Kai's /SAVES save tree, at root only, like satiator-saves.
    [InlineData("SAVES", true, true)]
    [InlineData("saves", true, true)]
    [InlineData("satiator-saves", true, true)]
    [InlineData("SAVES", false, false)]
    public void Hidden_names_match_test_cardscan_py(string n, bool root, bool want) => Assert.Equal(want, LibraryIndex.IsHiddenName(n, root));

    [Fact]
    public void Depth_9_is_not_entered_and_is_counted()
    {
        // test_cardscan.py test_depth_limit_skips_folder_and_counts_it: d1..d9 nested, a game at the bottom
        var tree = new Dictionary<string, object>();
        string p = "";
        for (int i = 1; i <= 9; i++)
        {
            string c = $"d{i}";
            tree[p] = new { dirs = new[] { c }, files = Array.Empty<string>() };
            p = p == "" ? c : p + "/" + c;
        }
        tree[p] = new { dirs = Array.Empty<string>(), files = new[] { "x.iso", "y.iso" } };
        var j = JsonDocument.Parse(JsonSerializer.Serialize(new { tree, sysids = new { } })).RootElement;
        var r = CardScanner.Scan(new TreeSource(j.GetProperty("tree"), j.GetProperty("sysids")), [], new Dictionary<string, string>(), 1);
        Assert.Equal(1, r.SkippedFolders);
        Assert.Equal(9, r.Folders.Count);           // the root + d1..d8
        Assert.Empty(r.Entries);
    }

    static ScanResult ScanOne(object sysids, params string[] files)
    {
        var tree = new Dictionary<string, object>
        {
            [""] = new { dirs = new[] { "G" }, files = Array.Empty<string>() },
            ["G"] = new { dirs = Array.Empty<string>(), files },
        };
        var j = JsonDocument.Parse(JsonSerializer.Serialize(new { tree, sysids })).RootElement;
        return CardScanner.Scan(new TreeSource(j.GetProperty("tree"), j.GetProperty("sysids")), [], new Dictionary<string, string>(), 1);
    }

    static object Sid(string product, string device) => new { product, area = "J", device, title = "T" };

    [Fact]
    public void Game_directory_rules_of_clarification_7()
    {
        // one image, even unreadable: a single-game folder (its entry goes to the parent, folder 0)
        Assert.Equal((1, 0), (ScanOne(new { }, "a.iso").Folders.Count, ScanOne(new { }, "a.iso").Entries[0].FolderId));
        // 2026-11-23 (Satiator Rings' model): a multi-disc set, even a matching CD-n/m pair of one product, is a
        // folder row -- only a folder with exactly one image (and no subfolder) is a single-game folder
        var set = ScanOne(new Dictionary<string, object> { ["G/1.iso"] = Sid("A-1", "CD-1/2"), ["G/2.iso"] = Sid("A-1", "CD-2/2") }, "1.iso", "2.iso");
        Assert.Equal(2, set.Folders.Count);
        var two = ScanOne(new Dictionary<string, object> { ["G/1.iso"] = Sid("A-1", "CD-1/2"), ["G/2.iso"] = Sid("B-2", "CD-2/3") }, "1.iso", "2.iso");
        Assert.Equal(2, two.Folders.Count);
    }

    [Fact]
    public void Readers_reject_corruption()
    {
        var b = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.library.idx"));
        var c = (byte[])b.Clone(); c[64 + 5] ^= 0x40;
        Assert.Contains("CRC", Assert.Throws<FormatException>(() => LibraryIndex.Read(c)).Message);
    }

    // ===== manager fix round: duplicate games from a .cue + its own data track sharing a folder (real shape,
    // G:\SATIATOR BACKUP\!_LEISURE SUIT LARRY\LARRY 6\game.cue + game.iso, read-only, copied here) =====

    static void WriteCue(string path, string fileName) =>
        File.WriteAllText(path, $"FILE \"{fileName}\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n    POSTGAP 00:02:00\n");

    [Fact]
    public void A_cues_own_iso_sharing_its_folder_is_not_counted_as_a_second_game()
    {
        using var card = new TempCard();
        string larry6 = Path.Combine(card.Dir, "LARRY 6");
        Directory.CreateDirectory(Path.Combine(larry6, "OLD"));
        WriteCue(Path.Combine(larry6, "game.cue"), "game.iso");
        File.WriteAllBytes(Path.Combine(larry6, "game.iso"), MakeIso("T-14096G"));
        File.WriteAllBytes(Path.Combine(larry6, "BOX.TGA"), [0]);
        WriteCue(Path.Combine(larry6, "OLD", "lsl6.cue"), "lsl6.bin");
        File.WriteAllBytes(Path.Combine(larry6, "OLD", "lsl6.bin"), MakeIso("T-14096G"));

        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261123);
        // Exactly one entry for LARRY 6's own game (game.cue), none for game.iso; one for OLD's (lsl6.cue).
        Assert.Single(res.Entries, e => e.Path.Replace('\\', '/') == "LARRY 6/game.cue");
        Assert.DoesNotContain(res.Entries, e => e.Path.Replace('\\', '/') == "LARRY 6/game.iso");
        Assert.Single(res.Entries, e => e.Path.Replace('\\', '/') == "LARRY 6/OLD/lsl6.cue");
        Assert.Equal(2, res.Entries.Count);
    }

    static byte[] MakeIso(string product) =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = "U", ["title"] = "LEISURE SUIT LARRY", ["device"] = "CD-1/1" });
}
