// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class DiscListReportTests
{
    static CardLoad Load(params GameRow[] rows)
    {
        var load = new CardLoad { Root = "X", Db = new GamesDbData([], [], new Dictionary<string, string>(), 0) };
        load.Rows.AddRange(rows);
        return load;
    }

    static GameRow Row(string path, string title, bool hasArt, string loader = "auto") => new()
    {
        Path = path, OriginalPath = path, HasArt = hasArt, Loader = loader, TitleOverride = title,
    };

    [Fact]
    public void BuildRows_orders_by_folder_then_title_and_skips_removed_rows()
    {
        var removed = Row("Games/Z/GAME.ISO", "Zed", true);
        removed.Removed = true;
        var load = Load(Row("Games/B/GAME.ISO", "Beta", true), Row("Games/A/GAME.ISO", "Alpha", false), removed);
        var rows = DiscListReport.BuildRows(load);
        Assert.Equal(["Alpha", "Beta"], rows.Select(r => r.Title));
        Assert.False(rows[0].Cover);
        Assert.True(rows[1].Cover);
    }

    [Fact]
    public void WriteTxt_is_tab_separated_with_a_header()
    {
        var rows = new List<DiscListRow> { new("Alpha", "T-1", "U", "1/1", "Games/A", true, "Auto") };
        string txt = DiscListReport.WriteTxt(rows);
        Assert.StartsWith("Title\tCode\tRegion\tDisc\tFolder\tCover\tLoader\r\n", txt);
        Assert.Contains("Alpha\tT-1\tU\t1/1\tGames/A\tyes\tAuto\r\n", txt);
    }

    [Fact]
    public void WriteXlsx_carries_the_same_rows_as_WriteTxt()
    {
        var rows = new List<DiscListRow> { new("Beta", "T-2", "JU", "2/2", "(root)", false, "JHL") };
        byte[] xlsx = DiscListReport.WriteXlsx(rows);
        using var ms = new MemoryStream(xlsx);
        using var zip = new System.IO.Compression.ZipArchive(ms, System.IO.Compression.ZipArchiveMode.Read);
        using var s = zip.GetEntry("xl/worksheets/sheet1.xml")!.Open();
        var doc = System.Xml.Linq.XDocument.Load(s);
        System.Xml.Linq.XNamespace ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        var texts = doc.Descendants(ns + "t").Select(t => t.Value).ToList();
        Assert.Equal(["Title", "Code", "Region", "Disc", "Folder", "Cover", "Loader", "Beta", "T-2", "JU", "2/2", "(root)", "no", "JHL"], texts);
    }
}
