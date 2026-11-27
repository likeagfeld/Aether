// Aether Card Manager, GPL-3.0-or-later.
// The real main window (user-approved mockup, .superpowers/sdd/2026-11-22-m4-card-manager/ui-mockup.html): a drive
// picker, a folder tree, a virtualized sortable game list and a cover/details panel. Every edit (rename, favorite,
// loader, cover, match override, metadata) is staged on CardLoad; nothing touches the card until "Save changes",
// which confirms through CardWriteGuard, then applies the whole staged edit as one transaction (CardEditSession.
// Apply) with progress and Cancel. Game and folder file management (adding, removing, moving, new folders) stays in
// Explorer, same as Satiator Rings -- this app never creates, moves or deletes a game file or folder; Refresh (and
// every OpenCard) rescans so anything added or removed there shows up here.
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using VibeMenuCardManager.Core.About;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Saves;
using System.Diagnostics;

namespace VibeMenuCardManager.UI;

public sealed class MainWindow : Window
{
    // ---- the legacy/test surface (ShellSmokeTests): Build and CardWriteGuard keep working unchanged ----
    public readonly TextBox CardPath = new() { PlaceholderText = "The SD card's root folder" };
    public readonly Button Browse = new() { Content = "Browse..." };
    public readonly TextBox ReleasePath = new() { PlaceholderText = "The Aether release folder" };
    public readonly Button BrowseRelease = new() { Content = "Browse..." };
    public readonly TextBox CoversZip = new() { PlaceholderText = "Optional: a covers zip" };
    public readonly TextBox ArtCache = new() { PlaceholderText = "Optional: an art cache folder" };
    public readonly CheckBox Download = new() { Content = "Download missing covers into the art cache" };
    public readonly Button BuildCard = new() { Content = "Build card" };
    // Punch-list round: IsVisible, not just IsEnabled -- "the Cancel button should only appear while a long
    // operation runs", not sit there greyed out the rest of the time.
    public readonly Button CancelButton = new() { Content = "Cancel", IsEnabled = false, IsVisible = false };
    public readonly Button AboutButton = new() { Content = "About" };
    public readonly TextBlock Status = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    public readonly TextBlock Progress = new() { Opacity = 0.75 };
    public readonly CheckBox NotSystemDrive = new() { IsVisible = false };
    string? _overrideFor;
    public IReadOnlyList<string> SystemVolumes = VolumeResolver.SystemVolumes();
    public CardWriteGuard.Ask? Ask;
    public string? FontPath;
    public Action<string>? ProgressHook;
    public Action? WorkStarted;
    public bool IsBusy { get; private set; }
    bool _closeWhenDone;
    CancellationTokenSource? _cts;

    // ---- PC backup ----
    /// <summary>Off by default, so an existing test's OpenCard never reaches the real per-user AppData folder by
    /// accident -- App.cs turns this on for the real app. A backup-specific test turns it on AND sets
    /// Core.Backup.CardBackup.RootOverrideForTests to a scratch folder.</summary>
    public bool AutoBackupOnOpen;
    /// <summary>Test hook: the Task from OpenCard's most recent background PC backup, so a test can await it
    /// deterministically instead of racing the status line. Never awaited by the real app.</summary>
    public Task? LastBackupTaskForTests;

    // ---- the mockup's own controls ----
    public readonly ComboBox DrivePicker = new() { MinWidth = 260 };
    public readonly Button RefreshDrives = new() { Content = "Refresh" };
    public readonly Button BatchRenameButton = new() { Content = "Batch rename…", IsEnabled = false };
    // VerticalAlignment.Center (user feedback round): without it, this TextBox stretches to the top bar's own row
    // height (set by the taller "SD card" chip on the other side of the same DockPanel row), ending up noticeably
    // taller than the drive ComboBox/Refresh button next to it, which both size to their own content.
    public readonly TextBox SearchBox = new() { PlaceholderText = "Search titles, files or codes", MinWidth = 140, Padding = new Thickness(6, 3, 56, 3), VerticalAlignment = VerticalAlignment.Center };
    /// <summary>The card chip's own label/subtitle (consolidation round: the real volume label and its free space,
    /// not a static "SD card"), updated by UpdateStatusCounts whenever a card is opened or its counts change.</summary>
    public readonly TextBlock CardChipLabel = new() { FontWeight = FontWeight.Bold, VerticalAlignment = VerticalAlignment.Center };
    public readonly TextBlock CardChipSubtitle = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11 };
    public readonly Button SaveChangesButton = new() { Content = "Save changes", IsEnabled = false, FontWeight = FontWeight.Bold, Classes = { "primary" } };
    /// <summary>"No Build card setup" fix round: the one big, obvious action, not a menu item. Appears once a card
    /// has been read (OpenCard); RefreshPrepareCardState sets its text/enabled from the card's own installed
    /// Aether version (none, the bundled one, or older).</summary>
    public readonly Button PrepareCardButton = new() { Content = "Prepare SD Card for Aether", FontWeight = FontWeight.Bold, IsVisible = false, Padding = new Thickness(16, 8) };

    public readonly ListBox ViewsList = new();
    public readonly ListBox GameList = new() { SelectionMode = SelectionMode.Multiple };
    /// <summary>Phase C: the cover-wall alternative to GameList's own row list -- every GameRow in the current
    /// view/filter, flattened (a cover wall has no use for a folder's own row), each its own bigger tile
    /// (BuildCoverTile). A ListBox, not a plain ItemsControl, so it keeps the same keyboard/selection behavior.</summary>
    public readonly ListBox CoversList = new();
    /// <summary>The toolbar's List/Covers segmented toggle (punch-list round reserved this place; Phase C fills
    /// it in) -- two mutually-exclusive ToggleButtons, not a single one, so both states read as a real segmented
    /// control (the mockup's own pair) rather than one lone on/off switch.</summary>
    public readonly ToggleButton ListModeButton = new() { Content = "☰ List", IsChecked = true };
    public readonly ToggleButton CoversModeButton = new() { Content = "▦ Covers", IsChecked = false };
    bool _coversMode;
    readonly StackPanel _headerRow = new() { Orientation = Orientation.Horizontal };
    Border? _cardDot;

    // Punch-list round: 156x256 (a Saturn box's own real proportions, not the old 220x220 square), a glow (the
    // accent color, not just a plain drop shadow) and a reflection (CoverReflection, built alongside it below).
    public readonly Image CoverImage = new() { Width = 156, Height = 256, Stretch = Stretch.Uniform, [!Visual.EffectProperty] = AppTheme.Dyn("CoverEffect") };
    /// <summary>A mirrored, fading copy of CoverImage directly beneath it (punch-list round, "glow and reflection")
    /// -- its own Image, not a RenderTransform on CoverImage itself, so the real cover stays undistorted; SetCover
    /// keeps its Source identical to CoverImage's own.</summary>
    public readonly Image CoverReflection = new()
    {
        Width = 156, Height = 64, Stretch = Stretch.UniformToFill,
        RenderTransform = new ScaleTransform(1, -1), [!Visual.OpacityProperty] = AppTheme.Dyn("ReflectionOpacity"),
        OpacityMask = new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
            GradientStops = { new GradientStop(Colors.White, 0), new GradientStop(Colors.Transparent, 1) },
        },
    };
    /// <summary>The cover's own source tag (punch-list round, the mockup's "COVER PACK" corner badge): "Your
    /// cover"/"Cover pack", uppercased, the same priority BuildStatusCell's pill already uses -- never shown when
    /// there is no cover (NoCoverThumb's placeholder has nothing to tag).</summary>
    public readonly TextBlock CoverSourceTag = new() { FontSize = 10, FontWeight = FontWeight.Bold, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text"), LetterSpacing = 0.5 };
    readonly Border _coverSourceTagBadge = new();
    readonly Control _coverPlaceholder = NoCoverThumb(156, 256);
    public readonly Button ChangeCoverButton = new() { Content = "Change cover…" };
    public readonly Button RestoreCoverButton = new() { Content = "Restore cover" };
    public readonly ToggleButton View3DButton = new() { Content = "2D / 3D", IsEnabled = false };
    public readonly Button ChangeFolderArtButton = new() { Content = "Change folder art…" };
    public readonly Button RemoveFolderArtButton = new() { Content = "Remove folder art" };
    public readonly TextBlock DetailTitle = new() { FontSize = 16, FontWeight = FontWeight.SemiBold, TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    /// <summary>Region/Code/Genre/Year as small pill chips under the title (consolidation round, mockup's ".meta"):
    /// a quick-glance summary; the fields further down stay the one editable source of truth for the same values.</summary>
    public readonly WrapPanel DetailChips = new() { Margin = new Thickness(0, 4, 0, 4) };
    public readonly TextBlock DetailCode = new(), DetailSaves = new(), DetailPath = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    /// <summary>Metadata overrides: edited in place, staged into
    /// Load.UserEdits[row.Key] (USEREDIT.INI) on LostFocus, re-applied onto GAMES.DB at Save changes.</summary>
    public readonly TextBox MetaTitleBox = new() { PlaceholderText = "GAMES.DB title" };
    public readonly TextBox DetailRegion = new() { PlaceholderText = "e.g. U, JU" }, DetailGenre = new(), DetailYear = new(), MetaPlayersBox = new();
    public readonly ComboBox LoaderBox = new() { MinWidth = 110 };
    /// <summary>The Loader row's own plain hint line (punch-list round, the mockup's own text): "Auto uses JHL for
    /// this disc" / "Auto uses CWX" -- set by ShowDetails from the matched record's LoaderHint, not baked into the
    /// ComboBox item text any more (that used to read "Auto (JHL)").</summary>
    public readonly TextBlock LoaderHintText = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11 };
    public readonly Button RenameToDbButton = new() { Content = "Rename to DB title" };
    // Punch-list round: a toggle switch (the mockup's own "Favorite [=On]"), not a button whose own label had to
    // say which state it was in -- the field name stays FavoriteButton (no test references it by name, a repo-wide
    // search turned up none) so ToggleFavorite()'s own wiring below is the only thing that changed.
    public readonly ToggleSwitch FavoriteButton = new() { OnContent = "On", OffContent = "Off" };
    public readonly Button MatchButton = new() { Content = "Match to game…" };
    /// <summary>"Add to database…" for a game GAMES.DB does not know (a user record, Core.Card.UserRecords), "Edit
    /// database entry…" for one it does (its USEREDIT.INI fields): one dialog, staged, written at Save changes.</summary>
    public readonly Button DatabaseEntryButton = new() { Content = "Add to database…" };
    /// <summary>Tests answer the database-entry dialog without showing it: null = cancelled.</summary>
    public Func<MainWindow, GameRow, Core.Formats.UserEditRecord, Task<Core.Formats.UserEditRecord?>>? DatabaseEntryPrompt;
    /// <summary>"Reset edits" (consolidation round, mockup's own button): undoes every staged change on the
    /// selected game alone -- title override, favorite, loader, cover, match override, and its USEREDIT.INI metadata
    /// -- back to what Load() actually read, through Stage() so the reset is itself one Ctrl+Z.</summary>
    public readonly Button ResetEditsButton = new() { Content = "Reset edits" };
    public readonly TextBlock CountsText = new(), UnsavedText = new();
    /// <summary>The library pane's own header (consolidation round, mockup's ".lh"): the active view's name and a
    /// live count/subtitle, plus the Sort dropdown.</summary>
    public readonly TextBlock LibraryTitle = new() { FontWeight = FontWeight.Bold, FontSize = 15, VerticalAlignment = VerticalAlignment.Center };
    public readonly TextBlock LibrarySubtitle = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) };
    /// <summary>Title/Region/Genre (the coordinator: "a single-entry dropdown looks broken" -- all three are
    /// already in the data and cheap to sort on). Clicking the Title or Region column header sorts the same way
    /// (BuildGameListHeader); Genre has no column of its own, dropdown-only.</summary>
    public readonly ComboBox SortBox = new() { MinWidth = 120, ItemsSource = new[] { "Title", "Region", "Genre" }, SelectedIndex = 0 };
    /// <summary>The Views pane's own footer (consolidation round, mockup's "Backed up to PC" note).</summary>
    public readonly TextBlock ViewsFooterText = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11, TextWrapping = Avalonia.Media.TextWrapping.Wrap };

    public CardLoad? Load { get; private set; }
    /// <summary>Undo/redo of staged edits: Ctrl+Z/Ctrl+Y, Edit menu, 20+ levels.
    /// Reset on every OpenCard() (no cross-card history).</summary>
    public readonly Core.Card.EditHistory History = new();

    /// <summary>Call at the top of a staging action, before it mutates anything: pushes the pre-change state.</summary>
    void Stage()
    {
        if (Load != null) History.Push(Load);
    }

    /// <summary>RestoreWindow's own entry point to the same undo stack every other staging action uses (Stage()
    /// above is private -- RestoreWindow is a separate class) -- so Ctrl+Z after "Stage restore" reverts it like
    /// any other edit, not a lingering half-undone restore.</summary>
    public void StageForUndo() => Stage();

    readonly MenuItem _undoItem = new() { Header = "Undo", IsEnabled = false, InputGesture = new KeyGesture(Key.Z, KeyModifiers.Control) };
    readonly MenuItem _redoItem = new() { Header = "Redo", IsEnabled = false, InputGesture = new KeyGesture(Key.Y, KeyModifiers.Control) };
    readonly MenuItem _editMenu = new() { Header = "_Edit" };
    /// <summary>"Batch rename…" moved off the toolbar and into the Edit menu (punch-list round: one toolbar row).</summary>
    readonly MenuItem _batchRenameItem = new() { Header = "Batch rename…", IsEnabled = false };

    void RefreshUndoRedoMenu()
    {
        _undoItem.IsEnabled = History.CanUndo;
        _redoItem.IsEnabled = History.CanRedo;
    }

    public void UndoLast()
    {
        if (Load == null || !History.Undo(Load)) return;
        AfterHistoryChange("Undid the last change.");
    }

    public void RedoLast()
    {
        if (Load == null || !History.Redo(Load)) return;
        AfterHistoryChange("Redid the change.");
    }

    void AfterHistoryChange(string status)
    {
        _coverCache.Clear();
        _folderArtCache.Clear();
        RefreshGameList();
        ShowDetails(GameList.SelectedItem as GameRow);
        UpdateStatusCounts();
        RefreshUndoRedoMenu();
        Status.Text = status;
    }

    /// <summary>Consolidation round ("one library instead of two panes"): the left pane is a short, fixed list of
    /// views, not a folder browser -- what you want to see, not where files live. All replaces the old folder-tree
    /// browsing (now the library list's own expand-in-place, still _collapsed-driven); the others are flat,
    /// cross-folder filters, same idea as the old "\0FAV"/"\0RECENT" sentinels but named instead of stringly typed.</summary>
    public enum LibraryView { All, Favorites, Recent, Attention, NoCover, NotInDb, Backups }
    LibraryView _activeView = LibraryView.All;
    List<object> _view = [];
    /// <summary>A game's indent level in the "All games" view's recursive folder tree (its real folder depth);
    /// unused (0) in every flat view. Keyed by the live GameRow instance, which OpenCard/Refresh recreate but a
    /// resort/re-filter within the same Load does not -- cleared and rebuilt every RefreshGameList.</summary>
    readonly Dictionary<GameRow, int> _gameDepth = new();
    string _sortCol = "title";
    bool _sortDesc;
    GameRow? _editingRow;
    bool _loadingDetails;

    /// <summary>The Aether mark on its navy tile (assets/logo/aether_icon_1024.png), packed into Assets/icon.ico at commit time;
    /// loaded once so the title-bar render matches the .exe's icon exactly. Null (never throws) when the resource is
    /// missing, e.g. a build that did not embed it.</summary>
    static readonly Lazy<WindowIcon?> AppIcon = new(() =>
    {
        try { using var s = AssetLoader.Open(new Uri("avares://AetherCardManager/Assets/icon.ico")); return new WindowIcon(s); }
        catch (Exception e) when (e is FileNotFoundException or IOException) { return null; }
    });

    /// <summary>Tests: the window and taskbar icon actually loaded (the embedded Assets/icon.ico resource resolved).</summary>
    public static bool IconLoaded => AppIcon.Value != null;

    /// <summary>The Aether mark (no tile, its glow baked for the dark header; Assets/header_mark.png, 88 x 53, shown at
    /// 44 x 26.5) for the branded header row's own Image -- the native OS title bar/chrome stays
    /// (ExtendClientAreaToDecorationsHint and a custom drag/min/max/close is a cross-platform risk this restyle does not
    /// need), so this is a second, in-content header row just below it, not a chrome replacement.</summary>
    static readonly Lazy<Bitmap?> AppLogoBitmap = new(() =>
    {
        try { using var s = AssetLoader.Open(new Uri("avares://AetherCardManager/Assets/header_mark.png")); return new Bitmap(s); }
        catch (Exception e) when (e is FileNotFoundException or IOException or NotSupportedException or ArgumentException) { return null; }
    });

    /// <param name="removableDriveListForTests">Tests only: replaces the real removable-drive enumeration from the
    /// very first drive detection (the constructor's own RefreshDriveList() call below) -- a constructor parameter,
    /// not a property set by the usual object-initializer syntax, because that runs only after the constructor (and
    /// so after this first detection) has already returned, which is too late to stop a real removable drive from
    /// auto-selecting and opening itself before a test gets a chance to override anything.</param>
    readonly UiSettings _settings;

    /// <param name="autoBackupOnOpen">PC backup/restore: same timing concern as removableDriveListForTests above --
    /// RefreshDriveList() below can call OpenCard synchronously, during construction, for a real or injected drive
    /// at index 0, so this must be set before that call too, not by an object-initializer property afterward (that
    /// bug, already fixed once for the drive list, would otherwise mean the single most common path -- the app
    /// auto-opening whatever card is plugged in -- never backs up at all).</param>
    public MainWindow(Func<List<string>>? removableDriveListForTests = null, bool autoBackupOnOpen = false, bool checkUpdatesOnStart = false)
    {
        RemovableDriveListForTests = removableDriveListForTests;
        AutoBackupOnOpen = autoBackupOnOpen;
        _settings = UiSettings.Load();
        _autoUpdateItem.IsChecked = _settings.CheckForUpdates;
        _autoUpdateItem.Click += (_, _) => { _settings.CheckForUpdates = !_settings.CheckForUpdates; _autoUpdateItem.IsChecked = _settings.CheckForUpdates; _settings.Save(); };
        if (checkUpdatesOnStart && _settings.CheckForUpdates) Opened += async (_, _) => await CheckForUpdates(manual: false);
        Title = $"{Credits.Product} {Credits.Version}";
        Width = 1180;
        Height = 760;
        MinWidth = 900;     // below this the tree/center/details three-pane layout has nothing left for the center column
        MinHeight = 520;
        Icon = AppIcon.Value;
        WireLegacyHandlers();
        ApplyTheme();
        WireThemeControls();

        Closing += OnClosing;
        Content = BuildLayout();
        RefreshDriveList();

        // Packaging: a packaged release ships its own "release" folder next to the exe, so Build card
        // needs no setup -- auto-fill ReleasePath when one is there and looks complete, never overriding a user's
        // own choice on a later OpenCard/refresh.
        string bundled = Path.Combine(AppContext.BaseDirectory, "release");
        if (Directory.Exists(bundled) && Core.Card.CardPipeline.CheckRelease(bundled, compat: true).Count == 0)
            ReleasePath.Text = bundled;
    }

    /// <summary>The Saturn-menu look (user-approved mockup): the window's own gradient
    /// background and text color, plus a few Style rules for the things that repeat across the window (panes,
    /// buttons, text inputs, the tree/list selection highlight) -- never set on App, so this stays scoped to this
    /// window and never touches AboutWindow or ProblemsWindow, neither of which this restyle covers.</summary>
    void ApplyTheme()
    {
        AppTheme.ApplyToWindow(this);
        // the tree's and the game list's selected/hovered row (".tree .on" / "tr.on" in the mockup): a blue-gradient
        // bar with a left accent edge, on both FolderList and GameList -- Fluent's own ListBoxItem control theme sets
        // an explicit (light-theme, near-black) Foreground on the item container, which ApplyToList corrects.
        foreach (var list in new ListBox[] { ViewsList, GameList, CoversList }) AppTheme.ApplyToList(list);
    }

    void WireLegacyHandlers()
    {
        Browse.Click += async (_, _) => await PickCard();
        BrowseRelease.Click += async (_, _) => await PickFolder(ReleasePath, "The Aether release folder");
        BuildCard.Click += async (_, _) => await RunBuild();
        CancelButton.Click += (_, _) => CancelImport();
        AboutButton.Click += async (_, _) => await new AboutWindow().ShowDialog(this);
    }

    // =========================== Layout ===========================

    Control BuildLayout()
    {
        // "Fill missing covers…" (Tools), "Import from Kai/Rings…" and "Tidy saves…" (Saves) are removed for 0.1
        // (user: the bulk cover fill is "too broad and dangerous"; saves import/tidy return post-0.1 with proper
        // UI). Both menus had only those items, so both menus are gone too, not left empty. Per-game Change
        // cover/Restore cover are untouched; the Core save-import/tidy/cover-fill code is untouched, just unreached
        // from here -- see RunImport/RunTidy/FillMissingCovers' own removal notes in git history if this returns.
        var menu = new Menu
        {
            Items =
            {
                new MenuItem { Header = "_File", Items = { MI("Open card…", async () => await PickCard()),
                                                            MI("Refresh", () => OpenCard(CardPath.Text ?? "")),
                                                            MI("Export report…", async () => await ExportReport()),
                                                            new Separator(),
                                                            MI("Open backups folder", OpenBackupsFolder),
                                                            MI("Restore from backup…", async () => await ShowRestoreWindow()),
                                                            new Separator(), MI("Exit", Close) } },
                _editMenu,
                new MenuItem { Header = "_View", Items = { MI("Expand all", () => SetAllCollapsed(false)),
                                                            MI("Collapse all", () => SetAllCollapsed(true)),
                                                            new Separator(),
                                                            new MenuItem { Header = "_Theme", Items = { _lightThemeItem, _darkThemeItem } } } },
                new MenuItem { Header = "_Help", Items = { MI("Check for updates…", async () => await CheckForUpdates(manual: true)),
                                                            _autoUpdateItem,
                                                            new Separator(),
                                                            MI("Send feedback…", async () => await new FeedbackWindow(InstalledOnCard(), context: FeedbackContextFor()).ShowDialog(this)),
                                                            MI("About", async () => await new AboutWindow().ShowDialog(this)) } },
            },
        };

        // The card chip (mockup ".card"): a rounded pane around the drive picker, with a green
        // dot when a card is actually open and read (never lying about "connected" when nothing has been opened).
        var cardDot = new Border { Width = 10, Height = 10, CornerRadius = new CornerRadius(5), [!Border.BackgroundProperty] = AppTheme.Dyn("Placeholder"),
                                   VerticalAlignment = VerticalAlignment.Center };
        _cardDot = cardDot;
        var cardChipLabels = new StackPanel { Children = { CardChipLabel, CardChipSubtitle } };
        // Punch-list round: the drive ComboBox/Refresh read as low-contrast grey boxes inside the chip -- restyled
        // here as minimal-chrome (transparent, no border, just a caret/icon), not via the shared Window-level Style
        // (which every other ComboBox/Button in the app still uses), since only the chip's own pair should look
        // this way.
        DrivePicker.Background = Brushes.Transparent; DrivePicker.BorderThickness = new Thickness(0);
        DrivePicker.Bind(TextBlock.ForegroundProperty, "Muted"); DrivePicker.MinWidth = 0; DrivePicker.Padding = new Thickness(6, 0);
        DrivePicker.HorizontalContentAlignment = HorizontalAlignment.Left;
        // Capped so a long volume label (or the --card dev flag's own full folder path, used only for screenshots
        // and never real hardware) can't balloon the chip and squeeze the search box out of the single-row toolbar.
        DrivePicker.MaxWidth = 150;
        DrivePicker.ItemTemplate = new Avalonia.Controls.Templates.FuncDataTemplate<object>((item, _) =>
            new TextBlock { Text = item?.ToString(), [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), MaxWidth = 150,
                            TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis });
        RefreshDrives.Content = "⟳"; RefreshDrives.Width = 28; RefreshDrives.Padding = new Thickness(0);
        ToolTip.SetTip(RefreshDrives, "Refresh the drive list");
        ToolTip.SetTip(ListModeButton, "Show the games as a list");
        ToolTip.SetTip(CoversModeButton, "Show the games as a wall of covers");
        ToolTip.SetTip(SaveChangesButton, "No unsaved changes");
        // Prepare and Save changes are often disabled (nothing to do); their tips explain that too
        ToolTip.SetShowOnDisabled(PrepareCardButton, true);
        ToolTip.SetShowOnDisabled(SaveChangesButton, true);
        var cardChip = new Border
        {
            // Padding (10,3), not the pane default's own 7: this row's real height should come from its 28px
            // ComboBox/Button contents, not an oversized chip forcing the whole top bar (SearchBox included) taller.
            Classes = { "pane" }, Padding = new Thickness(10, 3), VerticalAlignment = VerticalAlignment.Center,
            Child = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, VerticalAlignment = VerticalAlignment.Center,
                                     Children = { cardDot, cardChipLabels, DrivePicker, RefreshDrives } },
        };
        // The search box's "Ctrl F" hint (mockup's own <kbd>) is a decorative overlay, not a real control -- a Grid
        // over the TextBox, right-aligned, IsHitTestVisible false so it never steals a click from the field under it.
        var searchHint = new Border
        {
            [!Border.BackgroundProperty] = AppTheme.Dyn("PillBg"), CornerRadius = new CornerRadius(5), Padding = new Thickness(6, 1),
            Margin = new Thickness(0, 0, 6, 0), HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center,
            IsHitTestVisible = false,
            Child = new TextBlock { Text = "Ctrl F", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11, FontWeight = FontWeight.SemiBold },
        };
        // flex:1, max-width 420 (mockup's own ".search"): grows with the window but never past 420, pill-radius 10
        // (already ApplyToWindow's default TextBox CornerRadius); a Grid, not a StackPanel, so this single child can
        // actually be the DockPanel's own "fill" region below. HorizontalAlignment.Left, not Stretch (advisor
        // round): the DockPanel's fill child is always arranged to the FULL remaining rect between the chip and the
        // right-hand buttons, so once MaxWidth caps this Grid below that rect's width, Stretch alignment centered it
        // in the leftover space instead of sitting next to the chip the way the mockup's own search box does.
        var searchBoxArea = new Grid { MinWidth = 140, HorizontalAlignment = HorizontalAlignment.Stretch, Margin = new Thickness(10, 0, 10, 0),
                                       VerticalAlignment = VerticalAlignment.Center, Children = { SearchBox, searchHint } };
        KeyBindings.Add(new KeyBinding { Gesture = new KeyGesture(Key.F, KeyModifiers.Control), Command = new RelayCommand(() => SearchBox.Focus()) });

        // One toolbar row (punch-list round): chip | search (flex) | [List/Covers toggle's own place, Phase C] |
        // installed pill/Prepare button | Save changes. Batch rename moved to the Edit menu (it has no place of its
        // own in the mockup's own toolbar).
        PrepareCardButton.Click += async (_, _) => await PrepareCard();
        // List/Covers (Phase C, filling in the place the toolbar restructure round reserved): a segmented pair,
        // styled as one pill (shared CornerRadius split left/right) rather than two separate buttons.
        ListModeButton.CornerRadius = new CornerRadius(8, 0, 0, 8);
        CoversModeButton.CornerRadius = new CornerRadius(0, 8, 8, 0);
        ListModeButton.Padding = CoversModeButton.Padding = new Thickness(10, 5);
        ListModeButton.Click += (_, _) => SetCoversMode(false);
        CoversModeButton.Click += (_, _) => SetCoversMode(true);
        var viewModeToggle = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 1, VerticalAlignment = VerticalAlignment.Center,
                                              Children = { ListModeButton, CoversModeButton } };
        var topBarRight = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center,
                                           Children = { ThemeToggleButton, viewModeToggle, PrepareCardButton, SaveChangesButton } };
        DockPanel.SetDock(cardChip, Dock.Left);
        DockPanel.SetDock(topBarRight, Dock.Right);
        // the fill region: column 0 grows with the window up to 420px and sits next to the chip; column 1 takes the rest
        var searchFlex = new Grid { ColumnDefinitions = { new ColumnDefinition(1, GridUnitType.Star) { MaxWidth = 430 }, new ColumnDefinition(0.0001, GridUnitType.Star) }, Children = { searchBoxArea } };
        var topBar = new DockPanel { Margin = new Thickness(8, 6), Children = { cardChip, topBarRight, searchFlex } };
        DockPanel.SetDock(topBar, Dock.Top);

        _editMenu.Items.Add(_undoItem);
        _editMenu.Items.Add(_redoItem);
        _editMenu.Items.Add(new Separator());
        _editMenu.Items.Add(_batchRenameItem);
        _batchRenameItem.Click += async (_, _) => await BatchRenameSelected();
        _editMenu.SubmenuOpened += (_, _) => { RefreshUndoRedoMenu(); _batchRenameItem.IsEnabled = BatchRenameButton.IsEnabled; };
        _undoItem.Click += (_, _) => UndoLast();
        _redoItem.Click += (_, _) => RedoLast();
        KeyBindings.Add(new KeyBinding { Gesture = new KeyGesture(Key.Z, KeyModifiers.Control), Command = new RelayCommand(UndoLast) });
        KeyBindings.Add(new KeyBinding { Gesture = new KeyGesture(Key.Y, KeyModifiers.Control), Command = new RelayCommand(RedoLast) });

        DrivePicker.SelectionChanged += (_, _) => OnDrivePicked();
        RefreshDrives.Click += (_, _) => RefreshDriveList();
        SearchBox.TextChanged += (_, _) => RefreshGameList();
        SaveChangesButton.Click += async (_, _) => await SaveChanges();
        BatchRenameButton.Click += async (_, _) => await BatchRenameSelected();

        DockPanel.SetDock(NotSystemDrive, Dock.Top);

        // The left pane is a short, fixed list of views (consolidation round, "the left side becomes a short list of
        // views: what you want to see, not where files live") -- not a folder browser any more. No explicit Width
        // here: the pane's own Grid ColumnDefinition (treeCol, below) sets it, so the splitter's drag has somewhere
        // to apply it.
        var leftContent = new DockPanel();
        var viewsFooter = new Border { Padding = new Thickness(10), BorderThickness = new Thickness(0, 1, 0, 0), [!Border.BorderBrushProperty] = AppTheme.Dyn("Line"), Child = ViewsFooterText };
        DockPanel.SetDock(viewsFooter, Dock.Bottom);
        leftContent.Children.Add(viewsFooter);
        leftContent.Children.Add(new ScrollViewer { Content = ViewsList, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });
        var left = leftContent;
        ViewsList.ItemTemplate = new FuncDataTemplate<object>((item, _) => item switch
        {
            string header => BuildViewSectionHeader(header),
            NavViewItem v => BuildViewItemView(v),
            _ => new TextBlock(),
        }, supportsRecycling: true);
        ViewsList.SelectionChanged += (_, _) => _ = OnViewSelected();

        GameList.AddHandler(KeyDownEvent, OnLibraryKeyDown, Avalonia.Interactivity.RoutingStrategies.Tunnel);

        BuildGameListHeader();
        GameList.ItemsPanel = new FuncTemplate<Panel?>(() => new VirtualizingStackPanel());
        // Advisor round: GameList's own control-template ScrollViewer defaults its OWN HorizontalScrollBarVisibility
        // to Auto too, same as centerScroll's -- a real screenshot still showed a horizontal thumb at 1065 after
        // widening RecalcTitleColumn's safety margin, which this (a redundant inner bar, not an actual further
        // width overflow) explains. centerScroll is the one and only horizontal scrollbar this list should ever show.
        ScrollViewer.SetHorizontalScrollBarVisibility(GameList, ScrollBarVisibility.Disabled);
        GameList.ItemTemplate = new FuncDataTemplate<object>((item, _) => item switch
        {
            FolderListRow f => BuildFolderRowView(f),
            GameRow r => BuildGameRowView(r),
            _ => new TextBlock(),
        }, supportsRecycling: true);
        GameList.SelectionChanged += (_, _) => OnGameSelectionChanged();
        WireGameListContextMenu(GameList);
        var center = new DockPanel { MinWidth = ColSum() };
        DockPanel.SetDock(_headerRow, Dock.Top);
        center.Children.Add(_headerRow);
        center.Children.Add(GameList);
        // Resizable columns make the list wider than the pane at the default sizes (coordinator round, "no text
        // anywhere should be unreachable"): one ScrollViewer around header + rows together, so they always scroll in
        // lock-step; vertical scrolling stays GameList's own (it is a ListBox, already virtualized/scrollable).
        // Auto, not Disabled: a tested, deliberate safety valve for a window narrower than this round's own two
        // target sizes (TreeAndProblemsTests, "the game list scrolls horizontally") -- a column would otherwise
        // silently clip below the Title column's own 100px floor. RecalcTitleColumn is what should keep this bar
        // from ever actually appearing at 1400x900/1065x783 specifically; its own buffer is what needed widening.
        var centerScroll = new ScrollViewer { Content = center, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled };
        // Punch-list round, "no horizontal scrollbar at 1400px or 1065px": Title is the one column that grows/
        // shrinks with the window (the mockup's own minmax(0,1fr)), recomputed here on every resize rather than as
        // a real Grid Star column -- the header (_headerRow, a StackPanel of fixed-width Buttons) and the data rows
        // (a Grid built from the same _colWidths) are two independently-measured layouts kept in sync only by both
        // reading that one array, so a Star column in the Grid alone would desync from the header's own static
        // button widths as the window resizes. Recomputing the fixed width here, then rebuilding both from it,
        // reaches the same visual result without that split.
        _centerListContainer = center;
        _centerScroll = centerScroll;
        centerScroll.SizeChanged += (_, e) => { _listAvailWidth = e.NewSize.Width; RecalcTitleColumn(_listAvailWidth, center); };

        // The library pane's own title row (consolidation round, mockup's ".lh") -- fixed, never scrolls with the
        // columns, unlike _headerRow/GameList inside centerScroll above.
        SortBox.SelectionChanged += (_, _) =>
        {
            _sortCol = (SortBox.SelectedItem as string) switch { "Region" => "region", "Genre" => "genre", _ => "title" };
            _sortDesc = false;
            BuildGameListHeader();
            RefreshGameList();
        };
        var libraryHeader = new DockPanel { Margin = new Thickness(14, 10, 14, 8) };
        var sortRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { new TextBlock { Text = "Sort:", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center }, SortBox } };
        DockPanel.SetDock(sortRow, Dock.Right);
        libraryHeader.Children.Add(sortRow);
        libraryHeader.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Children = { LibraryTitle, LibrarySubtitle } });
        // Phase C: the cover wall. A WrapPanel of bigger tiles (BuildCoverTile), its own ScrollViewer (vertical
        // only -- a wall wraps instead of needing horizontal scroll), in the same Grid cell as centerScroll so
        // SetCoversMode can just flip IsVisible between the two instead of rebuilding centerContent each time.
        CoversList.ItemsPanel = new FuncTemplate<Panel?>(() => new WrapPanel());
        CoversList.ItemTemplate = new FuncDataTemplate<object>((item, _) => item is GameRow r ? BuildCoverTile(r) : new TextBlock(), supportsRecycling: true);
        CoversList.SelectionChanged += (_, _) => { if (CoversList.SelectedItem is GameRow r) GameList.SelectedItem = r; };
        CoversList.IsVisible = false;
        var coversScroll = new ScrollViewer { Content = CoversList, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, IsVisible = false };
        _coversScroll = coversScroll;
        var centerSwitch = new Grid { Children = { centerScroll, coversScroll } };

        var centerContent = new DockPanel();
        DockPanel.SetDock(libraryHeader, Dock.Top);
        centerContent.Children.Add(libraryHeader);
        centerContent.Children.Add(centerSwitch);

        var side = BuildSidePanel();

        // Pane proportions: the Views pane defaults to the mockup's own ~212px (punch-list round, superseding the
        // earlier user-feedback round's ~33%/300px-minimum -- a fixed list of 7 short view names never needed that
        // much room the way a folder tree did); still a draggable splitter, with the last-dragged width remembered
        // across launches (UiSettings) -- never reset by a resize of the window itself, only by dragging again.
        double treeWidth = _settings.TreePaneWidth ?? 212;
        double detailsWidth = Math.Max(280, _settings.DetailsPaneWidth ?? 300);
        var treeCol = new ColumnDefinition(treeWidth, GridUnitType.Pixel) { MinWidth = 180 };
        var centerCol = new ColumnDefinition(1, GridUnitType.Star);
        var detailsCol = new ColumnDefinition(detailsWidth, GridUnitType.Pixel) { MinWidth = 280 };
        var mainGrid = new Grid { ColumnDefinitions = { treeCol, new ColumnDefinition(5, GridUnitType.Pixel), centerCol, new ColumnDefinition(5, GridUnitType.Pixel), detailsCol } };
        // Rounded translucent panels (Saturn-menu look): each pane is wrapped in its own
        // Border.pane (ApplyTheme's Style), never changing what FolderList/GameList/DetailsScrollViewer's own
        // Parent chain is -- just one more layer outside what was already there.
        var leftPane = new Border { Classes = { "pane" }, Margin = new Thickness(0, 10, 4, 10), Child = left };
        var centerPane = new Border { Classes = { "pane" }, Margin = new Thickness(4, 10, 4, 10), Child = centerContent };
        side.Classes.Add("pane");
        side.Margin = new Thickness(4, 10, 0, 10);
        Grid.SetColumn(leftPane, 0); Grid.SetColumn(centerPane, 2); Grid.SetColumn(side, 4);
        var treeSplitter = BuildPaneSplitter(treeCol, growsRight: true, () => { _settings.TreePaneWidth = treeCol.Width.Value; _settings.Save(); });
        var detailsSplitter = BuildPaneSplitter(detailsCol, growsRight: false, () => { _settings.DetailsPaneWidth = detailsCol.Width.Value; _settings.Save(); });
        Grid.SetColumn(treeSplitter, 1); Grid.SetColumn(detailsSplitter, 3);
        mainGrid.Children.Add(leftPane); mainGrid.Children.Add(treeSplitter); mainGrid.Children.Add(centerPane);
        mainGrid.Children.Add(detailsSplitter); mainGrid.Children.Add(side);
        mainGrid.Margin = new Thickness(14, 0, 14, 12);
        mainGrid.ColumnSpacing = 0;   // the splitters themselves are the gap; a real ColumnSpacing would double it

        // "N problems" (the status-bar pill) is gone (consolidation round): the Views pane's "Needs attention"/
        // "No cover"/"Not in database" rows replace it -- selecting one shows exactly those games, in place, instead
        // of a separate window.
        // Punch-list round, "One bar": Status used to be its own Dock.Bottom row above this one -- a second line
        // for the same "N games · M covers / what's staged / Ctrl+Z to undo" idea the mockup draws as a single row.
        // Status itself is unchanged (every Status.Text assignment across the app still works exactly the same);
        // it just lives in the middle column here now, ellipsised (not wrapped, which would grow the bar's own
        // height) with its full text always available as a tooltip for a long message like "Save changes blocked:".
        var statusBar = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto,Auto,Auto"), Margin = new Thickness(8, 4) };
        Status.TextWrapping = Avalonia.Media.TextWrapping.NoWrap;
        Status.TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis;
        Status.Margin = new Thickness(12, 0);
        Status.PropertyChanged += (_, e) => { if (e.Property == TextBlock.TextProperty) ToolTip.SetTip(Status, Status.Text); };
        Grid.SetColumn(CountsText, 0); Grid.SetColumn(Status, 1); Grid.SetColumn(UnsavedText, 2); Grid.SetColumn(Progress, 3); Grid.SetColumn(CancelButton, 4);
        UnsavedText.Margin = Progress.Margin = new Thickness(12, 0, 0, 0);
        statusBar.Children.Add(CountsText); statusBar.Children.Add(Status); statusBar.Children.Add(UnsavedText);
        statusBar.Children.Add(Progress); statusBar.Children.Add(CancelButton);
        DockPanel.SetDock(statusBar, Dock.Bottom);

        // The branded header row ("the Saturn-ring logo in the title bar"): the native OS title
        // bar/chrome stays (a custom one is a cross-platform risk this restyle does not need), so this is a second
        // row, just below it, with the same dark overlay + ring logo the mockup's own title bar shows.
        var headerRow = new Border
        {
            [!Border.BackgroundProperty] = AppTheme.Dyn("HeaderBarBg"), BorderThickness = new Thickness(0, 0, 0, 1), [!Border.BorderBrushProperty] = AppTheme.Dyn("Line"),
            Padding = new Thickness(14, 8),
            Child = new StackPanel
            {
                Orientation = Orientation.Horizontal, Spacing = 10,
                Children =
                {
                    HeaderLogo(),
                    // Credits.Product is already the full "Aether Card Manager" -- a second "Card Manager" label
                    // here used to repeat it (user feedback round).
                    new TextBlock { Text = $"{Credits.Product}", FontWeight = FontWeight.Bold, FontSize = 15, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Acc2"), VerticalAlignment = VerticalAlignment.Center },
                    new TextBlock { Text = Credits.Version, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center },
                },
            },
        };
        DockPanel.SetDock(headerRow, Dock.Top);

        statusBar.Bind(Border.BackgroundProperty, "HeaderBg");

        var root = new DockPanel();
        root.Children.Add(headerRow);
        DockPanel.SetDock(menu, Dock.Top);
        root.Children.Add(menu);
        root.Children.Add(statusBar);
        root.Children.Add(topBar);
        DockPanel.SetDock(UpdateBanner, Dock.Top);
        root.Children.Add(UpdateBanner);
        root.Children.Add(NotSystemDrive);
        root.Children.Add(mainGrid);
        return root;
    }

    /// <summary>The header's Aether mark: the PNG as drawn (pale, its glow baked for a dark header) in the Dark theme;
    /// in Light, the same shape filled with the accent blue (the PNG as an opacity mask), since a pale mark would
    /// vanish on the white header bar.</summary>
    static Control HeaderLogo()
    {
        if (AppLogoBitmap.Value is not Bitmap logo) return new Panel { IsVisible = false };
        return new Panel
        {
            Width = 44, Height = 26.5,
            Children =
            {
                new Image { Source = logo, [!IsVisibleProperty] = AppTheme.Dyn("IsDark") },
                new Border { [!Border.BackgroundProperty] = AppTheme.Dyn("Acc"), [!IsVisibleProperty] = AppTheme.Dyn("IsLight"),
                             OpacityMask = new ImageBrush(logo) { Stretch = Stretch.Uniform } },
            },
        };
    }

    // ---- View > Theme ----
    readonly MenuItem _lightThemeItem = new() { Header = "_Light", ToggleType = MenuItemToggleType.Radio, GroupName = "theme" };
    readonly MenuItem _darkThemeItem = new() { Header = "_Dark", ToggleType = MenuItemToggleType.Radio, GroupName = "theme" };
    /// <summary>The toolbar's one-click Light/Dark switch (a half-filled circle), beside the List/Covers toggle.</summary>
    public readonly Button ThemeToggleButton = new()
    {
        Width = 30, Height = 30, Padding = new Thickness(0), VerticalAlignment = VerticalAlignment.Center,
        HorizontalContentAlignment = HorizontalAlignment.Center, VerticalContentAlignment = VerticalAlignment.Center,
        Content = new Panel
        {
            Width = 14, Height = 14,
            Children =
            {
                new Avalonia.Controls.Shapes.Ellipse { Width = 14, Height = 14, StrokeThickness = 1.6, [!Avalonia.Controls.Shapes.Shape.StrokeProperty] = AppTheme.Dyn("Text") },
                new Avalonia.Controls.Shapes.Path { Data = Geometry.Parse("M 7,0 A 7,7 0 0 0 7,14 Z"), [!Avalonia.Controls.Shapes.Shape.FillProperty] = AppTheme.Dyn("Text") },
            },
        },
    };

    void WireThemeControls()
    {
        _lightThemeItem.Click += (_, _) => SetTheme(ThemeKind.Light);
        _darkThemeItem.Click += (_, _) => SetTheme(ThemeKind.Dark);
        ThemeToggleButton.Click += (_, _) => SetTheme(AppTheme.Kind == ThemeKind.Light ? ThemeKind.Dark : ThemeKind.Light);
        RefreshThemeControls();
    }

    /// <summary>View > Theme (or the toolbar switch): applies live to every open window and is remembered in the
    /// per-user UI settings.</summary>
    public void SetTheme(ThemeKind kind)
    {
        AppTheme.Use(kind);
        _settings.Theme = kind.ToString();
        _settings.Save();
        RefreshThemeControls();
    }

    void RefreshThemeControls()
    {
        _lightThemeItem.IsChecked = AppTheme.Kind == ThemeKind.Light;
        _darkThemeItem.IsChecked = AppTheme.Kind == ThemeKind.Dark;
        ToolTip.SetTip(ThemeToggleButton, AppTheme.Kind == ThemeKind.Light ? "Switch to the Dark theme" : "Switch to the Light theme");
    }

    static MenuItem MI(string text, Action action) => new() { Header = text, Command = new RelayCommand(action) };
    static MenuItem MI(string text, Func<Task> action) => new() { Header = text, Command = new RelayCommand(() => _ = action()) };

    // Column headers and their starting widths (coordinator round: resizable, so these are only the defaults --
    // _colWidths is what every header cell, row cell and the shared MinWidth actually reads). Consolidation round:
    // Code/Disc/Folder are gone as separate columns (Code moved to the inspector's own chips, Disc into the title
    // line, Folder is now the row's own position in the tree) -- Status replaces the lone "⚠" with the mockup's
    // pill set. Column 0 is wide enough for a folder row's chevron + its own thumbnail side by side.
    // Punch-list round: a fixed pixel budget, not true minmax(0,1fr) -- the header (_headerRow, a StackPanel of
    // fixed-width Buttons) and the data rows (a Grid with these same widths) are kept in sync only by both reading
    // _colWidths, so a Star/flex column in the Grid alone would desync from the header's static buttons as the
    // window resizes. Title's own 160 here is only its starting value -- RecalcTitleColumn (below) recomputes it on
    // every resize to fill whatever the other four columns don't need, the mockup's own minmax(0,1fr) without a
    // real Star column. Status stays comfortably above the 128px the status pills need so "Not in DB"/"Cover
    // pack"/"Edited"/a star never clip (screenshot-verified at both 1400x900 and 1065x783).
    static readonly (string Header, double Width)[] Columns =
        [("", 64), ("Title", 160), ("Region", 68), ("Loader", 85), ("Status", 170)];

    readonly double[] _colWidths = Columns.Select(c => c.Width).ToArray();
    /// <summary>RecalcTitleColumn's own target and its last known available width -- a folder expanding/collapsing
    /// (ToggleCollapsed, via RefreshGameList below) can change the deepest row's own depth without the window
    /// itself resizing, so RefreshGameList re-checks the Title width too, not only centerScroll.SizeChanged. Gated
    /// on maxDepth actually changing (_lastMaxDepthForRecalc), not run on every RefreshGameList call: a user's own
    /// manual drag-resize of the Title column (BuildColumnSplitter, which also calls RefreshGameList) must survive
    /// a later refresh that has nothing to do with indentation -- a real test caught this recomputing (and
    /// silently undoing) a manual resize the very first time something else refreshed the list afterward.</summary>
    Control? _centerListContainer;
    double _listAvailWidth;
    double _lastMaxDepthForRecalc = -1;
    /// <summary>Phase C: the two Views the toolbar's List/Covers toggle swaps between -- set once in BuildLayout,
    /// read by SetCoversMode.</summary>
    ScrollViewer? _centerScroll, _coversScroll;

    /// <summary>The row Grid's column spec, shared by the header and every data row so a drag-resize (coordinator
    /// round) moves both together; its sum is the list's MinWidth, so a narrow window gets a horizontal scrollbar
    /// (ScrollViewer.SetHorizontalScrollBarVisibility on GameList) instead of silently clipping a column.</summary>
    string ColSpec() => string.Join(",", _colWidths.Select(w => w.ToString(System.Globalization.CultureInfo.InvariantCulture)));
    /// <summary>A row indented `indent` px gives that much up from its Title column, so Region/Loader/Status line up
    /// with the header on every row, nested or not (polish round).</summary>
    string ColSpec(double indent) => string.Join(",", _colWidths.Select((w, i) => (i == 1 ? Math.Max(40, w - indent) : w).ToString(System.Globalization.CultureInfo.InvariantCulture)));
    double ColSum() => _colWidths.Sum();

    /// <summary>Title (_colWidths[1]) grows or shrinks to fill whatever the other four columns don't need, so the
    /// game list fills the available width at any window size instead of leaving a gap (wide) or forcing a
    /// horizontal scrollbar (narrow) -- see centerScroll.SizeChanged's own comment for why this is a recompute, not
    /// a real Star column. 24px is a ListBox's own vertical scrollbar reserve (centerScroll's horizontal bar is
    /// Auto, but GameList's internal vertical one still eats into availableWidth); the 100/500 clamp keeps the
    /// column sane at both an unusually narrow and an unusually wide window.</summary>
    void RecalcTitleColumn(double availableWidth, Control center)
    {
        if (availableWidth <= 0) return;
        double others = _colWidths.Where((_, i) => i != 1).Sum();
        // The real rendered width is ColSum() plus a few things _colWidths itself doesn't account for: the header's
        // own 4px splitters between Columns.Length-1 cells (BuildGameListHeader), ApplyToList's own ListBoxItem
        // Padding (6+6 horizontal), and a nested row's own left margin (4 + depth*20, BuildGameRowView/
        // BuildFolderRowView) -- the deepest row in the current _view, not just depth 0, or an expanded folder two
        // levels deep would still get a scrollbar. A headless-test probe (ScrollViewer.Extent vs .Viewport) pinned
        // this exactly: Extent - ColSum() was 40px at maxDepth 1 and 60px at maxDepth 2, precisely 12 (ListBoxItem
        // padding) + 8 (row margin's own 4-left-base + 4-right) + depth*20 -- no extra fudge factor needed once this
        // is right, confirmed the same probe way.
        double maxDepth = _view.Count == 0 ? 0 : _view.Max(DepthOf);
        double headerSplitters = (Columns.Length - 1) * 4;
        double rowOwnWidth = 12 + 8;   // a nested row gives its indent up from its own Title column (ColSpec(indent))
        double buffer = Math.Max(headerSplitters, rowOwnWidth);
        // Floor 70, not 100: at 1065x783 with the Views pane at 212px and the inspector at its 280px floor, the
        // same probe measured only 78px left for Title after Region/Loader/Status/cover and the row overhead above
        // -- a 100px floor was the actual cause of the scrollbar this was meant to prevent (Title couldn't shrink
        // low enough to fit, not that the arithmetic was short). 70px still shows a few characters plus an ellipsis.
        // 1065x783 (the residual thin line at the list's foot was this list's horizontal scrollbar: Title hit its floor
        // and the row overflowed by ~9px): below a 110px Title, Status, Loader and Region give up their slack first,
        // down to 124/76/68 (Region keeps room for its own header), so Title has room and nothing scrolls sideways.
        double[] wide = [Columns[2].Width, Columns[3].Width, Columns[4].Width], narrow = [68, 76, 124];
        double slack = wide.Sum() - narrow.Sum();
        double need = Math.Clamp(110 - (availableWidth - (Columns[0].Width + wide.Sum()) - buffer), 0, slack);
        double[] was = [.. _colWidths];
        for (int i = 0; i < 3; i++) _colWidths[i + 2] = wide[i] - (wide[i] - narrow[i]) * (need / slack);
        others = _colWidths.Where((_, i) => i != 1).Sum();
        double title = Math.Clamp(availableWidth - others - buffer, 70, 500);
        if (Math.Abs(was[1] - title) < 0.5 && Enumerable.Range(2, 3).All(i => Math.Abs(was[i] - _colWidths[i]) < 0.5)) return;
        _colWidths[1] = title;
        center.MinWidth = ColSum();
        BuildGameListHeader();
        RefreshGameList();
    }

    /// <summary>A 5px drag handle between two panes ("pane proportions"): resizes the pane's own
    /// ColumnDefinition directly (never rebuilding the Grid mid-drag), highlights on hover/drag, and persists the
    /// new width (UiSettings) once the drag ends.</summary>
    Control BuildPaneSplitter(ColumnDefinition col, bool growsRight, Action onReleased)
    {
        var split = new Border { Width = 5, Background = Brushes.Transparent, Cursor = new Cursor(StandardCursorType.SizeWestEast) };
        Point? dragFrom = null;
        double startWidth = 0;
        split.PointerEntered += (_, _) => split.Bind(Border.BackgroundProperty, "HoverOverlay");
        split.PointerExited += (_, _) => { if (dragFrom == null) split.Bind(Border.BackgroundProperty, "Clear"); };
        split.PointerPressed += (_, e) => { dragFrom = e.GetPosition(this); startWidth = col.Width.Value; e.Pointer.Capture(split); };
        split.PointerMoved += (_, e) =>
        {
            if (dragFrom is not Point from) return;
            double dx = e.GetPosition(this).X - from.X;
            col.Width = new GridLength(Math.Max(col.MinWidth, startWidth + (growsRight ? dx : -dx)), GridUnitType.Pixel);
        };
        split.PointerReleased += (_, e) =>
        {
            if (dragFrom == null) return;
            dragFrom = null;
            e.Pointer.Capture(null);
            split.Bind(Border.BackgroundProperty, "Clear");
            onReleased();
        };
        return split;
    }

    readonly List<Button> _headerButtons = [];

    void BuildGameListHeader()
    {
        _headerRow.Children.Clear();
        _headerButtons.Clear();
        _headerRow.Bind(Border.BackgroundProperty, "HeaderBg");
        _headerRow.MinWidth = ColSum();
        for (int i = 0; i < Columns.Length; i++)
        {
            var (header, _) = Columns[i];
            var b = new Button { Content = header == "" ? " " : (header.ToUpperInvariant() + (_sortCol == header.ToLowerInvariant() ? (_sortDesc ? " ▼" : " ▲") : "")),
                                 Width = _colWidths[i], HorizontalContentAlignment = HorizontalAlignment.Left,
                                 Background = Brushes.Transparent, BorderThickness = new Thickness(0), FontWeight = FontWeight.Bold,
                                 [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Acc2"), FontSize = 12 };
            // Clicking a column header sorts the same way the Sort dropdown does (consolidation round) -- only
            // Title and Region have one here; Genre is dropdown-only (there is no Genre column to click).
            string col = header.ToLowerInvariant();
            if (col is "title" or "region") b.Click += (_, _) => { if (_sortCol == col) _sortDesc = !_sortDesc; else { _sortCol = col; _sortDesc = false; } BuildGameListHeader(); RefreshGameList(); };
            _headerButtons.Add(b);
            _headerRow.Children.Add(b);
            if (i < Columns.Length - 1) _headerRow.Children.Add(BuildColumnSplitter(i));
        }
    }

    /// <summary>A 4px drag handle between two header cells ("resizable columns"): drags
    /// _colWidths[i] and resizes the header button and the row MinWidth in place (never rebuilding _headerRow mid-
    /// drag, which would drop the pointer capture) -- RefreshGameList() alone is enough to re-lay the data rows.
    /// A splitter's own width is not part of _colWidths, so it does not shrink the columns it sits between.</summary>
    Control BuildColumnSplitter(int i)
    {
        var split = new Border { Width = 4, Background = Brushes.Transparent, Cursor = new Cursor(StandardCursorType.SizeWestEast) };
        Point? dragFrom = null;
        double startWidth = 0;
        split.PointerPressed += (_, e) => { dragFrom = e.GetPosition(this); startWidth = _colWidths[i]; e.Pointer.Capture(split); };
        split.PointerMoved += (_, e) =>
        {
            if (dragFrom is not Point from) return;
            _colWidths[i] = Math.Max(24, startWidth + (e.GetPosition(this).X - from.X));
            _headerButtons[i].Width = _colWidths[i];
            _headerRow.MinWidth = ColSum();
            RefreshGameList();
        };
        split.PointerReleased += (_, e) => { dragFrom = null; e.Pointer.Capture(null); };
        return split;
    }

    /// <summary>Every problem on `row` right now (the game list's "⚠" column, and its own tooltip/sort key).</summary>
    public int ProblemCountFor(GameRow row) => CurrentProblems.Count(p => p.Row == row);

    /// <summary>A game row in the one library list (consolidation round): the cover, a two-line title cell (the
    /// real title bold, the exact file name muted/monospace/ellipsized beneath it), region, loader, and the status
    /// pills (BuildStatusCell) -- Code/Disc moved to the inspector, Folder is now just the row's own tree position
    /// (its indent, from _gameDepth). Indented by its real folder depth in the "All games" view (0 in a flat view).</summary>
    Control BuildGameRowView(GameRow row)
    {
        int depth = _gameDepth.GetValueOrDefault(row, 0);
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions(ColSpec(depth * 20)), Margin = new Thickness(4 + depth * 20, 3, 4, 3), MinWidth = ColSum() - depth * 20 };
        Control cover = ThumbFor(row) is Bitmap thumb
            ? new Image { Width = 28, Height = 36, Stretch = Stretch.Uniform, Source = thumb }
            : NoCoverThumb(26, 36);
        Grid.SetColumn(cover, 0);

        Control titleCell;
        if (_editingRow == row)
        {
            var box = new TextBox { Text = row.TitleOverride != "" ? row.TitleOverride : row.DisplayTitle(DbByKey(), Load?.UserEdits) };
            box.AttachedToVisualTree += (_, _) => { box.Focus(); box.SelectAll(); };
            box.KeyDown += (_, e) =>
            {
                if (e.Key == Avalonia.Input.Key.Enter) { CommitRename(row, box.Text ?? ""); }
                else if (e.Key == Avalonia.Input.Key.Escape) { _editingRow = null; RefreshGameList(); }
            };
            box.LostFocus += (_, _) => { if (_editingRow == row) CommitRename(row, box.Text ?? ""); };
            titleCell = box;
        }
        else
        {
            var db = DbByKey();
            string text = row.DisplayTitle(db, Load?.UserEdits);
            string dbTitle = DbTitle(row, db);
            int n = ProblemCountFor(row);
            // Punch-list round: the title is always white/semibold now -- orange is a pill-only color (BuildStatusCell
            // already says "Not in DB"/"No cover" there); n (ProblemCountFor) still drives the tooltip below.
            var titleLine = new TextBlock { Text = text, VerticalAlignment = VerticalAlignment.Center, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis,
                                            FontWeight = FontWeight.SemiBold, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text") };
            // the user's standing rule (Satiator Rings' model): a game is listed by its exact name -- the menu's own
            // display name above, bold; its database title (or the user's edited one) muted beneath, or "Not in database"
            var fileLine = new TextBlock { Text = dbTitle != "" ? dbTitle : row.Key == "" ? "Not in database" : "", VerticalAlignment = VerticalAlignment.Center,
                                           TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis, FontSize = 11.5, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"),
                                           FontStyle = dbTitle == "" ? FontStyle.Italic : FontStyle.Normal };
            var stack = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Children = { titleLine, fileLine } };
            string tip = n > 0 ? string.Join("\n", CurrentProblems.Where(p => p.Row == row).Select(p => $"{Problem.KindText(p.Kind)}: {p.Explanation}")) : text;
            ToolTip.SetTip(stack, tip);
            ToolTip.SetTip(titleLine, tip);   // a tooltip lookup on the title TextBlock itself (not just its container) finds one too
            stack.DoubleTapped += (_, _) => { _editingRow = row; RefreshGameList(); };
            titleCell = stack;
        }
        Grid.SetColumn(titleCell, 1);

        var region = Cell(RegionLabel(row));
        var loader = Cell(row.Loader == "auto" ? "Auto" : row.Loader.ToUpperInvariant());
        var status = BuildStatusCell(row);
        Grid.SetColumn(region, 2); Grid.SetColumn(loader, 3); Grid.SetColumn(status, 4);

        grid.Children.Add(cover); grid.Children.Add(titleCell); grid.Children.Add(region); grid.Children.Add(loader); grid.Children.Add(status);
        return grid;
    }

    /// <summary>The Status column's pills (consolidation round): exactly one cover-status pill (Your cover/Cover
    /// pack/No cover/Not in DB, in that priority -- what the menu will actually show, SPEC cover lookup order), an
    /// Edited pill for a staged change, and a gold star for a favorite. "N cheats on" and a region-fallback cover
    /// note are deferred (they need the CHEATS.PAK reader and the cover pack's fallback_region data -- step 3).</summary>
    Control BuildStatusCell(GameRow row)
    {
        var panel = new WrapPanel { VerticalAlignment = VerticalAlignment.Center };
        // Sid != null, not just Key == "": an unreadable header is a different problem (UnreadableHeader, "Needs
        // attention"), not "Not in DB" -- the same distinction the NotInDb view and its own count use.
        if (row.Sid != null && row.Key == "") panel.Children.Add(StatusPill("Not in DB", "Warn"));
        else if (NoCoverAnywhere(row)) panel.Children.Add(StatusPill("No cover", "Warn"));
        else if (row.HasBox || row.PendingCover != null) panel.Children.Add(StatusPill("Your cover", "Muted"));
        else
        {
            var pill = StatusPill(PackLabel(row) is string l ? $"{l} cover" : "Cover pack", "Muted");
            if (!row.HasArt) ToolTip.SetTip(pill, "From the bundled cover pack: it goes on the card at the next Save changes or Prepare");
            panel.Children.Add(pill);
        }
        if (row.Key != "" && Load?.CheatsOn.GetValueOrDefault(row.Key) is int on and > 0)
            panel.Children.Add(StatusPill($"{on} cheat{(on == 1 ? "" : "s")} on", "Acc2"));
        if (row.Dirty) panel.Children.Add(StatusPill("Edited", "Acc2"));
        if (row.Favorite) panel.Children.Add(new TextBlock { Text = "★", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Gold"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(2, 0, 0, 0) });
        return panel;
    }

    static Border StatusPill(string text, string fg) => new()
    {
        [!Border.BackgroundProperty] = AppTheme.Dyn("PillBg"), CornerRadius = new CornerRadius(10), Padding = new Thickness(7, 1), Margin = new Thickness(0, 0, 4, 2),
        Child = new TextBlock { Text = text, [!TextBlock.ForegroundProperty] = AppTheme.Dyn(fg), FontSize = 11, FontWeight = FontWeight.SemiBold },
    };

    /// <summary>A cell whose text, when it is too long for its column, shows the full text in a tooltip (coordinator
    /// round, "no text anywhere should be unreachable") -- CharacterEllipsis alone only shows that there is more.</summary>
    /// <summary>The Region column's display spelling (punch-list round: "US / JP / EU, not U / E / J"). Only the
    /// single-letter case is remapped -- a multi-region disc's own compound code (e.g. "JTUE") is already a real,
    /// if unusual, abbreviation and is left as-is rather than guessed at.</summary>
    /// <summary>The list's muted second line (polish round, corrected by the user): the user's edited title, else a
    /// matched game's GAMES.DB title; "" for a game with no record.</summary>
    string DbTitle(GameRow row, Dictionary<string, GameRecord> db)
    {
        if (row.Key != "" && Load?.UserEdits.GetValueOrDefault(row.Key) is { Title.Length: > 0 } ue) return ue.Title;
        return row.Key != "" && db.TryGetValue(row.Key, out var rec) ? rec.TitleEn : "";
    }

    /// <summary>One clean region label (polish round): US, JP or EU (TW for T, BR for B), never a raw letter string or
    /// blank. A disc carrying one area is that area; one carrying several (JTUE) is its own release's region when the
    /// file name or GAMES.DB says which ("(USA)", "(Japan)", "(Europe)", or a single-region record), else the first of
    /// US, JP, EU it carries, else "Multi". A disc with no readable area falls back to GAMES.DB, then the file name.</summary>
    string RegionLabel(GameRow row)
    {
        static string Name(char c) => c switch { 'U' => "US", 'J' => "JP", 'E' => "EU", 'T' => "TW", 'B' => "BR", _ => "" };
        string area = new string([.. row.RegionText.ToUpperInvariant().Where(c => "JTUEBKAL".Contains(c)).Distinct()]);
        string fromName = row.Path.Contains("(USA", StringComparison.OrdinalIgnoreCase) || row.Path.Contains("(US)", StringComparison.Ordinal) ? "US"
                        : row.Path.Contains("(Japan", StringComparison.OrdinalIgnoreCase) || row.Path.Contains("(JP)", StringComparison.Ordinal) ? "JP"
                        : row.Path.Contains("(Europe", StringComparison.OrdinalIgnoreCase) || row.Path.Contains("(EU)", StringComparison.Ordinal) ? "EU" : "";
        string fromDb = row.Key != "" && DbByKey().TryGetValue(row.Key, out var rec) && GamesDb.RegionLettersFrom(rec.RegionFlags) is { Length: 1 } one ? Name(one[0]) : "";
        if (area.Length == 1 && Name(area[0]) != "") return Name(area[0]);
        if (area.Length == 0) return fromDb != "" ? fromDb : fromName != "" ? fromName : "—";
        if (fromName != "" && area.Contains(fromName == "US" ? 'U' : fromName == "JP" ? 'J' : 'E')) return fromName;
        if (fromDb != "" && area.Contains(fromDb[0] == 'U' ? 'U' : fromDb[0] == 'J' ? 'J' : fromDb[0] == 'E' ? 'E' : '?')) return fromDb;
        foreach (char c in "UJE") if (area.Contains(c)) return Name(c);
        return "Multi";
    }

    /// <summary>The "no cover" thumbnail (punch-list round): a hatched placeholder with a "?", never a blank box --
    /// used for both a game row with no art (BuildGameRowView) and the Inspector's own large cover when nothing
    /// matches it.</summary>
    static Control NoCoverThumb(double w, double h) => new Grid
    {
        Width = w, Height = h,
        Children =
        {
            new Border { [!Border.BackgroundProperty] = AppTheme.Dyn("Placeholder"), CornerRadius = new CornerRadius(3) },
            AppTheme.Hatch(w, h).Also(p => p.Opacity = 0.6),
            new TextBlock { Text = "?", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontWeight = FontWeight.Bold, FontSize = Math.Max(11, h * 0.4),
                            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center },
        },
    };

    /// <summary>The yellow folder glyph for a folder row with no cover of its own (punch-list round, "not a blank
    /// square") -- two plain Gold Borders (a tab + a body), not an emoji TextBlock: a "\U0001F4C1" glyph rendered
    /// via the window's own Inter/Segoe UI FontFamily came out as a flat monochrome outline, not Windows' colored
    /// emoji (that needs an explicit "Segoe UI Emoji" font fallback Avalonia's text layout does not reach for on
    /// its own), confirmed by a real screenshot. Hand-drawn shapes render identically on every platform instead.</summary>
    static Control FolderGlyph() => new Grid
    {
        Width = 26, Height = 36,
        Children =
        {
            new Border { Width = 11, Height = 5, CornerRadius = new CornerRadius(2, 2, 0, 0), [!Border.BackgroundProperty] = AppTheme.Dyn("Gold"),
                        HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(-5, -8, 0, 0) },
            new Border { Width = 20, Height = 14, CornerRadius = new CornerRadius(2), [!Border.BackgroundProperty] = AppTheme.Dyn("Gold"),
                        HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 2, 0, 0) },
        },
    };

    static TextBlock Cell(string text)
    {
        var tb = new TextBlock { Text = text, VerticalAlignment = VerticalAlignment.Center, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis,
                                 [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text") };
        if (text != "") ToolTip.SetTip(tb, text);
        return tb;
    }

    /// <summary>A folder's own row in the one library list (consolidation round: folders expand in place, no
    /// separate browse pane any more): a chevron (▾/▸, clickable, 22x22) beside its own art in column 0, its name
    /// and recursive game count in the title column. Click selects it (ShowFolderDetails, via OnGameSelectionChanged
    /// -- unchanged); double-click, or the chevron alone, toggles it collapsed/expanded in place.</summary>
    Control BuildFolderRowView(FolderListRow f)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions(ColSpec(f.Depth * 20)), Margin = new Thickness(4 + f.Depth * 20, 2, 4, 2), MinWidth = ColSum() - f.Depth * 20 };
        bool collapsed = _collapsed.Contains(f.Folder.Path);
        // The clickable hit target stays 24x24 (user feedback round, "far too small and fiddly to click" -- still a
        // real accessibility floor); the punch list's own "22px rounded box with a faint fill" is the inner visual
        // only, an 11px glyph inside a faintly-filled rounded Border that doesn't itself have to be the hit target.
        var chevronGlyph = new Border
        {
            Width = 22, Height = 22, CornerRadius = new CornerRadius(6), [!Border.BackgroundProperty] = AppTheme.Dyn("ButtonBg"),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Child = new Avalonia.Controls.Shapes.Path
            {
                Data = Geometry.Parse(collapsed ? "M 3,0 L 8,5 L 3,10" : "M 0,3 L 5,8 L 10,3"), Width = 11, Height = 11,
                [!Avalonia.Controls.Shapes.Shape.StrokeProperty] = AppTheme.Dyn("Text"), StrokeThickness = 1.8, StrokeLineCap = PenLineCap.Round, StrokeJoin = PenLineJoin.Round,
                HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(collapsed ? 2 : 0, collapsed ? 0 : 1, 0, 0),
            },
        };
        var chevron = new Border
        {
            Width = 24, Height = 24, Background = Brushes.Transparent, Cursor = new Cursor(StandardCursorType.Hand),
            Child = chevronGlyph,
        };
        chevron.PointerEntered += (_, _) => chevronGlyph.Bind(Border.BackgroundProperty, "HoverOverlay");
        chevron.PointerExited += (_, _) => chevronGlyph.Bind(Border.BackgroundProperty, "ButtonBg");
        chevron.PointerPressed += (_, e) => { ToggleCollapsed(f.Folder.Path); e.Handled = true; };
        ToolTip.SetTip(chevron, collapsed ? "Expand folder" : "Collapse folder");
        var folderArt = FolderArtBitmap(f.Folder);
        // A folder with no cover of its own gets the mockup's yellow folder glyph, not a blank placeholder box --
        // NoCoverThumb's hatched "?" is for a GAME with no cover, a different (and narrower) meaning here.
        Control art = folderArt is Bitmap thumb
            ? new Image { Width = 28, Height = 36, Stretch = Stretch.Uniform, Source = thumb }
            : FolderGlyph();
        var cover = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, VerticalAlignment = VerticalAlignment.Center, Children = { chevron, art } };
        Grid.SetColumn(cover, 0);
        // Two lines (consolidation round, mockup's own folder row): the name bold, "N games" (plus "folder cover"
        // when it has its own art) muted beneath it -- the same shape a game row's title/file-name pair already has.
        string sub = $"{f.Count} game{(f.Count == 1 ? "" : "s")}" + (folderArt != null ? " · folder cover" : "");
        var nameLine = new TextBlock { Text = f.Folder.Name, VerticalAlignment = VerticalAlignment.Center,
                                       FontWeight = FontWeight.SemiBold, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis };
        var subLine = new TextBlock { Text = sub, VerticalAlignment = VerticalAlignment.Center, FontSize = 11,
                                      [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis };
        var title = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Children = { nameLine, subLine } };
        ToolTip.SetTip(title, $"{f.Folder.Name}  {sub}");
        Grid.SetColumn(title, 1);
        grid.Children.Add(cover);
        grid.Children.Add(title);
        grid.DoubleTapped += (_, _) => ToggleCollapsed(f.Folder.Path);
        return grid;
    }

    /// <summary>"Match to game…" on the right-click menu (M-art-pack §3): right-click does not select in Avalonia's
    /// ListBox, so this selects the row under the pointer first, then opens the same action as the details panel's
    /// button.</summary>
    void WireGameListContextMenu(ListBox list)
    {
        var matchItem = new MenuItem { Header = "Match to game…" };
        var dbItem = new MenuItem { Header = "Add to / edit database…" };
        dbItem.Click += async (_, _) => await EditDatabaseEntry();
        matchItem.Click += async (_, _) => await MatchToGame();
        list.ContextMenu = new ContextMenu { Items = { matchItem, dbItem } };
        list.AddHandler(Control.ContextRequestedEvent, (_, e) =>
        {
            for (var v = e.Source as Visual; v != null; v = v.GetVisualParent())
                if (v is ListBoxItem { DataContext: GameRow row }) { list.SelectedItem = row; break; }
        });
    }

    void CommitRename(GameRow row, string text)
    {
        Stage();
        row.TitleOverride = text.Trim();
        _editingRow = null;
        RefreshGameList();
        UpdateStatusCounts();
    }

    // WrapPanel, not a horizontal StackPanel (panel-layout fix round): "Change cover.../Restore cover/2D/3D" and
    // "Rename to DB title/Favorite/Cheats/Match to game" no longer fit one row at the panel's minimum width --
    // wrap instead of clip.
    readonly WrapPanel _gameCoverButtons = new() { Orientation = Orientation.Horizontal };
    readonly StackPanel _folderArtButtons = new() { Orientation = Orientation.Horizontal, Spacing = 6, IsVisible = false };
    readonly WrapPanel _gameOnlyButtons = new() { Orientation = Orientation.Horizontal };

    // Rows that do not apply to a folder (SetDetailMode hides both the label and the value together, so a folder
    // never shows an empty "Loader"/"Saves" label beside nothing -- the panel-layout fix round, diagnosed on the
    // user's own screenshot: labels with no value, a wrapped "Title (DB)" label pushing its box out of line).
    readonly List<Control> _gameOnlyDetailRows = [];

    /// <summary>Everything the panel shows for a selected game or folder, as one group so "nothing selected" can
    /// hide it all at once behind _emptyHint (panel-layout fix round: no more empty fields).</summary>
    readonly StackPanel _detailContent = new() { Spacing = 8 };
    readonly TextBlock _emptyHint = new()
    {
        HorizontalAlignment = HorizontalAlignment.Center, TextAlignment = Avalonia.Media.TextAlignment.Center,
        TextWrapping = Avalonia.Media.TextWrapping.Wrap, Opacity = 0.6, Margin = new Thickness(12, 40, 12, 0), IsVisible = false,
    };

    Control BuildSidePanel()
    {
        View3DButton.IsVisible = false;   // shown only when the matched game's pack cover has a 3D render (ShowDetails)
        ToolTip.SetTip(View3DButton, "A 3D box render from the cover pack");
        // No fixed Width: the panel fills its Border/Grid column (mainGrid's "300" column), so it cannot overflow
        // that column regardless of the window's actual size; MinWidth keeps every value box readably wide even if
        // a future layout gives the side column less room.
        var panel = new StackPanel { Margin = new Thickness(12), Spacing = 8, MinWidth = 260 };
        var inner = _detailContent;
        // The cover "stage" (punch-list round): the 156x256 cover plus its source-tag badge (top-right corner,
        // Grid-overlaid), then its own reflection directly beneath -- one HorizontalAlignment.Center column so the
        // whole stage stays centered in the panel regardless of the panel's own (wider) width.
        // HeaderBg (near-opaque), not ButtonBg (8% white) -- advisor round: an 8% overlay sat almost unreadably low-
        // contrast directly over a bright cover image, unlike every other place ButtonBg is used (a dark pane).
        _coverSourceTagBadge.Bind(Border.BackgroundProperty, "HeaderBg");
        _coverSourceTagBadge.CornerRadius = new CornerRadius(4);
        _coverSourceTagBadge.Padding = new Thickness(6, 2);
        _coverSourceTagBadge.Margin = new Thickness(0, 6, 6, 0);
        _coverSourceTagBadge.HorizontalAlignment = HorizontalAlignment.Right;
        _coverSourceTagBadge.VerticalAlignment = VerticalAlignment.Top;
        _coverSourceTagBadge.Child = CoverSourceTag;
        var coverStage = new Grid { HorizontalAlignment = HorizontalAlignment.Center,
            Children = { _coverPlaceholder, CoverImage, _coverSourceTagBadge } };
        var coverArea = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center,
            Children = { coverStage, CoverReflection } };
        inner.Children.Add(coverArea);
        _gameCoverButtons.Children.Add(ChangeCoverButton); _gameCoverButtons.Children.Add(RestoreCoverButton); _gameCoverButtons.Children.Add(View3DButton);
        _folderArtButtons.Children.Add(ChangeFolderArtButton); _folderArtButtons.Children.Add(RemoveFolderArtButton);
        inner.Children.Add(_gameCoverButtons);
        inner.Children.Add(_folderArtButtons);
        inner.Children.Add(DetailTitle);
        inner.Children.Add(DetailChips);
        // Punch-list round, the mockup's own field set and order: Title, Genre, Year, Loader (+ its own plain hint
        // line), Favorite as a toggle, then the path box, then Match to game.../Reset edits. Code/Region/Players
        // move into the "More" Expander below -- still staged the same way (CommitMetaEdit doesn't care which
        // panel a TextBox lives in), just not first-class rows any more.
        var kv = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("92,*"),
            RowDefinitions = new RowDefinitions("Auto,Auto,Auto,Auto,Auto,Auto"),
            RowSpacing = 4,   // user feedback round: the form's rows were reading as far apart even once each one shrank
        };
        void Row(Grid g, int r, string label, Control value, bool gameOnly)
        {
            var l = new TextBlock { Text = label, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis };
            value.VerticalAlignment = VerticalAlignment.Center;
            value.HorizontalAlignment = HorizontalAlignment.Stretch;
            Grid.SetRow(l, r); Grid.SetColumn(l, 0);
            Grid.SetRow(value, r); Grid.SetColumn(value, 1);
            g.Children.Add(l); g.Children.Add(value);
            if (gameOnly) { _gameOnlyDetailRows.Add(l); _gameOnlyDetailRows.Add(value); }
        }
        // "Title", not "Title (DB)" (advisor round, matching the mockup's own wording exactly) -- the tooltip below
        // still explains what it actually overrides.
        Row(kv, 0, "Title", MetaTitleBox, gameOnly: true);
        Row(kv, 1, "Genre", DetailGenre, gameOnly: true);
        Row(kv, 2, "Year", DetailYear, gameOnly: true);
        Row(kv, 3, "Loader", LoaderBox, gameOnly: true);
        Row(kv, 4, "", LoaderHintText, gameOnly: true);
        Row(kv, 5, "Favorite", FavoriteButton, gameOnly: true);
        inner.Children.Add(kv);

        // "More" (punch-list round): Code/Region/Players, collapsed by default -- an IsExpanded-driven Expander,
        // not a second always-visible grid, so the trimmed field set above stays the thing people actually see.
        DetailCode.VerticalAlignment = VerticalAlignment.Center;
        DetailCode.TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis;
        DetailSaves.VerticalAlignment = VerticalAlignment.Center;
        var more = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("92,*"), RowDefinitions = new RowDefinitions("Auto,Auto,Auto,Auto"), RowSpacing = 4,
            Margin = new Thickness(0, 4, 0, 0),
        };
        Row(more, 0, "Code", DetailCode, gameOnly: true);
        Row(more, 1, "Region", DetailRegion, gameOnly: true);
        Row(more, 2, "Players", MetaPlayersBox, gameOnly: true);
        Row(more, 3, "Saves", DetailSaves, gameOnly: true);
        var moreExpander = new Expander { Header = "More", Content = more, IsExpanded = false, Margin = new Thickness(0, 2, 0, 0) };
        _gameOnlyDetailRows.Add(moreExpander);
        inner.Children.Add(moreExpander);

        DetailPath.TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis;
        inner.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4,
            Children = { new TextBlock { Text = "Path  ", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Top }, DetailPath } });
        DatabaseEntryButton.Click += async (_, _) => await EditDatabaseEntry();
        foreach (var b in new[] { DatabaseEntryButton, RenameToDbButton, MatchButton, ResetEditsButton })
        {
            b.Margin = new Thickness(0, 0, 6, 6);
            _gameOnlyButtons.Children.Add(b);
        }
        MatchButton.Click += async (_, _) => await MatchToGame();
        ResetEditsButton.Click += (_, _) => ResetEdits();
        inner.Children.Add(_gameOnlyButtons);
        panel.Children.Add(inner);
        panel.Children.Add(_emptyHint);

        ToolTip.SetTip(MetaTitleBox, "Overrides this game's GAMES.DB title (every entry of it), staged in USEREDIT.INI");
        ChangeCoverButton.Click += async (_, _) => await ChooseCover();
        RestoreCoverButton.Click += (_, _) => RestoreCover();
        ChangeFolderArtButton.Click += async (_, _) => await ChangeFolderArt();
        RemoveFolderArtButton.Click += (_, _) => RemoveFolderArt();
        RenameToDbButton.Click += (_, _) => RenameToDbTitle();
        // IsCheckedChanged (not Click -- ToggleSwitch has no Click), guarded by _loadingDetails the same way every
        // other field's commit is: ShowDetails sets FavoriteButton.IsChecked to reflect the row it is populating,
        // which would otherwise fire this handler and flip the very row it just finished reading.
        FavoriteButton.IsCheckedChanged += (_, _) => { if (!_loadingDetails) ToggleFavorite(); };
        LoaderBox.SelectionChanged += (_, _) => OnLoaderChanged();
        MetaTitleBox.LostFocus += (_, _) => CommitMetaEdit();
        DetailGenre.LostFocus += (_, _) => CommitMetaEdit();
        DetailYear.LostFocus += (_, _) => CommitMetaEdit();
        DetailRegion.LostFocus += (_, _) => CommitMetaEdit();
        MetaPlayersBox.LostFocus += (_, _) => CommitMetaEdit();
        DetailsScrollViewer = new ScrollViewer { Content = panel, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled };
        return new Border { MinWidth = 280, Child = DetailsScrollViewer };   // Border/BorderBrush/CornerRadius: ApplyTheme's ".pane" style (set by the caller)
    }

    /// <summary>The details panel's own ScrollViewer (panel-layout fix round): HorizontalScrollBarVisibility must
    /// stay Disabled, which is what lets its WrapPanel button rows wrap instead of clip (a regression guard, and a
    /// named reference for tests -- the panel also contains GameList/FolderList's own internal ScrollViewers, which
    /// a blind visual-tree search could find first).</summary>
    public ScrollViewer? DetailsScrollViewer;

    void SetDetailMode(bool folder)
    {
        _gameCoverButtons.IsVisible = !folder;
        _folderArtButtons.IsVisible = folder;
        _gameOnlyButtons.IsVisible = !folder;
        LoaderBox.IsVisible = !folder;
        foreach (var c in _gameOnlyDetailRows) c.IsVisible = !folder;
    }

    /// <summary>Sets DetailPath's text and a matching tooltip, so a long SD path that is ellipsised still reads in
    /// full on hover (panel-layout fix round).</summary>
    void SetPathText(string text)
    {
        DetailPath.Text = text;
        ToolTip.SetTip(DetailPath, text);
    }

    // =========================== Card open / scan ===========================

    /// <summary>Tests: replaces the real removable-drive enumeration below, so a test run never depends on (or
    /// reads) whatever is actually plugged into the machine running it -- a real removable drive showing up here
    /// auto-selects and opens it (OnDrivePicked), which a test that never calls OpenCard itself has no way to
    /// expect or guard against otherwise.</summary>
    public Func<List<string>>? RemovableDriveListForTests;
    /// <summary>Re-runs drive detection after RemovableDriveListForTests is set (object-initializer syntax sets it
    /// only after the constructor -- which already called RefreshDriveList once -- has returned).</summary>
    public void RefreshDriveListForTests() => RefreshDriveList();

    static List<string> RealRemovableDrives()
    {
        var items = new List<string>();
        try
        {
            foreach (var d in DriveInfo.GetDrives())
            {
                if (d.DriveType != DriveType.Removable) continue;
                try { if (d.IsReady) items.Add($"{d.Name}  {(string.IsNullOrEmpty(d.VolumeLabel) ? "no label" : d.VolumeLabel)} ({d.TotalFreeSpace / (1024 * 1024 * 1024)} GB free)"); }
                catch (IOException) { }
            }
        }
        catch (IOException) { }
        return items;
    }

    void RefreshDriveList()
    {
        var items = RemovableDriveListForTests?.Invoke() ?? RealRemovableDrives();
        if (items.Count == 0) items.Add("Browse…");
        DrivePicker.ItemsSource = items;
        if (items.Count > 0) DrivePicker.SelectedIndex = 0;
    }

    async void OnDrivePicked()
    {
        if (DrivePicker.SelectedItem is not string s) return;
        if (s == "Browse…") { await PickCard(); return; }
        string drive = s.Split(' ')[0];
        OpenCard(drive);
    }

    public void OpenCard(string root)
    {
        if (root == "" || !Directory.Exists(root)) return;
        CardPath.Text = root;
        Load = CardEditSession.Load(root, BuildDate(DateTime.Now));
        if (root != _collapsedRoot) { _collapsed.Clear(); _collapsedRoot = root; }   // a different card: start fresh
        _activeView = LibraryView.All;
        _editingRow = null;
        _selectedFolderPath = null;
        _coverCache.Clear();
        _folderArtCache.Clear();
        History.Clear();
        RefreshUndoRedoMenu();
        if (Load.Problem != null) { Status.Text = Load.Problem; }
        else Status.Text = $"Opened {root}.";
        RefreshGameList();
        RefreshViewsList();
        ShowDetails(null);
        UpdateStatusCounts();
        RefreshPrepareCardState();
        if (AutoBackupOnOpen && Load.Problem == null) TriggerPcBackup(root);
    }

    /// <summary>Backs up everything a rebuild can't recreate (Core.Backup.CardBackup) to the per-user app-data
    /// folder, off the UI thread (a 689-game card's /aether/SAVES tree is real bytes to hash) -- reads the card
    /// only, never writes to it. The Views pane's own footer (RefreshViewsList) is the only place this shows up now
    /// (punch-list round: no separate "Backed up to PC" status line any more, to avoid saying it twice); silent on
    /// any read error, since a backup failure must never block or interrupt opening the card.</summary>
    void TriggerPcBackup(string root)
    {
        LastBackupTaskForTests = Task.Run(() =>
        {
            try
            {
                var target = Core.Card.CardTarget.Inspect(root, SystemVolumes);
                var result = Core.Backup.CardBackup.Snapshot(root, target, DateTime.UtcNow);
                Dispatcher.UIThread.Post(() =>
                {
                    if (CardPath.Text != root) return;   // a different card was opened while this ran
                    _lastBackup = result;
                    // Punch-list round: no separate Status line for this any more -- the Views pane's own footer
                    // (RefreshViewsList, below) already says "Backed up to PC", and a second copy of the same
                    // sentence sitting above the (now single) status bar was the "stray line" called out there.
                    RefreshViewsList();
                });
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException) { }
        });
    }

    /// <summary>The current card's own backup folder (Backups/&lt;label&gt;-&lt;serial&gt;), regardless of whether
    /// anything has been backed up into it yet.</summary>
    string CardBackupFolder(string root) =>
        Path.Combine(Core.Backup.CardBackup.Root(), Core.Backup.CardBackup.CardFolderName(Core.Card.CardTarget.Inspect(root, SystemVolumes)));

    /// <summary>File > "Open backups folder": the current card's own backup folder when one is open and has at
    /// least one snapshot, else the backups root -- created first so "open" never fails just because nothing has
    /// been backed up yet.</summary>
    void OpenBackupsFolder()
    {
        string path = Load is { Root: not "" } ? CardBackupFolder(Load.Root) : Core.Backup.CardBackup.Root();
        try
        {
            Directory.CreateDirectory(path);
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
        {
            Status.Text = $"Could not open the backups folder: {e.Message}";
        }
    }

    /// <summary>File > "Restore from backup…": nothing to pick without a card open and at least one snapshot for
    /// it. RestoreWindow itself only stages a restore into Load (CardRestore.Stage) -- nothing reaches the card
    /// until Save changes, through the same CardWriteGuard path as every other edit.</summary>
    async Task ShowRestoreWindow()
    {
        if (Load is not { Root: not "" } load) { Status.Text = "Open a card first."; return; }
        var snapshots = Core.Backup.CardBackup.ListSnapshots(CardBackupFolder(load.Root));
        if (snapshots.Count == 0) { Status.Text = "No PC backups yet for this card."; return; }
        var w = new RestoreWindow(this, load, snapshots);
        await w.ShowDialog(this);
    }

    /// <summary>RestoreWindow calls this after CardRestore.Stage: the same refreshes OpenCard itself runs, so the
    /// tree/list/status/Save-changes-button all pick up the newly staged restore immediately, without re-reading
    /// the card (a restore only ever mutates the already-loaded Load in memory).</summary>
    public void RefreshAfterRestore()
    {
        RefreshGameList();
        if (GameList.SelectedItem is GameRow row) ShowDetails(row);
        UpdateStatusCounts();
        RefreshUndoRedoMenu();
    }

    Task PickCard() => PickFolder(CardPath, "The SD card's root folder", OpenCard);

    // =========================== Prepare SD Card for Aether ===========================

    /// <summary>The bundled release: next to the exe (a packaged build's own "release" folder), else -- a dev
    /// checkout -- ReleaseAssembler over build/release + build/db + tools/data/compat, cached under the temp
    /// folder so every OpenCard does not re-assemble it. Null when neither is there or complete.</summary>
    /// <summary>Tests: a fixed release folder, bypassing the exe-relative/dev-checkout auto-detection (which would
    /// otherwise depend on this machine's own build/ state).</summary>
    public string? ReleaseOverrideForTests;

    string? FindBundledOrAssembledRelease()
    {
        if (ReleaseOverrideForTests != null) return ReleaseOverrideForTests;
        string bundled = Path.Combine(AppContext.BaseDirectory, "release");
        if (Directory.Exists(bundled) && CardPipeline.CheckRelease(bundled, compat: true).Count == 0) return bundled;

        try
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                if (!Directory.Exists(Path.Combine(dir.FullName, "build", "db")) || !Directory.Exists(Path.Combine(dir.FullName, "menu"))) continue;
                string cache = Path.Combine(Path.GetTempPath(), "vmcm-release-assembled");
                try
                {
                    if (CardPipeline.CheckRelease(cache, compat: true).Count > 0)
                    {
                        if (Directory.Exists(cache)) Directory.Delete(cache, recursive: true);
                        Directory.CreateDirectory(cache);
                        Core.Card.ReleaseAssembler.Assemble(dir.FullName, cache, allowStale: true);
                    }
                    return CardPipeline.CheckRelease(cache, compat: true).Count == 0 ? cache : null;
                }
                catch (Core.Card.ReleaseException) { return null; }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException or Core.Formats.FormatException) { return null; }
            }
        }
        catch (IOException) { }
        return null;
    }

    /// <summary>"Aether <build>" from an autoboot.iso's build string (ReleaseAssembler.BuildOf), just the version
    /// word; null when the image is not an Aether one at all.</summary>
    static string? InstalledVersion(string autobootIso)
    {
        var (build, _) = Core.Card.ReleaseAssembler.BuildOf(autobootIso);
        if (build == null) return null;
        var parts = build.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        return parts.Length >= 2 ? parts[1] : null;
    }

    /// <summary>A plain dotted version compare ("0.1" &lt; "0.2" &lt; "0.10"): -1/0/1, non-numeric parts compare as 0
    /// (never throws on an odd build string).</summary>
    static int CompareVersions(string a, string b)
    {
        var pa = a.Split('.'); var pb = b.Split('.');
        for (int i = 0; i < Math.Max(pa.Length, pb.Length); i++)
        {
            int na = i < pa.Length && int.TryParse(pa[i], out var x) ? x : 0;
            int nb = i < pb.Length && int.TryParse(pb[i], out var y) ? y : 0;
            if (na != nb) return na.CompareTo(nb);
        }
        return 0;
    }

    public void RefreshPrepareCardState()
    {
        if (Load == null) { PrepareCardButton.IsVisible = false; return; }
        PrepareCardButton.IsVisible = true;
        string autoboot = Path.Combine(Load.Root, "autoboot.iso");
        string? installed = File.Exists(autoboot) ? InstalledVersion(autoboot) : null;
        string bundled = Credits.Version;
        var folderState = CardFolderMigration.Detect(Load.Root);
        if (folderState is CardFolderState.Old or CardFolderState.Both)
        {
            // the Aether rename: Update card moves /vibemenu to /aether (PC backup, checked, one rename); with both
            // folders it explains and changes nothing (CardFolderMigration)
            PrepareCardButton.Content = $"Update card to {bundled}";
            PrepareCardButton.IsEnabled = folderState == CardFolderState.Old;
            ToolTip.SetTip(PrepareCardButton, folderState == CardFolderState.Old
                ? "This card was set up before the Aether rename: Update card backs up /vibemenu to this PC, checks the copy, and renames it to /aether"
                : CardFolderMigration.BothMessage(Load.Root));
            SetPrepareButtonReady(folderState == CardFolderState.Old);
        }
        else if (installed == null)
        {
            PrepareCardButton.Content = "Prepare SD Card for Aether";
            PrepareCardButton.IsEnabled = true;
            ToolTip.SetTip(PrepareCardButton, "Install Aether on this card and build its game list and covers");
            SetPrepareButtonReady(true);
        }
        else if (CompareVersions(installed, bundled) < 0)
        {
            PrepareCardButton.Content = $"Update card to {bundled}";
            PrepareCardButton.IsEnabled = true;
            ToolTip.SetTip(PrepareCardButton, $"Update this card from Aether {installed} to {bundled}");
            SetPrepareButtonReady(true);
        }
        else if (UpdateReason() is string why)
        {
            // same version, but the card's menu and LAUNCHER.BIN are not this release's pair, its GAMES.DB is older,
            // or the pack has covers it lacks: Prepare installs all of it together (never one file alone)
            PrepareCardButton.Content = $"Update card to {bundled}";
            PrepareCardButton.IsEnabled = true;
            ToolTip.SetTip(PrepareCardButton, why);
            SetPrepareButtonReady(true);
        }
        else
        {
            PrepareCardButton.Content = $"Aether {installed} installed";
            PrepareCardButton.IsEnabled = false;
            ToolTip.SetTip(PrepareCardButton, $"Aether {installed} is already on this card");
            SetPrepareButtonReady(false);
        }
    }

    /// <summary>Why an installed card of the bundled version still needs Prepare: CardPipeline.MenuPair against a
    /// release already at hand (never assembling one here), else games whose only cover is the pack's.</summary>
    /// <summary>One answer per opened card (MenuPair hashes the card's menu image, and RefreshPrepareCardState runs
    /// after every save and busy change): a new Load -- every OpenCard, every save's reopen -- asks again.</summary>
    CardLoad? _updateReasonFor;
    string? _updateReason;

    string? UpdateReason()
    {
        if (Load == null) return null;
        if (_updateReasonFor == Load) return _updateReason;
        _updateReasonFor = Load;
        return _updateReason = ComputeUpdateReason(Load);
    }

    string? ComputeUpdateReason(CardLoad load)
    {
        string? release = ReleaseOverrideForTests;
        string bundled = Path.Combine(AppContext.BaseDirectory, "release"), cache = Path.Combine(Path.GetTempPath(), "vmcm-release-assembled");
        release ??= new[] { bundled, cache }.FirstOrDefault(d => Directory.Exists(d) && CardPipeline.CheckRelease(d, compat: true).Count == 0);
        if (release != null)
        {
            try { if (CardPipeline.MenuPair(load.Root, release) is (CardPipeline.MenuPairState.Mismatched, string why)) return why; }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
        // games added, removed or moved in Explorer since Aether's list was built: the console only rescans when the
        // card root's own names change, so Update card rebuilds LIBRARY.IDX, ART.PAK and the disc sets
        if (load.Problem == null && CardEditSession.LibraryDrift(load) is string drift) return drift;
        int packOnly = load.Rows.Count(r => !r.Removed && !r.HasArt && r.PendingCover == null && !NoCoverAnywhere(r));
        return packOnly > 0 ? $"{packOnly} game{(packOnly == 1 ? "" : "s")} get their cover from the bundled cover pack" : null;
    }

    /// <summary>The Prepare button's two looks (mockup): a big green gradient with a glow when
    /// there is something to do, a flat grey (no glow) once Aether is already installed -- color only, never the
    /// button's own text (PrepareCardTests checks that string exactly).</summary>
    void SetPrepareButtonReady(bool ready)
    {
        // the "prepare" class (AppTheme.ApplyToWindow) is the green look in every state; without it, the plain button
        // fill and border with muted text
        PrepareCardButton.Classes.Set("prepare", ready);
        if (ready) PrepareCardButton.ClearValue(ForegroundProperty);
        else PrepareCardButton.Bind(ForegroundProperty, "Muted");
    }

    /// <summary>Installs the bundled Aether release and builds the card's data from its own games, bundled art and
    /// the card's own BOX.TGA covers (RunBuild/CardPipeline.Run, unchanged: backs up the old /autoboot.iso only, never
    /// touches menu.bin, confirms through CardWriteGuard, shows progress).</summary>
    public async Task PrepareCard()
    {
        string? release = FindBundledOrAssembledRelease();
        if (release == null) { Status.Text = "No bundled Aether release was found (a dev build needs build/db, build/release and menu/ to assemble one)."; return; }
        ReleasePath.Text = release;
        // an in-place upgrade of a card that already has Aether: a PC backup of everything the user made first
        // (Core.Backup.CardBackup), and no write at all when that backup cannot be taken
        // (a card from before the Aether rename: the move's own backup of the whole /vibemenu is taken instead, in RunBuild)
        if (Load is { Root: not "" } l && InstalledOnCard() != null && CardFolderMigration.Detect(l.Root) != CardFolderState.Old
            && (AutoBackupOnOpen || Core.Backup.CardBackup.RootOverrideForTests != null))
        {
            Status.Text = "Backing up this card's own files to the PC before the update...";
            try
            {
                var target = CardTarget.Inspect(l.Root, SystemVolumes);
                _lastBackup = await Task.Run(() => Core.Backup.CardBackup.Snapshot(l.Root, target, DateTime.UtcNow));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
            {
                Status.Text = $"The PC backup before the update failed ({e.Message}): nothing was written.";
                return;
            }
        }
        await RunBuild();
        RefreshPrepareCardState();
    }

    // =========================== Card Manager updates ===========================

    readonly MenuItem _autoUpdateItem = new() { Header = "Check for updates on start", ToggleType = MenuItemToggleType.CheckBox };
    /// <summary>The "a newer Card Manager is available" banner under the toolbar: non-blocking, dismissable.</summary>
    public readonly Border UpdateBanner = new() { IsVisible = false };
    /// <summary>Tests: the HTTP handler the update check and download use (never the network in a test).</summary>
    public HttpMessageHandler? UpdateHttpForTests;
    /// <summary>The newer release the banner offers, if any.</summary>
    public Core.Update.ReleaseInfo? AvailableUpdate { get; private set; }

    /// <summary>Tests: the running build's identity (else this assembly's own, Core.Update.BuildInfo.Running).</summary>
    public Core.Update.BuildInfo? RunningBuildForTests;
    Core.Update.BuildInfo RunningBuild => RunningBuildForTests ?? Core.Update.BuildInfo.Running();

    HttpClient UpdateHttp() => UpdateHttpForTests != null ? new HttpClient(UpdateHttpForTests, disposeHandler: false) : new HttpClient();

    /// <summary>Asks GitHub for a newer Card Manager. Quiet unless there is one; Help > Check for updates (manual)
    /// also says "up to date" or "could not check" (offline, or no public release yet) in the status line.</summary>
    public async Task CheckForUpdates(bool manual)
    {
        if (manual) Status.Text = "Checking for a newer Aether Card Manager...";
        using var http = UpdateHttp();
        var check = await Core.Update.AppUpdate.CheckAsync(http, RunningBuild);
        AvailableUpdate = check.Newer;
        if (AvailableUpdate != null) ShowUpdateBanner(AvailableUpdate);
        else if (manual) Status.Text = !check.Reached ? "Could not check for updates (offline, or no public release yet)."
                                                      : $"The Card Manager is up to date ({Credits.Version}).";
    }

    /// <summary>The banner's first line: a newer version, or (the same version) a newer build of it.</summary>
    public static string UpdateHeadline(Core.Update.ReleaseInfo r, Core.Update.BuildInfo running) =>
        Core.Update.AppVersion.Compare(r.Version, running.Version) == 0
            ? $"A newer build of Aether {r.Version} is available" + (r.Prerelease || r.Build?.Prerelease == true ? " (pre-release)" : "")
            : $"Aether Card Manager {r.Version} is available (you have {running.Version})";

    void ShowUpdateBanner(Core.Update.ReleaseInfo r)
    {
        string notes = r.Notes.Trim();
        if (notes.Length > 400) notes = notes[..400].TrimEnd() + "…";
        var install = new Button { Content = "Download & install", FontWeight = FontWeight.Bold, Classes = { "primary" } };
        var page = new Button { Content = "Release notes" };
        var later = new Button { Content = "Later" };
        install.Click += async (_, _) => await InstallUpdate(r);
        page.Click += (_, _) => AboutWindow.OpenUrl(r.HtmlUrl);
        later.Click += (_, _) => UpdateBanner.IsVisible = false;
        var text = new StackPanel { Spacing = 2, VerticalAlignment = VerticalAlignment.Center, Children =
        {
            new TextBlock { Text = UpdateHeadline(r, RunningBuild), FontWeight = FontWeight.Bold },
            new TextBlock { Text = notes, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), TextWrapping = Avalonia.Media.TextWrapping.Wrap, MaxHeight = 54, IsVisible = notes != "" },
        } };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, VerticalAlignment = VerticalAlignment.Center, Children = { install, page, later } };
        DockPanel.SetDock(buttons, Dock.Right);
        UpdateBanner.Classes.Add("pane");
        UpdateBanner.Margin = new Thickness(14, 0, 14, 8);
        UpdateBanner.Padding = new Thickness(14, 10);
        UpdateBanner.Bind(Border.BorderBrushProperty, "Acc");
        UpdateBanner.Child = new DockPanel { Children = { buttons, text } };
        UpdateBanner.IsVisible = true;
    }

    /// <summary>Download & install: this platform's package, verified (size and SHA256SUMS.txt), unpacked beside the
    /// app, then an updater script swaps it in once this window has closed and starts the new version (the old one
    /// kept as &lt;folder&gt;.old; a failed swap puts it back). macOS, a non-packaged build, or a release without this
    /// platform's package: the release page opens instead.</summary>
    public async Task InstallUpdate(Core.Update.ReleaseInfo r)
    {
        string rid = System.Runtime.InteropServices.RuntimeInformation.RuntimeIdentifier;
        string appDir = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, '/');
        bool packaged = Directory.Exists(Path.Combine(appDir, "release")) && File.Exists(Path.Combine(appDir, "NOTICE.txt"));
        if (Core.Update.AppUpdate.AssetFor(r, rid) is not { } asset || !packaged || OperatingSystem.IsMacOS())
        {
            AboutWindow.OpenUrl(r.HtmlUrl);
            Status.Text = "The release page is open: download and install the new version from there.";
            return;
        }
        try
        {
            Status.Text = $"Downloading {asset.Name}...";
            string tmp = Path.Combine(Path.GetTempPath(), "vmcm-update-" + r.Version);
            using var http = UpdateHttp();
            string pkg = await Core.Update.AppUpdate.DownloadVerifiedAsync(http, r, asset, tmp);
            string staged = await Task.Run(() => Core.Update.AppUpdate.Stage(pkg, appDir));
            bool win = OperatingSystem.IsWindows();
            string exe = Environment.ProcessPath ?? Path.Combine(appDir, win ? "AetherCardManager.exe" : "AetherCardManager");
            string script = Path.Combine(tmp, win ? "update.ps1" : "update.sh");
            File.WriteAllText(script, Core.Update.AppUpdate.Script(win, Environment.ProcessId, appDir, staged, exe));
            Process.Start(win ? new ProcessStartInfo("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script]) { UseShellExecute = false, CreateNoWindow = true }
                              : new ProcessStartInfo("/bin/sh", [script]) { UseShellExecute = false });
            Status.Text = $"Installing {r.Version}: the Card Manager restarts when this window closes.";
            Close();
        }
        catch (Exception e) when (e is Core.Update.UpdateException or IOException or UnauthorizedAccessException or InvalidDataException
                                      or System.ComponentModel.Win32Exception)
        {
            Status.Text = $"The update was not installed: {e.Message}";
        }
    }

    async Task PickFolder(TextBox into, string title, Action<string>? then = null)
    {
        var picked = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = title });
        if (picked.Count > 0 && picked[0].TryGetLocalPath() is string p) { into.Text = p; then?.Invoke(p); }
    }

    // =========================== Tree / list ===========================

    /// <summary>One row of the Views pane (consolidation round): a fixed, named view, never a folder path. Count is
    /// shown muted, or in the warn color (Warn) for "Needs attention". Backups is not a filter at all -- selecting
    /// it opens the restore window (ShowRestoreWindow) and leaves whatever view was showing alone.</summary>
    public sealed record NavViewItem(string Icon, string Text, LibraryView Kind, int Count, bool Warn);

    static bool Under(string rowFolder, string treeFolder) => treeFolder == "" || rowFolder == treeFolder || rowFolder.StartsWith(treeFolder + "/", StringComparison.Ordinal);

    /// <summary>A folder shown in the library list (navigation fix round, extended by the consolidation round with
    /// Depth): its own art, its recursive game count (the same number the old tree showed), and how many levels
    /// deep it sits in the "All games" view's recursive walk (0 elsewhere -- flat views never nest folders).</summary>
    public sealed record FolderListRow(FolderRow Folder, int Count, int Depth = 0);

    /// <summary>Expand/collapse state (still used by the "All games" view's own recursive walk):
    /// a folder's own path in here means it is drawn collapsed. Survives Refresh/File&gt;Refresh of the *same* root
    /// (OpenCard only clears it for a different one).</summary>
    readonly HashSet<string> _collapsed = [];
    string? _collapsedRoot;

    /// <summary>The Views pane's own fixed rows and their live counts (consolidation round) -- rebuilt whenever the
    /// counts are (UpdateStatusCounts), so a staged favorite/match/cover change updates a badge immediately. Section
    /// headers are plain strings; NavViewItem.Kind == Backups is excluded from RefreshGameList's filter switch.
    /// </summary>
    void RefreshViewsList()
    {
        var items = new List<object>();
        if (Load is { Problem: null })
        {
            var rows = Load.Rows.Where(r => !r.Removed).ToList();
            // CurrentProblems, not a fresh Problems.Find(Load): UpdateStatusCounts (the only real caller of this,
            // directly or via OnViewSelected/TriggerPcBackup after their own prior UpdateStatusCounts) already
            // recomputed it -- a third full pass over every row on a 689-game card for the exact same answer.
            var problems = CurrentProblems;
            int attention = problems.Select(p => p.Row).Where(r => r != null).Distinct().Count();
            int noCover = rows.Count(NoCoverAnywhere);
            // Sid != null, not just Key == "": an unreadable header (Sid == null) is a different problem
            // (UnreadableHeader, part of "Needs attention"), never double-counted here too -- same distinction
            // Problems.Find itself keeps between NotInDatabase and UnreadableHeader.
            int notInDb = rows.Count(r => r.Sid != null && r.Key == "");
            int recent = rows.Count(r => Load.RecentPaths.Contains(r.Path));
            items.Add("LIBRARY");
            items.Add(new NavViewItem("🎮", "All games", LibraryView.All, rows.Count, false));
            items.Add(new NavViewItem("★", "Favorites", LibraryView.Favorites, rows.Count(r => r.Favorite), false));
            items.Add(new NavViewItem("⏱", "Recently played", LibraryView.Recent, recent, false));
            items.Add("CHECK");
            items.Add(new NavViewItem("⚠", "Needs attention", LibraryView.Attention, attention, true));
            items.Add(new NavViewItem("🖼", "No cover", LibraryView.NoCover, noCover, false));
            items.Add(new NavViewItem("?", "Not in database", LibraryView.NotInDb, notInDb, false));
            items.Add("CARD");
            string backupNote = _lastBackup is BackupResult { Created: true } r2 ? $"today, {DateTime.Now:h:mm tt}"
                               : _lastBackup != null ? "no changes since" : "not yet";
            items.Add(new NavViewItem("⟲", "Backups", LibraryView.Backups, 0, false) { });
            ViewsFooterText.Text = $"Backed up to PC\n{backupNote}";
        }
        else ViewsFooterText.Text = "";
        ViewsList.ItemsSource = items;
        var sel = items.OfType<NavViewItem>().FirstOrDefault(v => v.Kind == _activeView);
        if (sel != null) ViewsList.SelectedItem = sel;
    }

    /// <summary>The most recent OpenCard's PC-backup outcome, for the Views pane's footer (RefreshViewsList).</summary>
    Core.Backup.BackupResult? _lastBackup;

    // Small caps, dim, letter-spaced (punch-list round, mockup's own ".sh"): not the shared ".sectionHeader" Style
    // (Acc2, bold, 12px) used elsewhere -- a Views-pane section label is a quiet divider, not a bright heading.
    Control BuildViewSectionHeader(string text) =>
        new TextBlock { Text = text.ToUpperInvariant(), FontSize = 10.5, FontWeight = FontWeight.Bold, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"),
                        LetterSpacing = 1.2, Margin = new Thickness(10, 14, 10, 4) };

    Control BuildViewItemView(NavViewItem item)
    {
        // Foreground set explicitly here, not left to inherit from the ListBoxItem container (the restyle round's
        // own lesson: a few TextBlocks nested that way still rendered Fluent's black regardless of the ListBoxItem-
        // level Style fix, for reasons not fully root-caused there either -- explicit is what actually works).
        var icon = new TextBlock { Text = item.Icon, Width = 18, Margin = new Thickness(0, 0, 8, 0), TextAlignment = Avalonia.Media.TextAlignment.Center,
                                   VerticalAlignment = VerticalAlignment.Center, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text") };
        var label = new TextBlock { Text = item.Text, VerticalAlignment = VerticalAlignment.Center, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text") };
        var count = new Border
        {
            [!Border.BackgroundProperty] = AppTheme.Dyn(item.Warn && item.Count > 0 ? "Warn" : "Clear"),
            CornerRadius = new CornerRadius(10), Padding = new Thickness(7, 1), Margin = new Thickness(8, 0, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center,
            Child = new TextBlock { Text = item.Count.ToString(System.Globalization.CultureInfo.InvariantCulture),
                                    [!TextBlock.ForegroundProperty] = AppTheme.Dyn(item.Warn && item.Count > 0 ? "WarnBadgeText" : "Muted"), FontWeight = FontWeight.SemiBold, FontSize = 11 },
        };
        var row = new Grid { ColumnDefinitions = new ColumnDefinitions("18,*,Auto"), MinHeight = 32, Margin = new Thickness(4, 0) };
        Grid.SetColumn(icon, 0); Grid.SetColumn(label, 1); Grid.SetColumn(count, 2);
        row.Children.Add(icon); row.Children.Add(label); row.Children.Add(count);
        return row;
    }

    /// <summary>Picking a view (consolidation round): Backups is not a filter -- it opens the restore window and
    /// leaves whatever was showing alone; every other row sets _activeView and re-filters the one library list.</summary>
    async Task OnViewSelected()
    {
        if (ViewsList.SelectedItem is not NavViewItem item) return;
        if (item.Kind == LibraryView.Backups) { await ShowRestoreWindow(); RefreshViewsList(); return; }
        // RefreshViewsList rebuilds its own items (fresh NavViewItem instances, including a re-selection of the
        // kind already active) every time the counts change -- without this guard, re-selecting the same kind would
        // call RefreshGameList -> UpdateStatusCounts -> RefreshViewsList -> re-select -> SelectionChanged, forever.
        if (item.Kind == _activeView) return;
        _activeView = item.Kind;
        RefreshGameList();
    }

    /// <summary>Expand all/Collapse all (View menu): only meaningful for the "All games" view's own recursive walk --
    /// harmless, a no-op in effect, when another (flat) view happens to be showing.</summary>
    void SetAllCollapsed(bool collapsed)
    {
        _collapsed.Clear();
        if (collapsed && Load != null)
            foreach (var f in Load.Folders.Skip(1)) _collapsed.Add(f.Path);   // index 0 is the root ("")
        var keepGame = GameList.SelectedItem as GameRow;
        string? keepFolder = (GameList.SelectedItem as FolderListRow)?.Folder.Path;
        RefreshGameList();
        if (keepGame != null) GameList.SelectedItem = keepGame;
        else if (keepFolder != null) SelectFolderRow(keepFolder);
    }

    /// <summary>Toggles one folder row's chevron (click, double-click, or Right/Left on it); keeps the current
    /// selection, since rebuilding the list replaces every FolderListRow object (GameRow instances are stable).</summary>
    void ToggleCollapsed(string path)
    {
        if (!_collapsed.Remove(path)) _collapsed.Add(path);
        var keepGame = GameList.SelectedItem as GameRow;
        string? keepFolder = (GameList.SelectedItem as FolderListRow)?.Folder.Path;
        RefreshGameList();
        if (keepGame != null) GameList.SelectedItem = keepGame;
        else if (keepFolder != null) SelectFolderRow(keepFolder);
    }

    void SelectFolderRow(string path)
    {
        var item = _view.OfType<FolderListRow>().FirstOrDefault(f => f.Folder.Path == path);
        if (item != null) GameList.SelectedItem = item;
    }

    int DepthOf(object item) => item switch { FolderListRow f => f.Depth, GameRow r => _gameDepth.GetValueOrDefault(r, 0), _ => 0 };

    /// <summary>Left/Right arrow keys on the library list (coordinator round, repointed at the unified list by the
    /// consolidation round): Right expands a collapsed folder, else moves into its first child; Left collapses an
    /// expanded folder, else moves to the nearest preceding row at a shallower depth (its real parent, by position,
    /// not a stored field). e.Handled stops the ListBox's own default handling (which otherwise also tries to scroll
    /// horizontally on Left/Right).</summary>
    void OnLibraryKeyDown(object? sender, KeyEventArgs e)
    {
        if (GameList.SelectedItem is not object item) return;
        int depth = DepthOf(item);
        if (e.Key == Key.Right)
        {
            e.Handled = true;
            if (item is FolderListRow f && _collapsed.Contains(f.Folder.Path)) ToggleCollapsed(f.Folder.Path);
            else
            {
                int i = _view.IndexOf(item);
                if (i >= 0 && i + 1 < _view.Count && DepthOf(_view[i + 1]) > depth) GameList.SelectedItem = _view[i + 1];
            }
        }
        else if (e.Key == Key.Left)
        {
            e.Handled = true;
            if (item is FolderListRow f2 && !_collapsed.Contains(f2.Folder.Path)) ToggleCollapsed(f2.Folder.Path);
            else
            {
                int i = _view.IndexOf(item);
                for (int j = i - 1; j >= 0; j--)
                    if (DepthOf(_view[j]) < depth) { GameList.SelectedItem = _view[j]; break; }
            }
        }
    }

    string? _selectedFolderPath;

    Dictionary<string, GameRecord> DbByKey() => Load?.Db.Records.ToDictionary(r => r.Key) ?? new();

    /// <summary>A row's genre (the consolidation round's Sort dropdown, and the details panel's own Genre field):
    /// a staged USEREDIT.INI edit first, else GAMES.DB's own genre table, else "" (never automatic for an unmatched
    /// game -- there is no record to read one from).</summary>
    string GenreFor(GameRow row, Dictionary<string, GameRecord> db)
    {
        var edit = row.Key != "" ? Load?.UserEdits.GetValueOrDefault(row.Key) : null;
        if (edit?.Genre is { Length: > 0 } eg) return eg;
        return db.TryGetValue(row.Key, out var rec) && rec.Genre != 0xFF && Load != null && rec.Genre < Load.Db.Genres.Count
            ? Load.Db.Genres[rec.Genre] : "";
    }

    List<GameRow> SortGames(IEnumerable<GameRow> rows, Dictionary<string, GameRecord> db)
    {
        Func<GameRow, object> key = _sortCol switch
        {
            "code" => r => r.Code, "region" => r => r.RegionText, "disc" => r => r.Disc,
            "loader" => r => r.Loader, "folder" => r => r.Folder, "problems" => r => ProblemCountFor(r),
            "genre" => r => GenreFor(r, db),
            _ => r => r.DisplayTitle(db, Load?.UserEdits),
        };
        var cmp = Comparer<object>.Create((a, b) => Comparer.DefaultInvariant.Compare(a, b));
        return (_sortDesc ? rows.OrderByDescending(key, cmp) : rows.OrderBy(key, cmp)).ToList();
    }

    /// <summary>The one library list's contents (consolidation round): a text search always flattens to every
    /// matching game, cutting across whatever view is active. Otherwise, "All games" is the real folder tree,
    /// recursively walked and interleaved with its own games, honoring _collapsed (BuildLibraryTree); every other
    /// view is a flat, cross-folder filter over Load.Rows, no folders at all -- a view is "what you want to see",
    /// not a place to browse into.</summary>
    void RefreshGameList()
    {
        _gameDepth.Clear();
        if (Load == null || Load.Problem != null) { GameList.ItemsSource = _view = []; UpdateStatusCounts(); return; }
        var db = DbByKey();
        string q = (SearchBox.Text ?? "").Trim();
        if (q != "")
        {
            var rows = Load.Rows.Where(r => !r.Removed)
                .Where(r => r.DisplayTitle(db, Load?.UserEdits).Contains(q, StringComparison.OrdinalIgnoreCase)
                         || r.Code.Contains(q, StringComparison.OrdinalIgnoreCase) || r.FileName.Contains(q, StringComparison.OrdinalIgnoreCase));
            _view = SortGames(rows, db).Cast<object>().ToList();
        }
        else if (_activeView == LibraryView.All)
        {
            _view = BuildLibraryTree(db);
        }
        else
        {
            var problems = FindProblems();
            CurrentProblems = problems;
            IEnumerable<GameRow> rows = Load.Rows.Where(r => !r.Removed);
            rows = _activeView switch
            {
                LibraryView.Favorites => rows.Where(r => r.Favorite),
                LibraryView.Recent => rows.Where(r => Load.RecentPaths.Contains(r.Path)),
                LibraryView.Attention => rows.Where(r => problems.Any(p => p.Row == r)),
                LibraryView.NoCover => rows.Where(NoCoverAnywhere),
                LibraryView.NotInDb => rows.Where(r => r.Sid != null && r.Key == ""),
                _ => rows,
            };
            _view = SortGames(rows, db).Cast<object>().ToList();
        }
        GameList.ItemsSource = _view;
        // Phase C: the cover wall flattens -- a folder row has no cover-wall meaning of its own, only the games
        // under it do, so this is just the GameRows already in _view, whatever the list view's own current
        // filter/collapsed-folder state left in it.
        CoversList.ItemsSource = _view.OfType<GameRow>().ToList();
        RefreshLibraryHeader(q);
        UpdateStatusCounts();
        // A folder expanding/collapsing changes the deepest visible row's own indent without the window itself
        // resizing (centerScroll.SizeChanged, which this would otherwise rely on alone) -- re-check here too, but
        // only when maxDepth actually changed: this runs on every refresh (a search keystroke, a favorite toggle,
        // a manual column drag's own RefreshGameList call), and re-running RecalcTitleColumn on all of those would
        // silently undo a manual Title-column resize the next time anything else refreshed the list.
        double maxDepth = _view.Count == 0 ? 0 : _view.Max(DepthOf);
        if (maxDepth != _lastMaxDepthForRecalc)
        {
            _lastMaxDepthForRecalc = maxDepth;
            if (_listAvailWidth > 0 && _centerListContainer != null) RecalcTitleColumn(_listAvailWidth, _centerListContainer);
        }
    }

    /// <summary>Phase C: swaps the center pane between GameList's own row list and CoversList's wall of tiles --
    /// ListModeButton/CoversModeButton are a segmented pair, not two independent toggles, so this is the one place
    /// that keeps them (and the two ScrollViewers) in the opposite state from each other.</summary>
    void SetCoversMode(bool covers)
    {
        _coversMode = covers;
        ListModeButton.IsChecked = !covers;
        CoversModeButton.IsChecked = covers;
        if (_centerScroll != null) _centerScroll.IsVisible = !covers;
        if (_coversScroll != null) _coversScroll.IsVisible = covers;
        CoversList.IsVisible = covers;
    }

    /// <summary>One tile in the cover wall (Phase C): a bigger cover (ThumbFor's own bitmap, or NoCoverThumb's
    /// hatched placeholder -- the same two sources BuildGameRowView's own thumbnail uses, just larger), the title
    /// beneath it wrapped to two lines, and a tooltip with the full title for whatever that truncates.</summary>
    Control BuildCoverTile(GameRow row)
    {
        Control cover = ThumbFor(row) is Bitmap thumb
            ? new Image { Width = 100, Height = 140, Stretch = Stretch.Uniform, Source = thumb, [!Visual.EffectProperty] = AppTheme.Dyn("TileEffect") }
            : NoCoverThumb(100, 140);
        string text = row.DisplayTitle(DbByKey(), Load?.UserEdits);
        var title = new TextBlock
        {
            Text = text, Width = 108, TextAlignment = Avalonia.Media.TextAlignment.Center, TextWrapping = Avalonia.Media.TextWrapping.Wrap,
            FontSize = 11, FontWeight = FontWeight.SemiBold, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text"), MaxHeight = 32,
        };
        var tile = new StackPanel { Width = 108, Margin = new Thickness(6), Spacing = 4, Children = { cover, title } };
        ToolTip.SetTip(tile, text);
        return tile;
    }

    /// <summary>The library pane's own title row (consolidation round, mockup's ".lh"): the active view's name, and
    /// a subtitle -- "N games · M folders" for "All games" (unfiltered, matching the old tree's own count), else
    /// just "N games" (a flat, cross-folder view has no folder count of its own). A text search names itself
    /// instead of whatever view is still selected underneath it.</summary>
    void RefreshLibraryHeader(string searchText)
    {
        int games = _view.OfType<GameRow>().Count();
        if (searchText != "") { LibraryTitle.Text = "Search results"; LibrarySubtitle.Text = $"{games} game{(games == 1 ? "" : "s")}"; return; }
        LibraryTitle.Text = _activeView switch
        {
            LibraryView.Favorites => "Favorites", LibraryView.Recent => "Recently played", LibraryView.Attention => "Needs attention",
            LibraryView.NoCover => "No cover", LibraryView.NotInDb => "Not in database", _ => "All games",
        };
        LibrarySubtitle.Text = _activeView == LibraryView.All && Load != null
            ? $"{games} game{(games == 1 ? "" : "s")} · {Load.Folders.Count(f => f.Path != "")} folder{(Load.Folders.Count(f => f.Path != "") == 1 ? "" : "s")}"
            : $"{games} game{(games == 1 ? "" : "s")}";
    }

    /// <summary>"All games": the real directory structure (same walk the old folder tree used), as one interleaved
    /// list of FolderListRow/GameRow -- a collapsed folder's own subfolders and direct games are simply left out of
    /// the list, not hidden some other way, so Up/Down/Home never land on one. A single-game folder is still one row
    /// labeled by the folder's own name, acting as that game (2026-11-23, Satiator Rings' model, item 9) -- never a
    /// separate folder row plus a game leaf. _gameDepth is populated here for every game this produces, read by
    /// BuildGameRowView for indentation and by OnLibraryKeyDown for Left/Right.</summary>
    List<object> BuildLibraryTree(Dictionary<string, GameRecord> db)
    {
        var result = new List<object>();
        if (Load == null) return result;
        var rows = Load.Rows.Where(r => !r.Removed).ToList();
        var childrenOf = new Dictionary<int, List<int>>();
        for (int i = 0; i < Load.Folders.Count; i++)
        {
            var f = Load.Folders[i];
            // UI-only hidden-folder filter (navigation fix round): LibraryIndex.IsHiddenName already keeps dot-
            // folders, "System Volume Information", aether (and the old vibemenu), satiator-rings and satiator-saves out of the scan
            // entirely; this adds any other root-level "satiator-*" folder on top, belt and braces.
            if (f.Parent == 0 && f.Name.StartsWith("satiator-", StringComparison.OrdinalIgnoreCase)) continue;
            if (!childrenOf.TryGetValue(f.Parent, out var list)) childrenOf[f.Parent] = list = [];
            list.Add(i);
        }
        foreach (var list in childrenOf.Values) list.Sort((a, b) => string.CompareOrdinal(Load.Folders[a].Name, Load.Folders[b].Name));
        var gamesByFolder = rows.GroupBy(r => r.Folder).ToDictionary(g => g.Key, g => SortGames(g, db));

        void Walk(int idx, string path, int depth)
        {
            if (childrenOf.TryGetValue(idx, out var kids))
                foreach (var ci in kids)
                {
                    var f = Load.Folders[ci];
                    var direct = gamesByFolder.GetValueOrDefault(f.Path);
                    if (!childrenOf.ContainsKey(ci) && direct is [{ OwnFolder: true } only])
                    {
                        _gameDepth[only] = depth;
                        result.Add(only);
                        continue;
                    }
                    int n = rows.Count(r => Under(r.Folder, f.Path));
                    result.Add(new FolderListRow(f, n, depth));
                    if (!_collapsed.Contains(f.Path)) Walk(ci, f.Path, depth + 1);
                }
            if (gamesByFolder.TryGetValue(path, out var games))
                foreach (var g in games) { _gameDepth[g] = depth; result.Add(g); }
        }
        Walk(0, "", 0);
        return result;
    }

    static class Comparer
    {
        public static readonly StringComparer DefaultInvariant = StringComparer.OrdinalIgnoreCase;
    }

    void OnGameSelectionChanged()
    {
        BatchRenameButton.IsEnabled = GameList.SelectedItems?.OfType<GameRow>().Any() == true;
        if (GameList.SelectedItem is FolderListRow flr) ShowFolderDetails(flr.Folder.Path);
        else ShowDetails(GameList.SelectedItem as GameRow);
    }

    // =========================== Details panel ===========================

    /// <summary>The cover "stage" (punch-list round): one place that sets CoverImage, keeps CoverReflection's own
    /// Source identical to it, and swaps in NoCoverThumb's styled placeholder instead of a blank area when there is
    /// nothing to show -- bmp == null means no cover (sourceTag is then ignored and the badge hides itself).</summary>
    void SetCover(Bitmap? bmp, string? sourceTag)
    {
        CoverImage.Source = bmp;
        CoverReflection.Source = bmp;
        CoverImage.IsVisible = bmp != null;
        CoverReflection.IsVisible = bmp != null;
        _coverPlaceholder.IsVisible = bmp == null;
        CoverSourceTag.Text = sourceTag ?? "";
        _coverSourceTagBadge.IsVisible = bmp != null && sourceTag != null;
    }

    /// <summary>The Inspector's own corner badge text for a game's cover (punch-list round, the mockup's "COVER
    /// PACK" tag) -- the same cover-presence/ownership the Status column's pill already reads (BuildStatusCell),
    /// just the two cover-bearing outcomes of it: null means "no cover" (SetCover hides the badge for that case).</summary>
    string? CoverSourceTagFor(GameRow row) =>
        NoCoverAnywhere(row) ? null : row.HasBox || row.PendingCover != null ? "YOUR COVER"
        : PackLabel(row) is string l ? $"{l} COVER" : "COVER PACK";

    /// <summary>The cover-pack entry the menu gets for this game (Prepare puts the pack's best cover in ART.PAK when the
    /// card has no BOX.TGA): its "US"/"JP"/"EU" when it is the same game's cover from another region, else null.</summary>
    string? PackLabel(GameRow row) => !row.HasBox && row.PendingCover == null && row.Key != "" ? BestPackEntry(row.Key)?.FallbackLabel : null;

    /// <summary>"No cover" only when the menu really shows none: nothing on the card (BOX.TGA, ART.PAK, a staged cover)
    /// and nothing in the bundled pack for Prepare to add (the user's card, 2026-11-24: 136 games flagged here had one).</summary>
    bool NoCoverAnywhere(GameRow row) =>
        !row.HasArt && row.PendingCover == null && (row.Key == "" || BestPackEntry(row.Key) == null);

    /// <summary>The Aether version on the open card's autoboot.iso, for the bug report; null with no card.</summary>
    /// <summary>What the feedback form fills in from the open card (read-only): its size, file system, label, free
    /// space and game count, and every game with its title, code, region, path, disc set and Launch Options (loader,
    /// saves, cheats with how many are on, In Game Reset) from GAMES.DB, PERGAME.INI, CHEATS.PAK and LIBRARY.IDX.</summary>
    public FeedbackContext FeedbackContextFor()
    {
        if (Load is not { Problem: null } load) return FeedbackContext.None;
        var live = load.Rows.Where(r => !r.Removed).ToList();
        string card = $"{live.Count} games";
        try
        {
            var di = new DriveInfo(Path.GetPathRoot(Path.GetFullPath(load.Root))!);
            static string Gb(long b) => (b / 1024.0 / 1024.0 / 1024.0).ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) + " GB";
            if (di.IsReady)
                card = $"{Gb(di.TotalSize)} {di.DriveFormat}, label \"{di.VolumeLabel}\", {Gb(di.AvailableFreeSpace)} free, {live.Count} games";
        }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException) { }
        var db = DbByKey();
        var perGame = Core.Formats.PerGameIni.Parse(load.PerGameText).ToDictionary(g => g.Id);
        var games = live.OrderBy(r => r.DisplayTitle(db, load.UserEdits), StringComparer.OrdinalIgnoreCase).Select(r =>
        {
            db.TryGetValue(r.Key, out var rec);
            var pg = perGame.GetValueOrDefault(r.GameIdIni);
            string loader = r.Loader switch { "jhl" => "JHL", "cwx" => "CWX", _ => $"Auto ({(rec?.LoaderHint == 1 ? "JHL" : "CWX")})" };
            int on = r.Key != "" ? load.CheatsOn.GetValueOrDefault(r.Key) : 0;
            string name = r.DisplayTitle(db, load.UserEdits);
            string details = string.Join("\n", new[]
            {
                $"Title: {(rec?.TitleEn is { Length: > 0 } t ? t : name)}",
                $"Product code: {(r.Code != "" ? r.Code : "none")}, region {RegionLabel(r)} ({(r.RegionText != "" ? r.RegionText : "none")}), GAMES.DB key {(r.Key != "" ? r.Key : "none")}",
                $"File: /{r.Path}",
                $"Disc: {r.DiscText}",
                $"Loader: {loader}",
                $"Saves: {pg?.Saves ?? "default"}, slot {pg?.Slot ?? "0"}",
                $"Cheats: {pg?.Cheats ?? "default"}, {on} on",
                $"In Game Reset: {pg?.Igr ?? "default"}",
            });
            return (name, details);
        }).ToList();
        return new FeedbackContext(card, games, Core.Formats.ConsoleSetup.TryLoad(load.Root));
    }

    string? InstalledOnCard() =>
        Load is { Root: not "" } l && File.Exists(Path.Combine(l.Root, "autoboot.iso")) ? InstalledVersion(Path.Combine(l.Root, "autoboot.iso")) : null;

    void ShowDetails(GameRow? row)
    {
        _selectedFolderPath = null;
        SetDetailMode(folder: false);
        _loadingDetails = true;
        try
        {
            var db = DbByKey();
            if (row == null)
            {
                _detailContent.IsVisible = false;
                _emptyHint.IsVisible = true;
                _emptyHint.Text = Load == null || Load.Problem != null ? "Select an SD card" : "Select a game";
                DetailTitle.Text = ""; DetailCode.Text = DetailSaves.Text = ""; SetPathText(""); DetailChips.Children.Clear();
                MetaTitleBox.Text = DetailRegion.Text = DetailGenre.Text = DetailYear.Text = MetaPlayersBox.Text = "";
                SetCover(null, null); LoaderBox.ItemsSource = null; LoaderHintText.Text = ""; FavoriteButton.IsChecked = false;
                ChangeCoverButton.IsEnabled = RestoreCoverButton.IsEnabled = RenameToDbButton.IsEnabled = FavoriteButton.IsEnabled = LoaderBox.IsEnabled = false;
                ToolTip.SetTip(ChangeCoverButton, null);
                MetaTitleBox.IsEnabled = DetailRegion.IsEnabled = DetailGenre.IsEnabled = DetailYear.IsEnabled = MetaPlayersBox.IsEnabled = false;
                return;
            }
            _detailContent.IsVisible = true;
            _emptyHint.IsVisible = false;
            // Advisor round: this had no Click/IsCheckedChanged handler at all (IsEnabled was permanently false from
            // its own field initializer, with nothing anywhere ever setting it true) -- a real 3D source made it
            // visible but still unclickable, a dead control with no feature behind it yet, not something this round
            // should half-wire. Hidden until the actual 2D/3D cover swap exists, the same call the four 0.1 feature
            // removals made earlier in this project.
            View3DButton.IsVisible = false;
            RestoreCoverButton.IsEnabled = RenameToDbButton.IsEnabled = FavoriteButton.IsEnabled = LoaderBox.IsEnabled = true;
            // Final review I1: a loose game (sharing its folder with other things, or sitting at the card root) has
            // no folder of its own for a BOX.TGA -- "Change cover" would otherwise overwrite that shared/root
            // BOX.TGA, replacing every other loose game's cover in it (or the last Fill wins, on a flat card root).
            ChangeCoverButton.IsEnabled = row.OwnFolder;
            ToolTip.SetTip(ChangeCoverButton, row.OwnFolder ? null
                : "This game shares its folder (or sits at the card root), so it has no folder of its own for a cover. Move it into its own folder first.");
            MetaTitleBox.IsEnabled = DetailRegion.IsEnabled = DetailGenre.IsEnabled = DetailYear.IsEnabled = MetaPlayersBox.IsEnabled = row.Key != "";
            DetailTitle.Text = row.DisplayTitle(db, Load?.UserEdits);
            DetailCode.Text = row.Code;
            db.TryGetValue(row.Key, out var rec);
            var edit = row.Key != "" ? Load!.UserEdits.GetValueOrDefault(row.Key) : null;
            MetaTitleBox.Text = edit?.Title is { Length: > 0 } et ? et : (rec?.TitleEn ?? "");
            // The region field edits GAMES.DB's own RegionFlags (what CommitMetaEdit compares against), not the
            // disc's own System ID area -- shown instead as a placeholder hint when GAMES.DB has no region set.
            DetailRegion.Text = edit?.Region is { Length: > 0 } er ? er : (rec != null ? GamesDb.RegionLettersFrom(rec.RegionFlags) : "");
            DetailRegion.PlaceholderText = row.RegionText != "" ? $"disc: {row.RegionText}" : "e.g. U, JU";
            DetailGenre.Text = edit?.Genre is { Length: > 0 } eg ? eg : (rec != null && rec.Genre != 0xFF && rec.Genre < Load!.Db.Genres.Count ? Load.Db.Genres[rec.Genre] : "");
            DetailYear.Text = (edit?.Year > 0 ? edit.Year : rec?.Year) is int yr and > 0 ? yr.ToString(System.Globalization.CultureInfo.InvariantCulture) : "";
            MetaPlayersBox.Text = (edit?.Players > 0 ? edit.Players : rec?.Players) is int pl and > 0 ? pl.ToString(System.Globalization.CultureInfo.InvariantCulture) : "";
            SetPathText("/" + row.Path);
            DetailChips.Children.Clear();
            foreach (string chip in new[] { RegionLabel(row), row.Code, DetailGenre.Text, DetailYear.Text })
                if (chip != "") DetailChips.Children.Add(new Border { [!Border.BackgroundProperty] = AppTheme.Dyn("ButtonBg"), CornerRadius = new CornerRadius(10),
                    Padding = new Thickness(7, 1), Margin = new Thickness(0, 0, 4, 4),
                    Child = new TextBlock { Text = chip, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Text"), FontSize = 11, FontWeight = FontWeight.SemiBold } });
            DetailSaves.Text = SavesSummary(row);
            FavoriteButton.IsChecked = row.Favorite;
            RenameToDbButton.IsEnabled = rec != null && rec.TitleEn != "" && row.TitleOverride != rec.TitleEn;
            DatabaseEntryButton.Content = row.Key == "" ? "Add to database…" : "Edit database entry…";

            // Punch-list round: the hint moved out of the ComboBox item text ("Auto (JHL)") into its own plain line
            // underneath (the mockup's own wording), so the dropdown itself just says "Auto".
            string hintLabel = rec != null && rec.LoaderHint == 1 ? "JHL" : "CWX";
            LoaderBox.ItemsSource = new[] { "Auto", "JHL", "CWX" };
            LoaderBox.SelectedIndex = row.Loader switch { "jhl" => 1, "cwx" => 2, _ => 0 };
            LoaderHintText.Text = $"Auto uses {hintLabel}" + (hintLabel == "JHL" ? " for this disc" : "");

            SetCover(CoverBitmap(row), CoverSourceTagFor(row));
        }
        finally { _loadingDetails = false; }
    }

    /// <summary>Stages the metadata panel's current text into Load.UserEdits[row.Key] (USEREDIT.INI): a field left
    /// blank, or matching the record's own GAMES.DB value, is not an override. Ignored while ShowDetails is
    /// (re)populating the boxes, and for a row with no GAMES.DB match (nothing to key an override on).</summary>
    public void CommitMetaEdit()
    {
        if (_loadingDetails || Load == null || _selectedFolderPath != null || GameList.SelectedItem is not GameRow row || row.Key == "") return;
        DbByKey().TryGetValue(row.Key, out var rec);
        string title = (MetaTitleBox.Text ?? "").Trim();
        string genre = (DetailGenre.Text ?? "").Trim();
        string region = (DetailRegion.Text ?? "").Trim().ToUpperInvariant();
        int.TryParse((DetailYear.Text ?? "").Trim(), out int year);
        int.TryParse((MetaPlayersBox.Text ?? "").Trim(), out int players);
        var e = new Core.Formats.UserEditRecord
        {
            Title = rec != null && title == rec.TitleEn ? "" : title,
            Genre = rec != null && Load.Db.Genres.ElementAtOrDefault(rec.Genre) == genre ? "" : genre,
            Region = rec != null && GamesDb.RegionFlagsFrom(region) == rec.RegionFlags ? "" : region,
            Year = rec != null && year == rec.Year ? 0 : year,
            Players = rec != null && players == rec.Players ? 0 : players,
        };
        var existing = Load.UserEdits.GetValueOrDefault(row.Key) ?? new Core.Formats.UserEditRecord();
        if (!e.SameAs(existing))
        {
            Stage();
            if (e.IsEmpty) Load.UserEdits.Remove(row.Key); else Load.UserEdits[row.Key] = e;
            UpdateStatusCounts();
        }
    }

    /// <summary>Custom artwork for folders: the tree's selected folder, its current art (read
    /// the same way the menu sees it -- ART.PAK by ArtPak.FolderArtKey, then the folder's own FOLDER.*), and the two
    /// staged actions, Change folder art and Remove folder art.</summary>
    void ShowFolderDetails(string folderPath)
    {
        if (Load == null) return;
        var folder = Load.Folders.FirstOrDefault(x => x.Path == folderPath);
        if (folder == null) return;
        _selectedFolderPath = folderPath;
        SetDetailMode(folder: true);
        _detailContent.IsVisible = true;
        _emptyHint.IsVisible = false;
        DetailTitle.Text = folder.Name;
        DetailChips.Children.Clear();
        DetailCode.Text = DetailRegion.Text = DetailGenre.Text = DetailYear.Text = DetailSaves.Text = "";
        MetaTitleBox.Text = MetaPlayersBox.Text = "";
        MetaTitleBox.IsEnabled = DetailRegion.IsEnabled = DetailGenre.IsEnabled = DetailYear.IsEnabled = MetaPlayersBox.IsEnabled = false;
        int n = Load.Rows.Count(r => !r.Removed && Under(r.Folder, folderPath));
        SetPathText($"/{folderPath}  ({n} game(s))");
        ChangeFolderArtButton.IsEnabled = RemoveFolderArtButton.IsEnabled = true;
        var folderArt = FolderArtBitmap(folder);
        SetCover(folderArt, folderArt != null ? "FOLDER COVER" : null);
    }

    readonly Dictionary<FolderRow, Bitmap?> _folderArtCache = new();

    void InvalidateFolderArt(FolderRow folder) => _folderArtCache.Remove(folder);

    /// <summary>SPEC §5.2 Folder art, read side: ART.PAK's F+FNV-1a-32 entry first, else the folder's own
    /// FOLDER.PNG/.JPG/.TGA (CardBuilder.FolderArtNames) -- the same two sources CardBuilder itself draws on.</summary>
    Bitmap? FolderArtBitmap(FolderRow folder)
    {
        if (_folderArtCache.TryGetValue(folder, out var cached)) return cached;
        Bitmap? bmp = null;
        try
        {
            if (folder.PendingCover is RgbaImage pending) bmp = ToBitmap(ToRgb555Preview(pending));
            else if (!folder.RemoveArtStaged && Load != null)
            {
                string key = Core.Formats.ArtPak.FolderArtKey(folder.Path);
                if (Load.ArtData != null && Core.Art.CoverLookup.FromArtPak(Load.ArtData, key, 0) is RgbaImage art) bmp = ToBitmap(art);
                else
                {
                    string dir = Path.Combine(Load.Root, folder.Path.Replace('/', Path.DirectorySeparatorChar));
                    if (Directory.Exists(dir))
                    {
                        var here = Directory.EnumerateFiles(dir).Select(Path.GetFileName).ToList();
                        string? found = here.FirstOrDefault(n => CardBuilder.FolderArtNames.Contains(n!.ToUpperInvariant()));
                        // Coordinator fix round, item 4: Rings shows a BOX.TGA inside a non-game folder (no .cue/.iso
                        // directly in it -- that BOX.TGA is a game's cover, not this folder's) as that folder's art.
                        bool hasOwnImage = here.Any(n => n!.EndsWith(".cue", StringComparison.OrdinalIgnoreCase) || n.EndsWith(".iso", StringComparison.OrdinalIgnoreCase));
                        if (found == null && !hasOwnImage) found = here.FirstOrDefault(n => n == "BOX.TGA");
                        if (found != null) bmp = ToBitmap(ToRgb555Preview(RgbaImage.Decode(File.ReadAllBytes(Path.Combine(dir, found)))));
                    }
                }
            }
        }
        catch (Exception e) when (e is IOException or Core.Formats.FormatException or System.FormatException or UnauthorizedAccessException) { }
        _folderArtCache[folder] = bmp;
        return bmp;
    }

    async Task ChangeFolderArt()
    {
        if (_selectedFolderPath is not string path || Load == null) return;
        var folder = Load.Folders.FirstOrDefault(x => x.Path == path);
        if (folder == null) return;
        var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Choose folder art",
            FileTypeFilter = [new FilePickerFileType("Images") { Patterns = ["*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp", "*.gif"] }],
        });
        if (picked.Count == 0 || picked[0].TryGetLocalPath() is not string p) return;
        RgbaImage img;
        try { img = RgbaImage.Decode(File.ReadAllBytes(p)); }
        catch (Exception e) when (e is IOException or Core.Formats.FormatException) { Status.Text = $"Could not read {p}: {e.Message}"; return; }
        Stage();
        folder.PendingCover = img; folder.RemoveArtStaged = false;
        InvalidateFolderArt(folder);
        ShowFolderDetails(path);
        RefreshGameList();
        UpdateStatusCounts();
        Status.Text = $"Staged: folder art for /{path} (written as FOLDER.PNG on Save changes).";
    }

    void RemoveFolderArt()
    {
        if (_selectedFolderPath is not string path || Load == null) return;
        var folder = Load.Folders.FirstOrDefault(x => x.Path == path);
        if (folder == null) return;
        Stage();
        folder.PendingCover = null;
        folder.RemoveArtStaged = true;
        InvalidateFolderArt(folder);
        ShowFolderDetails(path);
        UpdateStatusCounts();
        Status.Text = $"Staged: remove folder art for /{path} (applied on Save changes).";
    }

    string SavesSummary(GameRow row)
    {
        if (row.GameIdIni == "" || Load == null) return "none (header unreadable)";
        string? dir = Core.Saves.GameId.Dir(row.GameIdIni);
        if (dir == null) return "none";
        string folder = Path.Combine(Load.Root, "aether", "SAVES", dir);
        if (!Directory.Exists(folder)) return "no saves";
        var bups = Directory.EnumerateFiles(folder, "*.BUP", SearchOption.AllDirectories).ToList();
        if (bups.Count == 0) return "no saves";
        var last = bups.Select(f => File.GetLastWriteTime(f)).Max();
        return $"{bups.Count} save file(s) · last {last:yyyy-MM-dd}";
    }

    /// <summary>Decoded covers, cached per row: the same bitmap backs the list's thumbnail and the side panel's large
    /// preview, so a 700-game card decodes each cover once, lazily, only for the rows actually realized on screen.</summary>
    readonly Dictionary<GameRow, Bitmap?> _coverCache = new();

    Bitmap? ThumbFor(GameRow row) => CoverBitmap(row);

    void InvalidateCover(GameRow row) => _coverCache.Remove(row);

    /// <summary>The cover read order (diagnosed on a real card): a game's staged cover
    /// change first; else ART.PAK's hero/thumb (SPEC §5.6 steps 2-3 -- most games on a built card have this, and it
    /// needs no extra file I/O); else the game folder's BOX.TGA (step 1, the console's own first choice, kept here
    /// for a game ART.PAK has no entry for); else, only when the user has pointed the window at one, the build's own
    /// offline art sources (a covers zip, a retrohead/libretro cache) -- a preview convenience, not written to the
    /// card.</summary>
    Bitmap? CoverBitmap(GameRow row)
    {
        if (_coverCache.TryGetValue(row, out var cached)) return cached;
        Bitmap? bmp = null;
        try
        {
            // Coordinator fix round, item 2: the user's own BOX.TGA first (whether or not the game matched in
            // GAMES.DB -- BoxTgaImage only needs row.HasBox and row.Folder, never row.Key), then ART.PAK, then the
            // build's own offline sources.
            if (row.PendingCover is RgbaImage pending) bmp = ToBitmap(ToRgb555Preview(pending));
            else if (BoxTgaImage(row) is RgbaImage box) bmp = ToBitmap(ToRgb555Preview(box));
            else if (Load?.ArtData != null && Core.Art.CoverLookup.FromArtPak(Load.ArtData, row.Key, row.Disc) is RgbaImage art)
                bmp = ToBitmap(art);                              // already RGB555-decoded, at its own stored size
            // Automatic covers (M-art-pack §5): a matched game with neither -- the bundled cover pack's best source.
            else if (row.Key != "" && BestPackCover(row.Key) is RgbaImage packed) bmp = ToBitmap(ToRgb555Preview(packed));
            else if (FromBuildSources(row) is RgbaImage src) bmp = ToBitmap(ToRgb555Preview(src));
        }
        catch (Exception e) when (e is IOException or Core.Formats.FormatException or System.FormatException or UnauthorizedAccessException) { }
        _coverCache[row] = bmp;
        return bmp;
    }

    static RgbaImage ToRgb555Preview(RgbaImage img)
    {
        var (square, hero, _) = CoverEncoder.Cover(img);
        var (w, h) = square ? CoverEncoder.HeroSquare : CoverEncoder.HeroTall;
        return CoverEncoder.DecodeRgb555(hero, w, h);
    }

    RgbaImage? BoxTgaImage(GameRow row)
    {
        if (!row.HasBox || Load == null) return null;
        string box = Path.Combine(Load.Root, (row.Folder == "" ? "" : row.Folder.Replace('/', Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar) + "BOX.TGA");
        return File.Exists(box) ? RgbaImage.Decode(File.ReadAllBytes(box)) : null;
    }

    Core.Art.CoverZip? _coverZip;
    string? _coverZipPath;
    Core.Art.RemoteArt? _remoteArt;
    string? _remoteArtPath;

    Core.Art.CoverZip? CoverZipFor()
    {
        string? path = Opt(CoversZip);
        if (path == null) return null;
        if (_coverZip == null || _coverZipPath != path)
        {
            _coverZip?.Dispose();
            _coverZip = null; _coverZipPath = null;
            try { _coverZip = new Core.Art.CoverZip(path); _coverZipPath = path; }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { }
        }
        return _coverZip;
    }

    Core.Art.RemoteArt? RemoteArtFor()
    {
        string? path = Opt(ArtCache);
        if (path == null) return null;
        if (_remoteArt == null || _remoteArtPath != path) { _remoteArt = new Core.Art.RemoteArt(path, download: false); _remoteArtPath = path; }
        return _remoteArt;
    }

    /// <summary>The build's own offline sources, when the user has pointed this session at them (the Tools > Build
    /// card dialog's covers-zip and art-cache fields): the covers zip by file or folder name, then retrohead, then
    /// libretro from the art cache -- the same order CardBuilder tries them in, read-only here.</summary>
    RgbaImage? FromBuildSources(GameRow row)
    {
        if (Load == null) return null;
        var cz = CoverZipFor();
        if (cz != null)
        {
            string parent = row.Folder.LastIndexOf('/') is int i and >= 0 ? row.Folder[(i + 1)..] : row.Folder;
            string? member = cz.Find(row.FileName) ?? (parent != "" ? cz.Find(parent) : null);
            if (member != null)
            {
                try { return cz.Image(member); }
                catch (Exception e) when (e is IOException or Core.Formats.FormatException) { }
            }
        }
        if (row.Key == "") return null;
        var ra = RemoteArtFor();
        if (ra == null) return null;
        var (img, _) = ra.RetroheadCover(row.Key);
        if (img != null) return img;
        if (Load.Db.Records.FirstOrDefault(r => r.Key == row.Key) is { } rec)
        {
            var (img2, _) = ra.LibretroCover(rec.TitleEn, rec.RegionFlags);
            if (img2 != null) return img2;
        }
        return null;
    }

    // =========================== Cover pack / built-in DB / Match to game / Choose cover ===========================

    Core.Art.CoverPackData? _coverPack;
    bool _coverPackAttempted;
    Core.Formats.GamesDbData? _builtInDb;
    bool _builtInDbAttempted;

    /// <summary>The bundled cover pack (covers/COVERS.PAK next to the exe), opened once and kept for the window's
    /// life; null when it is not installed (dev build with no build/coverpack copy, or packaging has not shipped it
    /// yet). Never throws for a missing file; a corrupt one is treated the same as missing (logged to Status by the
    /// caller that first needed it, not here, since merely showing a cover must stay silent).</summary>
    Core.Art.CoverPackData? EnsureCoverPack()
    {
        if (!_coverPackAttempted)
        {
            _coverPackAttempted = true;
            try { _coverPack = Core.Art.CoverPack.OpenDefault(); }
            catch (Exception e) when (e is IOException or Core.Formats.FormatException) { _coverPack = null; }
        }
        return _coverPack;
    }

    /// <summary>Tests: inject a synthetic pack (or null, for the "pack not installed" state) without touching the
    /// real covers/COVERS.PAK path, which may or may not exist on the machine running the tests.</summary>
    public void SetCoverPackForTests(Core.Art.CoverPackData? pack) { _coverPack = pack; _coverPackAttempted = true; }

    /// <summary>The Card Manager's bundled GAMES.DB (embedded resource, built from build/db/GAMES.DB), for "Match to
    /// game..." to search -- a fuller/newer database than whatever is on the card. Falls back to the card's own
    /// Load.Db when the resource was not embedded (a dev build with no build/db at build time).</summary>
    Core.Formats.GamesDbData? EnsureBuiltInDb()
    {
        if (!_builtInDbAttempted) { _builtInDbAttempted = true; _builtInDb = Core.Formats.BuiltInDb.Load(); }
        return _builtInDb ?? Load?.Db;
    }

    public void SetBuiltInDbForTests(Core.Formats.GamesDbData? db) { _builtInDb = db; _builtInDbAttempted = true; }

    /// <summary>coverpack.py's source order (task spec order): the Cover Project zip first (it is the one most
    /// consistently curated for Satiator), then retrohead's flat front, then libretro, then retrohead's 3D render.</summary>
    static readonly string[] PackCoverOrder = ["coverproject", "retrohead2d", "libretro", "retrohead3d"];

    Core.Art.CoverPackEntry? BestPackEntry(string code)
    {
        if (EnsureCoverPack() is not { } pack) return null;
        var forCode = pack.For(code);
        foreach (var src in PackCoverOrder)
            if (forCode.FirstOrDefault(e => e.Source == src) is { } e) return e;
        return null;
    }

    /// <summary>Whether the pack has a cover from this exact source for this code (View3DButton: shown only when a
    /// 3D render actually exists, per the panel-layout fix round -- cleaner than a disabled, cut-off button).</summary>
    bool HasPackSource(string code, string source) =>
        EnsureCoverPack() is { } pack && pack.For(code).Any(e => e.Source == source);

    RgbaImage? BestPackCover(string code)
    {
        if (BestPackEntry(code) is not { } e || _coverPack == null) return null;
        try { return RgbaImage.Decode(_coverPack.Blob(e)); }
        catch (Exception ex) when (ex is IOException or Core.Formats.FormatException or System.FormatException) { return null; }
    }

    // ----- Match to game… -----

    /// <summary>Tests answer the Match dialog without showing it: null = cancelled, else the chosen GAMES.DB key.</summary>
    public Func<MainWindow, GameRow, Task<string?>>? MatchGameDialogPrompt;

    public async Task MatchToGame()
    {
        if (GameList.SelectedItem is not GameRow row || Load == null) return;
        string? chosen = await (MatchGameDialogPrompt ?? DefaultMatchGameDialog)(this, row);
        if (chosen == null) return;
        if (!Load.Db.Records.Any(r => r.Key == chosen)) { Status.Text = $"{chosen} is not in this card's GAMES.DB; nothing was staged."; return; }
        Stage();
        row.MatchOverride = chosen;
        InvalidateCover(row);
        ShowDetails(row);
        RefreshGameList();
        UpdateStatusCounts();
        Status.Text = $"Staged: match {row.FileName} to {chosen} (used for GAMES.DB/LIBRARY.IDX/ART.PAK on Save changes).";
    }

    async Task<string?> DefaultMatchGameDialog(Window owner, GameRow row)
    {
        string? result = null;
        var db = EnsureBuiltInDb();
        var search = new TextBox { Width = 320, Text = row.Code != "" ? row.Code : row.FileName, PlaceholderText = "Title or product code" };
        var results = new ListBox { Height = 320 };
        List<GameRecord> matches = [];
        void Refresh()
        {
            string q = (search.Text ?? "").Trim();
            matches = (db?.Records ?? []).Where(r => q == "" || r.TitleEn.Contains(q, StringComparison.OrdinalIgnoreCase) || r.Key.Contains(q, StringComparison.OrdinalIgnoreCase))
                .OrderBy(r => r.TitleEn, StringComparer.OrdinalIgnoreCase).Take(50).ToList();
            results.ItemsSource = matches.Select(r => $"{r.TitleEn}  [{r.Key}]  {GamesDb.RegionLettersFrom(r.RegionFlags)}  {(r.Year > 0 ? r.Year.ToString(System.Globalization.CultureInfo.InvariantCulture) : "")}").ToList();
        }
        search.TextChanged += (_, _) => Refresh();
        Refresh();
        var w = new Window { Title = $"Match to game — {row.FileName}", Width = 560, Height = 480, CanResize = true };
        var ok = new Button { Content = "Choose", IsDefault = true };
        var cancel = new Button { Content = "Cancel" };
        ok.Click += (_, _) => { if (results.SelectedIndex is int i and >= 0) result = matches[i].Key; w.Close(); };
        cancel.Click += (_, _) => w.Close();
        results.DoubleTapped += (_, _) => { if (results.SelectedIndex is int i and >= 0) { result = matches[i].Key; w.Close(); } };
        w.Content = new DockPanel
        {
            Margin = new Thickness(16),
            Children =
            {
                search.Also(c => DockPanel.SetDock(c, Dock.Top)),
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 10, 0, 0),
                    Children = { cancel, ok } }.Also(c => DockPanel.SetDock(c, Dock.Bottom)),
                results,
            },
        };
        await w.ShowDialog(owner);
        return result;
    }

    // ----- Add to database… / Edit database entry… -----

    /// <summary>The user's own GAMES.DB entry for the selected game (the user, 2026-11-24: "click them and manually add
    /// a database entry"): a game the database does not know gets a user record (Core.Card.UserRecords.Add -- its code,
    /// or a synthetic key for homebrew); a known one gets its USEREDIT.INI fields. Staged, one Ctrl+Z, written into
    /// GAMES.DB (user-title flag set) and LIBRARY.IDX at Save changes, so the menu shows it.</summary>
    public async Task EditDatabaseEntry()
    {
        if (GameList.SelectedItem is not GameRow row || Load == null) return;
        var db = DbByKey();
        db.TryGetValue(row.Key, out var rec);
        var cur = row.Key != "" ? Load.UserEdits.GetValueOrDefault(row.Key)?.Clone() ?? new() : new();
        var start = new Core.Formats.UserEditRecord
        {
            Title = cur.Title != "" ? cur.Title : rec?.TitleEn ?? row.DisplayTitle(db, Load.UserEdits),
            Genre = cur.Genre != "" ? cur.Genre : GenreFor(row, db),
            Year = cur.Year != 0 ? cur.Year : rec?.Year ?? 0,
            Region = cur.Region != "" ? cur.Region : rec != null ? GamesDb.RegionLettersFrom(rec.RegionFlags) : row.RegionText.Replace(" ", ""),
            Players = cur.Players != 0 ? cur.Players : rec?.Players ?? 0,
            New = cur.New,
        };
        var got = await (DatabaseEntryPrompt ?? DefaultDatabaseEntryDialog)(this, row, start);
        if (got == null) return;
        if (got.Title.Trim() == "") { Status.Text = "A database entry needs a title; nothing was staged."; return; }
        Stage();
        if (row.Key == "")
        {
            string key = Core.Card.UserRecords.Add(Load, row, got);
            Status.Text = $"Staged: \"{got.Title.Trim()}\" added to the database as {key} (written to GAMES.DB at Save changes).";
        }
        else
        {
            // only what differs from GAMES.DB's own record is an override (as CommitMetaEdit); a user record keeps new=1
            string region = got.Region.Trim().ToUpperInvariant();
            var e = new Core.Formats.UserEditRecord
            {
                Title = rec != null && got.Title.Trim() == rec.TitleEn ? "" : got.Title.Trim(),
                Genre = rec != null && Load.Db.Genres.ElementAtOrDefault(rec.Genre) == got.Genre.Trim() ? "" : got.Genre.Trim(),
                Year = rec != null && got.Year == rec.Year ? 0 : got.Year,
                Region = rec != null && GamesDb.RegionFlagsFrom(region) == rec.RegionFlags ? "" : region,
                Players = rec != null && got.Players == rec.Players ? 0 : got.Players,
                New = cur.New,
            };
            if (e.IsEmpty) Load.UserEdits.Remove(row.Key); else Load.UserEdits[row.Key] = e;
            Status.Text = $"Staged: the database entry for {row.Key} (written to GAMES.DB at Save changes).";
        }
        InvalidateCover(row);
        RefreshGameList();
        GameList.SelectedItem = row;
        ShowDetails(row);
        UpdateStatusCounts();
    }

    async Task<Core.Formats.UserEditRecord?> DefaultDatabaseEntryDialog(Window owner, GameRow row, Core.Formats.UserEditRecord start)
    {
        Core.Formats.UserEditRecord? result = null;
        bool adding = row.Key == "";
        var title = new TextBox { Text = start.Title, PlaceholderText = "The name the menu shows (required)" };
        var genre = new TextBox { Text = start.Genre, PlaceholderText = "e.g. Action - Platform" };
        var year = new TextBox { Text = start.Year > 0 ? start.Year.ToString(System.Globalization.CultureInfo.InvariantCulture) : "", PlaceholderText = "e.g. 1996" };
        var region = new TextBox { Text = start.Region, PlaceholderText = "U, J, E or a mix (e.g. JU)" };
        var players = new TextBox { Text = start.Players > 0 ? start.Players.ToString(System.Globalization.CultureInfo.InvariantCulture) : "", PlaceholderText = "1-8" };
        var problem = new TextBlock { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Warn"), TextWrapping = Avalonia.Media.TextWrapping.Wrap };
        var w = new Window { Title = adding ? "Add to database" : "Edit database entry", Width = 460, SizeToContent = SizeToContent.Height, CanResize = false };
        AppTheme.ApplyToWindow(w);
        var form = new Grid { ColumnDefinitions = new ColumnDefinitions("80,*"), RowDefinitions = new RowDefinitions("Auto,Auto,Auto,Auto,Auto"), RowSpacing = 6 };
        int r = 0;
        foreach (var (label, box) in new (string, Control)[] { ("Title", title), ("Genre", genre), ("Year", year), ("Region", region), ("Players", players) })
        {
            var l = new TextBlock { Text = label, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center };
            Grid.SetRow(l, r); Grid.SetRow(box, r); Grid.SetColumn(box, 1);
            form.Children.Add(l); form.Children.Add(box);
            r++;
        }
        var ok = new Button { Content = adding ? "Add" : "Save entry", IsDefault = true, FontWeight = FontWeight.Bold, Classes = { "primary" } };
        var cancel = new Button { Content = "Cancel", IsCancel = true };
        cancel.Click += (_, _) => w.Close();
        ok.Click += (_, _) =>
        {
            int y = 0, pl = 0;
            if ((title.Text ?? "").Trim() == "") { problem.Text = "The title is required."; return; }
            if ((year.Text ?? "").Trim() is { Length: > 0 } ys && (!int.TryParse(ys, out y) || y is < 1990 or > 2099)) { problem.Text = "The year is a number like 1996."; return; }
            if ((players.Text ?? "").Trim() is { Length: > 0 } ps && (!int.TryParse(ps, out pl) || pl is < 1 or > 12)) { problem.Text = "Players is a number from 1 to 12."; return; }
            string rg = (region.Text ?? "").Replace(",", "").Replace(" ", "").ToUpperInvariant();
            if (rg.Any(c => !"JTUEB".Contains(c))) { problem.Text = "Region is made of the letters J, T, U, E or B."; return; }
            result = new Core.Formats.UserEditRecord { Title = title.Text!.Trim(), Genre = (genre.Text ?? "").Trim(), Year = y, Region = rg, Players = pl, New = start.New };
            w.Close();
        };
        w.Content = new StackPanel
        {
            Margin = new Thickness(18, 16), Spacing = 10,
            Children =
            {
                new TextBlock { Text = adding ? "Add this game to the database" : "Edit this game's database entry", FontSize = 17, FontWeight = FontWeight.Bold },
                new TextBlock { Text = (adding ? $"{row.FileName} ({(row.Code != "" ? row.Code : "no product code")}) is not in GAMES.DB. " : "")
                                       + "The menu shows this after Save changes; it is kept in USEREDIT.INI, so a newer GAMES.DB or a rescan keeps it.",
                                [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), TextWrapping = Avalonia.Media.TextWrapping.Wrap },
                form, problem,
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { cancel, ok } },
            },
        };
        await w.ShowDialog(owner);
        return result;
    }

    // ----- Choose cover… -----

    public enum CoverChoiceKind { Cancel, Image, Box }
    /// <param name="Image">the chosen cover (pack or file); null when Kind is Box or Cancel.</param>
    public readonly record struct CoverChoice(CoverChoiceKind Kind, RgbaImage? Image)
    {
        public static readonly CoverChoice Cancel = new(CoverChoiceKind.Cancel, null);
    }

    /// <summary>Tests answer the Choose-cover dialog without showing it.</summary>
    public Func<MainWindow, GameRow, Task<CoverChoice>>? ChooseCoverDialogPrompt;

    async Task ChooseCover()
    {
        // Final review I1: the button is disabled for a loose game, but guard here too (defense in depth) --
        // this must never stage a write to a shared or root BOX.TGA.
        if (GameList.SelectedItem is not GameRow row || !row.OwnFolder) return;
        var choice = await (ChooseCoverDialogPrompt ?? DefaultChooseCoverDialog)(this, row);
        switch (choice.Kind)
        {
            case CoverChoiceKind.Cancel: return;
            case CoverChoiceKind.Box:
                Stage(); row.PendingCover = null; row.RestoreCoverStaged = true; break;
            case CoverChoiceKind.Image:
                if (choice.Image == null) return;
                Stage(); row.PendingCover = choice.Image; row.RestoreCoverStaged = false; break;
        }
        InvalidateCover(row);
        ShowDetails(row);
        RefreshGameList();
        UpdateStatusCounts();
        Status.Text = $"Staged: cover for {row.FileName} (written as BOX.TGA on Save changes).";
    }

    /// <summary>A grid of every pack cover for the game's matched code (each labeled with its source), plus the
    /// game's own BOX.TGA when it has one, plus "From file…" (M-art-pack §4).</summary>
    async Task<CoverChoice> DefaultChooseCoverDialog(Window owner, GameRow row)
    {
        CoverChoice result = CoverChoice.Cancel;
        var w = new Window { Title = $"Choose a cover — {row.FileName}", Width = 560, Height = 480, CanResize = true };
        AppTheme.ApplyToWindow(w);
        var grid = new WrapPanel { Margin = new Thickness(4) };

        Control Tile(string label, Bitmap? bmp, Action pick)
        {
            var img = bmp != null
                ? new Image { Width = 90, Height = 112, Stretch = Stretch.Uniform, Source = bmp }
                : (Control)new Border { Width = 90, Height = 112, [!Border.BackgroundProperty] = AppTheme.Dyn("Placeholder") };
            var cell = new StackPanel
            {
                Width = 106, Margin = new Thickness(4), Spacing = 2,
                Children = { img, new TextBlock { Text = label, TextWrapping = Avalonia.Media.TextWrapping.Wrap, TextAlignment = Avalonia.Media.TextAlignment.Center, FontSize = 11 } },
            };
            var btn = new Button { Content = cell, Padding = new Thickness(2) };
            btn.Click += (_, _) => pick();
            return btn;
        }

        if (row.Key != "" && EnsureCoverPack() is { } pack)
        {
            foreach (var src in PackCoverOrder)
                if (pack.For(row.Key).FirstOrDefault(e => e.Source == src) is { } e)
                {
                    Bitmap? bmp = null;
                    try { bmp = ToBitmap(ToRgb555Preview(RgbaImage.Decode(pack.Blob(e)))); } catch (Exception ex) when (ex is IOException or Core.Formats.FormatException or System.FormatException) { }
                    if (bmp == null) continue;
                    grid.Children.Add(Tile(src, bmp, () => { try { result = new CoverChoice(CoverChoiceKind.Image, RgbaImage.Decode(pack.Blob(e))); } catch (Exception ex) when (ex is IOException or Core.Formats.FormatException or System.FormatException) { result = CoverChoice.Cancel; } w.Close(); }));
                }
        }
        else if (row.Key == "")
            grid.Children.Add(new TextBlock { Text = "Match this game to a GAMES.DB entry first to see its cover pack options.", TextWrapping = Avalonia.Media.TextWrapping.Wrap, Width = 420, Margin = new Thickness(8) });
        else
            grid.Children.Add(new TextBlock { Text = "Cover pack not installed.", TextWrapping = Avalonia.Media.TextWrapping.Wrap, Width = 420, Margin = new Thickness(8) });

        RgbaImage? boxImg = null;
        try { if (row.HasBox) boxImg = BoxTgaImage(row); } catch (Exception ex) when (ex is IOException or Core.Formats.FormatException or System.FormatException) { }
        if (boxImg != null)
        {
            Bitmap? bmp = null;
            try { bmp = ToBitmap(ToRgb555Preview(boxImg)); } catch (Exception ex) when (ex is IOException or Core.Formats.FormatException or System.FormatException) { }
            grid.Children.Add(Tile("this game's BOX.TGA", bmp, () => { result = new CoverChoice(CoverChoiceKind.Box, null); w.Close(); }));
        }

        grid.Children.Add(Tile("From file…", null, async () =>
        {
            var img = await PickCoverFile();
            if (img == null) return;
            result = new CoverChoice(CoverChoiceKind.Image, img);
            w.Close();
        }));

        var cancel = new Button { Content = "Cancel", HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 10, 0, 0) };
        cancel.Click += (_, _) => { result = CoverChoice.Cancel; w.Close(); };
        w.Content = new DockPanel
        {
            Margin = new Thickness(16),
            Children = { cancel.Also(c => DockPanel.SetDock(c, Dock.Bottom)), new ScrollViewer { Content = grid } },
        };
        await w.ShowDialog(owner);
        return result;
    }

    // "Fill missing covers" (bulk cover fill, Tools menu) is removed for 0.1 (user: "too broad and dangerous").
    // Per-game Change cover/Restore cover (ChooseCover, below) are untouched; EnsureCoverPack/BestPackEntry/
    // BestPackCover stay -- ChooseCover's own picker still offers the cover pack as one source.

    static Bitmap ToBitmap(RgbaImage img)
    {
        var wb = new WriteableBitmap(new PixelSize(img.W, img.H), new Vector(96, 96), Avalonia.Platform.PixelFormat.Rgba8888, Avalonia.Platform.AlphaFormat.Unpremul);
        using var fb = wb.Lock();
        int stride = img.W * 4;
        for (int y = 0; y < img.H; y++)
            System.Runtime.InteropServices.Marshal.Copy(img.Rgba, y * stride, IntPtr.Add(fb.Address, y * fb.RowBytes), stride);
        return wb;
    }

    void OnLoaderChanged()
    {
        if (_loadingDetails || GameList.SelectedItem is not GameRow row) return;
        string loader = LoaderBox.SelectedIndex switch { 1 => "jhl", 2 => "cwx", _ => "auto" };
        if (loader == row.Loader) return;
        Stage();
        row.Loader = loader;
        UpdateStatusCounts();
    }

    /// <summary>"Rename to DB title" is now an explicit override: the GAMES.DB title
    /// is never an automatic fallback any more, so this stages TitleOverride = the DB title, rather than clearing
    /// the override to fall through to it.</summary>
    public void RenameToDbTitle()
    {
        if (GameList.SelectedItem is not GameRow row) return;
        if (!DbByKey().TryGetValue(row.Key, out var rec) || rec.TitleEn == "") return;
        Stage();
        row.TitleOverride = rec.TitleEn;
        RefreshGameList();
        ShowDetails(row);
        UpdateStatusCounts();
    }

    void ToggleFavorite()
    {
        Stage();
        foreach (var row in GameList.SelectedItems!.OfType<GameRow>().ToList()) row.Favorite = !row.Favorite;
        RefreshGameList();
        ShowDetails(GameList.SelectedItem as GameRow);
        UpdateStatusCounts();
    }

    /// <summary>"Reset edits" (consolidation round): the selected game's own staged changes, back to what Load()
    /// actually read -- title override, favorite, loader, a pending cover change, the match override, and its
    /// USEREDIT.INI metadata record. Through Stage() like every other staging action, so the reset is itself one
    /// Ctrl+Z, not a second, separate kind of undo.</summary>
    void ResetEdits()
    {
        if (GameList.SelectedItem is not GameRow row || Load == null) return;
        Stage();
        row.TitleOverride = row.OriginalTitleOverride;
        row.Favorite = row.OriginalFavorite;
        row.Loader = row.OriginalLoader;
        row.MatchOverride = row.OriginalMatchOverride;
        row.PendingCover = null;
        row.RestoreCoverStaged = false;
        if (row.Key != "")
        {
            if (Load.OriginalUserEdits.TryGetValue(row.Key, out var orig)) Load.UserEdits[row.Key] = orig.Clone();
            else Load.UserEdits.Remove(row.Key);
        }
        InvalidateCover(row);
        RefreshGameList();
        ShowDetails(row);
        UpdateStatusCounts();
        Status.Text = $"Reset: every staged change on {row.FileName} is back to what's on the card.";
    }

    /// <summary>The actual file-picking flow "From file..." uses, in ChooseCover's dialog and as a fallback when no
    /// prompt is wired (tests). Returns the decoded image, or null on cancel/a read error (status is set either way).</summary>
    async Task<RgbaImage?> PickCoverFile()
    {
        var picked = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Choose a cover image",
            FileTypeFilter = [new FilePickerFileType("Images") { Patterns = ["*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp", "*.gif"] }],
        });
        if (picked.Count == 0 || picked[0].TryGetLocalPath() is not string p) return null;
        try { return RgbaImage.Decode(File.ReadAllBytes(p)); }
        catch (Exception e) when (e is IOException or Core.Formats.FormatException) { Status.Text = $"Could not read {p}: {e.Message}"; return null; }
    }

    void RestoreCover()
    {
        if (GameList.SelectedItem is not GameRow row) return;
        Stage();
        row.PendingCover = null;
        row.RestoreCoverStaged = true;
        InvalidateCover(row);
        ShowDetails(row);
        RefreshGameList();
        UpdateStatusCounts();
    }

    // =========================== Batch rename ===========================

    /// <summary>Tests answer the batch-rename dialog without showing it: null = cancelled.</summary>
    public Func<MainWindow, IReadOnlyList<GameRow>, Task<(RenameSource Source, bool TitleCase)?>>? BatchRenamePrompt;

    public async Task BatchRenameSelected()
    {
        if (Load == null || GameList.SelectedItems == null || GameList.SelectedItems.Count == 0) return;
        var rows = GameList.SelectedItems.OfType<GameRow>().ToList();
        if (rows.Count == 0) return;
        var choice = await (BatchRenamePrompt ?? DefaultBatchRenameDialog)(this, rows);
        if (choice == null) return;
        var preview = Core.Card.BatchRename.Compute(rows, choice.Value.Source, choice.Value.TitleCase, DbByKey());
        Stage();
        int n = Core.Card.BatchRename.Apply(preview);
        RefreshGameList();
        ShowDetails(GameList.SelectedItem as GameRow);
        UpdateStatusCounts();
        Status.Text = n > 0 ? $"Staged: renamed {n} of {rows.Count} game(s)." : "Nothing to rename (the chosen source was empty for every selected game).";
    }

    static readonly (string Label, RenameSource Source)[] RenameSources =
    [("GAMES.DB title", RenameSource.DbTitle), ("Disc header name", RenameSource.DiscHeader),
     ("Folder name", RenameSource.FolderName), ("File name", RenameSource.FileName)];

    async Task<(RenameSource Source, bool TitleCase)?> DefaultBatchRenameDialog(Window owner, IReadOnlyList<GameRow> rows)
    {
        (RenameSource Source, bool TitleCase)? result = null;
        var db = DbByKey();
        var sourceBox = new ComboBox { ItemsSource = RenameSources.Select(s => s.Label).ToList(), SelectedIndex = 0, MinWidth = 160 };
        var titleCaseBox = new CheckBox { Content = "Title case" };
        var preview = new ListBox { Height = 220 };
        void Refresh()
        {
            var src = RenameSources[sourceBox.SelectedIndex].Source;
            var p = Core.Card.BatchRename.Compute(rows, src, titleCaseBox.IsChecked == true, db);
            preview.ItemsSource = p.Select(x => $"{x.OldTitle}  ->  {(x.NewTitle == "" ? "(no change: source is empty)" : x.NewTitle)}").ToList();
        }
        sourceBox.SelectionChanged += (_, _) => Refresh();
        titleCaseBox.IsCheckedChanged += (_, _) => Refresh();
        Refresh();
        var w = new Window { Title = $"Batch rename {rows.Count} game(s)", Width = 520, Height = 420, CanResize = false };
        var ok = new Button { Content = "Rename", IsDefault = true };
        var cancel = new Button { Content = "Cancel" };
        ok.Click += (_, _) => { result = (RenameSources[sourceBox.SelectedIndex].Source, titleCaseBox.IsChecked == true); w.Close(); };
        cancel.Click += (_, _) => w.Close();
        w.Content = new DockPanel
        {
            Margin = new Thickness(16),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { new TextBlock { Text = "Rename from:", VerticalAlignment = VerticalAlignment.Center }, sourceBox, titleCaseBox } }.Also(b => DockPanel.SetDock(b, Dock.Top)),
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 10, 0, 0), Children = { cancel, ok } }.Also(b => DockPanel.SetDock(b, Dock.Bottom)),
                preview,
            },
        };
        await w.ShowDialog(owner);
        return result;
    }

    // =========================== Card report ===========================

    /// <summary>File > Export report: writes DISCLIST.TXT and DISCLIST.XLSX (title, code, region, disc, folder,
    /// cover yes/no, loader) into a folder the user picks. Read-only: nothing is staged, nothing touches the card.</summary>
    public async Task ExportReport()
    {
        if (Load == null || Load.Problem != null) { Status.Text = "Open a card first."; return; }
        var picked = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "Choose where to save the report" });
        if (picked.Count == 0 || picked[0].TryGetLocalPath() is not string dir) return;
        WriteReport(dir);
    }

    /// <summary>The actual write, separated from the folder picker so tests can call it directly.</summary>
    public void WriteReport(string dir)
    {
        if (Load == null || Load.Problem != null) return;
        var rows = Core.Card.DiscListReport.BuildRows(Load);
        string txtPath = Path.Combine(dir, "DISCLIST.TXT");
        string xlsxPath = Path.Combine(dir, "DISCLIST.XLSX");
        try
        {
            File.WriteAllText(txtPath, Core.Card.DiscListReport.WriteTxt(rows), System.Text.Encoding.UTF8);
            File.WriteAllBytes(xlsxPath, Core.Card.DiscListReport.WriteXlsx(rows));
            Status.Text = $"Wrote the report for {rows.Count} game(s): {txtPath} and {xlsxPath}.";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Status.Text = $"Could not write the report: {e.Message}";
        }
    }


    // =========================== Save changes ===========================

    /// <summary>Every current problem, by row: recomputed whenever the counts are (every staged
    /// edit, OpenCard, Refresh), read by the Views pane's counts and the "Needs attention"/"No cover"/"Not in
    /// database" views alike -- the Problems window is gone (consolidation round); those views replace it.</summary>
    public List<Core.Card.Problem> CurrentProblems { get; private set; } = [];

    /// <summary>Problems.Find, less a "no cover" the bundled pack fills at the next Prepare (NoCoverAnywhere).</summary>
    List<Core.Card.Problem> FindProblems() => Load == null ? [] :
        [.. Core.Card.Problems.Find(Load).Where(p => !(p.Kind == Core.Card.ProblemKind.NoCover && p.Row is GameRow r && !NoCoverAnywhere(r)))];

    void UpdateStatusCounts()
    {
        int games = 0, covers = 0, unsaved = 0;
        CurrentProblems = FindProblems();
        _cardDot?.Bind(Border.BackgroundProperty, Load is { Problem: null } ? "Green" : "Placeholder");
        if (Load != null)
        {
            var live = Load.Rows.Where(r => !r.Removed).ToList();
            games = live.Count;
            covers = live.Count((GameRow r) => r.HasArt || r.PendingCover != null);
            unsaved = live.Count((GameRow r) => r.Dirty) + Load.Folders.Count(f => f.Dirty);
            // A staged PC-backup restore counts too, even in the (rare) case it touches nothing any row/folder's own
            // Dirty flag would catch on its own (e.g. a PERGAME.INI-only restore with no row.Loader difference) --
            // CardEditSession.HasChanges already includes this; the status line and Save changes button must agree.
            if (Load.PendingRestoreFrom != null) unsaved++;
        }
        CountsText.Text = $"{games} games · {covers} covers";
        UnsavedText.Text = unsaved > 0 ? $"{unsaved} unsaved change{(unsaved == 1 ? "" : "s")} · Ctrl+Z to undo" : "No unsaved changes";
        SaveChangesButton.Content = unsaved > 0
            ? new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children =
                {
                    new TextBlock { Text = "Save changes", VerticalAlignment = VerticalAlignment.Center },
                    new Border { [!Border.BackgroundProperty] = AppTheme.Dyn("CountBubble"), CornerRadius = new CornerRadius(10),
                                 Padding = new Thickness(7, 1),
                                 Child = new TextBlock { Text = unsaved.ToString(System.Globalization.CultureInfo.InvariantCulture), FontSize = 11, FontWeight = FontWeight.Bold } },
                } }
            : "Save changes";
        SaveChangesButton.IsEnabled = unsaved > 0 && !IsBusy;
        ToolTip.SetTip(SaveChangesButton, unsaved > 0 ? "Write your staged changes to the card" : "No unsaved changes");
        RefreshCardChip(games);
        RefreshViewsList();   // the Views pane's own counts (Favorites/Needs attention/No cover/...) stay in sync
    }

    /// <summary>The card chip's own label/subtitle (consolidation round, mockup's ".chip"): the drive's real volume
    /// label (never a static "SD card"), and "N games - M free" once a card is actually open. A lightweight, read-
    /// only DriveInfo lookup, not the full CardTarget.Inspect (UpdateStatusCounts runs on every keystroke via
    /// RefreshGameList -- Inspect's own autoboot.iso read and system-drive side effects do not belong on that path).</summary>
    void RefreshCardChip(int games)
    {
        if (Load is not { Problem: null })
        {
            CardChipLabel.Text = "No card open";
            CardChipSubtitle.Text = "";
            return;
        }
        string drive = Path.GetPathRoot(Load.Root) is { Length: > 0 } r ? r.TrimEnd('\\', '/') : Load.Root;
        string label = drive;
        try
        {
            var di = new DriveInfo(drive);
            if (di.IsReady && !string.IsNullOrEmpty(di.VolumeLabel)) label += " " + di.VolumeLabel;
        }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException) { }
        CardChipLabel.Text = label;
        long free = FreeBytes(Load.Root);
        string freeText = free == long.MaxValue ? "" : $" · {free / 1024.0 / 1024.0 / 1024.0:F0} GB free";
        CardChipSubtitle.Text = $"{games} game{(games == 1 ? "" : "s")}{freeText}";
    }

    public async Task SaveChanges()
    {
        if (Load == null || Load.Problem != null) return;
        if (!CardEditSession.HasChanges(Load)) { Status.Text = "Nothing to save."; return; }
        if (Target(out string root) is not CardTarget target) return;
        if (target.Refusal != null) { Status.Text = target.Refusal; return; }
        if (target.NeedsFixedOverride) { Status.Text = $"{target.Root} is on a fixed (non-removable) disk; tick \"This is not my system drive\" and try again."; return; }
        if (target.Volume.Identity is not string identity) { Status.Text = $"{root}: its volume could not be resolved, nothing was written."; return; }

        // Pre-save checks: free space against the whole change set (a preview
        // rebuild gives the real GAMES.DB/LIBRARY.IDX/ART.PAK sizes), and every target file or folder actually
        // writable -- before asking for confirmation, let alone writing anything.
        Status.Text = "Checking free space and file access...";
        var overrides = Load.Rows.Where(r => !r.Removed).ToDictionary(r => r.Path, r => r.TitleOverride);
        var matchOverrides = CardEditSession.MatchOverrides(Load);
        var pack = EnsureCoverPack();   // a newly matched game gets the pack's cover at this save too, as at Prepare
        RebuildResult preview;
        try
        {
            preview = await Task.Run(() => CardRebuilder.Run(root, BuildDate(DateTime.Now), Load.GamesDbBytes, Load.ArtPak, overrides, Load.UserEdits, matchOverrides, pack));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or Core.Formats.FormatException or ArgumentException)
        {
            Status.Text = $"Could not check the card before saving: {e.Message}";
            return;
        }
        var problems = Core.Card.PreSaveChecks.Run(root, Load, preview, FreeBytes(root));
        if (problems.Count > 0) { Status.Text = "Save changes blocked:\n" + string.Join("\n", problems.Select(p => "  - " + p)); return; }

        var writes = CardEditSession.Describe(Load);
        string? refused = await CardWriteGuard.Check(this, target, "Save changes", writes, Ask);
        if (refused != null) { Status.Text = refused; return; }

        Busy(true);
        CancelButton.IsEnabled = CancelButton.IsVisible = true;
        _cts = new CancellationTokenSource();
        var token = _cts.Token;
        void OnProgress(string line) { ProgressHook?.Invoke(line); Dispatcher.UIThread.Post(() => Progress.Text = line); }
        Status.Text = "Saving...";
        try
        {
            var fs = new CardFs(root, identity);
            var report = await Task.Run(() => { WorkStarted?.Invoke(); return CardEditSession.Apply(fs, Load, BuildDate(DateTime.Now), OnProgress, token, pack); }, CancellationToken.None);
            string saved = $"Saved: {string.Join("; ", report.Steps)}." + (report.Stranded.Count > 0 ? " WARNING, could not undo: " + string.Join("; ", report.Stranded) : "");
            OpenCard(root);
            Status.Text = saved;
        }
        catch (OperationCanceledException)
        {
            Status.Text = "Cancelled: every staged edit was undone, nothing was written to the rebuilt files.";
        }
        catch (ApplyStoppedException e)
        {
            Status.Text = e.Message;
        }
        finally
        {
            Busy(false);
            CancelButton.IsEnabled = CancelButton.IsVisible = false;
            _cts.Dispose();
            _cts = null;
        }
    }

    // No "Build card" dialog and no release-folder/art-source picker any more ("No Build card setup"):
    // the Card Manager bundles the matching Aether release (next to the exe in a package; build/release or
    // ReleaseAssembler in a dev checkout) and always installs that one -- see "Prepare SD Card for Aether" below.
    // Assemble release is still a developer step (vmcm assemble-release, Cli/Program.cs), not in this window.

    // =========================== Shared with the legacy surface ===========================

    public void CancelImport() => _cts?.Cancel();

    public void AllowFixedVolumeForTests(string volumeId) { _overrideFor = volumeId; NotSystemDrive.IsChecked = true; }

    // ---- library-tree expand/collapse test hooks (coordinator round, repointed at the unified list by the
    // consolidation round) ----
    public void ToggleFolderCollapsedForTests(string path) => ToggleCollapsed(path);
    public void SetAllFoldersCollapsedForTests(bool collapsed) => SetAllCollapsed(collapsed);
    public bool IsFolderCollapsedForTests(string path) => _collapsed.Contains(path);
    /// <summary>Runs the library list's own Left/Right key handler directly (no synthetic routed-event plumbing):
    /// the same code the real KeyDown subscription calls.</summary>
    public void SimulateFolderListKeyForTests(Key k) => OnLibraryKeyDown(GameList, new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = k });

    // ---- resizable-columns test hooks ----
    public double GameListColumnWidthForTests(int index) => _colWidths[index];
    public void ResizeGameListColumnForTests(int index, double width) { _colWidths[index] = width; BuildGameListHeader(); RefreshGameList(); }
    public void RefreshGameListForTests() => RefreshGameList();
    /// <summary>The one library list's current contents (consolidation round): FolderListRow/GameRow, in the exact
    /// order GameList shows them -- for tests to inspect structure and indentation without reaching into _view
    /// directly (same convention as every other *ForTests accessor here).</summary>
    public List<object> LibraryItemsForTests => _view;
    /// <summary>The same logic a folder row's own DoubleTapped handler runs, without fighting Avalonia's
    /// gesture-recognizer event construction in a test.</summary>
    public void SimulateFolderListDoubleTapForTests() { if (GameList.SelectedItem is FolderListRow f) ToggleCollapsed(f.Folder.Path); }
    /// <summary>Picks a view by name (consolidation round test hook): the real UI click is ViewsList.SelectedItem =
    /// the matching NavViewItem, which this mirrors without needing to find and click the real row.</summary>
    public void SelectViewForTests(LibraryView kind)
    {
        var item = (ViewsList.ItemsSource as IEnumerable<object>)?.OfType<NavViewItem>().FirstOrDefault(v => v.Kind == kind);
        if (item != null) ViewsList.SelectedItem = item;
    }

    /// <summary>Runs the same "Reset edits" the inspector's own button does, on the selected game.</summary>
    public void ResetEditsForTests() => ResetEdits();

    CardTarget? Target(out string root)
    {
        root = CardPath.Text?.Trim() ?? "";
        if (root == "" || !Directory.Exists(root)) { Status.Text = "Choose the card's root folder first."; return null; }
        CardTarget target;
        try { target = CardTarget.Inspect(root, SystemVolumes, NotSystemDrive.IsChecked == true ? _overrideFor : null); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException)
        {
            Status.Text = $"{root} could not be inspected ({e.Message}): nothing was written.";
            return null;
        }
        if (target.NeedsFixedOverride && target.Volume.Id != _overrideFor)
        {
            _overrideFor = target.Volume.Id;
            NotSystemDrive.IsChecked = false;
            NotSystemDrive.Content = $"This is not my system drive: {target.DriveRoot} ({target.Volume.Id})";
            NotSystemDrive.IsVisible = true;
        }
        return target;
    }

    public enum CloseDecision { Save, Discard, Cancel }

    /// <summary>Tests answer the unsaved-changes prompt without a dialog.</summary>
    public Func<Window, Task<CloseDecision>>? CloseAsk;

    void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (IsBusy) { e.Cancel = true; _closeWhenDone = true; _cts?.Cancel(); Status.Text = "Closing as soon as the card write stops safely -- do not remove the card."; return; }
        if (Load != null && CardEditSession.HasChanges(Load))
        {
            e.Cancel = true;
            _ = ConfirmCloseWithUnsavedChanges();
        }
    }

    /// <summary>Three choices, never two: Save changes (still confirmed through CardWriteGuard; the window stays open
    /// if that is cancelled or refused), Discard and exit (writes nothing), or Cancel (stays open).</summary>
    async Task ConfirmCloseWithUnsavedChanges()
    {
        var decision = await (CloseAsk ?? DefaultCloseDialog)(this);
        switch (decision)
        {
            case CloseDecision.Discard:
                Load = null;
                Close();
                break;
            case CloseDecision.Save:
                await SaveChanges();
                if (Load == null || !CardEditSession.HasChanges(Load)) Close();   // saved (or declined -> stays open)
                break;
            case CloseDecision.Cancel:
            default:
                break;
        }
    }

    static async Task<CloseDecision> DefaultCloseDialog(Window owner)
    {
        var result = CloseDecision.Cancel;
        var save = new Button { Content = "Save changes" };
        var discard = new Button { Content = "Discard and exit" };
        var cancel = new Button { Content = "Cancel", IsDefault = true };
        var w = new Window { Title = "Unsaved changes", Width = 420, SizeToContent = SizeToContent.Height, CanResize = false };
        save.Click += (_, _) => { result = CloseDecision.Save; w.Close(); };
        discard.Click += (_, _) => { result = CloseDecision.Discard; w.Close(); };
        cancel.Click += (_, _) => { result = CloseDecision.Cancel; w.Close(); };
        w.Content = new StackPanel
        {
            Margin = new Thickness(16), Spacing = 12,
            Children =
            {
                new TextBlock { Text = "There are unsaved changes.", TextWrapping = Avalonia.Media.TextWrapping.Wrap },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right,
                                 Children = { cancel, discard, save } },
            },
        };
        await w.ShowDialog(owner);
        return result;
    }

    void Busy(bool busy)
    {
        IsBusy = busy;
        if (!busy && _closeWhenDone) Dispatcher.UIThread.Post(Close);
        BuildCard.IsEnabled = !busy;
        if (busy) PrepareCardButton.IsEnabled = false; else RefreshPrepareCardState();
        SaveChangesButton.IsEnabled = !busy && Load != null && CardEditSession.HasChanges(Load);
    }

    static string? Opt(TextBox t) => string.IsNullOrWhiteSpace(t.Text) ? null : t.Text.Trim();

    /// <summary>Tests force a free-space figure, since a temp dir's real drive normally has plenty.</summary>
    public long? FreeBytesForTests;

    /// <summary>The target volume's available free space; long.MaxValue (never blocks on it) when it cannot be
    /// determined -- the pre-save free-space check is advisory, not a reason to refuse a write outright.</summary>
    long FreeBytes(string root)
    {
        if (FreeBytesForTests is long f) return f;
        try { return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(root)) is { Length: > 0 } r ? r : root).AvailableFreeSpace; }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException) { return long.MaxValue; }
    }

    public async Task RunBuild()
    {
        if (Target(out string root) is not CardTarget target) return;
        if (target.Refusal != null) { Status.Text = target.Refusal; return; }
        string? release = Opt(ReleasePath);
        if (release == null || !Directory.Exists(release)) { Status.Text = "Choose the Aether release folder first."; return; }
        var missing = CardPipeline.CheckRelease(release, compat: true);
        if (missing.Count > 0) { Status.Text = $"The release folder is missing {string.Join(", ", missing)}: nothing was written."; return; }
        if (CardFolderMigration.Detect(root) == CardFolderState.Both) { Status.Text = CardFolderMigration.BothMessage(root); return; }
        string? refused = await CardWriteGuard.Check(this, target, "Build an Aether card", CardFolderMigration.BuildWrites(root), Ask);
        if (refused != null) { Status.Text = refused; return; }
        Busy(true);
        Status.Text = "Building...";
        var p = new PipelineOptions(release, Opt(CoversZip), Opt(ArtCache), Download.IsChecked == true, FontPath: FontPath);
        if (target.Volume.Identity is not string identity) { Status.Text = $"{root}: its volume could not be resolved, nothing was written."; Busy(false); return; }
        try
        {
            var r = await Task.Run(() => { WorkStarted?.Invoke(); return CardPipeline.Run(root, p, BuildDate(DateTime.Now), identity); });
            var s = r.Build.Report["scan"]!;
            string built = (r.Folder is { Moved: true } mf ? $"Moved /vibemenu to /aether ({mf.Files} files, backed up to {mf.BackupDir}). " : "") +
                          $"Built and installed: {s["denominator"]}, {s["matched"]} matched, {r.Build.Report["art_entries"]} art entries, " +
                          $"{r.Build.Report["jp"]!["rendered"]} JP titles." +
                          (r.Install.Backups.Count > 0 ? " Backed up: " + string.Join(", ", r.Install.Backups) + "." : "") +
                          (r.Install.Warnings.Count > 0 ? " Warnings: " + string.Join("; ", r.Install.Warnings) : "") +
                          (CardBuilder.JpWarning(r.Build) is string jw ? " WARNING: " + jw : "");
            OpenCard(root);
            Status.Text = built;
        }
        catch (Exception e) when (e is VibeMenuCardManager.Core.Install.InstallException or IOException or UnauthorizedAccessException
                                      or VibeMenuCardManager.Core.Formats.FormatException or System.Text.Json.JsonException
                                      or ArgumentException)
        {
            Status.Text = $"Build failed: {e.Message}";
        }
        finally { Busy(false); }
    }

    // "Tidy saves" and "Import from Kai/Rings…" are removed for 0.1 (the user: they return post-0.1 with the same
    // UI sophistication as the in-menu save manager). Core.Saves.SaveTidy/SaveImporter are untouched, just
    // unreached from here; CardFs, TidyBusyException/TidyCancelledException/TidyStoppedException and
    // ImportCancelledException/ImportStoppedException stay in use by Core and by the golden tests.

    public static uint BuildDate(DateTime d) => (uint)(d.Year * 10000 + d.Month * 100 + d.Day);
}

/// <summary>A plain ICommand for code-built menus (no CommunityToolkit/ReactiveUI dependency).</summary>
sealed class RelayCommand(Action execute) : System.Windows.Input.ICommand
{
    public event EventHandler? CanExecuteChanged { add { } remove { } }
    public bool CanExecute(object? parameter) => true;
    public void Execute(object? parameter) => execute();
}
