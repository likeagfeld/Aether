// VibeMenu Card Manager, GPL-3.0-or-later.
// Cancel: an import cancelled after k files leaves the card at the un-imported state plus whole
// files only -- each one byte-identical to the console import's file at that path -- with no ".IMP" and no IMPORT.TXT;
// a run again converges on the console import's card (manager/golden/expect/merge.json).
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public class ImportCancelTests
{
    public static TheoryData<int> EveryFile => new(Enumerable.Range(1, 19));    // merge writes 20 files: cancel after each but the last

    [Theory]
    [MemberData(nameof(EveryFile))]
    public void A_cancel_leaves_whole_files_only_and_a_rerun_converges_on_the_golden(int k)
    {
        var x = ImportGolden.Load("merge");
        var golden = x.Tree.ToDictionary(t => t.Path);
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        var before = card.Manifest().ToDictionary(t => t.Path);
        using var cts = new CancellationTokenSource();
        int renames = 0;
        var fs = new CardFs(card.Dir) { BeforeRename = _ => { if (++renames == k) cts.Cancel(); } };
        var e = Assert.Throws<ImportCancelledException>(() => SaveImporter.Run(fs, null, cts.Token));
        Assert.Equal(k, renames);                                          // stopped at the next file, not mid-file
        Assert.Equal(k, e.SoFar.Kai + e.SoFar.Rings + e.SoFar.Older + e.SoFar.Cart);
        foreach (var t in card.Manifest())
        {
            Assert.False(t.Path.EndsWith(".IMP", StringComparison.Ordinal), $"a temporary file is left: {t.Path}");
            Assert.NotEqual("aether/SAVES/IMPORT.TXT", t.Path);
            if (before.TryGetValue(t.Path, out var b)) Assert.Equal(b, t);         // the original card, untouched
            else Assert.Equal(golden[t.Path], t);                               // a new file: whole, the console's bytes
        }
        SaveImporter.Run(new CardFs(card.Dir));                                // run again, to the end
        var bad = ImportGolden.Compare([], null, card.Manifest().Where(t => t.Path != "aether/SAVES/IMPORT.TXT").ToList(),
                                       x with { Runs = [], ImportTxt = null, Tree = x.Tree.Where(t => t.Path != "aether/SAVES/IMPORT.TXT").ToList() });
        Assert.True(bad.Count == 0, string.Join("\n", bad));
    }

    [Fact]
    public void An_already_cancelled_token_writes_nothing()
    {
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        var before = card.Manifest();
        Assert.Throws<ImportCancelledException>(() => SaveImporter.Run(new CardFs(card.Dir), null, new CancellationToken(true)));
        Assert.Equal(before, card.Manifest().Where(t => before.Any(b => b.Path == t.Path)).ToList());
        Assert.Equal(before.Count, card.Manifest().Count);
    }
}
