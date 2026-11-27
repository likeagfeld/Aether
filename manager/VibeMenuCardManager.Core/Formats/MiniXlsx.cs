// Aether Card Manager, GPL-3.0-or-later.
// A minimal, dependency-free .xlsx writer (OOXML: a zip of a few small XML parts), for DISCLIST.XLSX (coordinator
// feature round, item 3 -- the card report). One sheet, inline strings (no sharedStrings.xml part needed), no
// styling. This is not a general spreadsheet writer -- just enough for a plain header-plus-rows data export, so
// no third-party library (and its licence/credit) is needed for this.
using System.IO.Compression;
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public static class MiniXlsx
{
    static string Esc(string s) => s.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("\"", "&quot;").Replace("'", "&apos;");

    /// <summary>0-based column index -> "A", "B", ... "Z", "AA", ...</summary>
    static string ColRef(int col)
    {
        var sb = new StringBuilder();
        int n = col + 1;
        while (n > 0) { int rem = (n - 1) % 26; sb.Insert(0, (char)('A' + rem)); n = (n - 1) / 26; }
        return sb.ToString();
    }

    public static byte[] Write(IReadOnlyList<string> header, IEnumerable<IReadOnlyList<string>> rows)
    {
        var sheet = new StringBuilder();
        sheet.Append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n");
        sheet.Append("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>\r\n");
        void Row(int rowIndex, IReadOnlyList<string> cells)
        {
            sheet.Append($"<row r=\"{rowIndex}\">");
            for (int c = 0; c < cells.Count; c++)
            {
                string cell = cells[c] ?? "";
                sheet.Append($"<c r=\"{ColRef(c)}{rowIndex}\" t=\"inlineStr\"><is><t xml:space=\"preserve\">{Esc(cell)}</t></is></c>");
            }
            sheet.Append("</row>\r\n");
        }
        Row(1, header);
        int r = 2;
        foreach (var row in rows) Row(r++, row);
        sheet.Append("</sheetData></worksheet>");

        using var ms = new MemoryStream();
        using (var zip = new ZipArchive(ms, ZipArchiveMode.Create, leaveOpen: true))
        {
            void Put(string name, string content)
            {
                var entry = zip.CreateEntry(name, CompressionLevel.Optimal);
                using var w = new StreamWriter(entry.Open(), new UTF8Encoding(false));
                w.Write(content);
            }
            Put("[Content_Types].xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" +
                "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">" +
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>" +
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>" +
                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>" +
                "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>" +
                "</Types>");
            Put("_rels/.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" +
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" +
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>" +
                "</Relationships>");
            Put("xl/workbook.xml",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" +
                "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">" +
                "<sheets><sheet name=\"DISCLIST\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
            Put("xl/_rels/workbook.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>" +
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">" +
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>" +
                "</Relationships>");
            Put("xl/worksheets/sheet1.xml", sheet.ToString());
        }
        return ms.ToArray();
    }
}
