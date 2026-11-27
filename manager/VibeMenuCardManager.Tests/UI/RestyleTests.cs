// VibeMenu Card Manager, GPL-3.0-or-later.
// The Saturn-menu look in its two themes, Light (the default) and Dark: the window's own gradient background, rounded
// panes, a live switch that reaches every open window, the choice remembered, and -- the automated form of "both easy
// to read" -- every text/background pair the UI draws meeting WCAG AA, computed from the live resource values each
// theme installs, never from a copy of the palette.
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class RestyleTests
{
    public static TheoryData<ThemeKind> Themes => new() { ThemeKind.Light, ThemeKind.Dark };

    static MainWindow Window(string dir)
    {
        var w = new MainWindow(removableDriveListForTests: () => []);
        w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    static void BuildCard(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write([], [], new Dictionary<string, string>(), 20261124));
        Directory.CreateDirectory(Path.Combine(root, "Games", "Alpha"));
        File.WriteAllBytes(Path.Combine(root, "Games", "Alpha", "Alpha.iso"),
            SystemIdReader.Make(new Dictionary<string, string> { ["product"] = "T-00001", ["area"] = "U", ["title"] = "T", ["device"] = "CD-1/1" }));
    }

    static IEnumerable<T> AllDescendants<T>(Visual root) where T : Visual
    {
        foreach (var child in root.GetVisualChildren())
        {
            if (child is T t) yield return t;
            foreach (var d in AllDescendants<T>(child)) yield return d;
        }
    }

    /// <summary>Runs `f` on the UI thread with `kind` installed, always putting Dark (the default every other test
    /// expects) back before the UI thread is released.</summary>
    static Task<T> InTheme<T>(ThemeKind kind, Func<T> f) => HeadlessApp.Run(() =>
    {
        AppTheme.Use(kind);
        try { return Task.FromResult(f()); }
        finally { AppTheme.Use(ThemeKind.Dark); }
    });

    // ===== WCAG 2.x relative luminance and contrast =====
    static double Chan(byte c8)
    {
        double c = c8 / 255.0;
        return c <= 0.03928 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);
    }
    static double Luminance(Color c) => 0.2126 * Chan(c.R) + 0.7152 * Chan(c.G) + 0.0722 * Chan(c.B);
    static double Contrast(Color a, Color b)
    {
        double l1 = Luminance(a), l2 = Luminance(b);
        return l1 > l2 ? (l1 + 0.05) / (l2 + 0.05) : (l2 + 0.05) / (l1 + 0.05);
    }
    static Color Over(Color fg, Color bg)
    {
        double a = fg.A / 255.0;
        byte Mix(byte f, byte b) => (byte)Math.Round(f * a + b * (1 - a));
        return Color.FromRgb(Mix(fg.R, bg.R), Mix(fg.G, bg.G), Mix(fg.B, bg.B));
    }

    /// <summary>Every color a brush paints: a solid brush's one, a gradient's every stop (the extremes are the
    /// worst cases for whatever sits on it).</summary>
    static Color[] Colors(object? v) => v switch
    {
        ISolidColorBrush s => [s.Color],
        IGradientBrush g => [.. g.GradientStops.Select(st => st.Color)],
        Color c => [c],
        _ => throw new InvalidOperationException($"not a brush: {v}"),
    };

    static object? Live(string key)
    {
        Assert.True(Application.Current!.TryGetResource(key, Application.Current.ActualThemeVariant, out var v), $"no resource {key}");
        return v;
    }

    /// <summary>The live contrast table: each (foreground, layers...) is composited layer by layer from the bottom
    /// (each background stop of the window, the lightest and the darkest) up, every gradient stop of every layer
    /// tried, and the worst result must reach `min`.</summary>
    static List<string> ContrastFailures()
    {
        var bottoms = AppTheme.Current.BackgroundStops;
        var fails = new List<string>();
        void Check(string fg, double min, params string[] layers)
        {
            IEnumerable<Color> bgs = bottoms;
            foreach (var layer in layers.Reverse())
            {
                var cols = Colors(Live(layer));
                bgs = bgs.SelectMany(b => cols.Select(c => Over(c, b))).ToList();
            }
            foreach (var f in Colors(Live(fg)))
                foreach (var b in bgs)
                {
                    double r = Contrast(Over(f, b), b);
                    if (r < min - 1e-9) fails.Add($"{fg} on {string.Join(" over ", layers.DefaultIfEmpty("window"))}: {r:F2}:1 < {min}:1 (fg {f}, bg {b})");
                }
        }
        string A(string n) => "Aether." + n;
        // text straight on the window (the dialogs) and on a pane
        foreach (var fg in new[] { "Text", "Muted", "Acc2", "Warn", "Link" })
        {
            Check(A(fg), 4.5);
            Check(A(fg), 4.5, A("Panel"));
        }
        // the list: hover, the selection bar, the status pills on a plain and a selected row
        foreach (var fg in new[] { "Text", "Muted", "Acc2", "Warn" })
        {
            Check(A(fg), 4.5, A("ListHover"), A("Panel"));
            Check(A(fg), 4.5, A("Selection"), A("Panel"));
            Check(A(fg), 4.5, A("PillBg"), A("Panel"));
            Check(A(fg), 4.5, A("PillBg"), A("Selection"), A("Panel"));
        }
        Check(A("Gold"), 3.0, A("Panel"));                        // the favorite star and the folder glyph
        Check(A("Gold"), 3.0, A("Selection"), A("Panel"));
        Check(A("Acc"), 3.0, A("Panel"));                         // the selection bar's edge, a focused field's border
        Check(A("Muted"), 3.0, A("Placeholder"), A("Panel"));     // the "?" on a cover-less thumbnail
        // header rows (the list's column header and the status bar; the branded bar)
        foreach (var fg in new[] { "Text", "Muted", "Acc2" })
        {
            Check(A(fg), 4.5, A("HeaderBg"));
            Check(A(fg), 4.5, A("HeaderBg"), A("Panel"));
            Check(A(fg), 4.5, A("HeaderBarBg"));
        }
        Check(A("WarnBadgeText"), 4.5, A("Warn"));
        // buttons in every state (Fluent's own keys, as the templates read them)
        Check("ButtonForeground", 4.5, "ButtonBackground", A("Panel"));
        Check("ButtonForegroundPointerOver", 4.5, "ButtonBackgroundPointerOver", A("Panel"));
        Check("ButtonForegroundPressed", 4.5, "ButtonBackgroundPressed", A("Panel"));
        Check("ButtonForegroundDisabled", 4.5, "ButtonBackgroundDisabled", A("Panel"));
        Check("ToggleButtonForeground", 4.5, "ToggleButtonBackground", A("Panel"));
        Check("ToggleButtonForegroundPointerOver", 4.5, "ToggleButtonBackgroundPointerOver", A("Panel"));
        Check("ToggleButtonForegroundChecked", 4.5, "ToggleButtonBackgroundChecked");
        Check("ToggleButtonForegroundCheckedPointerOver", 4.5, "ToggleButtonBackgroundCheckedPointerOver");
        Check("ToggleButtonForegroundDisabled", 4.5, "ToggleButtonBackgroundDisabled", A("Panel"));
        // Save changes and Prepare: white on every stop of every state's gradient, and on the change-count bubble
        foreach (var bg in new[] { "PrimaryButton", "PrimaryButtonHover", "PrimaryButtonPressed", "PrepareButton", "PrepareButtonHover", "PrepareButtonPressed" })
            Check(A("OnAccent"), 4.5, A(bg));
        Check(A("OnAccent"), 4.5, A("CountBubble"), A("PrimaryButton"));
        // text fields and combo boxes: normal, hover, focused, disabled, the watermark
        Check("TextControlForeground", 4.5, "TextControlBackground", A("Panel"));
        Check("TextControlForegroundPointerOver", 4.5, "TextControlBackgroundPointerOver", A("Panel"));
        Check("TextControlForegroundFocused", 4.5, "TextControlBackgroundFocused", A("Panel"));
        Check("TextControlForegroundDisabled", 4.5, "TextControlBackgroundDisabled", A("Panel"));
        Check("TextControlPlaceholderForeground", 4.5, "TextControlBackground", A("Panel"));
        Check("TextControlPlaceholderForegroundFocused", 4.5, "TextControlBackgroundFocused", A("Panel"));
        Check("TextControlForeground", 4.5, "TextControlBackground");   // the feedback form's fields sit on the window
        Check("TextControlPlaceholderForeground", 4.5, "TextControlBackground");
        if (Live("TextControlPlaceholderOpacity") is not 1.0) fails.Add("the watermark is faded by TextControlPlaceholderOpacity");
        Check("ComboBoxForeground", 4.5, "ComboBoxBackground", A("Panel"));
        Check("ComboBoxForeground", 4.5, "ComboBoxBackgroundPointerOver", A("Panel"));
        Check("ComboBoxForegroundDisabled", 4.5, "ComboBoxBackgroundDisabled", A("Panel"));
        Check("ComboBoxPlaceHolderForeground", 4.5, "ComboBoxBackground");
        Check("ComboBoxDropDownGlyphForeground", 3.0, "ComboBoxBackground", A("Panel"));
        // the drop-downs: a combo box's list, the menus, tooltips
        Check("ComboBoxItemForeground", 4.5, "ComboBoxDropDownBackground");
        Check("ComboBoxItemForegroundPointerOver", 4.5, "ComboBoxItemBackgroundPointerOver", "ComboBoxDropDownBackground");
        Check("ComboBoxItemForegroundSelected", 4.5, "ComboBoxItemBackgroundSelected");
        Check("ComboBoxItemForegroundSelectedPointerOver", 4.5, "ComboBoxItemBackgroundSelectedPointerOver");
        Check("MenuFlyoutItemForeground", 4.5, "MenuFlyoutPresenterBackground");
        Check("MenuFlyoutItemForegroundPointerOver", 4.5, "MenuFlyoutItemBackgroundPointerOver", "MenuFlyoutPresenterBackground");
        Check("MenuFlyoutItemForegroundPressed", 4.5, "MenuFlyoutItemBackgroundPressed", "MenuFlyoutPresenterBackground");
        Check("MenuFlyoutItemForegroundDisabled", 4.5, "MenuFlyoutPresenterBackground");
        Check("MenuFlyoutItemKeyboardAcceleratorTextForeground", 4.5, "MenuFlyoutPresenterBackground");
        Check("ToolTipForeground", 4.5, "ToolTipBackground");
        // check boxes, the toggle switch, the expander
        Check("CheckBoxForegroundUnchecked", 4.5, A("Panel"));
        Check("CheckBoxForegroundUnchecked", 4.5);
        Check("CheckBoxForegroundUncheckedDisabled", 4.5);
        Check("CheckBoxCheckGlyphForegroundChecked", 3.0, "CheckBoxCheckBackgroundFillChecked");
        Check("CheckBoxCheckBackgroundStrokeUnchecked", 3.0, A("Panel"));
        Check("ToggleSwitchContentForeground", 4.5, A("Panel"));
        Check("ToggleSwitchKnobFillOn", 3.0, "ToggleSwitchFillOn");
        Check("ToggleSwitchKnobFillOff", 3.0, A("Panel"));
        Check("ExpanderHeaderForeground", 4.5, "ExpanderHeaderBackground", A("Panel"));
        Check("ExpanderHeaderForegroundPointerOver", 4.5, "ExpanderHeaderBackgroundPointerOver");
        Check("ExpanderHeaderForeground", 4.5, "ExpanderHeaderBackground");
        Check("ExpanderChevronForeground", 3.0, "ExpanderHeaderBackground");
        Check("Aether.Text", 4.5, "ExpanderContentBackground");
        Check("Aether.Muted", 4.5, "ExpanderContentBackground");
        // the tree's chevrons (a glyph on a faint box, hovered or not) and the scroll bar thumb
        Check(A("Text"), 3.0, A("ButtonBg"), A("Panel"));
        Check(A("Text"), 3.0, A("HoverOverlay"), A("Panel"));
        Check("ScrollBarThumbBackgroundColor", 3.0, A("Panel"));
        return fails;
    }

    [Theory]
    [MemberData(nameof(Themes))]
    public async Task Every_text_and_glyph_meets_WCAG_AA_against_its_live_background(ThemeKind kind)
    {
        var fails = await InTheme(kind, ContrastFailures);
        Assert.True(fails.Count == 0, $"{kind}:\n" + string.Join("\n", fails));
    }

    /// <summary>The overrides are the live values Fluent's templates read, in each theme (not shadowed by Fluent's
    /// own theme dictionaries), and Fluent's own variant follows, so the native menus and scroll bars match.</summary>
    [Theory]
    [MemberData(nameof(Themes))]
    public async Task The_theme_installs_its_palette_and_Fluent_follows(ThemeKind kind)
    {
        var (variant, ok) = await InTheme(kind, () =>
        {
            var p = AppTheme.For(kind);
            bool same = ReferenceEquals(Live("TextControlPlaceholderForeground"), p.Watermark)
                        && ReferenceEquals(Live("ComboBoxPlaceHolderForeground"), p.Watermark)
                        && ReferenceEquals(Live("ButtonForegroundDisabled"), p.Disabled)
                        && ReferenceEquals(Live("MenuFlyoutPresenterBackground"), p.PopupBg)
                        && ReferenceEquals(Live("Aether.Panel"), p.Panel)
                        && ReferenceEquals(AppTheme.Panel, p.Panel);
            return (Application.Current!.ActualThemeVariant, same);
        });
        Assert.Equal(kind == ThemeKind.Dark ? ThemeVariant.Dark : ThemeVariant.Light, variant);
        Assert.True(ok);
    }

    /// <summary>The live-rendered check behind the table: headless Focus() does flip a real TextBox to ":focus", so
    /// this reads the template's actual TextPresenter.Foreground and PART_BorderElement.Background after focusing --
    /// catching Fluent's own :focus StyleTrigger winning with an off-theme fill.</summary>
    [Theory]
    [MemberData(nameof(Themes))]
    public async Task A_focused_text_field_renders_the_theme_text_on_the_theme_fill(ThemeKind kind)
    {
        var (fg, bg) = await InTheme(kind, () =>
        {
            var w = new Window();
            AppTheme.ApplyToWindow(w);
            var tb = new TextBox { Text = "x" };
            w.Content = tb;
            w.Show();
            Dispatcher.UIThread.RunJobs();
            tb.Focus();
            Dispatcher.UIThread.RunJobs();
            var presenter = AllDescendants<TextPresenter>(tb).First();
            var border = AllDescendants<Border>(tb).First(b => b.Name == "PART_BorderElement");
            var r = (((ISolidColorBrush)presenter.Foreground!).Color, ((ISolidColorBrush)border.Background!).Color);
            w.Close();
            return r;
        });
        var p = AppTheme.For(kind);
        Assert.Equal(p.TextColor, fg);
        Assert.Equal(((ISolidColorBrush)p.FieldBgFocused).Color, bg);
        foreach (var stop in p.BackgroundStops)
            Assert.True(Contrast(fg, Over(bg, Over(((ISolidColorBrush)p.Panel).Color, stop))) >= 4.5);
    }

    [Fact]
    public async Task Dark_is_the_default_theme_and_a_saved_choice_wins()
    {
        var kind = await HeadlessApp.Run(() => Task.FromResult(AppTheme.Kind));
        Assert.Equal(ThemeKind.Dark, kind);
        Assert.Equal(ThemeKind.Dark, AppTheme.Parse(null));
        Assert.Equal(ThemeKind.Dark, AppTheme.Parse("Purple"));
        Assert.Equal(ThemeKind.Light, AppTheme.Parse("Light"));
        Assert.Equal(ThemeKind.Dark, AppTheme.Parse("Dark"));
    }

    [Theory]
    [MemberData(nameof(Themes))]
    public async Task The_window_background_is_the_theme_gradient_not_the_Fluent_default(ThemeKind kind)
    {
        bool isThemeBg = await InTheme(kind, () => ReferenceEquals(new MainWindow(removableDriveListForTests: () => []).Background, AppTheme.For(kind).WindowBackground));
        Assert.True(isThemeBg);
    }

    [Theory]
    [MemberData(nameof(Themes))]
    public async Task Every_pane_is_rounded_and_drawn_in_the_theme_panel_color(ThemeKind kind)
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (paneCount, allRounded) = await InTheme(kind, () =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            Dispatcher.UIThread.RunJobs();
            var p = AppTheme.For(kind);
            var panes = AllDescendants<Border>(w).Where(b => b.Classes.Contains("pane")).ToList();
            bool rounded = panes.Count > 0 && panes.All(b => b.CornerRadius.TopLeft == 12 &&
                ReferenceEquals(b.Background, p.Panel) && ReferenceEquals(b.BorderBrush, p.Line));
            w.Close();
            return (panes.Count, rounded);
        });
        Assert.True(paneCount >= 3);   // the tree, the game list, and the details panel
        Assert.True(allRounded);
    }

    /// <summary>The nearest painted background behind a visual: the first ancestor (itself included) with a
    /// non-transparent Background, composited down to the window (gradients: every stop). Used to check that every
    /// TextBlock actually on screen reads against what is really behind it.</summary>
    static IEnumerable<Color> BackdropOf(Visual v, Color[] windowStops)
    {
        var layers = new List<IBrush>();
        for (Visual? cur = v; cur != null && cur is not Avalonia.Controls.Window; cur = cur.GetVisualParent())
        {
            IBrush? b = cur switch
            {
                // a text field's and a combo box's fill is a template sibling of their text, not an ancestor of it
                TextBox or ComboBox => AllDescendants<Border>(cur).FirstOrDefault(x => x.Name is "PART_BorderElement" or "Background")?.Background,
                Border bd => bd.Background,
                Panel pn => pn.Background,
                ContentPresenter cp => cp.Background,
                // a templated control's own Background is painted by its template's parts (above), which a state
                // (":disabled", ":pointerover") may repaint differently -- so it is never itself the backdrop
                _ => null,
            };
            if (b == null || (b is ISolidColorBrush s && s.Color.A == 0)) continue;
            layers.Add(b);
            if (Colors(b).All(c => c.A == 255)) break;
        }
        IEnumerable<Color> bgs = windowStops;
        for (int i = layers.Count - 1; i >= 0; i--)
        {
            var cols = Colors(layers[i]);
            bgs = bgs.SelectMany(b => cols.Select(c => Over(c, b))).ToList();
        }
        return bgs;
    }

    /// <summary>Every TextBlock on screen in the main window (a card open, a game selected, its details shown), the
    /// restore window and the feedback form with "Your setup" open reaches 4.5:1 against what is really painted
    /// behind it -- the general form of the old "no black text on the dark background" check, in both themes.</summary>
    [Theory]
    [MemberData(nameof(Themes))]
    public async Task Every_visible_text_reads_against_what_is_behind_it(ThemeKind kind)
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var fails = await InTheme(kind, () =>
        {
            var stops = AppTheme.For(kind).BackgroundStops;
            var bad = new List<string>();
            void Scan(Window w)
            {
                foreach (var t in AllDescendants<TextBlock>(w))
                {
                    if (string.IsNullOrWhiteSpace(t.Text) || !t.IsEffectivelyVisible || t.Foreground is not ISolidColorBrush fg) continue;
                    // the text's own and its ancestors' Opacity fade it toward what is behind it
                    double opacity = 1;
                    for (Visual? cur = t; cur != null && cur is not Avalonia.Controls.Window; cur = cur.GetVisualParent()) opacity *= cur.Opacity;
                    if (opacity == 0) continue;   // e.g. a ToggleSwitch's hidden On/Off content
                    var col = Color.FromArgb((byte)Math.Round(fg.Color.A * opacity), fg.Color.R, fg.Color.G, fg.Color.B);
                    // the native menu bar's own access-key headers sit on the window like any other text: included
                    foreach (var b in BackdropOf(t, stops))
                    {
                        double r = Contrast(Over(col, b), b);
                        if (r < 4.5) { bad.Add($"{w.GetType().Name}: \"{t.Text}\" {col} on {b}: {r:F2}"); break; }
                    }
                }
            }
            var main = Window(card.Dir);
            main.Show();
            main.OpenCard(card.Dir);
            main.GameList.SelectedItem = main.Load!.Rows.First(r => r.FileName.StartsWith("Alpha"));
            Dispatcher.UIThread.RunJobs();
            Scan(main);
            var restore = new RestoreWindow(main, main.Load!, []);
            restore.Show();
            Dispatcher.UIThread.RunJobs();
            Scan(restore);
            var feedback = new FeedbackWindow();
            feedback.Show();
            feedback.SetupSection.IsExpanded = true;
            Dispatcher.UIThread.RunJobs();
            Scan(feedback);
            feedback.Close(); restore.Close(); main.Close();
            return bad;
        });
        Assert.True(fails.Count == 0, $"{kind}:\n" + string.Join("\n", fails.Distinct()));
    }

    [Theory]
    [MemberData(nameof(Themes))]
    public async Task The_prepare_button_is_green_when_there_is_something_to_do_and_plain_once_installed(ThemeKind kind)
    {
        using var card = new TempCard();
        using var rel = new TempCard();
        BuildCard(card.Dir);
        Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
        var (readyBg, readyFg, doneBg) = await InTheme(kind, () =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);   // no autoboot.iso yet: "Prepare SD Card for Aether"
            var ready = (w.PrepareCardButton.Background, w.PrepareCardButton.Foreground);
            File.WriteAllBytes(Path.Combine(card.Dir, "autoboot.iso"),
                [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU", ["product"] = "OMSAT-MENU" }), .. new byte[2048],
                 .. System.Text.Encoding.ASCII.GetBytes($"VibeMenu {VibeMenuCardManager.Core.About.Credits.Version} abc1234\0")]);
            w.ReleaseOverrideForTests = PrepareCardTests.MatchingRelease(card.Dir, rel.Dir);   // the card's own pair: current
            PrepareCardTests.ListAsBuilt(card.Dir);   // and its game list matches its games
            w.OpenCard(card.Dir);   // now installed: the plain/done look
            return (ready.Background, ready.Foreground, w.PrepareCardButton.Background);
        });
        var p = AppTheme.For(kind);
        Assert.True(ReferenceEquals(readyBg, p.PrepareButton));
        Assert.True(ReferenceEquals(readyFg, p.OnAccent));
        Assert.True(ReferenceEquals(doneBg, p.PrepareDone));
    }

    /// <summary>RestoreWindow is a second top-level window, not a child control of MainWindow: it gets the theme's
    /// background too, in both themes.</summary>
    [Theory]
    [MemberData(nameof(Themes))]
    public async Task The_restore_window_matches_the_theme_too(ThemeKind kind)
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        bool isThemeBg = await InTheme(kind, () =>
        {
            var w = Window(card.Dir);
            w.OpenCard(card.Dir);
            var restore = new RestoreWindow(w, w.Load!, []);
            restore.Show();
            Dispatcher.UIThread.RunJobs();
            bool bg = ReferenceEquals(restore.Background, AppTheme.For(kind).WindowBackground);
            restore.Close();
            return bg;
        });
        Assert.True(isThemeBg);
    }

    /// <summary>Every toolbar button and icon-only control says what it does on hover, disabled ones included; and
    /// the app names itself (macOS's app menu and its About item read Application.Name).</summary>
    [Fact]
    public async Task Toolbar_buttons_and_folder_chevrons_have_tooltips_and_the_app_has_its_name()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var (missing, chevrons, name) = await HeadlessApp.Run(() =>
        {
            var w = Window(card.Dir);
            w.Show();
            w.OpenCard(card.Dir);
            Dispatcher.UIThread.RunJobs();
            var controls = new Dictionary<string, Control>
            {
                ["theme"] = w.ThemeToggleButton, ["refresh"] = w.RefreshDrives, ["list"] = w.ListModeButton,
                ["covers"] = w.CoversModeButton, ["prepare"] = w.PrepareCardButton, ["save"] = w.SaveChangesButton,
            };
            var miss = controls.Where(kv => ToolTip.GetTip(kv.Value) is not string { Length: > 0 }).Select(kv => kv.Key).ToList();
            if (!ToolTip.GetShowOnDisabled(w.SaveChangesButton) || !ToolTip.GetShowOnDisabled(w.PrepareCardButton)) miss.Add("show on disabled");
            var chev = AllDescendants<Border>(w.GameList).Where(b => b.Width == 24 && b.Height == 24 && b.Cursor != null).ToList();
            miss.AddRange(chev.Where(b => ToolTip.GetTip(b) is not string { Length: > 0 }).Select(_ => "chevron"));
            w.Close();
            return Task.FromResult((miss, chev.Count, Application.Current!.Name));
        });
        Assert.Empty(missing);
        Assert.True(chevrons > 0);
        Assert.Equal("Aether Card Manager", name);
    }

    /// <summary>View > Theme applies live, with no restart, to every open window: the main window's panes, its list
    /// rows, a dialog already open (the feedback form, the About box), then back again.</summary>
    [Fact]
    public async Task Switching_the_theme_repaints_every_open_window_live()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        UiSettings.PathOverrideForTests = Path.Combine(Path.GetTempPath(), $"vmcm-uisettings-{Guid.NewGuid():N}.json");
        try
        {
            var r = await HeadlessApp.Run(() =>
            {
                try
                {
                    var w = Window(card.Dir);
                    w.Show();
                    w.OpenCard(card.Dir);
                    w.GameList.SelectedItem = w.Load!.Rows.First(x => x.FileName.StartsWith("Alpha"));
                    var feedback = new FeedbackWindow();
                    feedback.Show();
                    var about = new AboutWindow();
                    about.Show();
                    Dispatcher.UIThread.RunJobs();
                    var pane = AllDescendants<Border>(w).First(b => b.Classes.Contains("pane"));
                    TextBlock MutedRow() => AllDescendants<TextBlock>(w.GameList).First(t => t.Text == "Not in database");
                    bool darkFirst = ReferenceEquals(pane.Background, AppTheme.Dark.Panel) && ReferenceEquals(MutedRow().Foreground, AppTheme.Dark.Muted);

                    w.SetTheme(ThemeKind.Light);
                    Dispatcher.UIThread.RunJobs();
                    bool light = ReferenceEquals(pane.Background, AppTheme.Light.Panel) && ReferenceEquals(MutedRow().Foreground, AppTheme.Light.Muted)
                                && ReferenceEquals(w.Background, AppTheme.Light.WindowBackground)
                                && ReferenceEquals(feedback.Background, AppTheme.Light.WindowBackground)
                                && ReferenceEquals(about.Background, AppTheme.Light.WindowBackground)
                                && ReferenceEquals(feedback.OpenButton.Foreground, AppTheme.Light.OnAccent);
                    string? saved = UiSettings.Load().Theme;

                    w.SetTheme(ThemeKind.Dark);
                    Dispatcher.UIThread.RunJobs();
                    bool darkAgain = ReferenceEquals(pane.Background, AppTheme.Dark.Panel) && ReferenceEquals(feedback.Background, AppTheme.Dark.WindowBackground);
                    about.Close(); feedback.Close(); w.Close();
                    return Task.FromResult((darkFirst, light, saved, darkAgain, UiSettings.Load().Theme));
                }
                finally { AppTheme.Use(ThemeKind.Dark); }
            });
            Assert.True(r.darkFirst);
            Assert.True(r.light);
            Assert.Equal("Light", r.saved);
            Assert.True(r.darkAgain);
            Assert.Equal("Dark", r.Item5);
        }
        finally { UiSettings.PathOverrideForTests = null; }
    }
}
