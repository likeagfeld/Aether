// VibeMenu Card Manager, GPL-3.0-or-later.
// Tidy saves on the PC against the launcher's own (M6 Task 24b; manager/golden/expect/tidy_*.json from tidy.c and r1.c
// compiled for the host, run_golden.sh): the same progress lines and counts for every preview and run, and the same
// card, file by file and byte for byte. Then the properties tidy.h promises, on the C# side: the preview writes
// nothing; a power cut or a cancel before any removal leaves every record version and every current set intact and the
// next run ends where the clean one did; a second run finds nothing; R1 re-reads the kept copy; mutation demos.
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public static class TidyGolden
{
    public static readonly string[] Cases = ["gens", "plain", "busy", "busy_new", "journal_empty", "random", "nosaves", "alt"];

    public sealed record Expect(string Sha, List<string> Log, List<TempCard.Row> Tree);

    public static JsonElement Spec(string c) =>
        JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/tidy_cases/{c}.json"))).RootElement;

    public static Expect Load(string c)
    {
        var r = JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/expect/tidy_{c}.json"))).RootElement;
        return new Expect(r.GetProperty("pgsbak_sha").GetString()!,
            r.GetProperty("log").EnumerateArray().Select(x => x.GetString()!).ToList(),
            r.GetProperty("tree").EnumerateArray().Select(t => new TempCard.Row(t.GetProperty("path").GetString()!,
                t.GetProperty("size").GetInt64(), t.GetProperty("sha256").GetString()!)).ToList());
    }

    public static string Line(bool run, TidyResult r, int rc) => string.Create(System.Globalization.CultureInfo.InvariantCulture,
        $"tidy run={(run ? 1 : 0)} files={r.Files} bytes={r.Bytes} bups={r.Bups} bkps={r.Bkps} folders={r.Folders} slots={r.Slots} skipped={r.Skipped} rc={rc}");

    /// <summary>One op as tidy_main.c logs it: the progress lines, then the counts (OM_TIDY_BUSY: rc -160, zeros).</summary>
    public static void Op(CardFs fs, bool run, List<string> log, Func<CardFs, bool, Action<string>, TidyResult>? tidy = null)
    {
        tidy ??= (f, r, say) => SaveTidy.Run(f, r, say);
        try { log.Add(Line(run, tidy(fs, run, l => log.Add("say " + l)), 0)); }
        catch (TidyBusyException) { log.Add(Line(run, new TidyResult(), -160)); }
    }

    /// <summary>Realize the case, run its ops through the C# Tidy, and compare with the console's log and card.</summary>
    public static List<string> Run(string c, Func<CardFs, bool, Action<string>, TidyResult>? tidy = null)
    {
        var spec = Spec(c);
        var x = Load(c);
        using var card = new TempCard();
        card.Realize(spec);
        var log = new List<string>();
        foreach (char op in spec.GetProperty("ops").GetString()!) Op(new CardFs(card.Dir), op == 'r', log, tidy);
        var bad = new List<string>();
        var want = x.Log.Where(l => l.StartsWith("say ", StringComparison.Ordinal) || l.StartsWith("tidy ", StringComparison.Ordinal)).ToList();
        for (int i = 0; i < Math.Max(want.Count, log.Count); i++)
            if (i >= want.Count || i >= log.Count || want[i] != log[i])
                bad.Add($"log line {i}: {(i < log.Count ? log[i] : "(none)")} expect {(i < want.Count ? want[i] : "(none)")}");
        bad.AddRange(TreeDiff(card.Manifest(), x));
        return bad;
    }

    /// <summary>The card against an expect's tree (the import comparison; IMPORT.TXT is not Tidy's).</summary>
    public static List<string> TreeDiff(List<TempCard.Row> tree, Expect x) =>
        ImportGolden.Compare([], null, tree, new ImportGolden.Expect(x.Sha, [], null, x.Tree))
                    .Where(b => !b.StartsWith("IMPORT.TXT", StringComparison.Ordinal)).ToList();

    /// <summary>Every record version on the card: a .BUP that decodes, or each record of a .BKP -- its fields
    /// (0x10-0x2F) and data, hashed. Tidy must never lose one.</summary>
    public static SortedSet<string> Versions(string dir)
    {
        var o = new SortedSet<string>(StringComparer.Ordinal);
        void Add(byte[] b, int off, long avail)
        {
            if (BupFile.TryDecode(b.AsSpan(off), (uint)avail, out var d) != BupError.None) return;
            o.Add(Convert.ToHexStringLower(SHA256.HashData([.. b.AsSpan(off + 0x10, 0x20), .. b.AsSpan(off + 64, (int)d!.DataSize)])));
        }
        foreach (var f in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
        {
            var b = File.ReadAllBytes(f);
            if (f.EndsWith(".BUP", StringComparison.OrdinalIgnoreCase) && b.Length >= 64) Add(b, 0, b.Length);
            else if (f.EndsWith(".BKP", StringComparison.OrdinalIgnoreCase) && b.Length >= 64 && b.AsSpan(0, 8).SequenceEqual("OMSATBKP"u8))
            {
                int cnt = (b[10] << 8) | b[11];
                long off = 64 + cnt * 32L;
                for (int k = 0; k < cnt && off + 64 <= b.Length; k++)
                {
                    if (BupFile.TryDecode(b.AsSpan((int)off), (uint)(b.Length - off), out var d) != BupError.None) break;
                    Add(b, (int)off, b.Length - off);
                    off += 64 + d!.DataSize + ((4 - (d.DataSize & 3)) & 3);
                }
            }
        }
        return o;
    }

    /// <summary>Every save folder's current set (SaveGen): its folder and the bytes of each *.BUP in it.</summary>
    public static SortedDictionary<string, string> CurrentSets(string dir)
    {
        var o = new SortedDictionary<string, string>(StringComparer.Ordinal);
        var fs = new CardFs(dir);
        var saves = Path.Combine(dir, "aether", "SAVES");
        if (!Directory.Exists(saves)) return o;
        foreach (var g in Directory.GetDirectories(saves))
        {
            string n = Path.GetFileName(g);
            if (n[0] == '_' && n != "_GENERAL") continue;
            var ds = new List<string> { "/aether/SAVES/" + n };
            for (int k = 1; k <= 9; k++) if (Directory.Exists(Path.Combine(g, "0" + k))) ds.Add($"/aether/SAVES/{n}/0{k}");
            foreach (var d in ds)
            {
                var (cur, gen) = SaveGen.Cur(fs, d);
                var files = fs.List(cur).Where(e => !e.IsDir && e.Name.EndsWith(".BUP", StringComparison.OrdinalIgnoreCase))
                              .Select(e => e.Name + ":" + Convert.ToHexStringLower(SHA256.HashData(fs.ReadAll(cur + "/" + e.Name))));
                o[d] = gen + " " + string.Join(",", files);
            }
        }
        return o;
    }
}

public class SaveTidyTests
{
    public static TheoryData<string> Cases => new(TidyGolden.Cases);

    [Theory]
    [MemberData(nameof(Cases))]
    public void Tidy_on_the_PC_equals_the_launchers(string c)
    {
        var bad = TidyGolden.Run(c);
        Assert.True(bad.Count == 0, $"{c}: {bad.Count} differences:\n" + string.Join("\n", bad.Take(30)));
    }

    [Fact]
    public void The_console_side_found_no_violation_and_the_tables_match_the_host_build()
    {
        foreach (var c in TidyGolden.Cases)
        {
            var x = TidyGolden.Load(c);
            Assert.Equal("014189c", x.Sha);   // frozen: the console Tidy's last commit (52e13a6 removed tidy.c and r1.c)
            Assert.Contains("violations=0", x.Log);                             // the fake card's own add-only check
            Assert.Contains($"caps tcap={SaveTidy.TCap} kcap={SaveTidy.KCap}", x.Log);
        }
        // what the golden must keep covering (a regenerated expect that lost it shows here)
        var g = TidyGolden.Load("gens").Log.Where(l => l.StartsWith("tidy ", StringComparison.Ordinal)).ToList();
        Assert.Equal("tidy run=1 files=15 bytes=4926 bups=10 bkps=5 folders=6 slots=5 skipped=2 rc=0", g[1]);
        Assert.Equal(g[0].Replace("run=0", "run=1").Replace("folders=0", "folders=6"), g[1]);   // the preview counts what a run removes
        Assert.StartsWith("tidy run=1 files=0 bytes=0", g[3]);                                  // the second run: nothing
        var r = TidyGolden.Load("random").Log.Where(l => l.StartsWith("tidy ", StringComparison.Ordinal)).ToList();
        Assert.StartsWith("tidy run=1 files=284 ", r[1]);
        Assert.Contains("rc=-160", TidyGolden.Load("busy_new").Log[0] + TidyGolden.Load("busy_new").Log[1]);
        // ALT/ (alternate imports, never loaded) is deduped across generations too -- the
        // current generation's copies are kept (G00002's), G00001's duplicate ALT/A_SAV goes the same as its main
        // A_SAV did, emptying G00001 (ALT/ then the generation itself); a backup is now covered when its only other
        // copy is in a kept ALT/ (Z_SAV, G00002's only copy), so 00001.BKP goes too -- only the newest (00003) stays
        var alt = TidyGolden.Load("alt").Tree.Select(t => t.Path).ToList();
        Assert.Contains("aether/SAVES/T-00001[U]/G000/G00002/ALT/Z_SAV.BUP", alt);
        Assert.Contains("aether/SAVES/T-00001[U]/G000/G00002/ALT/A_SAV.BUP", alt);
        Assert.DoesNotContain("aether/SAVES/T-00001[U]/G000/G00001", alt);
        Assert.DoesNotContain("aether/SAVES/_BACKUP/000/00001.BKP", alt);
        Assert.DoesNotContain("aether/SAVES/_BACKUP/000/00002.BKP", alt);
        Assert.Contains("aether/SAVES/_BACKUP/000/00003.BKP", alt);
        // no /vibemenu/SAVES at all (the journal path FR_NO_PATH on real FatFs) is nothing to do, not an error
        Assert.Equal("tidy run=1 files=0 bytes=0 bups=0 bkps=0 folders=0 slots=0 skipped=0 rc=0", TidyGolden.Load("nosaves").Log[3]);
    }

    static (TempCard Card, List<TempCard.Row> Before) Realize(string c)
    {
        var t = new TempCard();
        t.Realize(TidyGolden.Spec(c));
        return (t, t.Manifest());
    }

    [Theory]
    [InlineData("gens")]
    [InlineData("random")]
    public void The_preview_writes_nothing_and_a_second_run_finds_nothing(string c)
    {
        var (t, before) = Realize(c);
        using (t)
        {
            var p = SaveTidy.Run(new CardFs(t.Dir), run: false);
            Assert.Equal(before, t.Manifest());
            Assert.True(p.Files > 0);
            var r = SaveTidy.Run(new CardFs(t.Dir), run: true);
            Assert.Equal((p.Files, p.Bytes, p.Bups, p.Bkps), (r.Files, r.Bytes, r.Bups, r.Bkps));
            var end = t.Manifest();
            var z = SaveTidy.Run(new CardFs(t.Dir), run: true);
            Assert.Equal((0L, 0), (z.Files, z.Folders));
            Assert.Equal(end, t.Manifest());
        }
    }

    sealed class Cut : Exception;

    /// <summary>A power cut before removal k (every one of a run's removals for gens, K of them; for the random card,
    /// whose every layout takes seconds, six cuts spread over its K, the first and the last included):
    /// at the cut every record version and every current set are as before; the next run ends as the clean run did.</summary>
    [Theory]
    [InlineData("gens", 0)]
    [InlineData("random", 6)]
    public void A_power_cut_before_any_removal_loses_nothing_and_the_next_run_finishes(string c, int points)
    {
        var x = TidyGolden.Load(c);
        int K;
        List<TempCard.Row> end;
        SortedSet<string> v0;
        SortedDictionary<string, string> c0;
        {
            var (t, _) = Realize(c);
            using (t)
            {
                v0 = TidyGolden.Versions(t.Dir);
                c0 = TidyGolden.CurrentSets(t.Dir);
                var fs = new CardFs(t.Dir);
                int n = 0;
                fs.BeforeRemove = _ => n++;
                SaveTidy.Run(fs, run: true);
                K = n;
                end = t.Manifest();
            }
        }
        Assert.True(TidyGolden.TreeDiff(end, x).Count == 0, "the clean run differs from the launcher's");
        Assert.True(K > 20, $"{c}: only {K} removals");
        int swept = 0;
        var ks = points == 0 ? Enumerable.Range(1, K) : Enumerable.Range(0, points).Select(i => 1 + i * (K - 1) / (points - 1)).Distinct();
        foreach (int k in ks)
        {
            var (t, _) = Realize(c);
            using (t)
            {
                var fs = new CardFs(t.Dir);
                int n = 0;
                fs.BeforeRemove = _ => { if (++n == k) throw new Cut(); };
                Assert.Throws<Cut>(() => SaveTidy.Run(fs, run: true));
                Assert.Equal(v0, TidyGolden.Versions(t.Dir));
                Assert.Equal(c0, TidyGolden.CurrentSets(t.Dir));
                SaveTidy.Run(new CardFs(t.Dir), run: true);
                Assert.True(end.SequenceEqual(t.Manifest()), $"{c}: cut before removal {k} of {K}: the next run did not end as the clean one");
                swept++;
            }
        }
        Assert.True(swept == (points == 0 ? K : points));
    }

    [Fact]
    public void Cancel_after_any_removal_keeps_every_version_and_a_run_again_finishes()
    {
        var x = TidyGolden.Load("gens");
        for (int k = 1; k <= 30; k++)
        {
            var (t, _) = Realize("gens");
            using (t)
            {
                var v0 = TidyGolden.Versions(t.Dir);
                using var cts = new CancellationTokenSource();
                var fs = new CardFs(t.Dir);
                int n = 0;
                fs.BeforeRemove = _ => { if (++n == k) cts.Cancel(); };
                TidyResult? done = null;
                try { done = SaveTidy.Run(fs, run: true, null, cts.Token); }
                catch (TidyCancelledException e) { Assert.True(e.SoFar.Files <= 15); }
                Assert.Equal(v0, TidyGolden.Versions(t.Dir));
                if (done == null) SaveTidy.Run(new CardFs(t.Dir), run: true);
                var diff = TidyGolden.TreeDiff(t.Manifest(), x);
                Assert.True(diff.Count == 0, $"cancel after removal {k}: " + string.Join("\n", diff.Take(5)));
                if (done != null) { Assert.True(k > 20); break; }                 // k past the run's removals
            }
        }
    }

    [Fact]
    public void A_journal_on_the_card_stops_it_before_anything_is_read_or_removed()
    {
        var (t, before) = Realize("busy");
        using (t)
        {
            Assert.Throws<TidyBusyException>(() => SaveTidy.Run(new CardFs(t.Dir), run: true));
            Assert.Equal(before, t.Manifest());
        }
    }

    [Fact]
    public void R1_re_reads_the_kept_copy_at_the_moment_of_removal()
    {
        // the kept copy changes between the grouping and the removal (another writer): the duplicate is kept, not lost
            var (g, _) = Realize("gens");
            using (g)
            {
                string kept = Path.Combine(g.Dir, "aether/SAVES/T-00001[U]/01/G000/G00001/S1_SAV.BUP");
                var fs = new CardFs(g.Dir);
                bool hit = false;
                fs.BeforeRemove = p =>
                {
                    if (hit || !p.EndsWith("/01/S1_SAV.BUP", StringComparison.Ordinal)) return;
                    hit = true;
                    var b = File.ReadAllBytes(kept);
                    b[^1] ^= 0xFF;                                    // the kept copy is no longer byte-identical
                    File.WriteAllBytes(kept, b);
                };
                var v0 = TidyGolden.Versions(g.Dir);
                // S1_SAV is in slot 1's root (a duplicate), its current generation (kept) and backup 00006. The hook
                // fires at the root copy's removal, after its re-check passed, and changes the kept copy: backup 00006
                // is then re-checked against the changed copy and stays -- the version is not lost
                SaveTidy.Run(fs, run: true);
                Assert.True(hit);
                var v1 = TidyGolden.Versions(g.Dir);
                Assert.True(v0.IsSubsetOf(v1), "a record version was lost");
                Assert.True(File.Exists(Path.Combine(g.Dir, "aether/SAVES/_BACKUP/000/00006.BKP")), "backup 00006 went");
            }
    }

    public static TheoryData<int> Mutations => new((int)TidyMutation.CurrentNotKept, (int)TidyMutation.NewestBackupNotKept, (int)TidyMutation.NoRecheck);

    [Theory]
    [MemberData(nameof(Mutations))]
    public void Mutation_breaking_one_rule_fails_the_golden(int m)
    {
        var bad = TidyGolden.Run("gens", (fs, run, say) => SaveTidy.Run(fs, run, say, default, (TidyMutation)m));
        if ((TidyMutation)m == TidyMutation.NoRecheck)
        {
            // CRC grouping alone agrees on gens; the re-check matters when a kept copy changes under Tidy (the R1 test
            // above): a mutant that does not re-read it removes backup 00006 and loses the version
            var (g, _) = Realize("gens");
            using (g)
            {
                string kept = Path.Combine(g.Dir, "aether/SAVES/T-00001[U]/01/G000/G00001/S1_SAV.BUP");
                var fs = new CardFs(g.Dir);
                bool hit = false;
                fs.BeforeRemove = p =>
                {
                    if (hit || !p.EndsWith("/01/S1_SAV.BUP", StringComparison.Ordinal)) return;
                    hit = true;
                    var b = File.ReadAllBytes(kept);
                    b[^1] ^= 0xFF;
                    File.WriteAllBytes(kept, b);
                };
                var v0 = TidyGolden.Versions(g.Dir);
                SaveTidy.Run(fs, true, null, default, TidyMutation.NoRecheck);
                Assert.False(v0.IsSubsetOf(TidyGolden.Versions(g.Dir)), "the mutant lost nothing: the R1 demo is not sharp");
            }
            return;
        }
        Assert.NotEmpty(bad);
    }
}
