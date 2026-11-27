// Aether Card Manager, GPL-3.0-or-later.
// The Aether rename's folder move (CardFolderMigration): a pre-rename card's /vibemenu -- the user's saves, favorites
// and settings included -- is backed up to the PC, checked, renamed to /aether in one move and checked again; both
// folders are refused untouched; a failed check moves it back; Rollback undoes it; Update card (CardPipeline.Run) runs
// it first and puts the folder back when the install then fails.
using System.Text.Json;
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Tests.Card;

[Collection("CardBackup (static RootOverrideForTests)")]
public class CardFolderMigrationTests
{
    static readonly DateTime Now = new(2026, 11, 25, 12, 0, 0, DateTimeKind.Utc);
    static readonly byte[] OldIso = [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU" }), .. "VibeMenu 0.1 1111111\0"u8.ToArray()];

    /// <summary>A card prepared before the rename: the menu's files and the user's own under /vibemenu.</summary>
    static Dictionary<string, byte[]> OldCard(string card)
    {
        var files = new Dictionary<string, byte[]>
        {
            ["GAMES.DB"] = TempCard.DataBytes(1, 4000),
            ["LAUNCHER.BIN"] = TempCard.DataBytes(2, 3000),
            ["SETTINGS.INI"] = "theme=vibemenu\nclock=24\n"u8.ToArray(),
            ["FAVS.TXT"] = "Games/Alpha/Alpha.iso\n"u8.ToArray(),
            ["SAVES/ACTIVE.TXT"] = "SAVES/T-00001 1 1234\n"u8.ToArray(),
            ["SAVES/JOURNAL.TXT"] = "SAVES/T-00001\n"u8.ToArray(),
            ["SAVES/T-00001/01/VIBEMENU.TXT"] = "1\n"u8.ToArray(),
            ["SAVES/T-00001/01/GAME_SAVE_01.BUP"] = TempCard.DataBytes(3, 700),
            ["MUSIC/BGM.MUS"] = TempCard.DataBytes(4, 5000),
            ["CHEATS.PAK"] = "the 0.1 cheat choices"u8.ToArray(),
        };
        foreach (var (rel, data) in files)
        {
            string p = Path.Combine(card, "vibemenu", rel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(p)!);
            File.WriteAllBytes(p, data);
        }
        File.WriteAllText(Path.Combine(card, "menu.bin"), "the official menu");
        return files;
    }

    static void AssertFolder(string dir, Dictionary<string, byte[]> files)
    {
        var have = Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories)
            .Select(f => Path.GetRelativePath(dir, f).Replace(Path.DirectorySeparatorChar, '/')).Order(StringComparer.Ordinal).ToList();
        Assert.Equal(files.Keys.Order(StringComparer.Ordinal).ToList(), have);
        foreach (var (rel, data) in files) Assert.Equal(data, File.ReadAllBytes(Path.Combine(dir, rel.Replace('/', Path.DirectorySeparatorChar))));
    }

    [Fact]
    public void States_are_detected_from_the_two_folder_names()
    {
        using var t = new TempCard();
        Assert.Equal(CardFolderState.None, CardFolderMigration.Detect(t.Dir));
        Directory.CreateDirectory(Path.Combine(t.Dir, "vibemenu"));
        Assert.Equal(CardFolderState.Old, CardFolderMigration.Detect(t.Dir));
        Directory.CreateDirectory(Path.Combine(t.Dir, "aether"));
        Assert.Equal(CardFolderState.Both, CardFolderMigration.Detect(t.Dir));
        Directory.Delete(Path.Combine(t.Dir, "vibemenu"));
        Assert.Equal(CardFolderState.New, CardFolderMigration.Detect(t.Dir));
    }

    [Fact]
    public void An_old_card_is_backed_up_checked_and_moved_with_every_file()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        var r = CardFolderMigration.Migrate(card, VolumeResolver.Resolve(card).Identity, "CARD-1234", Now, backupRoot: b.Dir);
        Assert.True(r.Moved);
        Assert.Equal(CardFolderState.New, CardFolderMigration.Detect(card));
        AssertFolder(Path.Combine(card, "aether"), files);                       // saves, journal, stamps: byte for byte
        Assert.Equal(Path.Combine(b.Dir, "CARD-1234", "20261125-120000"), r.BackupDir);
        AssertFolder(Path.Combine(r.BackupDir!, "vibemenu"), files);           // the PC copy is the whole old folder
        var man = JsonSerializer.Deserialize<List<CardFolderMigration.Entry>>(File.ReadAllText(Path.Combine(r.BackupDir!, "manifest.json")))!;
        Assert.Equal(files.Count, man.Count);
        Assert.All(man, e => Assert.True(e.Path == "MUSIC/BGM.MUS" ? e.Sha256 == null : e.Sha256 is { Length: 64 }));
        Assert.Equal("the official menu", File.ReadAllText(Path.Combine(card, "menu.bin")));
        Assert.Equal(files.Count, r.Files);
    }

    [Fact]
    public void Both_folders_are_refused_and_nothing_is_touched()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        Directory.CreateDirectory(Path.Combine(card, "aether"));
        File.WriteAllText(Path.Combine(card, "aether", "GAMES.DB"), "a release copied in by hand");
        var e = Assert.Throws<InstallException>(() => CardFolderMigration.Migrate(card, null, "C", Now, backupRoot: b.Dir));
        Assert.Contains("has both /vibemenu", e.Message);
        AssertFolder(Path.Combine(card, "vibemenu"), files);
        Assert.Equal("a release copied in by hand", File.ReadAllText(Path.Combine(card, "aether", "GAMES.DB")));
        Assert.Empty(Directory.EnumerateFileSystemEntries(b.Dir));             // no backup was even started
    }

    [Fact]
    public void A_new_or_empty_card_has_nothing_to_move()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        Assert.False(CardFolderMigration.Migrate(t.Dir, null, "C", Now, backupRoot: b.Dir).Moved);
        Directory.CreateDirectory(Path.Combine(t.Dir, "aether"));
        Assert.False(CardFolderMigration.Migrate(t.Dir, null, "C", Now, backupRoot: b.Dir).Moved);
        Assert.Empty(Directory.EnumerateFileSystemEntries(b.Dir));
    }

    [Fact]
    public void An_aether_folder_appearing_before_the_move_stops_it()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        CardFolderMigration.BeforeMove = () => Directory.CreateDirectory(Path.Combine(card, "aether"));
        try { Assert.Throws<InstallException>(() => CardFolderMigration.Migrate(card, null, "C", Now, backupRoot: b.Dir)); }
        finally { CardFolderMigration.BeforeMove = null; }
        AssertFolder(Path.Combine(card, "vibemenu"), files);
        Assert.Empty(Directory.EnumerateFileSystemEntries(Path.Combine(card, "aether")));
    }

    [Fact]
    public void A_moved_folder_that_does_not_match_the_backup_is_moved_back()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        CardFolderMigration.AfterMove = () => File.WriteAllText(Path.Combine(card, "aether", "FAVS.TXT"), "changed under us");
        InstallException e;
        try { e = Assert.Throws<InstallException>(() => CardFolderMigration.Migrate(card, null, "C", Now, backupRoot: b.Dir)); }
        finally { CardFolderMigration.AfterMove = null; }
        Assert.Contains("FAVS.TXT:", e.Message);
        Assert.Contains("moved back to /vibemenu", e.Message);
        Assert.Equal(CardFolderState.Old, CardFolderMigration.Detect(card));
        // the backup still holds the user's original
        Assert.Equal(files["FAVS.TXT"], File.ReadAllBytes(Path.Combine(b.Dir, "C", "20261125-120000", "vibemenu", "FAVS.TXT")));
    }

    [Fact]
    public void A_changed_volume_stops_the_move_before_anything_is_written()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        Assert.Throws<InstallException>(() => CardFolderMigration.Migrate(card, "some other volume", "C", Now, backupRoot: b.Dir));
        AssertFolder(Path.Combine(card, "vibemenu"), files);
        Assert.Empty(Directory.EnumerateFileSystemEntries(b.Dir));
    }

    [Fact]
    public void Rollback_puts_the_folder_the_menu_files_and_the_old_autoboot_back()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        var files = OldCard(card);
        byte[] oldIso = OldIso;
        byte[] newIso = [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "AETHER" }), .. "Aether 0.1 2222222\0"u8.ToArray()];
        var r = CardFolderMigration.Migrate(card, null, "C", Now, backupRoot: b.Dir);
        // what an install then does: a new LAUNCHER.BIN, the old menu image set aside, a save made by the new menu
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso.bak-20261125-120000"), oldIso);
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso"), newIso);
        File.WriteAllBytes(Path.Combine(card, "aether", "LAUNCHER.BIN"), TempCard.DataBytes(9, 100));
        File.WriteAllText(Path.Combine(card, "aether", "CHEATS.PAK"), "the user's cheat choices since");
        File.WriteAllText(Path.Combine(card, "aether", "SAVES", "T-00001", "01", "NEW_SAVE_02.BUP"), "kept");
        var log = CardFolderMigration.Rollback(card, r.BackupDir!, null);
        Assert.Equal(CardFolderState.Old, CardFolderMigration.Detect(card));
        Assert.Equal(files["LAUNCHER.BIN"], File.ReadAllBytes(Path.Combine(card, "vibemenu", "LAUNCHER.BIN")));
        Assert.Equal("kept", File.ReadAllText(Path.Combine(card, "vibemenu", "SAVES", "T-00001", "01", "NEW_SAVE_02.BUP")));
        Assert.Equal(oldIso, File.ReadAllBytes(Path.Combine(card, "autoboot.iso")));
        // the newer files it replaced are kept beside, never overwritten
        Assert.Single(Directory.GetFiles(Path.Combine(card, "vibemenu"), "LAUNCHER.BIN.aether-*"));
        Assert.Equal("the user's cheat choices since", File.ReadAllText(Directory.GetFiles(Path.Combine(card, "vibemenu"), "CHEATS.PAK.aether-*").Single()));
        // run again: everything is already back, nothing more to do
        Assert.DoesNotContain(CardFolderMigration.Rollback(card, r.BackupDir!, null), l => l.StartsWith("restored", StringComparison.Ordinal));
        Assert.Contains(log, l => l.StartsWith("restored /autoboot.iso", StringComparison.Ordinal));
        Assert.Equal("the official menu", File.ReadAllText(Path.Combine(card, "menu.bin")));
    }

    [Fact]
    public void Update_card_moves_an_old_card_and_installs_into_aether()
    {
        using var t = new TempCard();
        t.Realize(JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card"), release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var files = OldCard(card);
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso"),
            [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU" }), .. "VibeMenu 0.1 1111111 2026-11-24\0"u8.ToArray()]);
        using var b = new TempCard();
        CardBackup.RootOverrideForTests = Path.Combine(b.Dir, "Backups");
        try
        {
            Assert.Equal("0.1", CardTarget.Inspect(card, []).VibeMenuVersion);   // the old menu is recognised (no fresh install)
            var r = CardPipeline.Run(card, new PipelineOptions(release), 20261125, VolumeResolver.Resolve(card).Identity, "20261125-120000");
            Assert.True(r.Folder!.Moved);
            Assert.StartsWith(Path.Combine(b.Dir, "Migrations"), r.Folder.BackupDir);
            Assert.Equal(CardFolderState.New, CardFolderMigration.Detect(card));
            string vm = Path.Combine(card, "aether");
            foreach (var k in new[] { "SETTINGS.INI", "FAVS.TXT", "SAVES/ACTIVE.TXT", "SAVES/JOURNAL.TXT", "SAVES/T-00001/01/VIBEMENU.TXT",
                                      "SAVES/T-00001/01/GAME_SAVE_01.BUP" })
                Assert.Equal(files[k], File.ReadAllBytes(Path.Combine(vm, k.Replace('/', Path.DirectorySeparatorChar))));
            Assert.Equal(r.Build.GamesDb, File.ReadAllBytes(Path.Combine(vm, "GAMES.DB")));
            Assert.Equal(File.ReadAllBytes(Path.Combine(release, "LAUNCHER.BIN")), File.ReadAllBytes(Path.Combine(vm, "LAUNCHER.BIN")));
            Assert.Equal("the official menu", File.ReadAllText(Path.Combine(card, "menu.bin")));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void Update_card_puts_the_folder_back_when_the_install_fails()
    {
        using var t = new TempCard();
        t.Realize(JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card"), release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var files = OldCard(card);
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso"), OldIso);
        File.WriteAllBytes(Path.Combine(release, "LAUNCHER.BIN"), [1, 2, 3]);         // a release the installer rejects
        using var b = new TempCard();
        CardBackup.RootOverrideForTests = Path.Combine(b.Dir, "Backups");
        try
        {
            var e = Assert.Throws<InstallException>(() => CardPipeline.Run(card, new PipelineOptions(release), 20261125, null, "s"));
            Assert.Contains("moved back to /vibemenu", e.Message);
            Assert.Equal(CardFolderState.Old, CardFolderMigration.Detect(card));
            AssertFolder(Path.Combine(card, "vibemenu"), files);
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void Rollback_refuses_an_unfinished_backup_another_cards_backup_and_a_card_with_no_old_menu()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        OldCard(card);
        var r = CardFolderMigration.Migrate(card, null, "CARD-A", Now, backupRoot: b.Dir);
        // no VibeMenu autoboot.iso anywhere: going back would leave a menu that cannot read /vibemenu
        Assert.Contains("no VibeMenu autoboot", Assert.Throws<InstallException>(() => CardFolderMigration.Rollback(card, r.BackupDir!, null)).Message);
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso.bak-1"), OldIso);
        Assert.Contains("another card", Assert.Throws<InstallException>(() => CardFolderMigration.Rollback(card, r.BackupDir!, null, "CARD-B")).Message);
        File.Delete(Path.Combine(r.BackupDir!, "migration.log"));
        Assert.Contains("not a completed", Assert.Throws<InstallException>(() => CardFolderMigration.Rollback(card, r.BackupDir!, null)).Message);
        Assert.Equal(CardFolderState.New, CardFolderMigration.Detect(card));               // nothing changed by any of them
    }

    [Fact]
    public void A_file_appearing_in_the_moved_folder_is_kept_and_not_a_failure()
    {
        using var t = new TempCard();
        using var b = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        OldCard(card);
        CardFolderMigration.AfterMove = () => File.WriteAllText(Path.Combine(card, "aether", "Thumbs.db"), "explorer");
        try { Assert.True(CardFolderMigration.Migrate(card, null, "C", Now, backupRoot: b.Dir).Moved); }
        finally { CardFolderMigration.AfterMove = null; }
        Assert.Equal(CardFolderState.New, CardFolderMigration.Detect(card));
    }

    [Fact]
    public void An_old_vibemenu_release_is_never_installed_and_the_card_is_not_touched()
    {
        using var t = new TempCard();
        t.Realize(JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card"), release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var files = OldCard(card);
        File.WriteAllBytes(Path.Combine(release, "autoboot.iso"), OldIso);           // a stale 0.1 VibeMenu release
        using var b = new TempCard();
        CardBackup.RootOverrideForTests = Path.Combine(b.Dir, "Backups");
        try
        {
            Assert.Contains("not an Aether menu image", Assert.Throws<InstallException>(() => CardPipeline.Run(card, new PipelineOptions(release), 20261125, null, "s")).Message);
            AssertFolder(Path.Combine(card, "vibemenu"), files);
            Assert.False(Directory.Exists(Path.Combine(b.Dir, "Migrations")));
        }
        finally { CardBackup.RootOverrideForTests = null; }
    }

    [Fact]
    public void The_installer_never_writes_beside_an_old_folder()
    {
        using var t = new TempCard();
        string card = Path.Combine(t.Dir, "card");
        OldCard(card);
        var e = Assert.Throws<InstallException>(() => Installer.Install(card, new InstallSources("x", "y", "z", "w")));
        Assert.Contains("before the Aether rename", e.Message);
        Assert.False(Directory.Exists(Path.Combine(card, "aether")));
    }

    [Fact]
    public void A_pre_rename_snapshot_reads_as_the_aether_folder()
    {
        Assert.Equal("/aether/SAVES/T-1/X.BUP", CardBackup.Renamed("/vibemenu/SAVES/T-1/X.BUP"));
        Assert.Equal("Games/A/BOX.TGA", CardBackup.Renamed("Games/A/BOX.TGA"));
        using var t = new TempCard();
        string base_ = t.Dir;
        Directory.CreateDirectory(Path.Combine(base_, "VibeMenuCardManager", "Backups"));
        Assert.Equal(Path.Combine(base_, "AetherCardManager"), AppDataDir.Resolve(base_));
        Assert.True(Directory.Exists(Path.Combine(base_, "AetherCardManager", "Backups")));      // moved, snapshots kept
        Assert.False(Directory.Exists(Path.Combine(base_, "VibeMenuCardManager")));
    }
}
