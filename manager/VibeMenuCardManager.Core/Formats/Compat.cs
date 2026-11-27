// Aether Card Manager, GPL-3.0-or-later.
// The compat rows of GAMES.DB bytes 0x2E-0x3F (contract/SPEC.md §5.3): loader hint and region
// mask from tools/data/compat/loader_compat.json (Kai's compatibility page and PSKai v6.545 autoset.c), the controller
// note and flags from tools/data/compat/controller_compat.json, the save-tool flag from
// tools/data/compat/save_tools.json. A port of tools/omsat/compat.py (load, load_controller, fill_compat).
using System.Text.Json;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Core.Formats;

public sealed class CompatRow
{
    public int Hint, Flags, Regions;
    public string Note = "";
    /// <summary>Each loader row's (area, hint, flags, regions): Fill resolves the product through the §6 matcher once
    /// per area, so each lands on the record a disc of that area matches (1fa596c). Empty: the aggregate applies to
    /// every record the product resolves to.</summary>
    public List<(string Area, int Hint, int Flags, int Regions)> Parts = [];
    /// <summary>2026-11-25: the loader rows behind it (Fill's per-disc rules).</summary>
    public List<LoaderRow> Src = [];
}

/// <summary>One loader_compat.json row as the per-disc rules see it (compat.py LoaderRow).</summary>
public sealed record LoaderRow(string Product, string Area, int Hint, uint? Crc, bool Override);

/// <summary>One card image for Fill's per-disc rules (compat.py Disc): its LIBRARY.IDX key and area bits, its IP area
/// text and product, Kai's IP CRC (SystemIdReader.KaiIpCrc), and whether DiscSets put it in a set of 2 or more discs.</summary>
public sealed record CompatDisc(string Path, string Key, int AreaFlags, string Area, string Product, uint Crc, bool InSet = false);

public static class Compat
{
    public const int NoteMax = 46, CfNoIgr = 0x20, CfSaveTool = 0x80;
    /// <summary>2026-11-25: loader_compat.json rows whose "source" starts with this are our own hardware findings
    /// (NiGHTS): they apply to every disc of their product, ahead of Kai's rows.</summary>
    public const string OverrideSource = "Aether hardware test";
    static readonly Dictionary<int, string> HintNames = new() { [1] = "JHL", [2] = "CWX", [3] = "bad" };
    static readonly Dictionary<string, int> Hints = new() { ["jhl"] = 1, ["cwx"] = 2, ["bad"] = 3 };
    static readonly HashSet<string> Confident = ["confirmed", "multiple reports"];

    static int RegionMask(string area) => area is "" or "*" ? 0 : GamesDb.RegionFlagsFrom(area);

    static string Str(JsonElement e, string k) => e.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString()! : "";

    public static Dictionary<string, CompatRow> LoadController(JsonElement rows, List<string>? report)
    {
        var o = new Dictionary<string, CompatRow>();
        foreach (var row in rows.EnumerateArray())
        {
            string title = row.TryGetProperty("title", out var t) ? t.GetString() ?? "?" : "?";
            string? confidence = row.TryGetProperty("confidence", out var c) && c.ValueKind == JsonValueKind.String ? c.GetString() : null;
            if (confidence == null || !Confident.Contains(confidence))
            {
                report?.Add($"{title}: confidence {PyRepr(confidence)} not used (dropped, no compat row)");
                continue;
            }
            string peripheral = Str(row, "peripheral").ToLowerInvariant();
            int flags;
            if (peripheral.Contains("3d control pad")) flags = 0x01 | 0x02;
            else if (peripheral.Contains("multitap")) flags = 0x04;
            else if (peripheral.Contains("stick")) flags = 0x08;
            else
            {
                flags = 0x02;
                report?.Add($"{title}: unrecognized peripheral {PyRepr(row.TryGetProperty("peripheral", out var p) ? p.GetString() : null)}, using bit1 only");
            }
            string note = GamesDb.AsciiFold(Str(row, "warning_text"));
            if (note.Length > NoteMax) note = note[..NoteMax];
            if (row.TryGetProperty("product_codes", out var codes))
                foreach (var code in codes.EnumerateArray()) o[Keys.Normalise(code.GetString()!)] = new CompatRow { Flags = flags, Note = note };
        }
        return o;
    }

    /// <summary>Python's repr() of a str or None, for the report's wording.</summary>
    static string PyRepr(string? s) => s == null ? "None" : s.Contains('\'') && !s.Contains('"') ? $"\"{s}\"" : $"'{s.Replace("'", "\\'")}'";

    public static Dictionary<string, CompatRow> Load(string loaderJson, string? controllerJson, List<string>? report = null, string? saveToolsJson = null)
    {
        var rows = new Dictionary<string, CompatRow>();
        CompatRow Row(string k) => rows.TryGetValue(k, out var r) ? r : rows[k] = new CompatRow();
        using (var d = JsonDocument.Parse(File.ReadAllText(loaderJson)))
            if (d.RootElement.TryGetProperty("games", out var games))
                foreach (var g in games.EnumerateArray())
                {
                    var row = Row(Keys.Normalise(g.GetProperty("product").GetString()!));
                    string area = Str(g, "area");
                    if (area == "") area = Str(g, "region");
                    int hint = Hints.GetValueOrDefault(Str(g, "loader")), flags = Str(g, "igr") == "bad" ? CfNoIgr : 0;
                    if (hint != 0) row.Hint = hint;
                    row.Regions |= RegionMask(area);
                    row.Flags |= flags;
                    row.Parts.Add((area == "*" ? "" : area, hint, flags, RegionMask(area)));
                    string crc = Str(g, "ip_crc");
                    row.Src.Add(new LoaderRow(g.GetProperty("product").GetString()!, area, hint,
                        crc != "" ? Convert.ToUInt32(crc, 16) : null, Str(g, "source").StartsWith(OverrideSource, StringComparison.Ordinal)));
                }
        if (saveToolsJson != null && File.Exists(saveToolsJson))
            using (var d = JsonDocument.Parse(File.ReadAllText(saveToolsJson)))
                if (d.RootElement.TryGetProperty("tools", out var tools))
                    foreach (var t in tools.EnumerateArray())
                        if (t.TryGetProperty("product_codes", out var pc))
                            foreach (var p in pc.EnumerateArray()) Row(Keys.Normalise(p.GetString()!)).Flags |= CfSaveTool;
        if (controllerJson != null && File.Exists(controllerJson))
            using (var d = JsonDocument.Parse(File.ReadAllText(controllerJson)))
                foreach (var (k, c) in LoadController(d.RootElement, report))
                {
                    var row = Row(k);
                    row.Flags |= c.Flags;
                    if (row.Note == "") row.Note = c.Note;
                }
        return rows;
    }

    static readonly string[] AllAreas = ["J", "T", "U", "E", ""];

    /// <summary>The GAMES.DB keys a disc with this product resolves to (SPEC §6, the matcher the scan and the menu
    /// use), for this area; with no area, for any area.</summary>
    static HashSet<string> Resolve(Matcher m, string product, string area) =>
        (area != "" ? [area] : AllAreas).Select(a => m.Match(product, a).Key).OfType<string>().ToHashSet();

    /// <summary>discs_of: the scan's final entries (after DiscSets) that have a key and a readable System ID.</summary>
    public static List<CompatDisc> DiscsOf(IEnumerable<LibEntry> entries, IReadOnlyDictionary<string, SystemId?> sysIds) =>
        entries.Where(e => e.Key != "" && sysIds.TryGetValue(e.Path, out var s) && s != null)
               .Select(e => { var s = sysIds[e.Path]!; return new CompatDisc(e.Path, e.Key, e.AreaFlags, s.Area, s.Product, SystemIdReader.KaiIpCrc(s.Raw256), e.Sets >> 4 != 0); })
               .ToList();

    /// <summary>same_product: equal after Cleanup + Normalise, or the same -50/-18 suffix (none counts) and an
    /// overlapping skeleton form ("MK-81307" ~ "81307"). "T-8103H" and "T-8103H-50" differ.</summary>
    public static bool SameProduct(string a, string b)
    {
        string x = Keys.Normalise(Matcher.Cleanup(a)), y = Keys.Normalise(Matcher.Cleanup(b));
        if (x == "" || y == "") return false;
        if (x == y) return true;
        var (bx, sx) = Matcher.Split(x);
        var (by, sy) = Matcher.Split(y);
        return sx == sy && Matcher.SkeletonForms(bx).Overlaps(Matcher.SkeletonForms(by));
    }

    /// <summary>The hint the menu reads for a disc of these area bits (menu/src/gamesdb.c om_gdb_compat's gate).</summary>
    static int Effect(CompatRow? t, int area) => t != null && t.Hint != 0 && (t.Regions == 0 || (t.Regions & area) != 0) ? t.Hint : 0;

    static LoaderRow? One(List<LoaderRow> rows) =>
        rows.Count > 0 && rows[0].Hint != 0 && rows.All(r => r.Hint == rows[0].Hint) ? rows[0] : null;

    /// <summary>decide (compat.py; 2026-11-25, Mortal Kombat II USA region-patched to area J): (1) our override rows of
    /// the disc's product, any area; (2) Kai's own method, a row whose ip_crc is the disc's IP CRC, whatever its
    /// product or area; (3) what its product and area reach today (current, the record-level fill); (4) the product
    /// alone, any area, when every row of that product (not its -50/-18 variants) agrees; (5) 2026-11-27, a disc of a
    /// multi-disc set (DiscSets) gets JHL unless a row of its product or its IP CRC says CWX: the in-game-reset stub hands
    /// a game's lid-exit disc swap (SYS_EXECDMP) to the BIOS CD player only under JHL (Enemy Zero, Policenauts on hardware).</summary>
    public static (int Hint, string? How, LoaderRow? Row) Decide(CompatDisc d, List<LoaderRow> src, int current)
    {
        var r = One(src.Where(s => s.Override && SameProduct(s.Product, d.Product)).ToList());
        if (r != null) return (r.Hint, "override", r);
        r = One(src.Where(s => !s.Override && s.Crc == d.Crc).ToList());
        if (r != null) return (r.Hint, "crc", r);
        if (current != 0) return (current, "exact", null);
        r = One(src.Where(s => !s.Override && SameProduct(s.Product, d.Product)).ToList());
        if (r != null) return (r.Hint, "product", r);
        if (d.InSet && !src.Any(s => s.Hint == Hints["cwx"] && (SameProduct(s.Product, d.Product) || s.Crc == d.Crc)))
            return (Hints["jhl"], "set", null);
        return (0, null, null);
    }

    /// <summary>_fold: one disc's Decide onto its record -- the hint set (if the record has none or the same one) and
    /// the region mask widened by the disc's area bits; a record holding a different hint is kept and the conflict
    /// named.</summary>
    static void Fold(Dictionary<string, CompatRow> got, List<LoaderRow> src, CompatDisc d, List<string>? report)
    {
        got.TryGetValue(d.Key, out var t);
        int cur = Effect(t, d.AreaFlags);
        var (hint, how, row) = Decide(d, src, cur);
        if (how == "crc" && !SameProduct(row!.Product, d.Product))
            report?.Add($"{d.Path}: IP CRC {d.Crc:X8} is Kai's row for {row.Product}, the disc says {d.Product}");
        if (hint == 0 || hint == cur) return;
        string why = how switch
        {
            "override" => "by our hardware test", "crc" => $"by IP CRC {d.Crc:X8}", "set" => "multi-disc set", _ => "by product only"
        };
        string srcOf = row != null ? $" ({row.Product}, area {row.Area})" : "";
        if (t != null && t.Hint != 0 && t.Hint != hint)
        {
            report?.Add($"{d.Path}: {HintNames[hint]} {why}{srcOf} not applied, {d.Key} already has {HintNames[t.Hint]}");
            return;
        }
        if (t == null) got[d.Key] = t = new CompatRow { Regions = d.AreaFlags };
        t.Hint = hint;
        // a disc with no area letters (a patched header: debug menus, XBAND builds) passes the menu's gate only with mask 0
        t.Regions = t.Regions != 0 && d.AreaFlags != 0 ? t.Regions | d.AreaFlags : 0;
        string area = d.Area != "" ? d.Area : "-";
        report?.Add(how == "set" ? $"{d.Path}: multi-disc set → JHL, disc area {area}"
                                 : $"{d.Path}: {HintNames[hint]} matched {why}{srcOf}, disc area {area}");
    }

    /// <summary>fill_compat: each record a row resolves to (the record a disc of that product and area matches, never
    /// only a byte-identical key: "MK-81307" is GAMES.DB's "81307" and "MK81307-50") gets its hint, flags, note and
    /// region mask; then each of the card's discs (DiscsOf) goes through Decide's rule order, folded onto its record,
    /// each hint that changes a disc's Auto named in report; the input bytes come back unchanged when nothing is
    /// patched.</summary>
    public static (byte[] Data, int Patched) Fill(byte[] data, IReadOnlyDictionary<string, CompatRow> rows,
                                                  IEnumerable<CompatDisc>? discs = null, List<string>? report = null)
    {
        var db = GamesDb.Read(data);
        var m = new Matcher(db.Records.Select(r => r.Key), db.Aliases);
        var got = new Dictionary<string, CompatRow>();
        CompatRow Got(string k, int regions) => got.TryGetValue(k, out var t) ? t : got[k] = new CompatRow { Regions = regions };
        foreach (var (product, row) in rows)
        {
            var parts = row.Parts.Count > 0 ? row.Parts : [("", row.Hint, row.Flags, row.Regions)];
            foreach (var (area, hint, flags, regions) in parts)
                foreach (var k in Resolve(m, product, area))
                {
                    var t = Got(k, -1);
                    if (hint != 0) t.Hint = hint;
                    t.Flags |= flags | (row.Parts.Count == 0 ? row.Flags : 0);
                    if (t.Note == "") t.Note = row.Note;
                    t.Regions = t.Regions == -1 ? regions : (t.Regions == 0 || regions == 0 ? 0 : t.Regions | regions);
                }
            if (row.Parts.Count > 0 && (row.Flags != 0 || row.Note != ""))    // controller/save-tool flags on a loader row
                foreach (var k in Resolve(m, product, ""))
                {
                    var t = Got(k, 0);
                    t.Flags |= row.Flags;
                    if (t.Note == "") t.Note = row.Note;
                }
        }
        var src = rows.Values.SelectMany(r => r.Src).ToList();
        foreach (var d in discs ?? []) Fold(got, src, d, report);
        int n = 0;
        foreach (var r in db.Records)
        {
            if (!got.TryGetValue(r.Key, out var c)) continue;
            (r.LoaderHint, r.CompatFlags, r.CompatNote, r.CompatRegions) = (c.Hint, c.Flags, c.Note, Math.Max(c.Regions, 0));
            n++;
        }
        return n == 0 ? (data, 0) : (GamesDb.Write(db.Records, db.Genres, db.Aliases, db.BuildDate), n);
    }
}
