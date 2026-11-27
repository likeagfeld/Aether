// VibeMenu Card Manager, GPL-3.0-or-later.
// "Update card to <ver>" on a card that already has VibeMenu (a card-write safety path): the PC backup snapshot of the
// user's files exists before anything is asked or written, and when that backup cannot be taken nothing is written.
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Card;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

[Collection("CardBackup (static RootOverrideForTests)")]
public class UpgradeBackupTests
{
    /// <summary>A card a 0.0.9 VibeMenu prepared, with the user's own files, and a release of the bundled version.</summary>
    static (string Card, string Release) Setup(string dir)
    {
        string card = Path.Combine(dir, "card"), vm = Path.Combine(card, "aether"), release = ReleaseFixture.Make(Path.Combine(dir, "release"));
        Directory.CreateDirectory(Path.Combine(card, "Games", "Alpha"));
        Directory.CreateDirectory(Path.Combine(vm, "SAVES", "T-00001@U"));
        File.WriteAllBytes(Path.Combine(card, "Games", "Alpha", "Alpha.iso"),
            SystemIdReader.Make(new Dictionary<string, string> { ["product"] = "T-00001", ["area"] = "U", ["title"] = "ALPHA", ["device"] = "CD-1/1" }));
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso"),
            [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU", ["product"] = "OMSAT-MENU" }), .. new byte[2048],
             .. "VibeMenu 0.0.9 1111111 2026-11-01\0"u8.ToArray()]);
        File.WriteAllText(Path.Combine(vm, "LAUNCHER.BIN"), "the old launcher");
        File.Copy(Path.Combine(release, "GAMES.DB"), Path.Combine(vm, "GAMES.DB"));
        File.WriteAllText(Path.Combine(vm, "FAVS.TXT"), "Games/Alpha/Alpha.iso\n");
        File.WriteAllText(Path.Combine(vm, "USEREDIT.INI"), "[T-00001]\ntitle=Mine\n");
        File.WriteAllBytes(Path.Combine(vm, "SAVES", "T-00001@U", "SAVE1.BUP"), [9, 9, 9]);
        return (card, release);
    }

    static MainWindow Window(string card, string release, Func<bool> onAsk)
    {
        var w = new MainWindow(removableDriveListForTests: () => [])
        {
            SystemVolumes = [], ReleaseOverrideForTests = release,
            Ask = (_, _, _) => Task.FromResult(onAsk()),
        };
        w.AllowFixedVolumeForTests(VolumeResolver.Resolve(card).Id!);
        w.CardPath.Text = card;
        return w;
    }

    [Fact]
    public async Task The_pc_backup_is_taken_before_the_update_asks_or_writes_anything()
    {
        using var t = new TempCard();
        var (card, release) = Setup(t.Dir);
        string backups = Path.Combine(t.Dir, "backups");
        CardBackup.RootOverrideForTests = backups;
        try
        {
            int snapshotsWhenAsked = -1;
            string label = await HeadlessApp.Run(async () =>
            {
                var w = Window(card, release, () =>
                {
                    snapshotsWhenAsked = Directory.EnumerateDirectories(backups, "*", SearchOption.TopDirectoryOnly)
                        .Sum(d => CardBackup.ListSnapshots(d).Count);
                    return false;                                   // declined: this test is about the order only
                });
                w.OpenCard(card);
                string l = w.PrepareCardButton.Content as string ?? "";
                await w.PrepareCard();
                w.Close();
                return l;
            });
            Assert.Equal("Update card to 0.1", label);
            Assert.Equal(1, snapshotsWhenAsked);                   // the snapshot existed before the write was confirmed
            var snap = CardBackup.ListSnapshots(Directory.GetDirectories(backups).Single()).Single();
            Assert.Equal("Games/Alpha/Alpha.iso\n", System.Text.Encoding.ASCII.GetString(CardBackup.ReadSnapshotFile(snap.Dir, "/aether/FAVS.TXT")));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public async Task A_backup_that_fails_writes_nothing_and_asks_nothing()
    {
        using var t = new TempCard();
        var (card, release) = Setup(t.Dir);
        string notAFolder = Path.Combine(t.Dir, "backups-is-a-file");
        File.WriteAllText(notAFolder, "a file where the backup folder should go");
        CardBackup.RootOverrideForTests = notAFolder;
        try
        {
            var before = t.Manifest().Where(r => r.Path.StartsWith("card/", StringComparison.Ordinal)).ToList();
            bool asked = false;
            string status = await HeadlessApp.Run(async () =>
            {
                var w = Window(card, release, () => { asked = true; return true; });
                w.OpenCard(card);
                await w.PrepareCard();
                string s = w.Status.Text ?? "";
                w.Close();
                return s;
            });
            Assert.StartsWith("The PC backup before the update failed", status);
            Assert.False(asked);
            Assert.Equal(before, t.Manifest().Where(r => r.Path.StartsWith("card/", StringComparison.Ordinal)).ToList());
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }
}
