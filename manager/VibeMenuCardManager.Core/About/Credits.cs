// Aether Card Manager, GPL-3.0-or-later.
// The credits the About dialog shows: CREDITS.md, embedded at build time (VibeMenuCardManager.Core.csproj) and parsed
// here, so the dialog can never drift from the file. CREDITS.md fix round: one table ("Credited for source code or
// inspiration"), the creator line and a closing pointer to LICENSES/ and the release's NOTICE file -- bundled
// dependency names (Avalonia, SkiaSharp, Noto Sans JP, cover sources, Kai's cheat data...) moved there
// (About/NoticeFile.cs), since they are not in CREDITS.md any more.
using System.Reflection;

namespace VibeMenuCardManager.Core.About;

/// <param name="Section">the CREDITS.md heading the entry is under.</param>
/// <param name="Columns">the row's cells after Who (the project or work with its link, then the license).</param>
public sealed record Credit(string Section, string Who, IReadOnlyList<string> Columns);

public sealed record CreditSection(string Title, IReadOnlyList<string> Header, IReadOnlyList<Credit> Rows, string Prose);

public static class Credits
{
    public const string Product = "Aether Card Manager";
    public const string Version = "0.1";
    public const string License = "GPL-3.0-or-later (derived from GD MENU Card Manager, GPL-3.0)";

    /// <summary>CREDITS.md's one table.</summary>
    public static readonly string[] Sections = ["Credited for source code or inspiration"];

    /// <summary>The credits every About screen must carry, by the name CREDITS.md gives them.</summary>
    public static readonly string[] Required =
    [
        "mrneo240, megavolt85, sbstnc", "Derek Pascarella (ateam)", "sonik-br", "abrasive (James Laird-Wah)", "retrohead",
        "cafe-alpha", "Theo Berkau (CyberWarriorX) and the Pseudo Saturn team", "tpunix", "Slinga", "Daniel Hepper",
    ];

    public static string Markdown()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("CREDITS.md")
            ?? throw new InvalidOperationException("CREDITS.md is not embedded in VibeMenuCardManager.Core");
        using var r = new StreamReader(s);
        return r.ReadToEnd();
    }

    /// <summary>Every "## " section of a CREDITS.md text: its table (header, rows) and its other lines.</summary>
    public static List<CreditSection> Parse(string md)
    {
        var o = new List<CreditSection>();
        string? title = null;
        List<string>? header = null;
        var rows = new List<Credit>();
        var prose = new System.Text.StringBuilder();
        void Flush()
        {
            if (title != null) o.Add(new CreditSection(title, header ?? [], rows, prose.ToString().Trim()));
            header = null; rows = []; prose.Clear();
        }
        foreach (var raw in md.Replace("\r\n", "\n").Split('\n'))
        {
            string line = raw.TrimEnd();
            if (line.StartsWith("## ", StringComparison.Ordinal)) { Flush(); title = line[3..].Trim(); continue; }
            if (title == null) continue;
            if (line.StartsWith('|'))
            {
                var cells = line.Trim('|').Split('|').Select(c => c.Trim()).ToList();
                if (cells.All(c => c.Length > 0 && c.All(ch => ch is '-' or ':'))) continue;          // |---|---|
                if (header == null) { header = cells; continue; }
                rows.Add(new Credit(title, cells[0], cells.Skip(1).ToList()));
                continue;
            }
            prose.AppendLine(line);
        }
        Flush();
        return o;
    }

    public static List<CreditSection> All() => Parse(Markdown());

    /// <summary>The checks the About dialog's credits must pass; the failures (empty = complete): the one table,
    /// complete rows, and every required credit. Bundled dependencies are NOTICE.txt's job (About/NoticeFile.cs),
    /// not CREDITS.md's.</summary>
    public static List<string> Check(List<CreditSection> sections)
    {
        var bad = new List<string>();
        foreach (var t in Sections)
            if (!sections.Any(x => x.Title == t)) bad.Add($"missing section: {t}");
        var rows = sections.SelectMany(x => x.Rows).ToList();
        foreach (var r in rows)
            if (r.Who == "" || r.Columns.Count < 2 || r.Columns.Take(2).Any(c => c == ""))
                bad.Add($"incomplete row in {r.Section}: {r.Who}");
        foreach (var who in Required)
            if (!rows.Any(r => r.Who == who)) bad.Add($"missing credit: {who}");
        return bad;
    }
}
