// VibeMenu Card Manager, GPL-3.0-or-later.
// The Avalonia shell on Avalonia's headless platform (no display): the main window imports saves through Core only
// after CardWriteGuard's confirmation, refuses a system drive without asking, writes nothing when the user declines,
// shows progress and cancels between files; the About dialog shows every credit row of CREDITS.md.
using Avalonia;
using Avalonia.Headless;
using VibeMenuCardManager.Core.About;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public static class HeadlessApp
{
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UseHeadless(new AvaloniaHeadlessPlatformOptions());

    public static readonly Lazy<HeadlessUnitTestSession> Session = new(() => HeadlessUnitTestSession.StartNew(typeof(HeadlessApp)));

    public static Task<T> Run<T>(Func<Task<T>> f) => Session.Value.Dispatch(f, CancellationToken.None);
}

public class ShellSmokeTests
{
    /// <summary>A window for a temp card: the system volumes as given (none by default), and the fixed-disk override
    /// ticked for the temp card's volume (a temp folder is on a fixed disk), as a user would after the first refusal.</summary>
    static MainWindow Window(string dir, bool answer, List<string>? asked = null, IReadOnlyList<string>? systemVolumes = null,
                             bool overrideFixed = true)
    {
        var w = new MainWindow(removableDriveListForTests: () => [])
        {
            SystemVolumes = systemVolumes ?? [],
            Ask = (_, title, text) => { asked?.Add(text); return Task.FromResult(answer); },
        };
        w.CardPath.Text = dir;
        if (overrideFixed) w.AllowFixedVolumeForTests(VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(dir).Id!);
        return w;
    }

    // "Import from Kai/Rings…" (RunImport) is removed for 0.1 (returns post-0.1 with proper UI) -- its own
    // CardWriteGuard/cancel/golden-import tests went with it; Core.Saves.SaveImporter's own tests are untouched.

    [Fact]
    public async Task Build_card_builds_and_installs_after_the_confirmation_and_a_decline_writes_nothing()
    {
        using var card = new TempCard();
        card.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(Card.BuildCheck.Case("card.json"))).RootElement);
        string root = Path.Combine(card.Dir, "card");
        string release = Card.ReleaseFixture.Make(Path.Combine(card.Dir, "release"));
        var before = card.Manifest();
        var asked = new List<string>();
        var (declined, unchanged, built) = await HeadlessApp.Run(async () =>
        {
            var no = Window(root, false);
            no.ReleasePath.Text = release;
            await no.RunBuild();
            bool same = before.SequenceEqual(card.Manifest());
            var w = Window(root, true, asked);
            w.ReleasePath.Text = release;
            w.CoversZip.Text = Card.BuildCheck.Case("covers.zip");
            w.ArtCache.Text = Card.BuildCheck.Case("art_cache");
            await w.RunBuild();
            Assert.True(w.BuildCard.IsEnabled);
            return (no.Status.Text ?? "", same, w.Status.Text ?? "");
        });
        Assert.EndsWith("cancelled, nothing was written", declined);
        Assert.True(unchanged);
        Assert.True(built.StartsWith("Built and installed: 17 images, 16 matched", StringComparison.Ordinal), built);
        var text = Assert.Single(asked);
        Assert.Contains("/menu.bin is never touched", text);
        Assert.True(File.Exists(Path.Combine(root, "autoboot.iso")) && File.Exists(Path.Combine(root, "aether", "ART.PAK")));
    }

    [Fact]
    public async Task Build_card_without_the_font_says_the_JP_titles_were_not_rendered()
    {
        using var card = new TempCard();
        card.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(Card.BuildCheck.Case("card.json"))).RootElement);
        string root = Path.Combine(card.Dir, "card");
        string release = Card.ReleaseFixture.Make(Path.Combine(card.Dir, "release"));
        string status = await HeadlessApp.Run(async () =>
        {
            var w = Window(root, true);
            w.ReleasePath.Text = release;
            w.FontPath = Path.Combine(card.Dir, "none.otf");
            await w.RunBuild();
            return w.Status.Text ?? "";
        });
        Assert.StartsWith("Built and installed:", status);
        Assert.Contains("WARNING: JP titles were NOT rendered: font missing at", status);
    }

    [Fact]
    public async Task Build_card_refuses_a_system_drive_and_an_incomplete_release_without_asking()
    {
        using var card = new TempCard();
        card.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(Card.BuildCheck.Case("card.json"))).RootElement);
        string root = Path.Combine(card.Dir, "card");
        string release = Card.ReleaseFixture.Make(Path.Combine(card.Dir, "release"));
        File.Delete(Path.Combine(release, "LAUNCHER.BIN"));
        var before = card.Manifest();
        var asked = new List<string>();
        var (sys, partial) = await HeadlessApp.Run(async () =>
        {
            var a = Window(root, true, asked, [VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(root).Id!]);
            a.ReleasePath.Text = release;
            await a.RunBuild();
            var b = Window(root, true, asked);
            b.ReleasePath.Text = release;
            await b.RunBuild();
            return (a.Status.Text ?? "", b.Status.Text ?? "");
        });
        Assert.Contains("is on the system drive", sys);
        Assert.Contains("LAUNCHER.BIN", partial);
        Assert.Empty(asked);
        Assert.Equal(before, card.Manifest());
    }

    // "Tidy saves" (RunTidy) is removed for 0.1 (returns post-0.1 with proper UI) -- its own CardWriteGuard/cancel/
    // golden-tidy tests went with it; Core.Saves.SaveTidy's own tests are untouched.

    /// <summary>Final review I-5: a close during Build card waits for the install transaction to finish (the card is a
    /// whole VibeMenu card), then the window closes by itself.</summary>
    [Fact]
    public async Task Closing_during_a_card_write_waits_for_it_to_stop_safely()
    {
        using var card = new TempCard();
        card.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(Card.BuildCheck.Case("card.json"))).RootElement);
        string root = Path.Combine(card.Dir, "card");
        string release = Card.ReleaseFixture.Make(Path.Combine(card.Dir, "release"));
        var (built, closed, busyAtClose) = await HeadlessApp.Run(async () =>
        {
            var w = Window(root, true);
            w.ReleasePath.Text = release;
            bool isClosed = false, busy = false;
            string atClose = "";
            w.Closed += (_, _) => { isClosed = true; atClose = w.Status.Text ?? ""; };
            w.Show();
            w.WorkStarted = () => Avalonia.Threading.Dispatcher.UIThread.Post(() => { busy = w.IsBusy; w.Close(); });
            await w.RunBuild();
            await Task.Delay(50);
            Avalonia.Threading.Dispatcher.UIThread.RunJobs();
            return (atClose, isClosed, busy);
        });
        Assert.True(busyAtClose);                                  // the close came while the build ran
        Assert.StartsWith("Built and installed", built);           // the window went only after the build had finished
        Assert.True(closed);
        Assert.Equal(File.ReadAllBytes(Path.Combine(card.Dir, "release", "autoboot.iso")), File.ReadAllBytes(Path.Combine(root, "autoboot.iso")));
        Assert.True(File.Exists(Path.Combine(root, "aether", "ART.PAK")));
    }

    [Fact]
    public async Task The_about_dialog_shows_every_credit_row()
    {
        int want = Credits.All().Sum(s => s.Rows.Count);
        int shown = await HeadlessApp.Run(() =>
        {
            var a = new AboutWindow();
            a.Show();
            a.Close();
            return Task.FromResult(a.RowsShown);
        });
        Assert.Equal(want, shown);
    }

    [Fact]
    public async Task The_about_dialog_shows_the_authorship_line_and_a_clickable_repo_link()
    {
        string? clicked = null;
        var prevOpen = AboutWindow.OpenUrl;
        AboutWindow.OpenUrl = url => clicked = url;
        try
        {
            var (createdBy, linkText) = await HeadlessApp.Run(() =>
            {
                var a = new AboutWindow();
                a.Show();
                a.ClickRepoLink();
                a.Close();
                return Task.FromResult((a.CreatedByLine.Text, a.RepoLink.Text));
            });
            Assert.Equal(AboutWindow.CreatedByText, createdBy);
            Assert.Equal(AboutWindow.RepoUrl, linkText);
            Assert.Equal(AboutWindow.RepoUrl, clicked);
        }
        finally { AboutWindow.OpenUrl = prevOpen; }
    }
}
