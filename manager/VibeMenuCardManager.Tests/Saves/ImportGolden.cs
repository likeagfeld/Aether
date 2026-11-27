// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

/// <summary>The console import's result (manager/golden/expect/&lt;case&gt;.json, written by run_golden.sh from
/// menu/launcher/pgsbak.c compiled for the host) against the C# import's on the same case. One comparison, used by the
/// golden tests and the mutation demos.</summary>
public static class ImportGolden
{
    public static readonly string[] Cases = ["corpus", "merge", "existing", "rerun", "cart", "sticky", "gens", "highslot"];

    public sealed record Expect(string Sha, List<Dictionary<string, int>> Runs, string? ImportTxt, List<TempCard.Row> Tree);

    public static JsonElement Spec(string c) =>
        JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/cases/{c}.json"))).RootElement;

    public static Expect Load(string c)
    {
        var r = JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/expect/{c}.json"))).RootElement;
        return new Expect(
            r.GetProperty("pgsbak_sha").GetString()!,
            r.GetProperty("runs").EnumerateArray().Select(x => x.EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetInt32())).ToList(),
            r.GetProperty("import_txt").ValueKind == JsonValueKind.Null ? null : r.GetProperty("import_txt").GetString(),
            r.GetProperty("tree").EnumerateArray().Select(t => new TempCard.Row(t.GetProperty("path").GetString()!,
                t.GetProperty("size").GetInt64(), t.GetProperty("sha256").GetString()!)).ToList());
    }

    public static Dictionary<string, int> Counts(ImportResult r) => new()
    {
        ["kai"] = r.Kai, ["rings"] = r.Rings, ["present"] = r.Present, ["skipped"] = r.Skipped,
        ["conflicts"] = r.Conflicts, ["older"] = r.Older, ["cart"] = r.Cart, ["alternates"] = r.Alternates, ["rc"] = 0,
    };

    /// <summary>Realize the case, run the C# import the case's number of times, and compare with the expect.</summary>
    public static List<string> Run(string c, Expect x, Func<CardFs, ImportResult>? import = null)
    {
        import ??= fs => SaveImporter.Run(fs);
        var spec = Spec(c);
        using var card = new TempCard();
        card.Realize(spec);
        int runs = spec.TryGetProperty("runs", out var n) ? n.GetInt32() : 1;
        var got = new List<Dictionary<string, int>>();
        for (int i = 0; i < runs; i++) got.Add(Counts(import(new CardFs(card.Dir))));
        var txt = Path.Combine(card.Dir, "aether", "SAVES", "IMPORT.TXT");
        return Compare(got, File.Exists(txt) ? File.ReadAllText(txt) : null, card.Manifest(), x);
    }

    public static List<string> Compare(List<Dictionary<string, int>> runs, string? importTxt, List<TempCard.Row> tree, Expect x)
    {
        var bad = new List<string>();
        if (runs.Count != x.Runs.Count) bad.Add($"runs {runs.Count} != expect {x.Runs.Count}");
        for (int i = 0; i < Math.Min(runs.Count, x.Runs.Count); i++)
            foreach (var (k, v) in x.Runs[i])
                if (!runs[i].TryGetValue(k, out var g) || g != v) bad.Add($"run {i + 1}: {k}={(runs[i].TryGetValue(k, out var gg) ? gg : -1)} expect {v}");
        if (importTxt != x.ImportTxt) bad.Add($"IMPORT.TXT {Show(importTxt)} expect {Show(x.ImportTxt)}");
        var want = x.Tree.ToDictionary(t => t.Path);
        var have = tree.ToDictionary(t => t.Path);
        foreach (var t in x.Tree)
            if (!have.TryGetValue(t.Path, out var h)) bad.Add($"missing {t.Path}");
            else if (h != t) bad.Add($"differs {t.Path}: size {h.Size} sha {h.Sha256[..Math.Min(12, h.Sha256.Length)]} expect size {t.Size} sha {t.Sha256[..Math.Min(12, t.Sha256.Length)]}");
        foreach (var t in tree)
            if (!want.ContainsKey(t.Path)) bad.Add($"extra {t.Path}");
        return bad;
    }

    static string Show(string? s) => s == null ? "(absent)" : JsonSerializer.Serialize(s);
}
