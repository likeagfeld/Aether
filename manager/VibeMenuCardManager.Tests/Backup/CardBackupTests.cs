// VibeMenu Card Manager, GPL-3.0-or-later.
// PC backup: what gets collected, dedupe-by-hash (no new snapshot when nothing changed),
// pruning to the newest 10, and the restore side -- the diff, the staged merge that never clobbers an unsaved
// edit, and the explicit /menu.bin refusal.
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Backup;

/// <summary>Every test here (and PcBackupTests, the UI side) sets the static CardBackup.RootOverrideForTests --
/// xUnit runs different test classes in parallel by default, and TriggerPcBackup's background Task.Run reads that
/// field off the dispatcher thread, so two classes racing on it is a real, not theoretical, flaky-test risk. One
/// shared collection name serializes every test that touches it against every other one, with no effect on the
/// rest of the suite's parallelism.</summary>
[Collection("CardBackup (static RootOverrideForTests)")]
public class CardBackupTests
{
    static byte[] MakeIso(string product, string title, string area = "U") =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = area, ["title"] = title, ["device"] = "CD-1/1" });

    static void BuildCard(string root, string title = "Alpha Game")
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        var db = new List<GameRecord> { new() { Key = "T-00001", TitleEn = title, DiscCount = 1, Year = 1996 } };
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261124));
        File.WriteAllText(Path.Combine(root, "aether", "USEREDIT.INI"), "[T-00001]\ntitle=My Title\n");
        File.WriteAllText(Path.Combine(root, "aether", "FAVS.TXT"), "Games/Alpha/Alpha.iso\n");
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"), MakeIso("T-00001", "ALPHA GAME", "U"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "BOX.TGA"), [1, 2, 3, 4]);
        Directory.CreateDirectory(Path.Combine(root, "aether", "SAVES", "T-00001@U"));
        File.WriteAllBytes(Path.Combine(root, "aether", "SAVES", "T-00001@U", "SAVE1.BUP"), [9, 9, 9]);
    }

    static CardTarget Target(string root) => CardTarget.Inspect(root);

    [Fact]
    public void CollectFiles_excludes_GAMESDB_and_includes_box_art_and_saves()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var files = CardBackup.CollectFiles(card.Dir);
        Assert.DoesNotContain("/aether/GAMES.DB", files);
        Assert.DoesNotContain(files, f => f.Contains("LIBRARY.IDX") || f.Contains("ART.PAK"));
        Assert.Contains("/aether/USEREDIT.INI", files);
        Assert.Contains("/aether/FAVS.TXT", files);
        Assert.Contains("/Games/Alpha/BOX.TGA", files);
        Assert.Contains("/aether/SAVES/T-00001@U/SAVE1.BUP", files);
    }

    [Fact]
    public void A_second_snapshot_with_nothing_changed_writes_nothing_new()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            var r1 = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));
            var r2 = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 5, DateTimeKind.Utc));
            Assert.True(r1.Created);
            Assert.False(r2.Created);
            Assert.Single(CardBackup.ListSnapshots(r1.CardFolder));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void A_changed_file_produces_a_second_snapshot()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            var r1 = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));
            File.WriteAllText(Path.Combine(card.Dir, "aether", "FAVS.TXT"), "Games/Alpha/Alpha.iso\nGames/Beta/Beta.iso\n");
            var r2 = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 5, 0, DateTimeKind.Utc));
            Assert.True(r1.Created);
            Assert.True(r2.Created);
            var snaps = CardBackup.ListSnapshots(r1.CardFolder);
            Assert.Equal(2, snaps.Count);
            Assert.NotEqual(snaps[0].Manifest.ByPath["/aether/FAVS.TXT"].Sha256, snaps[1].Manifest.ByPath["/aether/FAVS.TXT"].Sha256);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void Only_the_newest_ten_snapshots_survive_pruning()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            string? cardFolder = null;
            for (int i = 0; i < 13; i++)
            {
                File.WriteAllText(Path.Combine(card.Dir, "aether", "FAVS.TXT"), $"Games/Alpha/Alpha.iso\n# {i}\n");
                var r = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc).AddMinutes(i));
                cardFolder = r.CardFolder;
            }
            var snaps = CardBackup.ListSnapshots(cardFolder!);
            Assert.Equal(CardBackup.KeepSnapshots, snaps.Count);
            // newest first, and the two oldest (minutes 0 and 1) are gone
            Assert.DoesNotContain(snaps, s => s.Time == new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));
            Assert.DoesNotContain(snaps, s => s.Time == new DateTime(2026, 11, 24, 10, 1, 0, DateTimeKind.Utc));
            Assert.Contains(snaps, s => s.Time == new DateTime(2026, 11, 24, 10, 12, 0, DateTimeKind.Utc));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    // ===== restore =====

    static CardLoad LoadIt(string root) => CardEditSession.Load(root, 20261124);

    [Fact]
    public void Plan_flags_a_hand_edited_menu_bin_entry_as_skipped_never_restorable()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            var r = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));
            var snap = CardBackup.ListSnapshots(r.CardFolder!)[0];
            // hand-edit the manifest to add a /menu.bin entry the real Snapshot() would never produce, and drop its
            // bytes in -- a defensive test for the "never restore over /menu.bin" rule, not just the happy path.
            Directory.CreateDirectory(Path.Combine(snap.Dir, "files"));
            File.WriteAllBytes(Path.Combine(snap.Dir, "files", "menu.bin"), [1, 2, 3]);
            var poisoned = snap with { Manifest = snap.Manifest with { Files = [.. snap.Manifest.Files, new ManifestEntry("/menu.bin", "deadbeef", 3)] } };

            var load = LoadIt(card.Dir);
            var plan = CardRestore.Plan(load, card.Dir, poisoned);
            var entry = Assert.Single(plan, e => e.CardPath == "/menu.bin");
            Assert.Equal(RestoreAction.Skipped, entry.Action);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    /// <summary>The actual guard the coordinator named: CardEditSession.Apply's own belt-and-braces refusal, not
    /// just CardRestore's (CanStage already never queues "/menu.bin" in the first place -- this is what happens if
    /// that first layer were ever wrong, e.g. a hand-crafted or corrupted manifest).</summary>
    [Fact]
    public void Apply_never_writes_menu_bin_even_when_a_raw_restore_file_names_it()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var load = LoadIt(card.Dir);
        load.PendingRestoreRawFiles.Add(("/menu.bin", [1, 2, 3]));
        load.PendingRestoreFrom = "test";
        var fs = new CardFs(card.Dir);
        var report = CardEditSession.Apply(fs, load, 20261124, null, CancellationToken.None);
        Assert.False(File.Exists(Path.Combine(card.Dir, "menu.bin")));
        Assert.DoesNotContain(report.Steps, s => s.Contains("menu.bin", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Stage_keeps_an_unsaved_favorite_and_an_unsaved_user_edit_instead_of_overwriting_them()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            var r = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));

            var load = LoadIt(card.Dir);
            var row = load.Rows.Single();
            Assert.True(row.Favorite);   // from FAVS.TXT at backup time

            // Stage an unsaved change after the backup but before restoring: un-favorite, and a different title.
            row.Favorite = false;
            load.UserEdits["T-00001"] = new UserEditRecord { Title = "A newer unsaved title" };

            var snap = CardBackup.ListSnapshots(r.CardFolder!)[0];
            CardRestore.Stage(load, card.Dir, snap);

            Assert.False(row.Favorite);   // the unsaved toggle survived the restore
            Assert.Equal("A newer unsaved title", load.UserEdits["T-00001"].Title);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void Stage_restores_an_untouched_favorite_and_user_edit_from_the_backup()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            BuildCard(card.Dir);
            var target = Target(card.Dir);
            var r = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));

            var load = LoadIt(card.Dir);
            var row = load.Rows.Single();
            // Nobody touches row.Favorite or UserEdits this session: a restore from the SAME backup should be a
            // no-op in effect (Unchanged), but still exercise the path with a genuinely different backup state.
            File.WriteAllText(Path.Combine(card.Dir, "aether", "FAVS.TXT"), "");   // live card now has no favorites
            var load2 = LoadIt(card.Dir);
            var row2 = load2.Rows.Single();
            Assert.False(row2.Favorite);

            var snap = CardBackup.ListSnapshots(r.CardFolder!)[0];   // the backup still says Alpha was a favorite
            CardRestore.Stage(load2, card.Dir, snap);
            Assert.True(row2.Favorite);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void A_shared_folders_box_art_is_skipped_not_staged()
    {
        using var card = new TempCard();
        using var backups = new TempCard();
        CardBackup.RootOverrideForTests = backups.Dir;
        try
        {
            Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
            var db = new List<GameRecord>
            {
                new() { Key = "T-00001", TitleEn = "A", DiscCount = 1 },
                new() { Key = "T-00002", TitleEn = "B", DiscCount = 1 },
            };
            File.WriteAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261124));
            Directory.CreateDirectory(Path.Combine(card.Dir, "Games", "Multi"));
            File.WriteAllBytes(Path.Combine(card.Dir, "Games", "Multi", "A.iso"), MakeIso("T-00001", "A", "U"));
            File.WriteAllBytes(Path.Combine(card.Dir, "Games", "Multi", "B.iso"), MakeIso("T-00002", "B", "U"));
            File.WriteAllBytes(Path.Combine(card.Dir, "Games", "Multi", "BOX.TGA"), [1, 2, 3, 4]);
            var target = Target(card.Dir);
            var r = CardBackup.Snapshot(card.Dir, target, new DateTime(2026, 11, 24, 10, 0, 0, DateTimeKind.Utc));
            File.WriteAllBytes(Path.Combine(card.Dir, "Games", "Multi", "BOX.TGA"), [5, 6, 7, 8]);   // changed on disk
            var load = LoadIt(card.Dir);
            var snap = CardBackup.ListSnapshots(r.CardFolder!)[0];
            var plan = CardRestore.Plan(load, card.Dir, snap);
            var boxEntry = Assert.Single(plan, e => e.CardPath.EndsWith("BOX.TGA"));
            Assert.Equal(RestoreAction.Skipped, boxEntry.Action);
            Assert.All(load.Rows, row => Assert.Null(row.PendingCover));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }
}
