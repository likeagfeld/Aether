// Aether Card Manager, GPL-3.0-or-later.
// The per-user app-data folder (%AppData%\AetherCardManager on Windows): PC backups, UI settings. Before the Aether
// rename (2026-11-25) it was VibeMenuCardManager; the first start after it moves that folder across (one rename on the
// same volume), so earlier snapshots and settings are kept. If the move cannot be made the old folder is used as is.
namespace VibeMenuCardManager.Core.Backup;

public static class AppDataDir
{
    public const string Name = "AetherCardManager", OldName = "VibeMenuCardManager";

    static readonly Lazy<string> Once = new(() => Resolve(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)));

    /// <summary>Resolved once a session, so the folder never changes under a running app.</summary>
    public static string Get() => Once.Value;

    /// <summary>base/AetherCardManager, after moving base/VibeMenuCardManager there when only the old one exists.</summary>
    public static string Resolve(string @base)
    {
        string now = Path.Combine(@base, Name), old = Path.Combine(@base, OldName);
        if (!Directory.Exists(now) && Directory.Exists(old))
        {
            try { Directory.Move(old, now); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return old; }
        }
        return now;
    }
}
