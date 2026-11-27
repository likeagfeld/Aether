// VibeMenu Card Manager, GPL-3.0-or-later.
// The About dialog's credits (CREDITS.md fix round: one table, "Credited for source code or inspiration") are the
// repository's CREDITS.md, embedded at build time: the embedded text equals the file on disk, every table row of it
// is parsed (counted independently of the parser), the table is there with complete rows, and every credit required
// is there. Bundled dependencies are NOTICE.txt's job now (see NoticeFileTests.cs), not CREDITS.md's.
using VibeMenuCardManager.Core.About;

namespace VibeMenuCardManager.Tests.About;

public class CreditsTests
{
    static string OnDisk() => File.ReadAllText(Repo.P("CREDITS.md"));

    [Fact]
    public void The_embedded_credits_are_the_repositorys_CREDITS_md() =>
        Assert.Equal(OnDisk().Replace("\r\n", "\n"), Credits.Markdown().Replace("\r\n", "\n"));

    [Fact]
    public void Every_table_row_of_CREDITS_md_is_parsed()
    {
        // independent count: table lines, minus each table's header and separator
        var lines = OnDisk().Replace("\r\n", "\n").Split('\n');
        int tableLines = lines.Count(l => l.StartsWith('|'));
        int separators = lines.Count(l => l.StartsWith("|---"));
        var sections = Credits.All();
        Assert.Equal(tableLines - 2 * separators, sections.Sum(s => s.Rows.Count));
        Assert.Equal(Credits.Sections, sections.Select(s => s.Title));
        Assert.All(sections, s => Assert.Equal(3, s.Header.Count));                  // who, project, license
        Assert.All(sections.SelectMany(s => s.Rows), r => Assert.Equal(2, r.Columns.Count));
    }

    [Fact]
    public void Every_required_credit_is_there() => Assert.Empty(Credits.Check(Credits.All()));

    [Fact]
    public void Mutation_a_removed_credit_is_caught()
    {
        var md = string.Join("\n", Credits.Markdown().Replace("\r\n", "\n").Split('\n').Where(l => !l.StartsWith("| sonik-br", StringComparison.Ordinal)));
        Assert.Contains("missing credit: sonik-br", Credits.Check(Credits.Parse(md)));
    }

    [Fact]
    public void Mutation_a_row_without_its_license_is_caught()
    {
        var md = Credits.Markdown().Replace(
            "| sonik-br | GDMENUCardManager (https://github.com/sonik-br/GDMENUCardManager) | GPL-3.0 |",
            "| sonik-br | GDMENUCardManager (https://github.com/sonik-br/GDMENUCardManager) | |");
        Assert.Contains("incomplete row in Credited for source code or inspiration: sonik-br", Credits.Check(Credits.Parse(md)));
    }
}
