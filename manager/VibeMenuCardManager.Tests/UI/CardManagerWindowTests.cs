// VibeMenu Card Manager, GPL-3.0-or-later.
// The real main window's own tests (M4 UI task): the virtualized game list on a 700-game synthetic card, staging and
// Save changes on temp-dir cards, the confirm and cancel paths, and that a write on a system drive is refused. Never
// the user's real card: every card here is a fresh temp directory (TempCard), and "system drive" means an injected
// volume id, never the machine's real C:.
using Avalonia.Controls;
using Avalonia.Threading;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Art;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class CardManagerWindowTests
{
    static MainWindow Window(string dir, bool answer = true, List<string>? asked = null, IReadOnlyList<string>? systemVolumes = null)
    {
        var w = new MainWindow(removableDriveListForTests: () => []) { SystemVolumes = systemVolumes ?? [], Ask = (_, title, text) => { asked?.Add(text); return Task.FromResult(answer); } };
        w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    static void EmptyGamesDb(string root, uint date = 20261123) =>
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), date));

    /// <summary>A plain Saturn System ID, exactly 256 bytes (the smallest file SystemIdReader.ReadImage reads as an
    /// .iso: no sync header needed for a 2048-byte-sector image).</summary>
    static byte[] MakeIso(string product, string title, string area = "U") =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = area, ["title"] = title, ["device"] = "CD-1/1" });

    static void MakeSyntheticCard(string root, int n)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        EmptyGamesDb(root);
        for (int i = 1; i <= n; i++)
        {
            string folder = Path.Combine(root, "Games", $"Game{i:0000}");
            Directory.CreateDirectory(folder);
            File.WriteAllBytes(Path.Combine(folder, "GAME.ISO"), MakeIso($"T-{i:00000}", $"SYNTHETIC GAME {i:0000}"));
        }
    }

    /// <summary>A small, realistic two-game card: GAMES.DB carries both keys, so Key/TitleEn resolve, and the games
    /// get usable PERGAME.INI ids from their System IDs.</summary>
    static (string Key1, string Key2) MakeSmallCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        string k1 = "T-00001", k2 = "T-00002";
        var db = new List<GameRecord>
        {
            new() { Key = k1, TitleEn = "Alpha Game", DiscCount = 1, Year = 1996 },
            new() { Key = k2, TitleEn = "Beta Game", DiscCount = 1, Year = 1997 },
        };
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261123));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Beta"));
        // Named Alpha.iso/Beta.iso, not a generic GAME.ISO (M-art-pack naming fix round: a game's default display
        // name is its own file name, not its folder's, so the fixture's file name is the one that must read "Alpha"/
        // "Beta" for the many assertions below that expect that default).
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"), MakeIso(k1, "ALPHA GAME", "U"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Beta", "Beta.iso"), MakeIso(k2, "BETA GAME", "U"));
        return (k1, k2);
    }

    [Fact]
    public async Task The_game_list_shows_every_game_on_a_700_game_card()
    {
        using var card = new TempCard();
        MakeSyntheticCard(card.Dir, 700);
        var (rowCount, lastRealized) = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            Assert.Equal("Opened " + card.Dir + ".", w.Status.Text);
            // "All games" is now the real recursive tree (consolidation round): the "Games" folder itself is one
            // more row (FolderListRow) alongside its 700 single-game-folder leaves, so this filters to the games.
            var items = ((System.Collections.Generic.List<object>)w.GameList.ItemsSource!).OfType<GameRow>().ToList();
            object? container = null;
            for (int i = 0; i < 20 && container == null; i++)
            {
                w.GameList.ScrollIntoView(items[^1]);
                Dispatcher.UIThread.RunJobs();
                await Task.Delay(20);
                Dispatcher.UIThread.RunJobs();
                container = w.GameList.ContainerFromItem(items[^1]);
            }
            return (items.Count, container != null);
        });
        Assert.Equal(700, rowCount);
        Assert.True(lastRealized, "the virtualized list must still be able to realize the last of 700 rows");
    }

    [Fact]
    public async Task Staged_edits_apply_on_save_changes_and_persist_on_disk()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        var asked = new List<string>();
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true, asked);
            w.Show();
            w.OpenCard(card.Dir);
            var alpha = w.Load!.Rows.Single(r => r.Key == k1);
            var beta = w.Load!.Rows.Single(r => r.Key == k2);
            alpha.TitleOverride = "My Alpha";
            alpha.Loader = "cwx";
            beta.Favorite = true;
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status);
        Assert.Contains("GAMES.DB, LIBRARY.IDX, ART.PAK rebuilt", status);
        var text = Assert.Single(asked);
        Assert.Contains("1 title override(s)", text);
        Assert.Contains("1 favorite change(s)", text);
        Assert.Contains("1 loader override(s)", text);

        var lib = LibraryIndex.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "LIBRARY.IDX")));
        var alphaEntry = lib.Entries.Single(e => e.Key == k1);
        Assert.Equal("My Alpha", alphaEntry.TitleOverride);

        string favs = File.ReadAllText(Path.Combine(card.Dir, "aether", "FAVS.TXT"));
        Assert.Contains("Games/Beta/Beta.iso", favs.Replace('\\', '/'));

        var sid = SystemIdReader.ReadImage(Path.Combine(card.Dir, "Games", "Alpha", "Alpha.iso"))!;
        string gid = VibeMenuCardManager.Core.Saves.GameId.Ini(sid.ProductField, sid.AreaField)!;
        string ini = File.ReadAllText(Path.Combine(card.Dir, "aether", "PERGAME.INI"));
        Assert.Contains($"[{gid}]", ini);
        Assert.Contains("loader=cwx", ini);
    }

    [Fact]
    public async Task A_declined_confirmation_writes_nothing()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var before = card.Manifest();
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, false);
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).TitleOverride = "Changed";
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.EndsWith("cancelled, nothing was written", status);
        Assert.Equal(before, card.Manifest());
    }

    [Fact]
    public async Task Nothing_to_save_when_there_are_no_staged_edits()
    {
        using var card = new TempCard();
        MakeSmallCard(card.Dir);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.Equal("Nothing to save.", status);
    }

    [Fact]
    public async Task Cancelling_mid_save_undoes_every_staged_filesystem_edit()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        var before = card.Manifest();
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).TitleOverride = "Changed";
            w.Load!.Rows.Single(r => r.Key == k2).PendingCover = Solid(4, 4, 1, 2, 3);
            w.ProgressHook = l => { if (l.StartsWith("COVER:", StringComparison.Ordinal)) w.CancelImport(); };
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Cancelled", status);
        Assert.Equal(before, card.Manifest());
    }

    [Fact]
    public async Task A_system_drive_is_refused_without_asking_and_writes_nothing()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var before = card.Manifest();
        var asked = new List<string>();
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true, asked, [VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(card.Dir).Id!]);
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).Favorite = true;
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.Contains("is on the system drive", status);
        Assert.Empty(asked);
        Assert.Equal(before, card.Manifest());
    }

    [Fact]
    public async Task Opening_a_card_with_no_GAMES_DB_is_read_only_and_safe()
    {
        using var card = new TempCard();
        string status = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult(w.Status.Text ?? "");
        });
        Assert.Contains("no /aether/GAMES.DB", status);
    }

    static RgbaImage Solid(int w, int h, byte r, byte g, byte b)
    {
        var px = new byte[w * h * 4];
        for (int i = 0; i < w * h; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
        return new RgbaImage(w, h, px);
    }

    // ===== coordinator fix round: ART.PAK covers, theme, icon, menus, drag-and-drop, 3-way close, folder art =====

    [Fact]
    public async Task A_game_with_only_ART_PAK_art_is_not_flagged_no_cover_but_one_with_neither_still_is()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        var (sq, hero, _) = CoverEncoder.Cover(Solid(64, 64, 10, 20, 30));
        byte[] pak = ArtPak.Write([new ArtEntry(k1, 1, sq, null, hero, null, 1)], 20261123);
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "ART.PAK"), pak);
        var (problem1, problem2, counts) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var r1 = w.Load!.Rows.Single(r => r.Key == k1);
            var r2 = w.Load!.Rows.Single(r => r.Key == k2);
            return Task.FromResult((r1.Problem, r2.Problem, w.CountsText.Text ?? ""));
        });
        Assert.Null(problem1);
        Assert.Equal("no cover", problem2);
        Assert.Contains("1 covers", counts);
    }

    [Fact]
    public async Task A_game_with_only_BOX_TGA_is_not_flagged_no_cover()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        File.WriteAllBytes(Path.Combine(card.Dir, "Games", "Alpha", "BOX.TGA"), TgaEncoder.Encode(Solid(32, 32, 1, 2, 3)));
        string? problem = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult(w.Load!.Rows.Single(r => r.Key == k1).Problem);
        });
        Assert.Null(problem);
    }

    [Fact]
    public async Task The_app_starts_in_the_dark_theme_by_default()
    {
        var variant = await HeadlessApp.Run(() => Task.FromResult(Avalonia.Application.Current?.RequestedThemeVariant));
        Assert.Equal(Avalonia.Styling.ThemeVariant.Dark, variant);
    }

    [Fact]
    public async Task The_window_and_taskbar_icon_load_from_the_saturn_logo()
    {
        bool loaded = await HeadlessApp.Run(() =>
        {
            var w = new MainWindow(removableDriveListForTests: () => []);
            return Task.FromResult(MainWindow.IconLoaded && w.Icon != null);
        });
        Assert.True(loaded);
    }

    [Fact]
    public async Task Opening_or_refreshing_the_card_picks_up_a_game_added_in_Explorer()
    {
        // File management fix round: this app never adds/removes/moves a game file or folder itself -- that stays
        // in Explorer (Satiator Rings parity) -- but OpenCard()/Refresh must rescan so a game dropped straight onto
        // the card by Explorer, with nothing done through this app, shows up.
        using var card = new TempCard();
        MakeSmallCard(card.Dir);
        int before = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult(w.Load!.Rows.Count);
        });
        Assert.Equal(2, before);

        string newFolder = Path.Combine(card.Dir, "Games", "Gamma");
        Directory.CreateDirectory(newFolder);
        File.WriteAllBytes(Path.Combine(newFolder, "Gamma.iso"), MakeIso("T-00009", "GAMMA GAME"));

        int after = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);   // Refresh does the same: OpenCard(CardPath.Text)
            return Task.FromResult(w.Load!.Rows.Count(r => r.Path.Replace('\\', '/') == "Games/Gamma/Gamma.iso"));
        });
        Assert.Equal(1, after);
    }

    [Fact]
    public async Task Closing_with_unsaved_changes_offers_save_discard_or_cancel_and_discard_writes_nothing()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var before = card.Manifest();
        var (stillOpenAfterCancel, closedAfterDiscard, manifestAfter) = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).Favorite = true;
            bool closed = false;
            w.Closed += (_, _) => closed = true;

            w.CloseAsk = _ => Task.FromResult(MainWindow.CloseDecision.Cancel);
            w.Close();
            await Task.Delay(30);
            Dispatcher.UIThread.RunJobs();
            bool stillOpen = !closed;

            w.CloseAsk = _ => Task.FromResult(MainWindow.CloseDecision.Discard);
            w.Close();
            await Task.Delay(30);
            Dispatcher.UIThread.RunJobs();

            return (stillOpen, closed, card.Manifest());
        });
        Assert.True(stillOpenAfterCancel);
        Assert.True(closedAfterDiscard);
        Assert.Equal(before, manifestAfter);
    }

    [Fact]
    public async Task Closing_with_unsaved_changes_can_save_through_the_write_confirmation_then_close()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var asked = new List<string>();
        bool closed = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true, asked);
            w.Show();
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).Favorite = true;
            bool c = false;
            w.Closed += (_, _) => c = true;
            w.CloseAsk = _ => Task.FromResult(MainWindow.CloseDecision.Save);
            w.Close();
            // Final review minor: a generous, wall-clock bound (not a fixed iteration count tied to one specific
            // delay) -- a 1 s/50-iteration budget was load-sensitive (CI once ran this alongside a parallel Docker
            // build and missed it). This still returns as soon as `c` flips, so it is not slower in the common case.
            var deadline = DateTime.UtcNow.AddSeconds(10);
            while (!c && DateTime.UtcNow < deadline) { await Task.Delay(20); Dispatcher.UIThread.RunJobs(); }
            return c;
        });
        Assert.True(closed);
        Assert.Single(asked);
    }

    [Fact]
    public async Task Folder_art_change_replaces_any_existing_file_and_writes_FOLDER_PNG_on_save()
    {
        using var card = new TempCard();
        MakeSmallCard(card.Dir);
        string gamesDir = Path.Combine(card.Dir, "Games");
        File.WriteAllBytes(Path.Combine(gamesDir, "FOLDER.JPG"), [1, 2, 3, 4]);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var folder = w.Load!.Folders.Single(f => f.Path == "Games");
            folder.PendingCover = Solid(8, 8, 200, 200, 200);
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status);
        Assert.False(File.Exists(Path.Combine(gamesDir, "FOLDER.JPG")));
        Assert.True(File.Exists(Path.Combine(gamesDir, "FOLDER.PNG")));
    }

    [Fact]
    public async Task Remove_folder_art_deletes_the_existing_file_on_save()
    {
        using var card = new TempCard();
        MakeSmallCard(card.Dir);
        string gamesDir = Path.Combine(card.Dir, "Games");
        File.WriteAllBytes(Path.Combine(gamesDir, "FOLDER.PNG"), [1, 2, 3]);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var folder = w.Load!.Folders.Single(f => f.Path == "Games");
            folder.RemoveArtStaged = true;
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status);
        Assert.False(File.Exists(Path.Combine(gamesDir, "FOLDER.PNG")));
    }

    [Fact]
    public async Task Batch_rename_stages_the_chosen_source_on_every_selected_row_and_saves()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var rows = w.Load!.Rows.Where(r => r.Key == k1 || r.Key == k2).ToList();
            w.GameList.SelectedItems!.Clear();
            foreach (var r in rows) w.GameList.SelectedItems.Add(r);
            w.BatchRenamePrompt = (_, _) => Task.FromResult<(Core.Card.RenameSource, bool)?>((Core.Card.RenameSource.FolderName, true));
            await w.BatchRenameSelected();
            string s1 = w.Load.Rows.Single(r => r.Key == k1).TitleOverride;
            string s2 = w.Load.Rows.Single(r => r.Key == k2).TitleOverride;
            await w.SaveChanges();
            return $"{s1}|{s2}|{w.Status.Text}";
        });
        var parts = status.Split('|', 3);
        Assert.Equal("Alpha", parts[0]);
        Assert.Equal("Beta", parts[1]);
        Assert.StartsWith("Saved:", parts[2]);

        var lib = LibraryIndex.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "LIBRARY.IDX")));
        Assert.Equal("Alpha", lib.Entries.Single(e => e.Key == k1).TitleOverride);
        Assert.Equal("Beta", lib.Entries.Single(e => e.Key == k2).TitleOverride);
    }

    [Fact]
    public async Task Batch_rename_cancelled_stages_nothing()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        string? overrideAfter = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Key == k1);
            w.GameList.SelectedItems!.Add(row);
            w.BatchRenamePrompt = (_, _) => Task.FromResult<(Core.Card.RenameSource, bool)?>(null);
            await w.BatchRenameSelected();
            return row.TitleOverride;
        });
        Assert.Equal("", overrideAfter);
    }

    [Fact]
    public async Task Metadata_edits_stage_and_survive_a_rescan_and_a_GAMES_DB_replacement()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Key == k1);
            w.GameList.SelectedItem = row;
            w.MetaTitleBox.Text = "Alpha Renamed";
            w.DetailGenre.Text = "Platformer";
            w.DetailYear.Text = "2001";
            w.DetailRegion.Text = "JU";
            w.MetaPlayersBox.Text = "4";
            w.CommitMetaEdit();
            Assert.True(w.Load.UserEdits.ContainsKey(k1));
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status);

        // GAMES.DB itself shows the edit (what the menu reads).
        var db = GamesDb.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB")));
        var rec = db.Records.Single(r => r.Key == k1);
        Assert.Equal("Alpha Renamed", rec.TitleEn);
        Assert.Equal(2001, rec.Year);
        Assert.Equal(4, rec.Players);
        Assert.Contains("Platformer", db.Genres);

        // USEREDIT.INI persists it, and a fresh Load() (a rescan) picks it back up.
        Assert.True(File.Exists(Path.Combine(card.Dir, "aether", "USEREDIT.INI")));
        var reload = CardEditSession.Load(card.Dir, 20261123);
        Assert.Equal("Alpha Renamed", reload.UserEdits[k1].Title);

        // Simulate a newer release replacing GAMES.DB wholesale (Build card / Installer never touches USEREDIT.INI).
        var fresh = new List<GameRecord> { new() { Key = k1, TitleEn = "Alpha Game (from a newer release)", Year = 1996 } };
        File.WriteAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB"), GamesDb.Write(fresh, [], new Dictionary<string, string>(), 20261124));
        string status2 = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);                 // a rescan after the "DB update"
            Assert.Equal("Alpha Renamed", w.Load!.UserEdits[k1].Title);   // the override survived the replacement
            w.Load.Rows.Single(r => r.Key == k1).Favorite = true;        // force an actual Save changes
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status2);
        var rebuilt = GamesDb.Read(File.ReadAllBytes(Path.Combine(card.Dir, "aether", "GAMES.DB")));
        Assert.Equal("Alpha Renamed", rebuilt.Records.Single(r => r.Key == k1).TitleEn);   // re-applied, not the new release's title
    }

    [Fact]
    public async Task A_metadata_field_matching_the_DB_value_is_not_staged_as_an_override()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        bool staged = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Key == k1);
            w.GameList.SelectedItem = row;
            w.MetaTitleBox.Text = "Alpha Game";    // same as GAMES.DB already has
            w.CommitMetaEdit();
            return Task.FromResult(w.Load.UserEdits.ContainsKey(k1));
        });
        Assert.False(staged);
    }

    [Fact]
    public async Task Export_report_writes_DISCLIST_TXT_and_XLSX_with_one_row_per_game()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        string reportDir = Path.Combine(card.Dir, "report");
        Directory.CreateDirectory(reportDir);
        string status = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            w.WriteReport(reportDir);
            return Task.FromResult(w.Status.Text ?? "");
        });
        Assert.Contains("Wrote the report for 2 game(s)", status);
        string txt = File.ReadAllText(Path.Combine(reportDir, "DISCLIST.TXT"));
        // The display name rule: a dedicated game folder's own name, not the GAMES.DB title.
        Assert.Contains("Alpha", txt);
        Assert.Contains("Beta", txt);
        Assert.True(File.Exists(Path.Combine(reportDir, "DISCLIST.XLSX")));

        // Never touches the card itself.
        Assert.False(File.Exists(Path.Combine(card.Dir, "DISCLIST.TXT")));
        Assert.False(File.Exists(Path.Combine(card.Dir, "aether", "DISCLIST.TXT")));
    }

    [Fact]
    public async Task Save_changes_is_blocked_before_asking_when_a_target_file_is_read_only()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        string gdb = Path.Combine(card.Dir, "aether", "GAMES.DB");
        File.SetAttributes(gdb, FileAttributes.ReadOnly);
        var before = card.Manifest();
        var asked = new List<string>();
        try
        {
            string status = await HeadlessApp.Run(async () =>
            {
                var w = Window(card.Dir, true, asked);
                w.OpenCard(card.Dir);
                w.Load!.Rows.Single(r => r.Key == k1).Favorite = true;
                await w.SaveChanges();
                return w.Status.Text ?? "";
            });
            Assert.StartsWith("Save changes blocked:", status);
            Assert.Contains("GAMES.DB", status);
            Assert.Contains("read-only", status);
            Assert.Empty(asked);                       // blocked before the write confirmation was even shown
            Assert.Equal(before, card.Manifest());
        }
        finally { File.SetAttributes(gdb, FileAttributes.Normal); }
    }

    [Fact]
    public async Task Save_changes_is_blocked_when_there_is_not_enough_free_space()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var before = card.Manifest();
        var asked = new List<string>();
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true, asked);
            w.FreeBytesForTests = 0;
            w.OpenCard(card.Dir);
            w.Load!.Rows.Single(r => r.Key == k1).Favorite = true;
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Save changes blocked:", status);
        Assert.Contains("not enough free space", status);
        Assert.Empty(asked);
        Assert.Equal(before, card.Manifest());
    }

    [Fact]
    public async Task Undo_and_redo_step_back_and_forward_through_a_batch_rename_and_a_metadata_edit()
    {
        using var card = new TempCard();
        var (k1, _) = MakeSmallCard(card.Dir);
        var r = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(x => x.Key == k1);
            w.GameList.SelectedItem = row;

            w.BatchRenamePrompt = (_, _) => Task.FromResult<(Core.Card.RenameSource, bool)?>((Core.Card.RenameSource.FolderName, false));
            await w.BatchRenameSelected();
            string afterRename = row.TitleOverride;

            w.MetaTitleBox.Text = "Alpha Renamed";
            w.CommitMetaEdit();
            bool stagedAfterMeta = w.Load.UserEdits.ContainsKey(k1);
            bool canUndo = w.History.CanUndo;

            w.UndoLast();                                 // undoes the metadata edit only
            bool stagedAfterUndo1 = w.Load.UserEdits.ContainsKey(k1);
            string titleAfterUndo1 = row.TitleOverride;

            w.UndoLast();                                 // undoes the rename too
            string titleAfterUndo2 = row.TitleOverride;
            bool canUndoAfterBoth = w.History.CanUndo;

            w.RedoLast();                                 // redo the rename
            string titleAfterRedo1 = row.TitleOverride;
            w.RedoLast();                                 // redo the metadata edit
            bool stagedAfterRedo2 = w.Load.UserEdits.ContainsKey(k1);

            return (afterRename, stagedAfterMeta, canUndo, stagedAfterUndo1, titleAfterUndo1, titleAfterUndo2, canUndoAfterBoth, titleAfterRedo1, stagedAfterRedo2);
        });
        Assert.Equal("Alpha", r.afterRename);
        Assert.True(r.stagedAfterMeta);
        Assert.True(r.canUndo);
        Assert.False(r.stagedAfterUndo1);
        Assert.Equal("Alpha", r.titleAfterUndo1);
        Assert.Equal("", r.titleAfterUndo2);
        Assert.False(r.canUndoAfterBoth);
        Assert.Equal("Alpha", r.titleAfterRedo1);
        Assert.True(r.stagedAfterRedo2);
    }

    [Fact]
    public async Task A_new_change_after_undo_drops_the_redo_branch_and_the_Edit_menu_reflects_it()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        var (canRedoBeforeNewChange, canRedoAfter) = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var row1 = w.Load!.Rows.Single(x => x.Key == k1);
            var row2 = w.Load.Rows.Single(x => x.Key == k2);
            w.GameList.SelectedItem = row1;
            w.BatchRenamePrompt = (_, _) => Task.FromResult<(Core.Card.RenameSource, bool)?>((Core.Card.RenameSource.FolderName, false));
            await w.BatchRenameSelected();
            w.UndoLast();
            bool before = w.History.CanRedo;

            w.GameList.SelectedItem = row2;
            w.MetaTitleBox.Text = "Something else";
            w.CommitMetaEdit();
            return (before, w.History.CanRedo);
        });
        Assert.True(canRedoBeforeNewChange);
        Assert.False(canRedoAfter);
    }

    // ===== coordinator fix round 3: tree nesting, custom covers, names, folder art (diagnosed on the real card's
    // mirror, G:\SATIATOR BACKUP, read-only, never written) =====

    /// <summary>The real card's shape, scaled down: a flat single-image folder; a multi-disc game in its own
    /// per-disc subfolders (PhantasM); an image beside a subfolder, with its own BOX.TGA (Andretti); and a
    /// folder-art-only folder with no image at all.</summary>
    static void BuildRealShapeCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        var db = new List<GameRecord> { new() { Key = "T-00002", TitleEn = "PhantasM DB Title", DiscCount = 2 } };
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261124));

        string games = Path.Combine(root, "Games");
        Directory.CreateDirectory(Path.Combine(games, "AlphaGame"));
        File.WriteAllBytes(Path.Combine(games, "AlphaGame", "AlphaGame.iso"), MakeIso("T-00001", "SOME DISC TITLE"));

        // The real card's actual PhantasM shape: each disc is its own flat sibling folder (not nested under a
        // shared parent) -- "multi-disc subfolders" of Games.
        Directory.CreateDirectory(Path.Combine(games, "PhantasM (JP) (Disc 1)"));
        Directory.CreateDirectory(Path.Combine(games, "PhantasM (JP) (Disc 2)"));
        File.WriteAllBytes(Path.Combine(games, "PhantasM (JP) (Disc 1)", "PHANTASM_1.iso"), MakeIso("T-00002", "JOENGINE"));
        File.WriteAllBytes(Path.Combine(games, "PhantasM (JP) (Disc 2)", "PHANTASM_2.iso"), MakeIso("T-00002", "JOENGINE"));

        Directory.CreateDirectory(Path.Combine(games, "Andretti Racing (USA)", "Extra"));
        File.WriteAllBytes(Path.Combine(games, "Andretti Racing (USA)", "andretti.iso"), MakeIso("T-00003", "SOME DISC TITLE"));
        File.WriteAllBytes(Path.Combine(games, "Andretti Racing (USA)", "BOX.TGA"), TgaEncoder.Encode(new RgbaImage(4, 4, new byte[4 * 4 * 4])));

        Directory.CreateDirectory(Path.Combine(games, "FolderArtOnly"));
        File.WriteAllBytes(Path.Combine(games, "FolderArtOnly", "BOX.TGA"), TgaEncoder.Encode(new RgbaImage(4, 4, new byte[4 * 4 * 4])));

        // A genuine nested container chain (a folder is only ever a tree node, never a game, once it holds a
        // subfolder of its own) -- proves depth-first ordering across more than one level.
        Directory.CreateDirectory(Path.Combine(games, "Old Consoles", "Saturn", "GameA"));
        File.WriteAllBytes(Path.Combine(games, "Old Consoles", "Saturn", "GameA", "GameA.iso"), MakeIso("T-00004", "SOME DISC TITLE"));
    }

    [Fact]
    public async Task The_tree_mirrors_the_real_directory_structure_exactly()
    {
        using var card = new TempCard();
        BuildRealShapeCard(card.Dir);
        var items = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            return Task.FromResult(w.LibraryItemsForTests);
        });
        int FolderIndex(string path) => items.FindIndex(i => i is MainWindow.FolderListRow f && f.Folder.Path == path);
        int oldConsoles = FolderIndex("Games/Old Consoles");
        int saturn = FolderIndex("Games/Old Consoles/Saturn");
        Assert.True(oldConsoles >= 0 && saturn >= 0);
        // Saturn must come immediately after its real parent (not after every other top-level folder, as the bug
        // this fixes did: it rendered a flat, breadth-first list as if it were already depth-first, so a deeper
        // folder could land under an unrelated, earlier-numbered sibling), one indent level deeper.
        Assert.Equal(oldConsoles + 1, saturn);
        Assert.Equal(((MainWindow.FolderListRow)items[oldConsoles]).Depth + 1, ((MainWindow.FolderListRow)items[saturn]).Depth);
        // GameA is a single-image game folder CardScanner folds into its parent for LIBRARY.IDX purposes (unchanged:
        // not this fix round's concern); the tree gives it one merged node, acting as that game (2026-11-23,
        // Satiator Rings' model, item 9), never a separate "GameA" folder row plus a "GameA.iso" leaf.
        Assert.DoesNotContain(items, i => i is MainWindow.FolderListRow f && f.Folder.Path == "Games/Old Consoles/Saturn/GameA");
        Assert.Contains(items, i => i is GameRow g && g.Path.EndsWith("GameA.iso") && g.OwnFolder);

        int andretti = FolderIndex("Games/Andretti Racing (USA)");
        int extra = FolderIndex("Games/Andretti Racing (USA)/Extra");
        Assert.True(andretti >= 0 && extra >= 0);
        Assert.Equal(andretti + 1, extra);
        Assert.Equal(((MainWindow.FolderListRow)items[andretti]).Depth + 1, ((MainWindow.FolderListRow)items[extra]).Depth);
    }

    [Fact]
    public async Task Display_names_default_to_the_single_game_folders_name_or_the_exact_file_name_never_the_DB_title()
    {
        // 2026-11-23 (Satiator Rings' model, menu/src/title.c om_entry_name): a single-game folder's one image shows
        // the folder's own name; every other image shows its exact file name, extension included. Never the disc
        // header's own title, or a non-user GAMES.DB title.
        using var card = new TempCard();
        BuildRealShapeCard(card.Dir);
        var (alpha, disc1Title, andrettiTitle) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var db = w.Load!.Db.Records.ToDictionary(r => r.Key);
            string a = w.Load.Rows.Single(r => r.Path.EndsWith("AlphaGame.iso")).DisplayTitle(db, w.Load.UserEdits);
            string d1 = w.Load.Rows.Single(r => r.Path.EndsWith("PHANTASM_1.iso")).DisplayTitle(db, w.Load.UserEdits);
            string an = w.Load.Rows.Single(r => r.Path.EndsWith("andretti.iso")).DisplayTitle(db, w.Load.UserEdits);
            return Task.FromResult((a, d1, an));
        });
        Assert.Equal("AlphaGame", alpha);               // AlphaGame's one image, no subfolder: a single-game folder
        Assert.Equal("PhantasM (JP) (Disc 1)", disc1Title); // its own per-disc folder, one image: a single-game folder
        Assert.Equal("andretti.iso", andrettiTitle);     // Andretti Racing (USA) has a subfolder (Extra): a folder row,
                                                          // so its loose andretti.iso shows its own exact file name
    }

    [Fact]
    public async Task Rename_to_DB_title_and_a_USEREDIT_title_both_count_as_explicit_overrides()
    {
        using var card = new TempCard();
        BuildRealShapeCard(card.Dir);
        var (afterRenameToDb, afterUserEditTitle) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var disc1 = w.Load!.Rows.Single(r => r.Path.EndsWith("PHANTASM_1.iso"));
            w.GameList.SelectedItem = disc1;
            // Rename to DB title now stages an explicit TitleOverride (not a cleared one).
            w.RenameToDbTitle();
            string a = disc1.TitleOverride;

            // A plain USEREDIT.INI title (no inline rename) also shows, for every entry sharing that key.
            disc1.TitleOverride = "";
            var disc2 = w.Load.Rows.Single(r => r.Path.EndsWith("PHANTASM_2.iso"));
            w.Load.UserEdits["T-00002"] = new Core.Formats.UserEditRecord { Title = "User Edited Title" };
            var db = w.Load.Db.Records.ToDictionary(r => r.Key);
            string b = disc2.DisplayTitle(db, w.Load.UserEdits);
            return Task.FromResult((a, b));
        });
        Assert.Equal("PhantasM DB Title", afterRenameToDb);
        Assert.Equal("User Edited Title", afterUserEditTitle);
    }

    [Fact]
    public async Task Folder_art_falls_back_to_a_non_game_folders_own_BOX_TGA()
    {
        using var card = new TempCard();
        BuildRealShapeCard(card.Dir);
        bool hasArt = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            var item = w.LibraryItemsForTests.OfType<MainWindow.FolderListRow>().Single(f => f.Folder.Path == "Games/FolderArtOnly");
            w.GameList.SelectedItem = item;
            return Task.FromResult(w.CoverImage.Source != null);
        });
        Assert.True(hasArt);
    }

    // ===== Final review I1: a loose game (sharing its folder, or at the card root) must never get "Change cover" /
    // "Fill missing covers" writing its folder's or the root's shared BOX.TGA. =====

    /// <summary>A shared folder with two loose images (own BOX.TGA shared between them), a single-image own-folder
    /// game, and a loose image at the card root -- plus a root BOX.TGA.</summary>
    static void BuildLooseGameCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        var db = new List<GameRecord>
        {
            new() { Key = "T-80001", TitleEn = "Shared A" },
            new() { Key = "T-80002", TitleEn = "Shared B" },
            new() { Key = "T-80003", TitleEn = "Own Folder Game" },
            new() { Key = "T-80004", TitleEn = "Root Loose Game" },
        };
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(db, [], new Dictionary<string, string>(), 20261124));

        string games = Path.Combine(root, "Games");
        Directory.CreateDirectory(Path.Combine(games, "Shared"));
        File.WriteAllBytes(Path.Combine(games, "Shared", "A.iso"), MakeIso("T-80001", "SHARED A"));
        File.WriteAllBytes(Path.Combine(games, "Shared", "B.iso"), MakeIso("T-80002", "SHARED B"));
        File.WriteAllBytes(Path.Combine(games, "Shared", "BOX.TGA"), TgaEncoder.Encode(Solid(4, 4, 9, 8, 7)));

        Directory.CreateDirectory(Path.Combine(games, "OwnFolderGame"));
        File.WriteAllBytes(Path.Combine(games, "OwnFolderGame", "own.iso"), MakeIso("T-80003", "OWN FOLDER GAME"));

        File.WriteAllBytes(Path.Combine(root, "Root.iso"), MakeIso("T-80004", "ROOT LOOSE GAME"));
        File.WriteAllBytes(Path.Combine(root, "BOX.TGA"), TgaEncoder.Encode(Solid(4, 4, 1, 1, 1)));
    }

    [Theory]
    [InlineData("A.iso")]
    [InlineData("Root.iso")]
    public async Task Change_cover_is_disabled_with_a_tooltip_for_a_loose_game(string fileName)
    {
        using var card = new TempCard();
        BuildLooseGameCard(card.Dir);
        var (enabled, tip) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Path.EndsWith(fileName));
            Assert.False(row.OwnFolder);
            w.GameList.SelectedItem = row;
            return Task.FromResult((w.ChangeCoverButton.IsEnabled, ToolTip.GetTip(w.ChangeCoverButton) as string));
        });
        Assert.False(enabled);
        Assert.NotNull(tip);
    }

    [Fact]
    public async Task Change_cover_is_enabled_for_a_game_in_its_own_folder()
    {
        using var card = new TempCard();
        BuildLooseGameCard(card.Dir);
        bool enabled = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Path.EndsWith("own.iso"));
            Assert.True(row.OwnFolder);
            w.GameList.SelectedItem = row;
            return Task.FromResult(w.ChangeCoverButton.IsEnabled);
        });
        Assert.True(enabled);
    }

    // "Fill missing covers" (bulk, Tools menu) is removed for 0.1 (user: "too broad and dangerous") -- its own
    // loose-game-skip test went with it; the same shared/root-BOX.TGA safety rule is still covered below for the
    // per-game Change cover path (force-staged, bypassing the UI gate entirely).

    [Theory]
    [InlineData("A.iso", "Games/Shared/BOX.TGA")]
    [InlineData("Root.iso", "BOX.TGA")]
    public async Task Save_changes_never_writes_a_loose_games_shared_or_root_BOX_TGA_even_if_force_staged(string fileName, string boxRel)
    {
        using var card = new TempCard();
        BuildLooseGameCard(card.Dir);
        string boxPath = Path.Combine(card.Dir, boxRel.Replace('/', Path.DirectorySeparatorChar));
        byte[] before = File.ReadAllBytes(boxPath);
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir, true);
            w.OpenCard(card.Dir);
            var row = w.Load!.Rows.Single(r => r.Path.EndsWith(fileName));
            Assert.False(row.OwnFolder);
            // Bypass the UI gating entirely -- this proves Apply() itself refuses, not just the button state.
            row.PendingCover = Solid(4, 4, 250, 1, 1);
            await w.SaveChanges();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Saved:", status);
        Assert.Equal(before, File.ReadAllBytes(boxPath));
    }

    /// <summary>A loose game's "No cover" problem (shares its folder, or sits at the card root) must never offer a
    /// Fix button that does nothing when clicked -- with no bulk fill any more, its NoCover's own Action has to be
    /// null (Explorer is the only path: move it into its own folder, then Refresh), never "ChooseCover".</summary>
    [Fact]
    public void A_loose_games_no_cover_problem_has_no_in_app_fix_but_an_own_folder_games_does()
    {
        using var card = new TempCard();
        BuildLooseGameCard(card.Dir);
        File.Delete(Path.Combine(card.Dir, "Games", "Shared", "BOX.TGA"));
        File.Delete(Path.Combine(card.Dir, "BOX.TGA"));
        var load = CardEditSession.Load(card.Dir, 20261124);
        var problems = Problems.Find(load);
        Assert.Contains(problems, p => p.Kind == ProblemKind.NoCover && p.Row!.Path.EndsWith("A.iso") && p.Action == null);
        Assert.Contains(problems, p => p.Kind == ProblemKind.NoCover && p.Row!.Path.EndsWith("Root.iso") && p.Action == null);
        Assert.Contains(problems, p => p.Kind == ProblemKind.NoCover && p.Row!.Path.EndsWith("own.iso") && p.Action == "ChooseCover");
    }

    /// <summary>"Reset edits" (consolidation round): only the selected game's own staged changes go back to what
    /// Load() read -- a different row's own staged change is untouched -- and the reset is itself one undo level
    /// (through Stage(), the same as every other staging action), not a separate kind of undo.</summary>
    [Fact]
    public async Task Reset_edits_undoes_only_the_selected_games_own_staged_changes_and_is_itself_undoable()
    {
        using var card = new TempCard();
        var (k1, k2) = MakeSmallCard(card.Dir);
        var (row1Reset, row2StillFavorite, canUndo, row1RestoredAfterUndo) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var row1 = w.Load!.Rows.Single(r => r.Key == k1);
            var row2 = w.Load!.Rows.Single(r => r.Key == k2);
            w.GameList.SelectedItem = row1;
            row1.TitleOverride = "Staged Title";
            row1.Favorite = true;
            row1.Loader = "jhl";
            w.Load.UserEdits[k1] = new Core.Formats.UserEditRecord { Genre = "Action" };
            row2.Favorite = true;   // a different row's own staged change
            w.ResetEditsForTests();
            bool reset1 = !row1.Dirty && row1.TitleOverride == "" && !row1.Favorite && row1.Loader == "auto" && !w.Load.UserEdits.ContainsKey(k1);
            bool stillFav2 = row2.Favorite;
            bool canUndoNow = w.History.CanUndo;
            w.UndoLast();
            bool restoredAfterUndo = row1.TitleOverride == "Staged Title" && row1.Favorite && row1.Loader == "jhl" && w.Load.UserEdits.ContainsKey(k1);
            return Task.FromResult((reset1, stillFav2, canUndoNow, restoredAfterUndo));
        });
        Assert.True(row1Reset);
        Assert.True(row2StillFavorite);
        Assert.True(canUndo);
        Assert.True(row1RestoredAfterUndo);
    }

    [Fact]
    public async Task Ctrl_F_is_bound_to_focusing_the_search_box()
    {
        bool bound = await HeadlessApp.Run(() =>
        {
            var w = new MainWindow(removableDriveListForTests: () => []);
            return Task.FromResult(w.KeyBindings.Any(k => k.Gesture.Key == Avalonia.Input.Key.F && k.Gesture.KeyModifiers == Avalonia.Input.KeyModifiers.Control));
        });
        Assert.True(bound);
    }
}
