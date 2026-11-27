// VibeMenu Card Manager, GPL-3.0-or-later.
// Final review I-4: the behavior goldens that come from tools/omsat (cover encoder and JP titles, ART.PAK, the card
// build, the installer, GAMES.DB dates, the matcher, .BUP decode) are only proof while tools/omsat is what they were
// made from. manager/golden/omsat_sources.txt pins those sources as sources.txt pins the console's (pin_omsat.py); a
// changed source fails here with "regenerate", and every such expect carries the pin it was made at.
using System.Text.Json;
using VibeMenuCardManager.Tests.Saves;

namespace VibeMenuCardManager.Tests;

public class OmsatPinTests
{
    static string[] Lines => File.ReadAllLines(Repo.P("manager/golden/omsat_sources.txt"));
    static string Pin => Lines.Single(l => l.StartsWith("pinned ", StringComparison.Ordinal)).Split(' ')[1];

    static readonly string[] Expects = ["art_covers", "art_titles", "artpak", "build", "bup_corpus", "gamesdb_fill", "match_real",
                                        "install_fresh", "install_replace", "install_twice"];

    [Fact]
    public void Tools_omsat_sources_are_still_the_ones_the_behavior_goldens_came_from()
    {
        var bad = SaveImporterGoldenTests.SourceDrift(Repo.Root, Lines);
        Assert.True(bad.Count == 0, $"{bad.Count} tools/omsat sources changed since the behavior goldens' pin {Pin} -- regenerate them " +
                                    "at the change's commit (manager/golden/README.md: mkcases.py, mkart.py, mkinstall.py, mkbuild.py), " +
                                    "port the change to the C#, re-measure the JP-title bounds if jpfont.py changed, then run " +
                                    "python manager/golden/pin_omsat.py <sha>:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void Every_behavior_expect_names_the_pin_it_was_made_at()
    {
        Assert.Matches("^[0-9a-f]{7}$", Pin);
        foreach (var e in Expects)
        {
            var r = JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/expect/{e}.json"))).RootElement;
            Assert.True(r.TryGetProperty("omsat_pin", out var p) && p.GetString() == Pin, $"{e}.json: omsat_pin is not {Pin}");
        }
        foreach (var m in new[] { "jpfont", "artpak", "cardbuild", "coverzip", "artsrc", "compat", "lnchhdr", "install", "gamesdb", "libidx",
                                  "cardscan", "match", "keys", "iphdr", "binfmt", "bupfile", "check" })
            Assert.Contains(Lines, l => l.EndsWith($" tools/omsat/{m}.py", StringComparison.Ordinal));
    }

    [SkippableFact]
    public void Mutation_a_changed_omsat_source_is_seen()
    {
        var has = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("git", ["-C", Repo.Root, "cat-file", "-e", $"{Pin}^{{commit}}"])
            { RedirectStandardError = true, UseShellExecute = false })!;
        has.WaitForExit();
        Skip.IfNot(has.ExitCode == 0, $"the pin {Pin} is not a commit in this checkout (a squashed or shallow history)");
        using var t = new TempCard();
        foreach (var l in Lines.Where(l => l.Length > 65 && !l.StartsWith('#')))
        {
            string rel = l[65..], dst = Path.Combine(t.Dir, rel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
            var b = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("git", ["-C", Repo.Root, "show", $"{Pin}:{rel}"])
                { RedirectStandardOutput = true, UseShellExecute = false })!;
            using (var f = File.Create(dst)) b.StandardOutput.BaseStream.CopyTo(f);
            b.WaitForExit();
        }
        Assert.Empty(SaveImporterGoldenTests.SourceDrift(t.Dir, Lines));         // the pinned commit's own copies match
        File.AppendAllText(Path.Combine(t.Dir, "tools", "omsat", "jpfont.py"), "# a baseline change\n");
        Assert.Equal(["tools/omsat/jpfont.py: differs from the pin"], SaveImporterGoldenTests.SourceDrift(t.Dir, Lines));
    }
}
