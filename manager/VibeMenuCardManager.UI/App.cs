// Aether Card Manager, GPL-3.0-or-later. Derived from GD MENU Card Manager (sonik-br, Derek Pascarella / ateam;
// design spec §8). The Avalonia shell: a card picker, the save import, and the About dialog with the full
// credits. Built in code (no XAML), so the whole UI reads top to bottom here; every operation is Core's.
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace VibeMenuCardManager.UI;

public sealed class App : Application
{
    public override void Initialize()
    {
        // macOS names the app menu and its "About ..." item from this (else "Avalonia Application")
        Name = VibeMenuCardManager.Core.About.Credits.Product;
        // Fluent's own accent (check boxes, focus rings, text selection) is Aether's blue, never the Windows accent.
        Styles.Add(new FluentTheme
        {
            Palettes =
            {
                [Avalonia.Styling.ThemeVariant.Light] = new ColorPaletteResources { Accent = Avalonia.Media.Color.FromRgb(0x0a, 0x5c, 0xb5) },
                [Avalonia.Styling.ThemeVariant.Dark] = new ColorPaletteResources { Accent = Avalonia.Media.Color.FromRgb(0x3f, 0xa9, 0xff) },
            },
        });
        // Dark until the saved choice is read (OnFrameworkInitializationCompleted, the real app only): never the OS
        // setting, and never the user's settings file in a test.
        AppTheme.Use(ThemeKind.Dark);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime d)
        {
            // Dev convenience only, never in any user-facing doc or the installer: "--card <path>" opens that folder
            // as if it were the only removable drive, so a manual check or a screenshot never has to auto-select
            // whatever real card happens to be plugged in (MainWindow.RefreshDriveList, called from its
            // constructor, otherwise always picks item 0). The path must not contain a space (OnDrivePicked splits
            // the picker's own "E: (label)" text on the first space; a bare path has none to split on).
            string? card = null;
            var args = d.Args ?? [];
            string? theme = null;
            for (int i = 0; i + 1 < args.Length; i++)
            {
                if (args[i] == "--card") card = args[i + 1];
                if (args[i] == "--theme") theme = args[i + 1];   // dev/screenshots: light or dark, applied, never saved
            }
            AppTheme.Use(AppTheme.Parse(theme ?? UiSettings.Load().Theme));
            // autoBackupOnOpen: true for the real app (off by default) -- a constructor parameter, not a property
            // set afterward, since the auto-opened real drive's OpenCard can run synchronously during construction.
            // false for --card (advisor round): a scratch/screenshot folder is not a real card, and backing it up
            // was writing real snapshots into the user's own %AppData%\VibeMenuCardManager\Backups every dev run.
            d.MainWindow = card != null
                ? new MainWindow(removableDriveListForTests: () => [card], autoBackupOnOpen: false)
                : new MainWindow(autoBackupOnOpen: true, checkUpdatesOnStart: true);
        }
        base.OnFrameworkInitializationCompleted();
    }
}

public static class Program
{
    [STAThread]
    public static int Main(string[] args) =>
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
}
