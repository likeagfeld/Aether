// Aether Card Manager, GPL-3.0-or-later.
// The card report: DISCLIST.TXT and DISCLIST.XLSX, title/code/region/disc/
// folder/cover/loader for every game on the card, written to a folder the user picks (File > Export report). A
// read-only report: never written onto the card itself as part of Save changes.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public sealed record DiscListRow(string Title, string Code, string Region, string Disc, string Folder, bool Cover, string Loader);

public static class DiscListReport
{
    public static readonly string[] Header = ["Title", "Code", "Region", "Disc", "Folder", "Cover", "Loader"];

    /// <summary>Every live (not staged-for-removal) game, folder then title order -- the same grouping the tree
    /// shows them in.</summary>
    public static List<DiscListRow> BuildRows(CardLoad load)
    {
        var db = load.Db.Records.ToDictionary(r => r.Key);
        return load.Rows.Where(r => !r.Removed)
            .OrderBy(r => r.Folder, StringComparer.Ordinal).ThenBy(r => r.DisplayTitle(db, load.UserEdits), StringComparer.OrdinalIgnoreCase)
            .Select(r => new DiscListRow(r.DisplayTitle(db, load.UserEdits), r.Code, r.RegionText, r.DiscText,
                                         r.Folder == "" ? "(root)" : r.Folder, r.HasArt || r.PendingCover != null,
                                         r.Loader == "auto" ? "Auto" : r.Loader.ToUpperInvariant()))
            .ToList();
    }

    static IEnumerable<string> Cells(DiscListRow r) => [r.Title, r.Code, r.Region, r.Disc, r.Folder, r.Cover ? "yes" : "no", r.Loader];

    /// <summary>Tab-separated, one game per line (opens cleanly in a text editor or a spreadsheet's "import").</summary>
    public static string WriteTxt(IReadOnlyList<DiscListRow> rows)
    {
        var sb = new System.Text.StringBuilder();
        sb.Append(string.Join('\t', Header)).Append("\r\n");
        foreach (var r in rows) sb.Append(string.Join('\t', Cells(r))).Append("\r\n");
        return sb.ToString();
    }

    public static byte[] WriteXlsx(IReadOnlyList<DiscListRow> rows) =>
        MiniXlsx.Write(Header, rows.Select(r => Cells(r).ToList()));
}
