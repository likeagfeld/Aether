// VibeMenu Card Manager, GPL-3.0-or-later.
// "Prepare SD Card for VibeMenu" ("No Build card setup"): the one big button's states --
// not installed, the bundled version already installed, an older version installed -- and the install path itself,
// on temp-dir cards only.
using VibeMenuCardManager.Core.About;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class PrepareCardTests
{
    /// <summary>Every window here gets an empty removable-drive list: otherwise a real
    /// removable drive actually plugged into the machine running the test would auto-select and open itself
    /// (RefreshDriveList -> OnDrivePicked -> OpenCard), which a test that never calls OpenCard explicitly has no
    /// way to expect or guard against.</summary>
    static MainWindow Window(string dir, bool answer = true)
    {
        var w = new MainWindow(removableDriveListForTests: () => []) { SystemVolumes = [], Ask = (_, _, _) => Task.FromResult(answer) };
        w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    /// <summary>A release folder holding the card's own menu pair and GAMES.DB, so "already installed" is judged
    /// against a fixture, never the machine's bundled or assembled release (which would make the card's pair stale).</summary>
    internal static string MatchingRelease(string card, string dir)
    {
        Directory.CreateDirectory(dir);
        string lnc = Path.Combine(card, "aether", "LAUNCHER.BIN");
        if (!File.Exists(lnc)) File.WriteAllBytes(lnc, [0x4C, 0x4E, 0x43, 0x48]);
        File.Copy(Path.Combine(card, "autoboot.iso"), Path.Combine(dir, "autoboot.iso"), true);
        File.Copy(lnc, Path.Combine(dir, "LAUNCHER.BIN"), true);
        File.Copy(Path.Combine(card, "aether", "GAMES.DB"), Path.Combine(dir, "GAMES.DB"), true);
        return dir;
    }

    static byte[] AutobootWithBuild(string build) =>
        [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU", ["product"] = "OMSAT-MENU" }), .. new byte[2048], .. System.Text.Encoding.ASCII.GetBytes(build + "\0")];

    [Fact]
    public async Task No_card_hides_the_button()
    {
        bool visible = await HeadlessApp.Run(() =>
        {
            var w = new MainWindow(removableDriveListForTests: () => []);
            return Task.FromResult(w.PrepareCardButton.IsVisible);
        });
        Assert.False(visible);
    }

    [Fact]
    public async Task A_card_with_no_autoboot_shows_the_enabled_prepare_button()
    {
        using var card = new TempCard();
        Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        var (visible, enabled, text) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult((w.PrepareCardButton.IsVisible, w.PrepareCardButton.IsEnabled, w.PrepareCardButton.Content as string));
        });
        Assert.True(visible);
        Assert.True(enabled);
        Assert.Equal("Prepare SD Card for Aether", text);
    }

    [Fact]
    public async Task No_GAMES_DB_at_all_still_shows_the_enabled_prepare_button()
    {
        // CardEditSession.Load's Problem state (no /vibemenu/GAMES.DB) IS "VibeMenu not installed": the button must
        // still offer to prepare the card, not just sit hidden because Load.Problem != null.
        using var card = new TempCard();
        var (visible, enabled) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult((w.PrepareCardButton.IsVisible, w.PrepareCardButton.IsEnabled));
        });
        Assert.True(visible);
        Assert.True(enabled);
    }

    [Fact]
    public async Task The_bundled_version_already_installed_disables_the_button()
    {
        using var card = new TempCard();
        Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        File.WriteAllBytes(Path.Combine(card.Dir, "autoboot.iso"), AutobootWithBuild($"VibeMenu {Credits.Version} abc1234"));
        using var rel = new TempCard();
        string release = MatchingRelease(card.Dir, rel.Dir);
        var (enabled, text) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.ReleaseOverrideForTests = release;
            w.OpenCard(card.Dir);
            return Task.FromResult((w.PrepareCardButton.IsEnabled, w.PrepareCardButton.Content as string));
        });
        Assert.False(enabled);
        Assert.Equal($"Aether {Credits.Version} installed", text);
    }

    [Fact]
    public async Task An_older_installed_version_offers_to_update()
    {
        using var card = new TempCard();
        Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        File.WriteAllBytes(Path.Combine(card.Dir, "autoboot.iso"), AutobootWithBuild("VibeMenu 0.0 abc1234"));
        var (enabled, text) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult((w.PrepareCardButton.IsEnabled, w.PrepareCardButton.Content as string));
        });
        Assert.True(enabled);
        Assert.Equal($"Update card to {Credits.Version}", text);
    }

    [Fact]
    public async Task Clicking_prepare_installs_the_bundled_release_and_builds_the_card_data()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"),
            SystemIdReader.Make(new Dictionary<string, string> { ["product"] = "T-00001", ["area"] = "U", ["title"] = "ALPHA GAME", ["device"] = "CD-1/1" }));
        string release = Card.ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var asked = new List<string>();

        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(root, true);
            w.Ask = (_, _, text) => { asked.Add(text); return Task.FromResult(true); };
            w.ReleaseOverrideForTests = release;
            w.OpenCard(root);
            Assert.True(w.PrepareCardButton.IsVisible && w.PrepareCardButton.IsEnabled);
            await w.PrepareCard();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Built and installed:", status);
        Assert.True(File.Exists(Path.Combine(root, "autoboot.iso")));
        Assert.True(File.Exists(Path.Combine(root, "aether", "GAMES.DB")));
        Assert.Contains(asked, a => a.Contains("autoboot.iso") && a.Contains("menu.bin is never touched"));

        // ReleaseFixture's autoboot.iso carries a stand-in build string ("VibeMenu 0.1 test", no real commit hash),
        // so this only re-confirms the button still reacts to whatever landed on the card, not a specific state --
        // The_bundled_version_already_installed_disables_the_button covers the real version-match case.
        bool stillVisible = await HeadlessApp.Run(() =>
        {
            var w = Window(root);
            w.OpenCard(root);
            return Task.FromResult(w.PrepareCardButton.IsVisible);
        });
        Assert.True(stillVisible);
    }
    // ---- games copied onto (or deleted from) a card that already has this Aether, in Explorer ----

    static void Game(string root, string rel, string product) =>
        WriteImage(Path.Combine(root, rel), SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = "U", ["title"] = product, ["device"] = "CD-1/1" }));

    static void WriteImage(string path, byte[] data) { Directory.CreateDirectory(Path.GetDirectoryName(path)!); File.WriteAllBytes(path, data); }

    /// <summary>A card with this Aether installed and its LIBRARY.IDX built from the card as it stands now (the
    /// scanner's own output, which is what CardBuilder writes).</summary>
    static string InstalledCardWithList(string root, string releaseDir)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        File.WriteAllBytes(Path.Combine(root, "autoboot.iso"), AutobootWithBuild($"Aether {Credits.Version} abc1234"));
        ListAsBuilt(root);
        return MatchingRelease(root, releaseDir);
    }

    /// <summary>The card's LIBRARY.IDX as a build would write it for the games on it now (the scanner's own output,
    /// which CardBuilder writes), so an "installed" fixture's games match its list.</summary>
    internal static void ListAsBuilt(string root)
    {
        var res = VibeMenuCardManager.Core.Card.CardScanner.Scan(new VibeMenuCardManager.Core.Card.FsSource(root), [], new Dictionary<string, string>(), 20261124);
        File.WriteAllBytes(Path.Combine(root, "aether", "LIBRARY.IDX"), res.Data);
    }

    static Task<(bool Enabled, string? Text, string? Tip)> ButtonAfterOpen(string root, string release) => HeadlessApp.Run(() =>
    {
        var w = Window(root);
        w.ReleaseOverrideForTests = release;
        w.OpenCard(root);
        return Task.FromResult((w.PrepareCardButton.IsEnabled, w.PrepareCardButton.Content as string, Avalonia.Controls.ToolTip.GetTip(w.PrepareCardButton) as string));
    });

    [Fact]
    public async Task A_game_added_inside_an_existing_folder_offers_to_update_the_card()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        string release = InstalledCardWithList(root, Path.Combine(t.Dir, "release"));
        Game(root, "Games/Sub/Beta/Beta.iso", "T-00002");   // copied in Explorer after the list was built
        Game(root, "Games/Sub/Gamma/Gamma.iso", "T-00003");

        var (enabled, text, tip) = await ButtonAfterOpen(root, release);
        Assert.True(enabled);
        Assert.Equal($"Update card to {Credits.Version}", text);
        Assert.Equal("2 new games on the card: Update card adds them to Aether's list with their covers", tip);
    }

    [Fact]
    public async Task A_removed_game_offers_to_update_the_card()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        Game(root, "Games/Sub/Beta/Beta.iso", "T-00002");
        string release = InstalledCardWithList(root, Path.Combine(t.Dir, "release"));
        Directory.Delete(Path.Combine(root, "Games", "Sub", "Beta"), recursive: true);

        var (enabled, text, tip) = await ButtonAfterOpen(root, release);
        Assert.True(enabled);
        Assert.Equal($"Update card to {Credits.Version}", text);
        Assert.Equal("1 game removed from the card: Update card takes it off Aether's list", tip);
    }

    [Fact]
    public async Task A_moved_game_counts_as_one_new_and_one_removed()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        Game(root, "Games/Sub/Beta/Beta.iso", "T-00002");
        string release = InstalledCardWithList(root, Path.Combine(t.Dir, "release"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Other"));
        Directory.Move(Path.Combine(root, "Games", "Sub", "Beta"), Path.Combine(root, "Games", "Other", "Beta"));

        var (enabled, _, tip) = await ButtonAfterOpen(root, release);
        Assert.True(enabled);
        Assert.Equal("1 new game and 1 removed game on the card: Update card brings Aether's list up to date, with covers for the new one", tip);
    }

    [Fact]
    public async Task An_unchanged_card_stays_installed()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        Game(root, "Games/Beta.iso", "T-00002");
        string release = InstalledCardWithList(root, Path.Combine(t.Dir, "release"));

        var (enabled, text, _) = await ButtonAfterOpen(root, release);
        Assert.False(enabled);
        Assert.Equal($"Aether {Credits.Version} installed", text);
    }

    [Fact]
    public async Task A_missing_game_list_offers_to_update_the_card()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        string release = InstalledCardWithList(root, Path.Combine(t.Dir, "release"));
        File.Delete(Path.Combine(root, "aether", "LIBRARY.IDX"));

        var (enabled, _, tip) = await ButtonAfterOpen(root, release);
        Assert.True(enabled);
        Assert.Equal("Aether's game list on this card (LIBRARY.IDX) is missing or unreadable: Update card rebuilds it with covers for the 1 game on the card", tip);
    }

    [Fact]
    public async Task After_update_card_a_new_game_no_longer_asks_for_an_update()
    {
        using var t = new TempCard();
        string root = Path.Combine(t.Dir, "card");
        Game(root, "Games/Sub/Alpha/Alpha.iso", "T-00001");
        string release = Card.ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        // the fixture's stand-in build string has no commit, so it would not read as installed: a real-shaped one
        File.WriteAllBytes(Path.Combine(release, "autoboot.iso"),
            [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "AETHER", ["product"] = "OMSAT-MENU" }), .. new byte[2048],
             .. System.Text.Encoding.ASCII.GetBytes($"Aether {Credits.Version} abc1234 ")]);

        // first Prepare (the real pipeline), then a game copied into the same folder in Explorer
        string first = await HeadlessApp.Run(async () =>
        {
            var w = Window(root);
            w.ReleaseOverrideForTests = release;
            w.OpenCard(root);
            await w.PrepareCard();
            return w.Status.Text ?? "";
        });
        Assert.True(first.StartsWith("Built and installed:"), first);
        Game(root, "Games/Sub/Beta/Beta.iso", "T-00002");

        var (beforeTip, afterTip, afterText, afterEnabled) = await HeadlessApp.Run(async () =>
        {
            var w = Window(root);
            w.ReleaseOverrideForTests = release;
            w.OpenCard(root);
            string? before = Avalonia.Controls.ToolTip.GetTip(w.PrepareCardButton) as string;
            await w.PrepareCard();   // Update card: runs the build and reopens the card
            return (before, Avalonia.Controls.ToolTip.GetTip(w.PrepareCardButton) as string, w.PrepareCardButton.Content as string, w.PrepareCardButton.IsEnabled);
        });
        Assert.Equal("1 new game on the card: Update card adds it to Aether's list with its cover", beforeTip);
        Assert.DoesNotContain("new game", afterTip ?? "");
        Assert.Null(VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(VibeMenuCardManager.Core.Card.CardEditSession.Load(root, 20261124)));
        Assert.Equal($"Aether {Credits.Version} installed", afterText);
        Assert.False(afterEnabled);
    }
    [Fact]
    public void Library_drift_counts_a_multi_disc_set_once_and_compares_paths_exactly()
    {
        static VibeMenuCardManager.Core.Card.GameRow Row(string p, int discs = 1) => new() { Path = p, OriginalPath = p, DiscTotal = discs };
        static Dictionary<string, (int, string)> Lib(params string[] paths) => paths.ToDictionary(p => p, p => (p.Contains("/Set/") ? 2 : 1, ""));
        var load = new VibeMenuCardManager.Core.Card.CardLoad
        {
            Root = "x",
            Rows = [Row("Games/A.iso"), Row("Games/Set/D1.cue", 2), Row("Games/Set/D2.cue", 2)],
            LibraryPaths = Lib("Games/A.iso"),
        };
        Assert.Equal("1 new game on the card: Update card adds it to Aether's list with its cover", VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(load));
        load.LibraryPaths = Lib("Games/a.iso", "Games/Set/D1.cue", "Games/Set/D2.cue");   // a case rename is a change
        Assert.Equal("1 new game and 1 removed game on the card: Update card brings Aether's list up to date, with covers for the new one",
                     VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(load));
        load.LibraryPaths = Lib("Games/A.iso", "Games/Set/D1.cue", "Games/Set/D2.cue");
        Assert.Null(VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(load));
        load.LibraryPaths = Lib("Games/A.iso", "Games/B.iso", "Games/C.iso", "Games/Set/D1.cue", "Games/Set/D2.cue");   // two loose games deleted from one folder
        Assert.Equal("2 games removed from the card: Update card takes them off Aether's list", VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(load));
        load.Rows = []; load.LibraryPaths = null;
        Assert.Null(VibeMenuCardManager.Core.Card.CardEditSession.LibraryDrift(load));   // no games, no list: nothing to do
    }
}
