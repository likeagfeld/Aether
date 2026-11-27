// Aether Card Manager, GPL-3.0-or-later.
// Version-to-version card steps for an in-place upgrade (Prepare on a card that already has Aether): after the new
// menu, LAUNCHER.BIN and data are installed, each step whose range the upgrade crosses runs once, in order. 0.1 has
// none; a later version that changes a card file's layout adds its step here, never touching /menu.bin.
using VibeMenuCardManager.Core.Update;

namespace VibeMenuCardManager.Core.Card;

/// <param name="From">the step runs for a card upgraded from a version below this one ...</param>
/// <param name="To">... to this one or later.</param>
public sealed record CardMigration(string From, string To, string What, Action<string, List<string>> Apply);

public static class CardMigrations
{
    public static readonly IReadOnlyList<CardMigration> Steps = [];

    /// <summary>Runs the steps an upgrade from `installed` (null: a fresh card, nothing to migrate) to `target`
    /// crosses; returns what each did.</summary>
    public static List<string> Run(string card, string? installed, string target, IReadOnlyList<CardMigration>? steps = null)
    {
        var log = new List<string>();
        if (installed == null) return log;
        foreach (var s in (steps ?? Steps).OrderBy(s => s.To, Comparer<string>.Create(AppVersion.Compare)))
            if (AppVersion.Compare(installed, s.To) < 0 && AppVersion.Compare(target, s.To) >= 0)
            {
                log.Add($"{s.From} -> {s.To}: {s.What}");
                s.Apply(card, log);
            }
        return log;
    }
}
