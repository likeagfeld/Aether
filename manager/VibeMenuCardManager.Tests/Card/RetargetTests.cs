// VibeMenu Card Manager, GPL-3.0-or-later.
// a card confirmed by volume and path is re-verified during the write. A junction (or SUBST)
// retargeted mid-import or mid-install stops the write cleanly: the confirmed card holds whole files only, the new
// target is never written.
using System.Diagnostics;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Install;
using VibeMenuCardManager.Core.Saves;
using VibeMenuCardManager.Tests.Install;
using VibeMenuCardManager.Tests.Saves;

namespace VibeMenuCardManager.Tests.Card;

public class RetargetTests
{
    static int Cmd(string args)
    {
        using var p = Process.Start(new ProcessStartInfo("cmd.exe", "/c " + args) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true })!;
        p.WaitForExit(15000);
        return p.ExitCode;
    }

    static void Junction(string link, string target)
    {
        if (Directory.Exists(link)) Directory.Delete(link);              // removes the junction only
        Assert.Equal(0, Cmd($"mklink /J \"{link}\" \"{target}\""));
    }

    [SkippableTheory]
    [InlineData(1)]
    [InlineData(5)]
    [InlineData(13)]
    public void A_junction_retargeted_mid_import_stops_it_with_the_confirmed_card_consistent(int k)
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "junctions are Windows-only");
        using var t = new TempCard();
        string a = Path.Combine(t.Dir, "A"), b = Path.Combine(t.Dir, "B"), link = Path.Combine(t.Dir, "card");
        using var ca = new TempCard();
        ca.Realize(ImportGolden.Spec("merge"));
        Directory.Move(ca.Dir, a);
        Directory.CreateDirectory(ca.Dir);
        Directory.CreateDirectory(Path.Combine(b, "aether"));
        File.WriteAllText(Path.Combine(b, "other.txt"), "another card");
        Junction(link, a);
        try
        {
            var golden = ImportGolden.Load("merge").Tree.ToDictionary(x => x.Path);
            var before = new TempCardView(a).Manifest().ToDictionary(x => x.Path);
            var bBefore = new TempCardView(b).Manifest();
            int n = 0;
            var fs = new CardFs(link, VolumeResolver.Resolve(link).Identity) { BeforeRename = _ => { if (++n == k) Junction(link, b); } };
            var e = Assert.Throws<ImportStoppedException>(() => SaveImporter.Run(fs));
            Assert.Contains("the card changed", e.Message);
            Assert.Equal(bBefore, new TempCardView(b).Manifest());                    // the new target: not one byte written
            foreach (var r in new TempCardView(a).Manifest())
            {
                if (before.TryGetValue(r.Path, out var old)) { Assert.Equal(old, r); continue; }
                if (r.Path.EndsWith(".IMP", StringComparison.Ordinal) || r.Path.EndsWith("/", StringComparison.Ordinal)) continue;   // never read as a save
                Assert.Equal(golden[r.Path], r);                                       // a whole file, the console's bytes
            }
            Assert.False(File.Exists(Path.Combine(a, "aether", "SAVES", "IMPORT.TXT")));
        }
        finally { if (Directory.Exists(link)) Directory.Delete(link); }
    }

    [SkippableFact]
    public void A_junction_retargeted_mid_install_stops_it_and_writes_nothing_to_the_new_target()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "junctions are Windows-only");
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            string b = Path.Combine(t.Dir, "B"), link = Path.Combine(t.Dir, "cardlink");
            Directory.CreateDirectory(b);
            File.WriteAllText(Path.Combine(b, "menu.bin"), "another card's menu");
            Junction(link, target);
            try
            {
                var before = InstallCheck.CardTree(t).Where(r => !r.Path.StartsWith("../", StringComparison.Ordinal)).ToList();
                int n = 0;
                var inst = new Installer { ConfirmedIdentity = VolumeResolver.Resolve(link).Identity, BeforeCopy = _ => { if (++n == 3) Junction(link, b); } };
                var e = Assert.Throws<InstallException>(() => inst.Run(link, src, "20261122-120000"));
                Assert.Contains("the card changed", e.Message);
                Assert.Equal(["menu.bin"], Directory.GetFileSystemEntries(b).Select(Path.GetFileName));     // the new target: untouched
                var now = InstallCheck.CardTree(t);
                // left on the confirmed card: the staged files and the folders staging made (transient, never read);
                // everything that was there is exactly as it was
                var bad = InstallCheck.Diff(now.Where(r => !r.Path.EndsWith(Installer.StageSuffix, StringComparison.Ordinal)).ToList(), before);
                Assert.All(bad, d => Assert.Matches("^extra .*/$", d));
                Assert.Equal(File.ReadAllBytes(Path.Combine(target, "autoboot.iso")), Convert.FromHexString(
                    InstallCheck.Spec("replace").GetProperty("files").EnumerateArray().First(f => f.GetProperty("path").GetString() == "card/autoboot.iso").GetProperty("raw").GetString()!));
            }
            finally { if (Directory.Exists(link)) Directory.Delete(link); }
        }
    }

    [Fact]
    public void Verify_without_a_confirmed_identity_never_throws_and_with_one_holds_while_unchanged()
    {
        using var t = new TempCard();
        new CardFs(t.Dir).Verify();
        new CardFs(t.Dir, VolumeResolver.Resolve(t.Dir).Identity).Verify();
        Assert.Throws<CardChangedException>(() => new CardFs(t.Dir, "\\\\?\\Volume{other}\\ serial 0 | X:\\").Verify());
    }

    [SkippableFact]
    public void Re_verifying_costs_well_under_a_millisecond()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "measured on Windows");
        using var t = new TempCard();
        var fs = new CardFs(t.Dir, VolumeResolver.Resolve(t.Dir).Identity);
        fs.Verify();
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < 200; i++) fs.Verify();
        double per = sw.Elapsed.TotalMilliseconds / 200;
        Assert.True(per < 5, $"{per:F3} ms per verify");      // measured this session: 0.18 ms, so it runs before every file
    }
}

/// <summary>A manifest of any folder (TempCard's, without owning or deleting it).</summary>
public sealed class TempCardView(string dir)
{
    public List<TempCard.Row> Manifest()
    {
        var o = new List<TempCard.Row>();
        foreach (var e in new DirectoryInfo(dir).EnumerateFileSystemInfos("*", SearchOption.AllDirectories))
        {
            string rel = Path.GetRelativePath(dir, e.FullName).Replace(Path.DirectorySeparatorChar, '/');
            o.Add(e is DirectoryInfo ? new TempCard.Row(rel + "/", 0, "")
                : new TempCard.Row(rel, ((FileInfo)e).Length, Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(e.FullName)))));
        }
        o.Sort((x, y) => VibeMenuCardManager.Core.Be.CompareBytes(x.Path, y.Path));
        return o;
    }
}
