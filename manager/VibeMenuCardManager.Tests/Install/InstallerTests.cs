// VibeMenu Card Manager, GPL-3.0-or-later.
// The installer against tools/omsat/install.py (manager/golden/expect/install_<case>.json, mkinstall.py): the card after
// the C# install equals the card after omsat.install, file by file and byte for byte. Then the transaction: a failure
// at every single rename of the commit leaves the card exactly as it was; a failed undo names what is stranded;
// menu.bin is never written; an old /openmenu/ folder is left alone. Temp-dir cards only.
using System.Text.Json;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Tests.Install;

public static class InstallCheck
{
    public static readonly string[] Cases = ["fresh", "replace", "twice"];

    public static JsonElement Spec(string c) => JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/install_cases/{c}.json"))).RootElement;
    public static JsonElement Expect(string c) => JsonDocument.Parse(File.ReadAllText(Repo.P($"manager/golden/expect/install_{c}.json"))).RootElement;

    public static InstallSources Sources(string root, JsonElement args)
    {
        string s = Path.Combine(root, "src");
        var themes = new Dictionary<string, string>();
        if (args.TryGetProperty("themes", out var th))
            foreach (var t in th.EnumerateObject()) themes[t.Name] = Path.Combine(s, "themes", t.Value.GetString()!);
        return new InstallSources(Path.Combine(s, "autoboot.iso"), Path.Combine(s, "LAUNCHER.BIN"), Path.Combine(s, "data"),
            Path.Combine(s, "VRTILES.BIN"),
            args.TryGetProperty("loadbg", out var lb) && lb.GetBoolean() ? Path.Combine(s, "LOADBG.BIN") : null,
            args.TryGetProperty("music", out var mu) && mu.GetBoolean() ? Path.Combine(s, "BGM.MUS") : null, themes);
    }

    /// <summary>The manifest of card/ only (the case's src/ tree is the installer's input).</summary>
    public static List<TempCard.Row> CardTree(TempCard t) =>
        t.Manifest().Where(r => r.Path.StartsWith("card/")).Select(r => r with { Path = r.Path["card/".Length..] })
         .Where(r => r.Path != "").ToList();

    public static List<TempCard.Row> Rows(JsonElement e) => e.GetProperty("tree").EnumerateArray()
        .Select(t => new TempCard.Row(t.GetProperty("path").GetString()!, t.GetProperty("size").GetInt64(), t.GetProperty("sha256").GetString()!)).ToList();

    public static List<string> Diff(List<TempCard.Row> got, List<TempCard.Row> want)
    {
        var g = got.ToDictionary(r => r.Path);
        var w = want.ToDictionary(r => r.Path);
        var bad = new List<string>();
        foreach (var r in want) if (!g.TryGetValue(r.Path, out var x)) bad.Add($"missing {r.Path}"); else if (x != r) bad.Add($"differs {r.Path}");
        foreach (var r in got) if (!w.ContainsKey(r.Path)) bad.Add($"extra {r.Path}");
        return bad;
    }

    public static (TempCard Card, InstallSources Src, string Target) Realize(string c)
    {
        var spec = Spec(c);
        var t = new TempCard();
        t.Realize(spec);
        string target = Path.Combine(t.Dir, "card");
        Directory.CreateDirectory(target);
        return (t, Sources(t.Dir, spec.GetProperty("args")), target);
    }
}

public class InstallerTests
{
    public static TheoryData<string> Cases => new(InstallCheck.Cases);

    [Theory]
    [MemberData(nameof(Cases))]
    public void The_card_after_install_equals_omsat_installs(string c)
    {
        var x = InstallCheck.Expect(c);
        var (t, src, target) = InstallCheck.Realize(c);
        using (t)
        {
            var reports = new List<InstallReport>();
            for (int i = 0; i < InstallCheck.Spec(c).GetProperty("runs").GetInt32(); i++)
                reports.Add(Installer.Install(target, src, x.GetProperty("stamp").GetString()));
            var bad = InstallCheck.Diff(InstallCheck.CardTree(t), InstallCheck.Rows(x));
            Assert.True(bad.Count == 0, string.Join("\n", bad));
            var want = x.GetProperty("reports").EnumerateArray().ToList();
            Assert.Equal(want.Count, reports.Count);
            for (int i = 0; i < want.Count; i++)
            {
                Assert.Equal(want[i].GetProperty("backups").EnumerateArray().Select(b => b.GetString()), reports[i].Backups);
                Assert.Equal(want[i].GetProperty("copied").EnumerateArray().Select(b => b.GetString()), reports[i].Copied);
                Assert.Equal(want[i].GetProperty("menu_bin_present").GetBoolean(), reports[i].MenuBinPresent);
                Assert.Equal(want[i].GetProperty("old_openmenu_folder_untouched").GetBoolean(), reports[i].OldOpenMenuFolder);
            }
        }
    }

    [Fact]
    public void A_failure_at_any_commit_rename_leaves_the_card_exactly_as_it_was()
    {
        int renames = 0;
        {
            var (t, src, target) = InstallCheck.Realize("replace");
            using (t) renames = Installer.Install(target, src, "20261122-120000").Renames;
        }
        Assert.True(renames >= 14, $"{renames} renames");
        for (int k = 1; k <= renames; k++)
        {
            var (t, src, target) = InstallCheck.Realize("replace");
            using (t)
            {
                var before = InstallCheck.CardTree(t);
                int n = 0;
                var inst = new Installer { BeforeRename = (_, _) => { if (++n == k) throw new IOException($"injected at rename {k}"); } };
                var e = Assert.Throws<InstallException>(() => inst.Run(target, src, "20261122-120000"));
                Assert.StartsWith("install failed, card restored", e.Message);
                var bad = InstallCheck.Diff(InstallCheck.CardTree(t), before);
                Assert.True(bad.Count == 0, $"rename {k} of {renames}: " + string.Join("; ", bad));
            }
        }
    }

    [Fact]
    public void A_rename_never_writes_over_a_file_that_appeared_after_planning()
    {
        // something else writes autoboot.iso.bak-<stamp> between the plan and the commit: the rename must refuse it
        // (no overwrite), and the undo must leave both the card and the intruder as they were
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            var before = InstallCheck.CardTree(t);
            string intruder = Path.Combine(target, "autoboot.iso.bak-s");
            bool once = false;
            var inst = new Installer { BeforeRename = (_, b) => { if (!once && b == intruder) { once = true; File.WriteAllText(intruder, "not ours"); } } };
            Assert.StartsWith("install failed, card restored", Assert.Throws<InstallException>(() => inst.Run(target, src, "s")).Message);
            Assert.Equal("not ours", File.ReadAllText(intruder));
            File.Delete(intruder);
            Assert.Empty(InstallCheck.Diff(InstallCheck.CardTree(t), before));
        }
    }

    [Fact]
    public void A_read_only_existing_file_is_replaced_and_its_held_copy_deleted()
    {
        // a read-only destination is moved aside (a rename ignores the attribute) and must still be deleted
        // after the commit -- the attribute cleared and the delete retried -- never reported as a failed install
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            string db = Path.Combine(target, "aether", "GAMES.DB");
            File.SetAttributes(db, File.GetAttributes(db) | FileAttributes.ReadOnly);
            var r = Installer.Install(target, src, "20261122-120000");
            Assert.Empty(r.Warnings);
            Assert.False(File.Exists(db + Installer.HoldSuffix));
            Assert.Equal(File.ReadAllBytes(Path.Combine(src.DataDir, "GAMES.DB")), File.ReadAllBytes(db));
        }
    }

    [Fact]
    public void A_held_copy_that_cannot_be_deleted_is_a_warning_not_a_failure()
    {
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            string held = Path.Combine(target, "aether", "GAMES.DB" + Installer.HoldSuffix);
            FileStream? lockIt = null;
            var inst = new Installer { BeforeCleanup = () => lockIt = new FileStream(held, FileMode.Open, FileAccess.Read, FileShare.None) };
            try
            {
                var r = inst.Run(target, src, "20261122-120000");     // committed: no exception
                Assert.Single(r.Warnings);
                Assert.Contains("GAMES.DB" + Installer.HoldSuffix, r.Warnings[0]);
                Assert.Equal(File.ReadAllBytes(Path.Combine(src.DataDir, "GAMES.DB")), File.ReadAllBytes(Path.Combine(target, "aether", "GAMES.DB")));
            }
            finally { lockIt?.Dispose(); }
        }
    }

    [Fact]
    public void A_copy_failure_while_staging_leaves_the_card_exactly_as_it_was()
    {
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            var before = InstallCheck.CardTree(t);
            int n = 0;
            var inst = new Installer { BeforeCopy = _ => { if (++n == 5) throw new IOException("disk full"); } };
            Assert.StartsWith("install failed while staging", Assert.Throws<InstallException>(() => inst.Run(target, src, "s")).Message);
            Assert.Empty(InstallCheck.Diff(InstallCheck.CardTree(t), before));
        }
    }

    [Fact]
    public void A_failed_undo_names_the_stranded_files()
    {
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            int n = 0;
            var inst = new Installer
            {
                BeforeRename = (a, b) =>
                {
                    n++;
                    if (n == 4) throw new IOException("commit fails");
                    if (n == 5) throw new IOException("undo fails too");          // the first undo (of rename 3)
                },
            };
            var e = Assert.Throws<InstallException>(() => inst.Run(target, src, "20261122-120000"));
            Assert.StartsWith("install failed AND rollback incomplete", e.Message);
            Assert.Contains("Manual fix: mv", e.Message);
        }
    }

    [Fact]
    public void Bad_inputs_are_refused_before_anything_changes()
    {
        var (t, src, target) = InstallCheck.Realize("replace");
        using (t)
        {
            var before = InstallCheck.CardTree(t);
            var lb = File.ReadAllBytes(src.Launcher);
            lb[0x10] ^= 1;                                                       // the body-size field: the header CRC fails
            File.WriteAllBytes(src.Launcher, lb);
            Assert.Contains("LAUNCHER.BIN rejected", Assert.Throws<InstallException>(() => Installer.Install(target, src)).Message);
            File.Delete(Path.Combine(src.DataDir, "ART.PAK"));
            Assert.Contains("missing data file(s): ART.PAK", Assert.Throws<InstallException>(() => Installer.Install(target, src)).Message);
            Assert.Contains("theme", Assert.Throws<InstallException>(() => Installer.Install(target,
                src with { Themes = new Dictionary<string, string> { ["a/b"] = src.Themes!["neon"] } })).Message);
            Assert.Empty(InstallCheck.Diff(InstallCheck.CardTree(t), before));
        }
    }

    [Fact]
    public void A_changed_menu_bin_fails_the_install()
    {
        var (t, src, target) = InstallCheck.Realize("fresh");
        using (t)
        {
            var inst = new Installer { AfterCommit = () => File.AppendAllText(Path.Combine(target, "menu.bin"), "x") };   // a rogue writer
            Assert.Contains("menu.bin changed", Assert.Throws<InstallException>(() => inst.Run(target, src, "s")).Message);
        }
    }
}
