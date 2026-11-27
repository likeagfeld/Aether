// Aether Card Manager, GPL-3.0-or-later.
// Batch rename: several selected games renamed at once from a chosen source --
// the GAMES.DB title, the disc's own System ID title, the folder name or the file name -- with an optional Title
// case transform. The result stages exactly like a single inline rename: GameRow.TitleOverride (LIBRARY.IDX, SPEC
// §5.6 step 1 of the title lookup order), so it applies, previews and rolls back the same way.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public enum RenameSource { DbTitle, DiscHeader, FolderName, FileName }

public static class BatchRename
{
    /// <summary>The raw text a source gives for a row, before any transform; "" when that source has nothing (a
    /// game with no GAMES.DB match, an unreadable header, or a root-level file for FolderName).</summary>
    public static string SourceText(GameRow row, RenameSource source, IReadOnlyDictionary<string, GameRecord> db) => source switch
    {
        RenameSource.DbTitle => row.Key != "" && db.TryGetValue(row.Key, out var r) ? r.TitleEn : "",
        RenameSource.DiscHeader => row.Sid is { Title: var t } ? t.Trim() : "",
        RenameSource.FolderName => row.Folder == "" ? "" : row.Folder[(row.Folder.LastIndexOf('/') + 1)..],
        RenameSource.FileName => System.IO.Path.GetFileNameWithoutExtension(row.FileName),
        _ => "",
    };

    /// <summary>Title Case: each word's first letter upper, the rest lower (invariant culture, ASCII-safe for the
    /// product names this deals with).</summary>
    public static string TitleCase(string s) =>
        s == "" ? s : System.Globalization.CultureInfo.InvariantCulture.TextInfo.ToTitleCase(s.ToLowerInvariant());

    public sealed record Preview(GameRow Row, string OldTitle, string NewTitle)
    {
        /// <summary>Nothing to stage: the source gave no text, or it equals what is already staged.</summary>
        public bool NoOp => NewTitle == "" || NewTitle == Row.TitleOverride;
    }

    /// <summary>What Apply() would stage, computed but not staged, for the confirmation dialog.</summary>
    public static List<Preview> Compute(IEnumerable<GameRow> rows, RenameSource source, bool titleCase, IReadOnlyDictionary<string, GameRecord> db) =>
        rows.Select(r =>
        {
            string old = r.TitleOverride != "" ? r.TitleOverride : r.DisplayTitle(db);
            string nw = SourceText(r, source, db);
            if (titleCase) nw = TitleCase(nw);
            return new Preview(r, old, nw);
        }).ToList();

    /// <summary>Stages every non-empty, changed preview's TitleOverride; returns how many rows actually changed.</summary>
    public static int Apply(IEnumerable<Preview> previews)
    {
        int n = 0;
        foreach (var p in previews)
        {
            if (p.NoOp) continue;
            p.Row.TitleOverride = p.NewTitle;
            n++;
        }
        return n;
    }
}
