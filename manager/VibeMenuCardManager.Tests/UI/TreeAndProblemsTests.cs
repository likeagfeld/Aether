// VibeMenu Card Manager, GPL-3.0-or-later.
// Tree expand/collapse (now the one library list's own recursive "All games" view), horizontal scroll, resizable
// columns, and the CHECK views that replaced the Problems window (consolidation round). The 1065x783/1400x900
// check (the user's own window size, and the restyle round's second requested size) is a layout-bounds check --
// see At_1065x783_and_1400x900_every_panel_lays_out_inside_the_window for why this environment cannot capture a
// real pixel screenshot headlessly.
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class TreeAndProblemsTests
{
    static MainWindow Window(string dir)
    {
        var w = new MainWindow(removableDriveListForTests: () => []) { SystemVolumes = [] };
        w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    static byte[] MakeIso(string product, string title = "T", string area = "U") =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = area, ["title"] = title, ["device"] = "CD-1/1" });

    /// <summary>Games/Alpha/Alpha.iso (a single-game folder); Games/Multi/{a.iso,b.iso} (a folder row: two images)
    /// with Games/Multi/Sub/c.iso one level deeper (another single-game folder).</summary>
    static void BuildCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"), MakeIso("T-00001"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Multi"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Multi", "a.iso"), MakeIso("T-00002"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Multi", "b.iso"), MakeIso("T-00003"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Multi", "Sub"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Multi", "Sub", "c.iso"), MakeIso("T-00004"));
    }

    static List<object> Items(MainWindow w) => w.LibraryItemsForTests;
    static MainWindow.FolderListRow FolderRow(MainWindow w, string path) => Items(w).OfType<MainWindow.FolderListRow>().Single(f => f.Folder.Path == path);

    // ===== expand/collapse ("All games", the recursive tree view) =====

    [Fact]
    public async Task Collapsing_a_folder_hides_its_descendants_and_re_expanding_restores_them()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (before, afterCollapse, afterExpand, collapsedFlag) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            int n0 = Items(w).Count;
            w.ToggleFolderCollapsedForTests("Games/Multi");
            int n1 = Items(w).Count;
            bool flag = w.IsFolderCollapsedForTests("Games/Multi");
            w.ToggleFolderCollapsedForTests("Games/Multi");
            int n2 = Items(w).Count;
            return Task.FromResult((n0, n1, n2, flag));
        });
        Assert.True(collapsedFlag);
        Assert.True(afterCollapse < before);   // Multi/a.iso, Multi/b.iso, Multi/Sub, and Multi/Sub/c.iso are gone
        Assert.Equal(before, afterExpand);     // re-expanding restores exactly what was there
    }

    [Fact]
    public async Task Double_click_on_a_folder_row_toggles_it()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        bool collapsedAfter = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var multi = FolderRow(w, "Games/Multi");
            w.GameList.SelectedItem = multi;
            w.SimulateFolderListDoubleTapForTests();
            return Task.FromResult(w.IsFolderCollapsedForTests("Games/Multi"));
        });
        Assert.True(collapsedAfter);
    }

    [Fact]
    public async Task Left_collapses_then_moves_to_the_parent_and_right_expands_then_moves_to_the_first_child()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (collapsedByLeft, parentAfterSecondLeft, expandedByRight, childDepthAfterSecondRight) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            w.GameList.SelectedItem = FolderRow(w, "Games/Multi");
            w.SimulateFolderListKeyForTests(Key.Left);                 // collapses it
            bool collapsed = w.IsFolderCollapsedForTests("Games/Multi");
            w.GameList.SelectedItem = FolderRow(w, "Games/Multi");
            w.SimulateFolderListKeyForTests(Key.Left);                 // already collapsed: up to its parent
            string? parent = (w.GameList.SelectedItem as MainWindow.FolderListRow)?.Folder.Path;

            w.GameList.SelectedItem = FolderRow(w, "Games/Multi");
            w.SimulateFolderListKeyForTests(Key.Right);                // expands it again
            bool expanded = !w.IsFolderCollapsedForTests("Games/Multi");
            w.GameList.SelectedItem = FolderRow(w, "Games/Multi");
            w.SimulateFolderListKeyForTests(Key.Right);                // already expanded: down to its first child
            var sel = w.GameList.SelectedItem;
            int? depth = sel is MainWindow.FolderListRow fr ? fr.Depth : sel is GameRow ? FolderRow(w, "Games/Multi").Depth + 1 : null;
            return Task.FromResult((collapsed, parent, expanded, depth));
        });
        Assert.True(collapsedByLeft);
        Assert.Equal("Games", parentAfterSecondLeft);
        Assert.True(expandedByRight);
        Assert.NotNull(childDepthAfterSecondRight);   // moved one level deeper than "Multi" itself
    }

    [Fact]
    public async Task Expand_all_and_collapse_all_affect_every_folder_at_once()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (full, allCollapsed, allExpandedAgain) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            int n0 = Items(w).Count;
            w.SetAllFoldersCollapsedForTests(true);
            int n1 = Items(w).Count;
            w.SetAllFoldersCollapsedForTests(false);
            int n2 = Items(w).Count;
            return Task.FromResult((n0, n1, n2));
        });
        Assert.True(allCollapsed < full);
        Assert.Equal(full, allExpandedAgain);
    }

    [Fact]
    public async Task Collapsed_state_survives_File_Refresh_of_the_same_card_but_not_a_different_one()
    {
        using var cardA = new TempCard();
        using var cardB = new TempCard();
        BuildCard(cardA.Dir);
        BuildCard(cardB.Dir);
        var (stillCollapsedAfterRefresh, resetOnDifferentCard) = await HeadlessApp.Run(() =>
        {
            var w = Window(cardA.Dir);
            w.OpenCard(cardA.Dir);
            w.ToggleFolderCollapsedForTests("Games/Multi");
            w.OpenCard(cardA.Dir);   // File > Refresh: the same root
            bool survived = w.IsFolderCollapsedForTests("Games/Multi");
            w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(cardB.Dir).Id!);
            w.OpenCard(cardB.Dir);   // a different card: starts fresh
            bool reset = !w.IsFolderCollapsedForTests("Games/Multi");
            return Task.FromResult((survived, reset));
        });
        Assert.True(stillCollapsedAfterRefresh);
        Assert.True(resetOnDifferentCard);
    }

    // ===== horizontal scroll and resizable columns =====

    [Fact]
    public async Task The_game_list_scrolls_horizontally_and_the_details_panel_keeps_wrapping()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (listScroll, detailsScroll) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            var listOuter = Assert.IsType<ScrollViewer>((w.GameList.Parent as Control)?.Parent);
            return Task.FromResult((listOuter.HorizontalScrollBarVisibility, w.DetailsScrollViewer?.HorizontalScrollBarVisibility));
        });
        Assert.Equal(ScrollBarVisibility.Auto, listScroll);
        // the details panel's own ScrollViewer must stay Disabled (panel-layout fix round): that is what makes its
        // WrapPanel rows wrap instead of clip. Horizontal scroll there would silently re-break that earlier fix.
        Assert.Equal(ScrollBarVisibility.Disabled, detailsScroll);
    }

    static T? FindDescendant<T>(Avalonia.Visual root, Func<T, bool>? match = null) where T : Avalonia.Visual
    {
        foreach (var child in root.GetVisualChildren())
        {
            if (child is T t && (match == null || match(t))) return t;
            if (FindDescendant<T>(child, match) is T found) return found;
        }
        return null;
    }

    [Fact]
    public async Task Game_list_columns_resize_and_a_long_cell_carries_a_tooltip()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (widthGrew, hasTooltip) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.Show();   // the row Grid must actually be realized for FindDescendant to find its TextBlock
            w.OpenCard(card.Dir);
            double before = w.GameListColumnWidthForTests(1);     // Title column
            w.ResizeGameListColumnForTests(1, before + 60);
            double after = w.GameListColumnWidthForTests(1);
            // a long title: confirm its cell got a tooltip (CharacterEllipsis alone only shows there is more)
            var longRow = w.Load!.Rows.First(r => r.FileName.StartsWith("Alpha"));
            longRow.TitleOverride = "A very very very very very long title that will not fit in the Title column at all";
            w.RefreshGameListForTests();
            Dispatcher.UIThread.RunJobs();   // let the virtualized ListBox actually realize the row's container
            var tb = FindDescendant<Avalonia.Controls.TextBlock>(w.GameList, t => t.Text == longRow.TitleOverride);
            bool tip = tb != null && ToolTip.GetTip(tb) != null;
            return Task.FromResult((after > before, tip));
        });
        Assert.True(widthGrew);
        Assert.True(hasTooltip);
    }

    // ===== CHECK views (consolidation round: these replace the Problems window) =====

    static byte[] IsoWithBadHeader() => new byte[256];   // never a valid Saturn System ID: Sid stays null

    static List<GameRecord> ProblemCardDb() =>
    [
        new() { Key = "T-CUEGAP", TitleEn = "Cue Gap" },
        new() { Key = "T-DUP", TitleEn = "Duplicated" },
        new() { Key = "T-WEIRD", TitleEn = "Weird Name" },
        // T-UNKNOWN is deliberately absent (NotInDatabase); T-EXISTS is a second, real key a "Match to game…" can
        // pick for T-UNKNOWN's row without needing T-UNKNOWN itself in the database.
        new() { Key = "T-EXISTS", TitleEn = "Exists" },
    ];

    /// <summary>One fixture per problem kind: Bad (an unreadable header, so Sid and Key both stay unset -- never
    /// also "not in the database", since that cannot be known without reading the header first), Unknown (a good
    /// header, but its own product is not in GAMES.DB), a .cue naming a file that is not beside it, two copies of
    /// the same product (Dup1/Dup2), and a non-ASCII folder name. No cover is automatic here (no art anywhere on a
    /// fresh TempCard, and no cover pack installed), so every row also raises NoCover.</summary>
    static void BuildProblemCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(ProblemCardDb(), [], new Dictionary<string, string>(), 20261124));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Bad"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Bad", "bad.iso"), IsoWithBadHeader());
        Directory.CreateDirectory(Path.Combine(root, "Games", "Unknown"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Unknown", "unknown.iso"), MakeIso("T-UNKNOWN"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "CueGap"));
        File.WriteAllText(Path.Combine(root, "Games", "CueGap", "game.cue"), "FILE \"missing.bin\" BINARY\n  TRACK 01 MODE1/2048\n");
        File.WriteAllBytes(Path.Combine(root, "Games", "CueGap", "game.iso"), MakeIso("T-CUEGAP"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Dup1"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Dup2"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Dup1", "dup1.iso"), MakeIso("T-DUP"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Dup2", "dup2.iso"), MakeIso("T-DUP"));
        Directory.CreateDirectory(Path.Combine(root, "Games", "ファイル"));
        File.WriteAllBytes(Path.Combine(root, "Games", "ファイル", "weird.iso"), MakeIso("T-WEIRD"));
    }

    [Fact]
    public void Problems_Find_raises_every_kind_with_an_explanation_and_a_how_to_fix()
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        var load = CardEditSession.Load(card.Dir, 20261124);
        var problems = Problems.Find(load);
        Assert.NotEmpty(problems);
        foreach (var kind in Enum.GetValues<ProblemKind>())
        {
            var p = problems.First(x => x.Kind == kind);
            Assert.False(string.IsNullOrWhiteSpace(p.Explanation));
            Assert.False(string.IsNullOrWhiteSpace(p.HowToFix));
        }
        // the fixable ones name their in-app action; everything else leaves it to Explorer
        Assert.Contains(problems, p => p.Kind == ProblemKind.UnreadableHeader && p.Action == "Match" && p.Row!.Path.Contains("Bad"));
        Assert.Contains(problems, p => p.Kind == ProblemKind.NotInDatabase && p.Action == "Match" && p.Row!.Path.Contains("Unknown"));
        Assert.Contains(problems, p => p.Kind == ProblemKind.NoCover && p.Action == "ChooseCover");   // every game here is in its own folder
        Assert.Contains(problems, p => p.Kind == ProblemKind.CueMissingFile && p.Explanation.Contains("missing.bin"));
        Assert.Contains(problems, p => p.Kind == ProblemKind.Duplicate && p.Row!.Path.Contains("Dup1") && p.Explanation.Contains("Dup2"));
        Assert.Contains(problems, p => p.Kind == ProblemKind.Duplicate && p.Row!.Path.Contains("Dup2") && p.Explanation.Contains("Dup1"));
        Assert.Contains(problems, p => p.Kind == ProblemKind.NameEncoding && p.Row!.Path.Contains("weird.iso"));
        // never both at once: an unreadable header is never also flagged "not in the database"
        Assert.DoesNotContain(problems, p => p.Kind == ProblemKind.NotInDatabase && p.Row!.Path.Contains("Bad"));
        // matched products are never flagged "not in the database"
        Assert.DoesNotContain(problems, p => p.Kind == ProblemKind.NotInDatabase && (p.Row!.Path.Contains("CueGap") || p.Row.Path.Contains("Dup") || p.Row.Path.Contains("weird.iso")));
    }

    [Fact]
    public async Task Needs_attention_shows_every_problem_game_and_its_own_badge_count_matches()
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        var (badgeCount, shownCount, allDistinctRows) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            int badge = ((MainWindow.NavViewItem)((IEnumerable<object>)w.ViewsList.ItemsSource!).OfType<MainWindow.NavViewItem>()
                .First(v => v.Kind == MainWindow.LibraryView.Attention)).Count;
            w.SelectViewForTests(MainWindow.LibraryView.Attention);
            int shown = Items(w).OfType<GameRow>().Count();
            int distinct = w.CurrentProblems.Select(p => p.Row).Where(r => r != null).Distinct().Count();
            return Task.FromResult((badge, shown, distinct));
        });
        Assert.True(badgeCount > 0);
        Assert.Equal(allDistinctRows, badgeCount);
        Assert.Equal(allDistinctRows, shownCount);
    }

    [Fact]
    public async Task No_cover_and_not_in_database_views_show_exactly_their_own_rows()
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        var (noCoverPaths, notInDbPaths) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            w.SelectViewForTests(MainWindow.LibraryView.NoCover);
            var noCover = Items(w).OfType<GameRow>().Select(r => r.Path).ToList();
            w.SelectViewForTests(MainWindow.LibraryView.NotInDb);
            var notInDb = Items(w).OfType<GameRow>().Select(r => r.Path).ToList();
            return Task.FromResult((noCover, notInDb));
        });
        // nothing has a cover on a fresh TempCard with no pack installed: every game is in "No cover" -- 7 rows in
        // all (Bad, Unknown, CueGap's two loose images game.cue+game.iso, Dup1, Dup2, weird.iso)
        Assert.Equal(7, noCoverPaths.Count);
        // Only Unknown (a good header, but its own product is not in GAMES.DB) -- Bad's header can't be read at
        // all (Sid == null), which is UnreadableHeader, a different problem ("Needs attention", not "Not in
        // database"); CueGap/Dup/weird all matched a real GAMES.DB key.
        string onlyNotInDb = Assert.Single(notInDbPaths);
        Assert.Contains("unknown.iso", onlyNotInDb);
    }

    [Fact]
    public async Task Selecting_a_problem_game_in_Needs_attention_shows_its_details()
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        bool detailsShowTheRightGame = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            w.SelectViewForTests(MainWindow.LibraryView.Attention);
            var dupRow = w.CurrentProblems.First(p => p.Kind == ProblemKind.Duplicate).Row!;
            w.GameList.SelectedItem = dupRow;
            return Task.FromResult(w.DetailPath.Text == "/" + dupRow.Path);
        });
        Assert.True(detailsShowTheRightGame);
    }

    [Fact]
    public async Task Matching_an_unmatched_game_clears_it_from_Not_in_database()
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        int countBefore, countAfter;
        (countBefore, countAfter) = await HeadlessApp.Run(async () =>
        {
            var w = Window(card.Dir);
            w.MatchGameDialogPrompt = (_, _) => Task.FromResult<string?>("T-EXISTS");   // a real GAMES.DB key
            w.OpenCard(card.Dir);
            w.SelectViewForTests(MainWindow.LibraryView.NotInDb);
            int before = Items(w).OfType<GameRow>().Count();
            var row = Items(w).OfType<GameRow>().First(r => r.Path.Contains("unknown.iso"));
            w.GameList.SelectedItem = row;
            await w.MatchToGame();
            int after = Items(w).OfType<GameRow>().Count(r => r.Path == row.Path);
            return (before, after);
        });
        Assert.True(countBefore > 0);
        Assert.Equal(0, countAfter);   // matched now, so it dropped out of the "Not in database" view entirely
    }

    // ===== layout at 1065x783 (the user's own window size) and 1400x900 (the restyle round's second size) =====

    /// <summary>This test session's headless platform draws no real pixels (Avalonia.Headless's own no-op renderer
    /// here reports no IPlatformRenderInterface, so CaptureRenderedFrame always throws) -- confirmed by trying it
    /// with UseHeadlessDrawing=false, which broke every other headless test in this suite the moment MainWindow's
    /// own XAML resources tried to render, so that option is not shared here. What this checks instead, at both
    /// the user's own 1065x783 and the restyle's second requested 1400x900: every key panel is actually laid out
    /// inside the window's client area (never negative, never past the far edge), which is what "nothing cut off"
    /// means without a captured frame. A real screenshot, when one is wanted for review, is taken interactively
    /// (as earlier rounds of this work did).</summary>
    [Theory]
    [InlineData(1065, 783)]
    [InlineData(1400, 900)]
    public async Task At_1065x783_and_1400x900_every_panel_lays_out_inside_the_window_nothing_cut_off(double w0, double h0)
    {
        using var card = new TempCard();
        BuildProblemCard(card.Dir);
        var (clientWidth, clientHeight, viewsRight, gameListRight, detailsRight, statusBarBottom) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.Width = w0; w.Height = h0;
            w.Show();
            w.OpenCard(card.Dir);
            Dispatcher.UIThread.RunJobs();
            var viewsBounds = w.ViewsList.Parent is Control fc ? fc.Bounds : default;
            var listBounds = (w.GameList.Parent as Control)?.Parent is Control lc ? lc.Bounds : default;
            var detailsBounds = w.DetailsScrollViewer?.Bounds ?? default;
            var statusBounds = w.CountsText.Bounds;
            return Task.FromResult((w.ClientSize.Width, w.ClientSize.Height,
                viewsBounds.Right, listBounds.Right, detailsBounds.Right, statusBounds.Bottom));
        });
        Assert.Equal(w0, clientWidth, 1);
        Assert.Equal(h0, clientHeight, 1);
        // every panel's right edge is at or before the window's own width (never pushed off-screen), and the status
        // bar sits above the window's bottom edge (never pushed below it)
        Assert.True(viewsRight <= clientWidth + 1);
        Assert.True(gameListRight <= clientWidth + 1);
        Assert.True(detailsRight <= clientWidth + 1);
        Assert.True(statusBarBottom <= clientHeight + 1);
    }

    /// <summary>User feedback round: the tree pane defaults to ~33% of the window (300px minimum) -- the Views pane
    /// inherits this unchanged (the consolidation round's own narrower-pane polish is a follow-up, not this one) --
    /// and a folder row's own +/- toggle is at least 24x24 px, not just its glyph's text bounds.</summary>
    [Fact]
    public async Task At_1065x783_the_views_pane_is_about_212px_and_the_toggle_is_at_least_24x24()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        UiSettings.PathOverrideForTests = Path.Combine(Path.GetTempPath(), $"vmcm-uisettings-{Guid.NewGuid():N}.json");
        try
        {
            var (viewsWidth, glyphBounds) = await HeadlessApp.Run(() =>
            {
                var w = Window(card.Dir);
                w.Width = 1065; w.Height = 783;
                w.Show();
                w.OpenCard(card.Dir);
                Dispatcher.UIThread.RunJobs();
                double vw = ((w.ViewsList.Parent as Control)!).Bounds.Width;
                w.GameList.SelectedItem = FolderRow(w, "Games/Multi");
                Dispatcher.UIThread.RunJobs();
                var glyph = FindDescendant<Border>(w.GameList, b => b.Width == 24 && b.Height == 24);
                return Task.FromResult((vw, glyph?.Bounds ?? default));
            });
            // ~212px (punch-list round, the mockup's own width -- supersedes the earlier ~33%/300px-minimum round:
            // a fixed 7-row list of view names never needed that much room), with slack for the pane's own border.
            Assert.InRange(viewsWidth, 195, 212);
            Assert.True(glyphBounds.Width >= 24 && glyphBounds.Height >= 24);
        }
        finally { UiSettings.PathOverrideForTests = null; }
    }

    /// <summary>User feedback round: a dragged splitter position is remembered the next time a window opens (a
    /// fresh UiSettings.Load(), as a real relaunch would do), not reset back to the ~33% default.</summary>
    [Fact]
    public async Task A_dragged_splitter_position_is_remembered_across_a_relaunch()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        UiSettings.PathOverrideForTests = Path.Combine(Path.GetTempPath(), $"vmcm-uisettings-{Guid.NewGuid():N}.json");
        try
        {
            var (firstWidth, secondWidth) = await HeadlessApp.Run(() =>
            {
                var w1 = Window(card.Dir);
                w1.Width = 1065; w1.Height = 783;
                w1.Show();
                var settings = UiSettings.Load();
                settings.TreePaneWidth = 450;
                settings.Save();
                double before = ((w1.ViewsList.Parent as Control)!).Bounds.Width;

                var w2 = Window(card.Dir);   // a fresh window, as a relaunch creates: re-reads the saved settings
                w2.Width = 1065; w2.Height = 783;
                w2.Show();
                Dispatcher.UIThread.RunJobs();
                double after = ((w2.ViewsList.Parent as Control)!).Bounds.Width;
                return Task.FromResult((before, after));
            });
            // the rendered width is the saved column width minus the pane Border's own margin and border thickness
            // (the restyle round's rounded panes), so this checks it is in that neighborhood, not exactly 450 --
            // UiSettings itself (not a rendered Bounds) is the real source of truth for "was it remembered".
            Assert.InRange(secondWidth, 430, 450);
            Assert.Equal(450, UiSettings.Load().TreePaneWidth);
        }
        finally { UiSettings.PathOverrideForTests = null; }
    }
}
