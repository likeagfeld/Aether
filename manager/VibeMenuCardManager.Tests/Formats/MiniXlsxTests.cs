// VibeMenu Card Manager, GPL-3.0-or-later.
using System.IO.Compression;
using System.Xml.Linq;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class MiniXlsxTests
{
    [Fact]
    public void Writes_a_valid_zip_with_every_required_OOXML_part_as_well_formed_XML()
    {
        byte[] data = MiniXlsx.Write(["A", "B"], [["1", "2"], ["3", "4"]]);
        using var ms = new MemoryStream(data);
        using var zip = new ZipArchive(ms, ZipArchiveMode.Read);
        string[] expected = ["[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels", "xl/worksheets/sheet1.xml"];
        foreach (var name in expected)
        {
            var entry = zip.GetEntry(name);
            Assert.NotNull(entry);
            using var s = entry!.Open();
            XDocument.Load(s);    // throws on malformed XML
        }
    }

    [Fact]
    public void The_sheet_holds_the_header_and_every_row_as_inline_strings_in_order()
    {
        byte[] data = MiniXlsx.Write(["Title", "Code"], [["Alpha", "T-1"], ["Beta", "T-2"]]);
        using var ms = new MemoryStream(data);
        using var zip = new ZipArchive(ms, ZipArchiveMode.Read);
        using var s = zip.GetEntry("xl/worksheets/sheet1.xml")!.Open();
        var doc = XDocument.Load(s);
        XNamespace ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        var texts = doc.Descendants(ns + "t").Select(t => t.Value).ToList();
        Assert.Equal(["Title", "Code", "Alpha", "T-1", "Beta", "T-2"], texts);
    }

    [Fact]
    public void Escapes_XML_special_characters_in_a_cell()
    {
        byte[] data = MiniXlsx.Write(["X"], [["A & B < C > D \"E\" 'F'"]]);
        using var ms = new MemoryStream(data);
        using var zip = new ZipArchive(ms, ZipArchiveMode.Read);
        using var s = zip.GetEntry("xl/worksheets/sheet1.xml")!.Open();
        var doc = XDocument.Load(s);      // throws if the escaping broke the XML
        XNamespace ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        Assert.Contains("A & B < C > D \"E\" 'F'", doc.Descendants(ns + "t").Select(t => t.Value));
    }

    [Fact]
    public void Column_references_go_past_Z_into_AA()
    {
        var header = Enumerable.Range(0, 28).Select(i => "C" + i).ToList();
        byte[] data = MiniXlsx.Write(header, []);
        using var ms = new MemoryStream(data);
        using var zip = new ZipArchive(ms, ZipArchiveMode.Read);
        using var s = zip.GetEntry("xl/worksheets/sheet1.xml")!.Open();
        var doc = XDocument.Load(s);
        XNamespace ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        var refs = doc.Descendants(ns + "c").Select(c => c.Attribute("r")!.Value).ToList();
        Assert.Equal("Z1", refs[25]);
        Assert.Equal("AA1", refs[26]);
        Assert.Equal("AB1", refs[27]);
    }
}
