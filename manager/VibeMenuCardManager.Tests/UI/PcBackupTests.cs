// VibeMenu Card Manager, GPL-3.0-or-later.
// PC backup/restore wiring: OpenCard backs up off the UI thread when AutoBackupOnOpen is on --
// including the timing-sensitive path (a drive auto-selected during construction) that a bug fix here specifically
// targets -- and File > "Restore from backup…" only ever stages into the live session, never the card itself,
// until Save changes.
using Avalonia.Controls;
using Avalonia.Threading;
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

/// <summary>Shares a collection with Tests.Backup.CardBackupTests: both set the static
/// CardBackup.RootOverrideForTests, and the backup here runs on a real background Task.Run thread (not the
/// dispatcher), so two classes racing on that one field is a real flaky-test risk, not a theoretical one.</summary>
[Collection("CardBackup (static RootOverrideForTests)")]
public class PcBackupTests
{
    static byte[] MakeIso(string product, string title, string area = "U") =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = area, ["title"] = title, ["device"] = "CD-1/1" });

    static void BuildCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        var db = new List<GameRecord> { new() { Key = "T-00001", TitleEn = "Alpha Game", DiscCount = 1, Year = 1996 } };
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261124));
        File.WriteAllText(Path.Combine(root, "aether", "FAVS.TXT"), "Games/Alpha/Alpha.iso\n");
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"), MakeIso("T-00001", "ALPHA GAME", "U"));
    }

    static MainWindow Window(string dir, bool autoBackup = false)
    {
        var w = new MainWindow(removableDriveListForTests: () => [], autoBackupOnOpen: autoBackup) { SystemVolumes = [] };
        w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    /// <summary>The exact bug this round fixed: a real or injected drive at index 0 can auto-open via
    /// RefreshDriveList -> OnDrivePicked -> OpenCard, synchronously, DURING the constructor -- so AutoBackupOnOpen
    /// must be a constructor parameter, not a property set afterward (the same class of bug as
    /// RemovableDriveListForTests earlier). This test fails if AutoBackupOnOpen is ever set outside the ctor again.</summary>
    [Fact]
    public async Task A_drive_auto_selected_during_construction_still_backs_up()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        BuildCard(card.Dir);
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            var (taskWasSetImmediately, backedUp, status) = await HeadlessApp.Run(async () =>
            {
                // removableDriveListForTests returns the card's own dir at index 0 -- RefreshDriveList's own
                // SelectedIndex = 0 then fires OnDrivePicked -> OpenCard synchronously, inside this constructor call.
                var w = new MainWindow(removableDriveListForTests: () => [card.Dir], autoBackupOnOpen: true);
                w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(card.Dir).Id!);
                bool hadTask = w.LastBackupTaskForTests != null;
                if (w.LastBackupTaskForTests != null) await w.LastBackupTaskForTests;
                Dispatcher.UIThread.RunJobs();
                // Punch-list round: the Views pane's footer is the only place "Backed up to PC" shows now (the old
                // Status-line copy of the same sentence was removed as a stray duplicate above the status bar).
                return (hadTask, Directory.Exists(backups.Dir) && Directory.EnumerateDirectories(backups.Dir).Any(), w.ViewsFooterText.Text);
            });
            Assert.True(taskWasSetImmediately, "AutoBackupOnOpen must take effect from construction, not after it");
            Assert.True(backedUp);
            Assert.Contains("Backed up to PC", status);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public async Task Opening_the_same_unchanged_card_twice_backs_up_only_once()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        BuildCard(card.Dir);
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            int snapshotCount = await HeadlessApp.Run(async () =>
            {
                var w = Window(card.Dir, autoBackup: true);
                w.OpenCard(card.Dir);
                if (w.LastBackupTaskForTests != null) await w.LastBackupTaskForTests;
                w.OpenCard(card.Dir);
                if (w.LastBackupTaskForTests != null) await w.LastBackupTaskForTests;
                Dispatcher.UIThread.RunJobs();
                string cardFolder = Directory.EnumerateDirectories(backups.Dir).First();
                return Directory.EnumerateDirectories(cardFolder).Count();
            });
            Assert.Equal(1, snapshotCount);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public async Task Restoring_a_backup_stages_it_without_writing_to_the_card_and_the_confirmation_mentions_it()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        BuildCard(card.Dir);
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            var (unsavedText, saveEnabled, menuBinUntouched, askedText) = await HeadlessApp.Run(async () =>
            {
                var w = Window(card.Dir, autoBackup: true);
                w.OpenCard(card.Dir);
                if (w.LastBackupTaskForTests != null) await w.LastBackupTaskForTests;
                Dispatcher.UIThread.RunJobs();
                w.AutoBackupOnOpen = false;   // this test's own second OpenCard below is just a rescan, not a new backup

                // an edit after the backup that the restore must bring back
                File.WriteAllText(Path.Combine(card.Dir, "aether", "FAVS.TXT"), "");
                w.OpenCard(card.Dir);   // rescans: live card now shows no favorite
                var row = w.Load!.Rows.Single();
                Assert.False(row.Favorite);

                string cardFolder = Directory.EnumerateDirectories(backups.Dir).First();
                var snapshots = CardBackup.ListSnapshots(cardFolder);
                var rw = new RestoreWindow(w, w.Load!, snapshots);
                Dispatcher.UIThread.RunJobs();
                Assert.NotEmpty(rw.Shown);
                Assert.True(rw.StageButton.IsEnabled);
                rw.StageForTests();

                Assert.True(row.Favorite);   // staged back from the backup
                Assert.False(File.Exists(Path.Combine(card.Dir, "menu.bin")));

                List<string> asked = [];
                w.Ask = (_, _, text) => { asked.Add(text); return Task.FromResult(false); };   // decline: never actually writes
                await w.SaveChanges();
                return (w.UnsavedText.Text ?? "", w.SaveChangesButton.IsEnabled, !File.Exists(Path.Combine(card.Dir, "menu.bin")), string.Join("\n", asked));
            });
            Assert.DoesNotContain("No unsaved changes", unsavedText);   // the staged restore counts (SaveChangesButton.IsEnabled below agrees)
            Assert.True(menuBinUntouched);
            Assert.Contains("restoring the PC backup from", askedText);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }
}
