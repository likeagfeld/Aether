// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

/// <summary>Kai's 326 release .BUP headers (contract/fixtures/kai_bup_headers.json) against omsat.bupfile's verdicts
/// (manager/golden/expect/bup_corpus.json, mkcases.py --bup-corpus). One comparison, used by the tests and by the
/// mutation demos, so a demo proves the real check can fail.</summary>
public static class CorpusCheck
{
    public sealed record Row(string Path, int Len, byte[] Hdr);
    public sealed record Expect(string Path, bool Ok, string Err, string Name, uint Date, uint DataSize, ushort Blocks);
    public sealed record Pair(string A, string B, bool AWins);

    public static List<Row> LoadRows()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Repo.P("contract/fixtures/kai_bup_headers.json")));
        return doc.RootElement.GetProperty("files").EnumerateArray()
            .Select(f => new Row(f.GetProperty("path").GetString()!, f.GetProperty("len").GetInt32(),
                                 Convert.FromHexString(f.GetProperty("hdr").GetString()!)))
            .ToList();
    }

    public static (List<Expect> Rows, List<Pair> Pairs) LoadExpect()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/bup_corpus.json")));
        var rows = doc.RootElement.GetProperty("rows").EnumerateArray().Select(r => new Expect(
            r.GetProperty("path").GetString()!, r.GetProperty("ok").GetBoolean(), r.GetProperty("err").GetString()!,
            r.GetProperty("name").GetString()!, r.GetProperty("date").GetUInt32(), r.GetProperty("datasize").GetUInt32(),
            r.GetProperty("blocks").GetUInt16())).ToList();
        var pairs = doc.RootElement.GetProperty("pairs").EnumerateArray().Select(p => new Pair(
            p.GetProperty("a").GetString()!, p.GetProperty("b").GetString()!, p.GetProperty("a_wins").GetBoolean())).ToList();
        return (rows, pairs);
    }

    static string ErrName(BupError e) => e switch
    {
        BupError.Short => "short",
        BupError.Magic => "magic",
        BupError.Field => "name",
        _ => "",
    };

    /// <summary>Every row's decode verdict and fields against the expect; returns the mismatches (empty = GREEN).</summary>
    public static List<string> CompareDecode(IReadOnlyList<Row> rows, IReadOnlyList<Expect> expect)
    {
        var bad = new List<string>();
        if (rows.Count != expect.Count) bad.Add($"row count {rows.Count} != expect {expect.Count}");
        for (int i = 0; i < Math.Min(rows.Count, expect.Count); i++)
        {
            var r = rows[i];
            var x = expect[i];
            if (r.Path != x.Path) { bad.Add($"row {i}: path {r.Path} != {x.Path}"); continue; }
            var err = BupFile.TryDecode(r.Hdr, (uint)r.Len, out var d);
            bool ok = err == BupError.None;
            if (ok != x.Ok || (!ok && ErrName(err) != x.Err)) { bad.Add($"{r.Path}: {err} != expect ok={x.Ok} err={x.Err}"); continue; }
            if (!ok) continue;
            string name = System.Text.Encoding.ASCII.GetString(d!.Name);
            if (name != x.Name || d.Date != x.Date || d.DataSize != x.DataSize || d.Blocks != x.Blocks)
                bad.Add($"{r.Path}: name={name} date={d.Date:X8} size={d.DataSize} blocks={d.Blocks} != expect " +
                        $"{x.Name} {x.Date:X8} {x.DataSize} {x.Blocks}");
        }
        return bad;
    }

    /// <summary>Prefer on every same-folder same-record pair against omsat.bupfile.prefer.</summary>
    public static List<string> ComparePrefer(IReadOnlyList<Row> rows, IReadOnlyList<Pair> pairs)
    {
        var by = rows.ToDictionary(r => r.Path);
        var bad = new List<string>();
        foreach (var p in pairs)
        {
            var a = by[p.A];
            var b = by[p.B];
            bool w = BupFile.Prefer(a.Hdr, p.A[(p.A.LastIndexOf('/') + 1)..], b.Hdr, p.B[(p.B.LastIndexOf('/') + 1)..]);
            if (w != p.AWins) bad.Add($"prefer({p.A}, {p.B}) = {w}, expect {p.AWins}");
        }
        return bad;
    }
}
