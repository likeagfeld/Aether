// VibeMenu Card Manager, GPL-3.0-or-later.
// review-1 I-2 (only "not found" is absent; step 1 never truncates), I-3 (a cut between write and rename leaves no
// final-named file), m-5 (FAT case folding the case-sensitive harness cannot model), m-7 (.NEW removed on a failure).
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public class CardFsTests
{
    static string P(TempCard c, string rel) => Path.Combine(c.Dir, rel.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));

    [Fact]
    public void A_cut_between_the_write_and_the_rename_leaves_no_final_named_file_and_a_rerun_completes()
    {
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        const string target = "/aether/SAVES/T-123/ABC_SAVE_01.BUP";
        var fs = new CardFs(card.Dir) { BeforeRename = p => { if (p == target) throw new IOException("simulated power cut"); } };
        var e = Assert.Throws<ImportStoppedException>(() => SaveImporter.Run(fs));
        Assert.Contains("simulated power cut", e.Message);
        Assert.False(File.Exists(P(card, target)));                              // no final-named file at all
        Assert.True(File.Exists(P(card, target + ".IMP")));                      // the temporary, never read as a save
        Assert.False(File.Exists(P(card, "aether/SAVES/IMPORT.TXT")));         // IMPORT.TXT is written last
        // the next run (no cut) finishes the job: the card ends exactly as the console's import leaves it
        var x = ImportGolden.Load("merge");
        SaveImporter.Run(new CardFs(card.Dir));
        // the rerun copies only the file the cut interrupted; everything else is already there
        Assert.Equal("kai=0 rings=1 present=16 skipped=4 conflicts=0 older=0 cart_only=0 alternates=0\n" +
                     "skipped not-a-game-folder /SAVES/BRAM/Gosub/SF3-1_1.BUP\n" +
                     "skipped general-set-backup /SAVES/BRAM/_BACKUP/GENERAL_01.BUP\n" +
                     "skipped reserved /SAVES/BRAM/MK-81214[UT]/PSEUDO_BRAM.BUP\n" +
                     "skipped not-a-save /SAVES/BRAM/MK-81214[UT]/TRUNC_00001.BUP\n", File.ReadAllText(P(card, "aether/SAVES/IMPORT.TXT")));
        var want = x with { Tree = x.Tree.Where(t => t.Path != "aether/SAVES/IMPORT.TXT").ToList() };
        var bad = ImportGolden.Compare([], want.ImportTxt, card.Manifest().Where(t => t.Path != "aether/SAVES/IMPORT.TXT").ToList(), want with { Runs = [] });
        Assert.True(bad.Count == 0, string.Join("\n", bad));                        // every other file: the console's result
    }

    [Fact]
    public void WriteReplace_never_truncates_the_old_file_and_removes_NEW_on_a_failure()
    {
        using var card = new TempCard();
        File.WriteAllText(P(card, "X.TXT"), "old contents");
        var fs = new CardFs(card.Dir) { FailWrite = "/X.TXT.NEW" };
        Assert.Throws<IOException>(() => fs.WriteReplace("/X.TXT", "new"u8.ToArray()));
        Assert.Equal("old contents", File.ReadAllText(P(card, "X.TXT")));
        Assert.False(File.Exists(P(card, "X.TXT.NEW")));
        new CardFs(card.Dir).WriteReplace("/X.TXT", "new"u8.ToArray());
        Assert.Equal("new", File.ReadAllText(P(card, "X.TXT")));
    }

    [Fact]
    public void WriteReplace_finishes_an_interrupted_replace_first()
    {
        using var card = new TempCard();
        File.WriteAllText(P(card, "X.TXT.NEW"), "complete");            // step 3 done (path removed), step 4 cut
        var fs = new CardFs(card.Dir) { FailWrite = "/X.TXT.NEW" };
        Assert.Throws<IOException>(() => fs.WriteReplace("/X.TXT", "next"u8.ToArray()));
        Assert.Equal("complete", File.ReadAllText(P(card, "X.TXT")));    // recovered, then left alone by the failure
    }

    [SkippableFact]
    [SupportedOSPlatform("windows")]
    public void An_unreadable_source_folder_stops_the_import_instead_of_counting_as_absent()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "the access-denied folder is made with a Windows ACL");
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        var dir = P(card, "satiator-saves/MK-81214");
        Deny(dir, true);
        try
        {
            var e = Assert.Throws<ImportStoppedException>(() => SaveImporter.Run(new CardFs(card.Dir)));
            Assert.False(File.Exists(P(card, "aether/SAVES/IMPORT.TXT")));
            Assert.Contains("IMPORT STOPPED", e.Message);
        }
        finally { Deny(dir, false); }
    }

    [SupportedOSPlatform("windows")]
    static void Deny(string dir, bool on)
    {
        var di = new DirectoryInfo(dir);
        var acl = di.GetAccessControl();
        var rule = new FileSystemAccessRule(WindowsIdentity.GetCurrent().User!, FileSystemRights.ListDirectory, AccessControlType.Deny);
        if (on) acl.AddAccessRule(rule); else acl.RemoveAccessRule(rule);
        di.SetAccessControl(acl);
    }

    [Fact]
    public void An_absent_folder_or_a_file_lists_nothing()
    {
        using var card = new TempCard();
        File.WriteAllText(P(card, "f"), "x");
        var fs = new CardFs(card.Dir);
        Assert.Empty(fs.List("/nope/deeper"));
        Assert.Empty(fs.List("/f"));
        Assert.True(fs.Absent("/nope/deeper"));
        Assert.False(fs.Absent("/f"));
    }

    [Fact]
    public void FAT_case_folding_a_lower_case_name_of_another_record_is_never_written_over()
    {
        // on FAT (and NTFS) "abc_save_01.bup" and "ABC_SAVE_01.BUP" are one name. The destination's
        // lower-case file holds another record; the import must pick ~1, as om_pgs_file_name's case-insensitive check
        // does, and leave the existing file as it was.
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        var other = File.ReadAllBytes(P(card, "aether/SAVES/MK-81214[J]/OWN_0000001.BUP"));
        Directory.CreateDirectory(P(card, "aether/SAVES/T-123"));
        File.WriteAllBytes(P(card, "aether/SAVES/T-123/abc_save_01.bup"), other);
        var r = SaveImporter.Run(new CardFs(card.Dir));
        Assert.Equal(other, File.ReadAllBytes(P(card, "aether/SAVES/T-123/abc_save_01.bup")));
        var names = Directory.GetFiles(P(card, "aether/SAVES/T-123")).Select(Path.GetFileName).Order(StringComparer.Ordinal).ToList();
        Assert.Equal(["ABC_SAVE_01~1.BUP", "abc_save_01.bup"], names);
        Assert.Equal(16, r.Kai + r.Rings);
    }
}
