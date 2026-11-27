// Aether Card Manager, GPL-3.0-or-later.
// The window's own layout preferences ("remember the user's splitter positions across launches").
// Never card data -- this never touches an SD card, and holds nothing from one.
using System.Text.Json;

namespace VibeMenuCardManager.UI;

public sealed class UiSettings
{
    public double? TreePaneWidth { get; set; }
    public double? DetailsPaneWidth { get; set; }
    /// <summary>Look for a newer Card Manager on GitHub at start (Help > Check for updates on start).</summary>
    public bool CheckForUpdates { get; set; } = true;
    /// <summary>View > Theme: "Dark" (the default, also for a missing or unknown value) or "Light".</summary>
    public string? Theme { get; set; }

    /// <summary>Tests: a path under the scratch/temp area, never the real per-user settings file.</summary>
    public static string? PathOverrideForTests;

    static string DefaultPath() =>
        Path.Combine(VibeMenuCardManager.Core.Backup.AppDataDir.Get(), "ui-settings.json");

    static string Path_() => PathOverrideForTests ?? DefaultPath();

    public static UiSettings Load()
    {
        try
        {
            string p = Path_();
            if (!File.Exists(p)) return new UiSettings();
            return JsonSerializer.Deserialize<UiSettings>(File.ReadAllText(p)) ?? new UiSettings();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException)
        {
            return new UiSettings();
        }
    }

    public void Save()
    {
        try
        {
            string p = Path_();
            string? dir = System.IO.Path.GetDirectoryName(p);
            if (dir != null && dir != "") Directory.CreateDirectory(dir);
            File.WriteAllText(p, JsonSerializer.Serialize(this));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }
}
