// VibeMenu Card Manager, GPL-3.0-or-later.
// The PC import and the console import must agree: every case's card after the C# import equals, file by
// file and byte for byte, the card after menu/launcher/pgsbak.c's import (manager/golden/expect, run_golden.sh).
namespace VibeMenuCardManager.Tests.Saves;

public class SaveImporterGoldenTests
{
    public static TheoryData<string> Cases => new(ImportGolden.Cases);

    [Theory]
    [MemberData(nameof(Cases))]
    public void Card_after_the_import_equals_the_console_imports(string c)
    {
        var x = ImportGolden.Load(c);
        var bad = ImportGolden.Run(c, x);
        Assert.True(bad.Count == 0, $"{c}: {bad.Count} differences over {x.Tree.Count} expected entries:\n" + string.Join("\n", bad.Take(30)));
    }

    [Fact]
    public void Every_expect_comes_from_the_pinned_commit()
    {
        string readme = File.ReadAllText(Repo.P("manager/golden/README.md"));
        foreach (var c in ImportGolden.Cases)
        {
            var sha = ImportGolden.Load(c).Sha;
            Assert.Matches("^[0-9a-f]{7}$", sha);
            Assert.Contains($"pinned: {sha}", readme);
        }
    }

    /// <summary>The comparison used by the drift gate and its mutation demo: every pinned source's SHA-256 (LF line
    /// endings) against the file under root; returns the files that differ or are missing.</summary>
    public static List<string> SourceDrift(string root, IEnumerable<string> pinLines)
    {
        var bad = new List<string>();
        foreach (var line in pinLines)
        {
            if (line.StartsWith('#') || line.StartsWith("pinned ") || line.Trim() == "") continue;
            var (want, rel) = (line[..64], line[65..]);
            var p = Path.Combine(root, rel.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(p)) { bad.Add($"{rel}: missing"); continue; }
            var raw = File.ReadAllBytes(p);                // only a CR before an LF is a line ending
            var b = raw.Where((x, i) => !(x == (byte)'\r' && i + 1 < raw.Length && raw[i + 1] == (byte)'\n')).ToArray();
            if (Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(b)) != want) bad.Add($"{rel}: differs from the pin");
        }
        return bad;
    }

    /// <summary>review-1 I-1: the golden only proves the PC import equals the console import while the console import's
    /// sources are the pinned ones. A changed source means the golden is stale: port the change, run
    /// manager/golden/run_golden.sh at its commit and update the pin.</summary>
    [Fact]
    public void The_console_imports_sources_are_still_the_pinned_ones()
    {
        var lines = File.ReadAllLines(Repo.P("manager/golden/sources.txt"));
        string pin = lines.Single(l => l.StartsWith("pinned ")).Split(' ')[1];
        Assert.Equal(pin, ImportGolden.Load("merge").Sha);
        Assert.True(lines.Count(l => l.Length > 65 && !l.StartsWith('#')) >= 19, "sources.txt lists fewer than the 19 import sources");
        foreach (var f in new[] { "menu/src/savegen.c", "menu/launcher/pgsbak.c" })
            Assert.Contains(lines, l => l.EndsWith(" " + f, StringComparison.Ordinal));
        var bad = SourceDrift(Repo.Root, lines);
        Assert.True(bad.Count == 0, $"{bad.Count} import sources changed since the golden's pin {pin} -- regenerate it " +
                                    $"(sh manager/golden/run_golden.sh <sha>) and port the change:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void The_cases_cover_what_the_plan_lists()
    {
        // denominators of the golden, so a regenerated expect that lost its coverage shows here
        var merge = ImportGolden.Load("merge");
        Assert.Equal(new() { ["kai"] = 8, ["rings"] = 8, ["present"] = 0, ["skipped"] = 4, ["conflicts"] = 0, ["older"] = 5, ["cart"] = 1, ["rc"] = 0 }, merge.Runs[0]);
        var existing = ImportGolden.Load("existing").Runs[0];
        Assert.Equal(1, existing["present"]);
        Assert.Equal(1, existing["conflicts"]);
        var rerun = ImportGolden.Load("rerun").Runs;
        Assert.Equal(2, rerun.Count);
        Assert.Equal(0, rerun[1]["kai"] + rerun[1]["rings"] + rerun[1]["older"]);
        var corpus = ImportGolden.Load("corpus").Runs[0];
        // the corpus' [SLOT12]/T-900 entry (slot 10-19) is now placed (place_high), not skipped; T-900 has
        // no other saves, so it goes to slot 0 (hardware 2026-11-25)
        Assert.Equal(304, corpus["kai"] + corpus["rings"]);     // test_pgsbak.c's want: every intact winner up to 32,832 B
        Assert.Equal(4, corpus["older"]);                      // the corpus' 4 losing duplicates
        Assert.Equal(19, corpus["cart"]);                      // 19 of the 326 release files are over 32 KB
        Assert.Contains(ImportGolden.Load("corpus").Tree, t => t.Path == "aether/SAVES/T-900/SLOT12_SAVE.BUP");
        var cart = ImportGolden.Load("cart").Runs;
        Assert.Equal(new() { ["kai"] = 1, ["rings"] = 0, ["present"] = 1, ["skipped"] = 1, ["conflicts"] = 1, ["older"] = 1, ["cart"] = 3, ["rc"] = 0 }, cart[0]);
        Assert.Equal(0, cart[1]["cart"] + cart[1]["older"]);   // the second run copies nothing again
        // sticky destinations -- VibeMenu's MK-81214 receives Kai's MK-81214[UT], no new folder;
        // T-555's seven sources of one slot (five Kai regions, Kai's product-only folder, Rings) all merge
        var sticky = ImportGolden.Load("sticky");
        Assert.Equal(new() { ["kai"] = 13, ["rings"] = 1, ["present"] = 0, ["skipped"] = 0, ["conflicts"] = 1, ["older"] = 4, ["cart"] = 0, ["rc"] = 0 }, sticky.Runs[0]);
        Assert.DoesNotContain(sticky.Tree, t => t.Path.StartsWith("aether/SAVES/MK-81214[", StringComparison.Ordinal));
        Assert.Contains(sticky.Tree, t => t.Path == "aether/SAVES/GS-9001[J]/GS_SAVE_JP0.BUP");
        Assert.Equal(5, sticky.Tree.Count(t => t.Path.StartsWith("aether/SAVES/T-555/REG_SAV_", StringComparison.Ordinal)));
        // Task 24b: into the current set -- the newest complete generation, never an incomplete one
        var gens = ImportGolden.Load("gens");
        Assert.Contains(gens.Tree, t => t.Path == "aether/SAVES/MK-81214[UT]/G000/G00003/SCORCHER_02.BUP");
        Assert.Contains(gens.Tree, t => t.Path == "aether/SAVES/MK-81214[UT]/03/G000/G00001/SLOTSAVE_03.BUP");
        Assert.Contains(gens.Tree, t => t.Path == "aether/SAVES/GS-9001/G000/G00099/GS_SAVE_000.BUP");
        Assert.Contains(gens.Tree, t => t.Path == "aether/SAVES/T-123/ABC_SAVE_01.BUP");
        Assert.Contains(gens.Tree, t => t.Path == "aether/SAVES/MK-81214[UT]/CART/BIGSAVE_001.BUP");
        Assert.DoesNotContain(gens.Tree, t => t.Path.StartsWith("aether/SAVES/MK-81214[UT]/G000/G00004/S", StringComparison.Ordinal) && t.Path != "aether/SAVES/MK-81214[UT]/G000/G00004/SCORCHER_02.BUP");
        // Rings [SLOT10]-[SLOT19] placement and Kai's SAVES/LATEST into _GENERAL
        var hs = ImportGolden.Load("highslot");
        Assert.Equal(new() { ["kai"] = 1, ["rings"] = 2, ["present"] = 0, ["skipped"] = 0, ["conflicts"] = 0, ["older"] = 0, ["cart"] = 0, ["rc"] = 0 }, hs.Runs[0]);
        Assert.Equal(4, hs.Runs[1]["present"]);     // the second run: every placement already its own (idempotent)
        Assert.Contains(hs.Tree, t => t.Path == "aether/SAVES/T-300/02/GAMESAV_001.BUP");          // not slot 01's: a new slot
        Assert.Contains(hs.Tree, t => t.Path == "aether/SAVES/T-300/01/GAMESAV_001.BUP");          // slot 01 untouched
        Assert.Contains(hs.Tree, t => t.Path == "aether/SAVES/T-301/ALT/GAMESAV_001.BUP");         // every slot full: ALT/
        Assert.Contains(hs.Tree, t => t.Path == "aether/SAVES/_GENERAL/GENSAVE_001.BUP");          // Kai's LATEST
        Assert.Contains(hs.Tree, t => t.Path == "aether/SAVES/GENLIST.TXT");                       // fix I9: the "I" line
    }
}
