// VibeMenu Card Manager, GPL-3.0-or-later.
// The user's card (2026-11-24): "Add to database…" for a disc GAMES.DB does not know, saved into GAMES.DB and
// LIBRARY.IDX; "No cover" only when the pack has nothing either, the pack's region-fallback cover named as such; the
// "N cheats on" pill; an installed card whose LAUNCHER.BIN is not its menu's pair offered the update.
using Avalonia.Controls;
using Avalonia.VisualTree;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Art;
using VibeMenuCardManager.Tests.Card;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class DatabaseEntryAndPillsTests
{
    static MainWindow Window(string dir)
    {
        var w = new MainWindow(removableDriveListForTests: () => []) { SystemVolumes = [], Ask = (_, _, _) => Task.FromResult(true) };
        w.AllowFixedVolumeForTests(VolumeResolver.Resolve(dir).Id!);
        w.SetCoverPackForTests(null);
        return w;
    }

    static byte[] Iso(string product, string title) =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = "U", ["title"] = title, ["device"] = "CD-1/1" });

    /// <summary>Alpha (in GAMES.DB, no cover), Homebrew (code "T-81", not in GAMES.DB), Beta (in GAMES.DB, BOX.TGA).</summary>
    static void Card(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(
            [new GameRecord { Key = "T-00001", TitleEn = "Alpha Game" }, new GameRecord { Key = "T-00002", TitleEn = "Beta Game" }],
            [], new Dictionary<string, string>(), 20261124));
        foreach (var (f, p, t) in new[] { ("Alpha", "T-00001", "ALPHA"), ("Homebrew", "T-81", "HOMEBREW"), ("Beta", "T-00002", "BETA") })
        {
            Directory.CreateDirectory(Path.Combine(root, "Games", f));
            File.WriteAllBytes(Path.Combine(root, "Games", f, $"{f}.iso"), Iso(p, t));
        }
        File.WriteAllBytes(Path.Combine(root, "Games", "Beta", "BOX.TGA"), Core.Art.TgaEncoder.Encode(new Core.Art.RgbaImage(4, 6, new byte[4 * 6 * 4])));
    }

    static List<string> Pills(MainWindow w, GameRow row)
    {
        var item = w.GameList.ContainerFromItem(row) as ListBoxItem;
        return item?.GetVisualDescendants().OfType<TextBlock>().Select(t => t.Text ?? "").ToList() ?? [];
    }

    [Fact]
    public async Task A_game_not_in_the_database_is_added_and_saved_where_the_menu_reads_it()
    {
        using var card = new TempCard();
        Card(card.Dir);
        var (before, after, key) = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Folder == "Games/Homebrew");
            w.GameList.SelectedItem = row;
            string b = w.DatabaseEntryButton.Content as string ?? "";
            w.DatabaseEntryPrompt = (_, _, start) => Task.FromResult<UserEditRecord?>(new UserEditRecord { Title = "My Homebrew", Year = 2025, Genre = start.Genre });
            await w.EditDatabaseEntry();
            string a = w.DatabaseEntryButton.Content as string ?? "";
            string k = row.Key;
            Assert.DoesNotContain(row, w.LibraryItemsForTests.OfType<GameRow>().Where(r => r.Key == ""));
            await w.SaveChanges();
            w.Close();
            return (b, a, k);
        });
        Assert.Equal("Add to database…", before);
        Assert.Equal("Edit database entry…", after);
        Assert.Matches("^U[0-9A-F]{8}$", key);                                           // "T-81" is a placeholder code
        var rec = GamesDb.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"))).Records.Single(r => r.Key == key);
        Assert.Equal(("My Homebrew", true, 2025), (rec.TitleEn, rec.IsUserTitle, rec.Year));
        Assert.Equal(key, LibraryIndex.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "LIBRARY.IDX")))
                              .Entries.Single(e => e.Path == "Games/Homebrew/Homebrew.iso").Key);
        Assert.Contains("new=1", File.ReadAllText(Path.Combine(card.Dir, "aether", "USEREDIT.INI")));
    }

    [Fact]
    public async Task No_cover_only_when_the_pack_has_none_a_fallback_is_named_and_cheats_on_show()
    {
        using var card = new TempCard();
        Card(card.Dir);
        var shipped = CheatPak.Read(File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak")));
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "CHEATS.PAK"), CheatPak.Write(new Dictionary<string, CheatRecord> { ["T-00002"] = shipped["GS-9001"] }));
        var pack = Core.Art.CoverPack.Read(CoverPackFixture.Build([new("T-00001", "coverproject", "tga", Core.Art.TgaEncoder.Encode(new Core.Art.RgbaImage(8, 12, new byte[8 * 12 * 4])), 8, 12, FallbackRegion: "J", FallbackFrom: "T-00001J")]));
        var (alpha, beta, noCoverCount, tag) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.SetCoverPackForTests(pack);
            w.Show();
            w.OpenCard(card.Dir);
            w.UpdateLayout();
            var a = w.Load!.Rows.Single(r => r.Key == "T-00001");
            var b = w.Load.Rows.Single(r => r.Key == "T-00002");
            int n = (w.ViewsList.ItemsSource as IEnumerable<object>)!.OfType<MainWindow.NavViewItem>().Single(v => v.Kind == MainWindow.LibraryView.NoCover).Count;
            w.GameList.SelectedItem = a;
            string t = w.CoverSourceTag.Text ?? "";
            var res = (Pills(w, a), Pills(w, b), n, t);
            w.Close();
            return Task.FromResult(res);
        });
        Assert.Contains("JP cover", alpha);                 // the pack's Japanese cover for a US game: named, not "No cover"
        Assert.DoesNotContain("No cover", alpha);
        Assert.Equal("JP COVER", tag);
        Assert.Contains("2 cheats on", beta);
        Assert.Contains("Your cover", beta);
        Assert.Equal(1, noCoverCount);                      // only Homebrew: not in the pack, no BOX.TGA
    }

    [Fact]
    public async Task Save_changes_puts_the_packs_cover_on_the_card_for_a_game_without_one()
    {
        using var card = new TempCard();
        Card(card.Dir);
        var pack = Core.Art.CoverPack.Read(CoverPackFixture.Build([new("T-00001", "coverproject", "tga", Core.Art.TgaEncoder.Encode(new Core.Art.RgbaImage(8, 12, new byte[8 * 12 * 4])), 8, 12)]));
        await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.SetCoverPackForTests(pack);
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == "T-00002").Favorite = true;      // any staged edit, so there is a save
            await w.SaveChanges();
            w.Close();
            return 0;
        });
        var art = ArtPak.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "ART.PAK")));
        Assert.True(Core.Art.CoverLookup.HasCover(art, "T-00001", 1));
        Assert.True(CardEditSession.Load(card.Dir, 20261124).Rows.Single(r => r.Key == "T-00001").HasArt);
    }

    [Fact]
    public async Task An_installed_card_whose_launcher_is_not_its_menus_pair_is_offered_the_update()
    {
        using var card = new TempCard();
        Card(card.Dir);
        string release = ReleaseFixture.Make(Path.Combine(card.Dir, "..", Path.GetFileName(card.Dir) + "-rel"));
        try
        {
            // a real build string ("VibeMenu 0.1 <commit>"), as the menu's own image carries
            string iso = Path.Combine(release, "autoboot.iso");
            File.WriteAllBytes(iso, [.. File.ReadAllBytes(iso).SkipLast(18), .. "VibeMenu 0.1 abc1234\0"u8.ToArray()]);
            File.Copy(iso, Path.Combine(card.Dir, "autoboot.iso"));
            File.Copy(Path.Combine(release, "GAMES.DB"), Path.Combine(card.Dir, "aether", "GAMES.DB"), overwrite: true);
            File.WriteAllText(Path.Combine(card.Dir, "aether", "LAUNCHER.BIN"), "an older launcher");
            PrepareCardTests.ListAsBuilt(card.Dir);   // the game list matches the card's games: only the pair is stale
            var (stale, current) = await HeadlessApp.Run(() =>
            {
                var w = Window(card.Dir);
                w.ReleaseOverrideForTests = release;
                w.OpenCard(card.Dir);
                string s = $"{w.PrepareCardButton.Content}|{w.PrepareCardButton.IsEnabled}|{ToolTip.GetTip(w.PrepareCardButton)}";
                File.Copy(Path.Combine(release, "LAUNCHER.BIN"), Path.Combine(card.Dir, "aether", "LAUNCHER.BIN"), overwrite: true);
                w.OpenCard(card.Dir);                                             // File > Refresh: the card is read again
                string c = $"{w.PrepareCardButton.Content}|{w.PrepareCardButton.IsEnabled}";
                w.Close();
                return Task.FromResult((s, c));
            });
            Assert.StartsWith("Update card to 0.1|True|", stale);
            Assert.Contains("LAUNCHER.BIN", stale);
            Assert.StartsWith("Aether 0.1 installed|False", current);
        }
        finally { Directory.Delete(release, recursive: true); }
    }

    [Fact]
    public async Task Help_has_send_feedback()
    {
        var headers = await HeadlessApp.Run(() =>
        {
            var w = Window(Path.GetTempPath());
            var menu = (w.Content as Control)!.GetVisualDescendants().OfType<Menu>().FirstOrDefault()
                       ?? w.GetLogicalDescendantsMenu();
            var help = menu!.Items.OfType<MenuItem>().Single(m => (m.Header as string) == "_Help");
            w.Close();
            return Task.FromResult(help.Items.OfType<MenuItem>().Select(m => m.Header as string).ToList());
        });
        Assert.Contains("Send feedback…", headers);
    }
}

static class MenuFinder
{
    public static Menu? GetLogicalDescendantsMenu(this Window w) =>
        Avalonia.LogicalTree.LogicalExtensions.GetLogicalDescendants(w).OfType<Menu>().FirstOrDefault();
}
