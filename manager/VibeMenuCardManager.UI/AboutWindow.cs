// Aether Card Manager, GPL-3.0-or-later.
// The About dialog (design spec §8.2, §10): the product, its version and licence, and the full credits -- every section
// and row of the repository's CREDITS.md (Core.About.Credits, embedded at build time). The authorship line and repo
// link (user's choice) sit above all of that.
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using VibeMenuCardManager.Core.About;

namespace VibeMenuCardManager.UI;

public sealed class AboutWindow : Window
{
    public const string CreatedByText = "Created by Farkus and Claude AI";
    public const string RepoUrl = "https://github.com/likeagfeld/Aether/";

    /// <summary>The logo loaded and shown (the embedded Assets/about_lockup.png), for the smoke test.</summary>
    public bool LogoShown { get; }

    /// <summary>The credit rows shown, for the smoke test.</summary>
    public int RowsShown { get; }

    /// <summary>The authorship line and the repo link, for the smoke test (and in tests, to click it without
    /// actually opening a browser: see OpenUrl).</summary>
    public readonly TextBlock CreatedByLine;
    public readonly TextBlock RepoLink;
    /// <summary>"Build 2026-11-27 15:32 UTC · c33b131 · pre-release" under the version (hidden when unknown).</summary>
    public readonly TextBlock BuildLine;

    /// <summary>Tests click the link this way, rather than constructing a synthetic pointer event.</summary>
    public void ClickRepoLink() => OpenUrl(RepoUrl);

    /// <summary>Tests replace this so clicking the link does not actually launch a browser.</summary>
    public static Action<string> OpenUrl = DefaultOpenUrl;

    static void DefaultOpenUrl(string url)
    {
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true }); }
        catch (Exception e) when (e is System.ComponentModel.Win32Exception or InvalidOperationException) { }
    }

    public AboutWindow()
    {
        Title = $"About {Credits.Product}";
        Width = 760;
        Height = 600;
        AppTheme.ApplyToWindow(this);
        var list = new StackPanel { Margin = new Avalonia.Thickness(16), Spacing = 6 };
        CreatedByLine = new TextBlock { Text = CreatedByText, FontWeight = FontWeight.SemiBold };
        try
        {   // the Aether logo (mark, AETHER, saturn; Assets/about_lockup.png at 2x) on the dialog's dark panel
            using var s = Avalonia.Platform.AssetLoader.Open(new Uri("avares://AetherCardManager/Assets/about_lockup.png"));
            LogoShown = true;
            list.Children.Add(new Border
            {
                [!Border.BackgroundProperty] = AppTheme.Dyn("BrandPlate"), CornerRadius = new Avalonia.CornerRadius(8), Padding = new Avalonia.Thickness(12),
                HorizontalAlignment = HorizontalAlignment.Left,
                Child = new Image { Source = new Avalonia.Media.Imaging.Bitmap(s), Width = 240, Height = 196.5 },
            });
        }
        catch (Exception e) when (e is FileNotFoundException or IOException or NotSupportedException or ArgumentException) { }
        RepoLink = new TextBlock
        {
            Text = RepoUrl, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Link"), TextDecorations = Avalonia.Media.TextDecorations.Underline,
            Cursor = new Cursor(StandardCursorType.Hand),
        };
        RepoLink.PointerPressed += (_, _) => OpenUrl(RepoUrl);
        list.Children.Add(CreatedByLine);
        list.Children.Add(RepoLink);
        list.Children.Add(new TextBlock { Text = $"{Credits.Product} {Credits.Version}", FontSize = 20, FontWeight = FontWeight.SemiBold, Margin = new Avalonia.Thickness(0, 10, 0, 0) });
        // the build identity (Directory.Build.props): which build of this version this is, for updates and bug reports
        BuildLine = new TextBlock { Text = VibeMenuCardManager.Core.Update.BuildInfo.Running().Describe(), FontSize = 12, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted") };
        BuildLine.IsVisible = BuildLine.Text != "";
        list.Children.Add(BuildLine);
        list.Children.Add(new TextBlock { Text = Credits.License, TextWrapping = TextWrapping.Wrap });
        list.Children.Add(new TextBlock
        {
            Text = "Prepares Satiator SD cards for Aether. Built on other people's work -- thank you all:",
            TextWrapping = TextWrapping.Wrap,
        });
        foreach (var s in Credits.All())
        {
            list.Children.Add(new TextBlock { Text = s.Title, FontSize = 16, FontWeight = FontWeight.SemiBold, Margin = new Avalonia.Thickness(0, 10, 0, 0) });
            foreach (var r in s.Rows)
            {
                list.Children.Add(new TextBlock { Text = r.Who, FontWeight = FontWeight.SemiBold, TextWrapping = TextWrapping.Wrap });
                list.Children.Add(new TextBlock { Text = string.Join("  |  ", r.Columns.Where(c => c != "")), TextWrapping = TextWrapping.Wrap, Opacity = 0.85 });
                RowsShown++;
            }
            if (s.Prose != "") list.Children.Add(new TextBlock { Text = s.Prose, TextWrapping = TextWrapping.Wrap, Opacity = 0.85 });
        }
        var close = new Button { Content = "Close", HorizontalAlignment = HorizontalAlignment.Right };
        close.Click += (_, _) => Close();
        Content = new DockPanel
        {
            Children = { new Border { Child = close, Padding = new Avalonia.Thickness(12) }.Also(b => DockPanel.SetDock(b, Dock.Bottom)),
                         new ScrollViewer { Content = list } },
        };
    }
}

static class ControlExt
{
    public static T Also<T>(this T c, Action<T> f) { f(c); return c; }
}
