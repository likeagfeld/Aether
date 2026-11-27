// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using System.Text.Json.Nodes;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Card;

namespace VibeMenuCardManager.Tests.Mutation;

public class LibIdxMutationTests
{
    static byte[] Golden() => File.ReadAllBytes(Repo.P("contract/fixtures/scan_tree.library.idx"));

    static byte[] ScanWith(Action<JsonNode> mutate)
    {
        var n = JsonNode.Parse(File.ReadAllText(Repo.P("contract/fixtures/scan_tree.json")))!;
        mutate(n);
        var t = JsonDocument.Parse(n.ToJsonString()).RootElement;
        var (keys, aliases) = ScanFixture.Db();
        return CardScanner.Scan(new TreeSource(t.GetProperty("tree"), t.GetProperty("sysids"), t.GetProperty("cues")), keys, aliases, t.GetProperty("build_date").GetUInt32()).Data;
    }

    [Fact]
    public void Unmutated_scan_is_green() => Assert.Null(ScanFixture.FirstDiff(ScanWith(_ => { }), Golden()));

    [Fact]
    public void A_changed_disc_product_is_caught() =>
        Assert.NotNull(ScanFixture.FirstDiff(ScanWith(n => n["sysids"]!["loose.iso"]!["product"] = "T-9999X"), Golden()));

    [Fact]
    public void A_removed_BOX_TGA_is_caught() =>
        Assert.NotNull(ScanFixture.FirstDiff(ScanWith(n =>
        {
            var files = n["tree"]!["Racing/Daytona USA"]!["files"]!.AsArray();
            files.RemoveAt(files.Select((v, i) => (v, i)).Single(x => x.v!.GetValue<string>() == "BOX.TGA").i);
        }), Golden()));

    [Fact]
    public void Two_swapped_entries_are_caught()
    {
        var d = LibraryIndex.Read(File.ReadAllBytes(Repo.P("contract/fixtures/tiny.library.idx")));
        (d.Entries[1], d.Entries[2]) = (d.Entries[2], d.Entries[1]);
        Assert.NotNull(ScanFixture.FirstDiff(LibraryIndex.Write(d.Entries, d.Folders, d.Fingerprint, d.BuildDate),
                                             File.ReadAllBytes(Repo.P("contract/fixtures/tiny.library.idx"))));
    }
}
