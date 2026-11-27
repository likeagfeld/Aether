// Aether Card Manager, GPL-3.0-or-later.
// The Problems view: every issue the scanner or the match can raise, named, explained, and
// with a one-line fix where one can be staged in-app. Pure (no Avalonia), so it is tested headlessly and shared by
// the status bar's "N problems" link, the game list's problem column, and the Problems window.
using System.Linq;

namespace VibeMenuCardManager.Core.Card;

public enum ProblemKind { UnreadableHeader, NotInDatabase, NoCover, CueMissingFile, Duplicate, NameEncoding }

/// <summary>One finding. Row is null for a card-wide finding (none yet raises one; kept so the Problems window's
/// columns do not have to special-case a future one). Action: "Match" (MatchToGame), "ChooseCover" (ChooseCover),
/// or null when there is no in-app fix (the fix is a file-management step in Explorer, named in HowToFix).</summary>
public sealed record Problem(ProblemKind Kind, GameRow? Row, string Subject, string Explanation, string HowToFix, string? Action)
{
    public static string KindText(ProblemKind k) => k switch
    {
        ProblemKind.UnreadableHeader => "Unreadable disc header",
        ProblemKind.NotInDatabase => "Not in the database",
        ProblemKind.NoCover => "No cover",
        ProblemKind.CueMissingFile => ".cue references a missing file",
        ProblemKind.Duplicate => "Duplicate",
        ProblemKind.NameEncoding => "Name or encoding issue",
        _ => k.ToString(),
    };
}

public static class Problems
{
    /// <summary>Every problem on the card right now (never removed rows). Order: per-game problems in row order
    /// (a row with more than one problem appears more than once, once per kind), then duplicates, then name/encoding
    /// issues -- stable, so the Problems window's default (unsorted) order does not jump around between refreshes.</summary>
    public static List<Problem> Find(CardLoad load)
    {
        var o = new List<Problem>();
        var rows = load.Rows.Where(r => !r.Removed).ToList();
        foreach (var row in rows)
        {
            if (row.Sid == null)
                o.Add(new Problem(ProblemKind.UnreadableHeader, row, row.Path,
                    "The disc header could not be read, so Aether cannot identify this game from the disc alone.",
                    "Use “Match to game…” to pick its GAMES.DB entry by hand.", "Match"));
            else if (row.Key == "")
                o.Add(new Problem(ProblemKind.NotInDatabase, row, row.Path,
                    "This game's product code is not in GAMES.DB, so it has no title, region or cover from the database.",
                    "Use “Match to game…” to pick the closest GAMES.DB entry.", "Match"));
            if (!row.HasArt && row.PendingCover == null)
                o.Add(row.OwnFolder
                    ? new Problem(ProblemKind.NoCover, row, row.Path,
                        "No cover image is available for this game (not on the card, and no bundled cover-pack match).",
                        "Use “Change cover…”.", "ChooseCover")
                    // A loose game (shares its folder, or sits at the card root) has no BOX.TGA of its own to
                    // change -- ChooseCover refuses it (Final review I1), so this has no in-app fix (Action: null),
                    // never a Fix button that does nothing when clicked.
                    : new Problem(ProblemKind.NoCover, row, row.Path,
                        "No cover image is available for this game (not on the card, and no bundled cover-pack match).",
                        "This game shares its folder (or sits at the card root), so it has no cover of its own to change. " +
                        "Move it into its own folder in Explorer, then Refresh.", null));
            if (load.CueMissing.TryGetValue(row.Path, out var missing))
                o.Add(new Problem(ProblemKind.CueMissingFile, row, row.Path,
                    $"Its .cue names {string.Join(", ", missing)}, which is not in the same folder.",
                    "Add the missing file next to the .cue in Explorer, or re-rip the disc, then Refresh.", null));
            if (row.Path.Any(c => c > 126 || (c < 32 && c != '\0')))
                o.Add(new Problem(ProblemKind.NameEncoding, row, row.Path,
                    "This path has non-ASCII or control characters, which the console's menu may not render correctly.",
                    "Rename the file or folder in Explorer using plain ASCII letters, digits, spaces and punctuation, then Refresh.", null));
        }
        // Duplicate: two rows share a GAMES.DB key and are not two discs of the same multi-disc set.
        foreach (var g in rows.Where(r => r.Key != "").GroupBy(r => r.Key))
            foreach (var d in g.GroupBy(r => r.DiscTotal > 1 ? r.Disc : 0))
            {
                var list = d.ToList();
                if (list.Count <= 1) continue;
                foreach (var row in list)
                    o.Add(new Problem(ProblemKind.Duplicate, row, row.Path,
                        "Another copy of this same game is also on the card: " +
                        string.Join(", ", list.Where(x => x != row).Select(x => x.Path)) + ".",
                        "Remove or move the extra copy in Explorer, then Refresh.", null));
            }
        return o;
    }
}
