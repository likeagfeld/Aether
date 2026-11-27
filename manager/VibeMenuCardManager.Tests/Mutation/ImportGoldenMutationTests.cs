// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos for the import golden (design spec §9): a doctored expect, and a broken importer, must each make the
// real comparison (ImportGolden.Run / Compare) go RED.
using VibeMenuCardManager.Core.Saves;
using VibeMenuCardManager.Tests.Saves;

namespace VibeMenuCardManager.Tests.Mutation;

public class ImportGoldenMutationTests
{
    static ImportGolden.Expect Merge() => ImportGolden.Load("merge");

    [Fact]
    public void A_flipped_byte_in_one_copied_file_is_caught()
    {
        var x = Merge();
        int i = x.Tree.FindIndex(t => t.Path == "aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP");
        Assert.True(i >= 0);
        var sha = x.Tree[i].Sha256.ToCharArray();
        sha[0] = sha[0] == '0' ? '1' : '0';
        x.Tree[i] = x.Tree[i] with { Sha256 = new string(sha) };
        var bad = ImportGolden.Run("merge", x);
        Assert.Single(bad);
        Assert.StartsWith("differs aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", bad[0]);
    }

    [Fact]
    public void One_count_off_is_caught()
    {
        var x = Merge();
        x.Runs[0]["older"]++;
        Assert.Contains(ImportGolden.Run("merge", x), m => m.StartsWith("run 1: older="));
    }

    [Fact]
    public void A_file_the_console_did_not_write_is_caught()
    {
        var x = Merge();
        x.Tree.RemoveAll(t => t.Path == "aether/SAVES/T-123/ABC_SAVE_01.BUP");
        Assert.Equal(["extra aether/SAVES/T-123/ABC_SAVE_01.BUP"], ImportGolden.Run("merge", x));
    }

    [Fact]
    public void A_wrong_IMPORT_TXT_is_caught()
    {
        var x = Merge() with { ImportTxt = "kai=8 rings=6 present=0 skipped=5 conflicts=0 older=4\n" };
        Assert.Contains(ImportGolden.Run("merge", x), m => m.StartsWith("IMPORT.TXT"));
    }

    [Fact]
    public void An_importer_with_the_duplicate_rule_reversed_fails_the_golden()
    {
        var bad = ImportGolden.Run("merge", Merge(), fs => SaveImporter.Run(fs, null, reversePrefer: true));
        Assert.NotEmpty(bad);
        Assert.Contains(bad, m => m.Contains("SCORCHER_01.BUP"));
    }
}
