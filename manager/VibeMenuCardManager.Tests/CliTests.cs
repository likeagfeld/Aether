// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Cli;

namespace VibeMenuCardManager.Tests;

public class CliTests
{
    /// <summary>The CLI with no system volumes (temp cards live on C:), as the build test does; a write verb also needs
    /// --allow-fixed there, since temp is on a fixed disk.</summary>
    static (int Rc, string Out, string Err) Run(params string[] a)
    {
        var o = new StringWriter();
        var e = new StringWriter();
        return (Program.Run(a, o, e, []), o.ToString(), e.ToString());
    }

    // "import-saves" is removed for 0.1 (the user: needs the same UI sophistication as the in-menu save manager) --
    // its own tests went with it; Core.Saves.SaveImporter's own tests are untouched.

    [Fact]
    public void Header_prints_the_fields_and_the_game_id()
    {
        using var card = new TempCard();
        var iso = Path.Combine(card.Dir, "g.iso");
        File.WriteAllBytes(iso, File.ReadAllBytes(Repo.P("contract/fixtures/sysid_gs9001.bin")));
        var (rc, o, _) = Run("header", iso);
        Assert.Equal(0, rc);
        Assert.Contains("product=GS-9001 ", o);
        Assert.Contains("game id=GS-9001@JTUE", o);
        Assert.Equal(1, Run("header", Path.Combine(card.Dir, "none.iso")).Rc);
    }

    [Fact]
    public void Scan_writes_the_golden_library_idx()
    {
        using var card = new TempCard();
        using var outd = new TempCard();
        Card.ScanFixture.Realize(Card.ScanFixture.Load(), card.Dir);
        var (rc, o, _) = Run("scan", card.Dir, "--gamesdb", Repo.P("contract/fixtures/tiny.games.db"), "--out", outd.Dir, "--allow-fixed");
        Assert.Equal(0, rc);
        Assert.Contains("9 images: 4 matched", o);
        var got = VibeMenuCardManager.Core.Formats.LibraryIndex.Read(File.ReadAllBytes(Path.Combine(outd.Dir, "LIBRARY.IDX")));
        var want = VibeMenuCardManager.Core.Formats.LibraryIndex.Read(File.ReadAllBytes(Repo.P("contract/fixtures/scan_tree.library.idx")));
        // the build date is today's, so compare the content; the CLI marks multi-disc sets as the card build does (the
        // console-scan golden has none): the fixture's two Grandia discs are set 1
        Assert.Equal(want.Entries, got.Entries.Select(e => e with { Sets = 0 }).ToList());
        Assert.Equal(got.Entries.Select(e => e.Path.StartsWith("RPG/Grandia/", StringComparison.Ordinal) ? 0x10 : 0), got.Entries.Select(e => e.Sets));
        Assert.Equal(want.Folders, got.Folders);
        Assert.Equal(want.Fingerprint, got.Fingerprint);
    }

    [Fact]
    public void Scan_into_an_unwritable_out_exits_1()
    {
        using var card = new TempCard();
        Card.ScanFixture.Realize(Card.ScanFixture.Load(), card.Dir);
        var outFile = Path.Combine(card.Dir, "not-a-dir");
        File.WriteAllText(outFile, "x");                       // --out is a file: Directory.CreateDirectory fails
        var (rc, _, e) = Run("scan", card.Dir, "--gamesdb", Repo.P("contract/fixtures/tiny.games.db"), "--out", outFile, "--allow-fixed");
        Assert.Equal(1, rc);
        Assert.StartsWith("scan stopped:", e);
    }

    [Fact]
    public void A_record_file_over_2_GB_is_an_IO_error()
    {
        Assert.Equal(1000, VibeMenuCardManager.Core.Saves.SaveImporter.BigLen("x", 1000));
        Assert.Throws<IOException>(() => VibeMenuCardManager.Core.Saves.SaveImporter.BigLen("x", 3L << 30));
    }

    [Fact]
    public void Install_backs_up_autoboot_and_leaves_menu_bin()
    {
        var (t, src, target) = Install.InstallCheck.Realize("replace");
        using (t)
        {
            var menu = File.ReadAllBytes(Path.Combine(target, "menu.bin"));
            var (rc, o, e) = Run("install", target, "--iso", src.Iso, "--launcher", src.Launcher, "--data", src.DataDir,
                                 "--vrtiles", src.VrTiles, "--theme", "neon=" + src.Themes!["neon"], "--allow-fixed");
            Assert.True(rc == 0, e);
            Assert.Contains("backed up: autoboot.iso.bak-", o);
            Assert.True(File.Exists(Path.Combine(target, "autoboot.cue")));                // never another autoboot.*
            Assert.Contains("note: an old /openmenu/ folder", o);
            Assert.Contains("menu.bin: present, unchanged", o);
            Assert.Equal(menu, File.ReadAllBytes(Path.Combine(target, "menu.bin")));
            Assert.Equal(2, Run("install", target, "--iso", src.Iso, "--allow-fixed").Rc);                 // the other inputs are required
            File.Delete(Path.Combine(src.DataDir, "GAMES.DB"));
            var (rc2, _, e2) = Run("install", target, "--iso", src.Iso, "--launcher", src.Launcher, "--data", src.DataDir, "--vrtiles", src.VrTiles, "--allow-fixed");
            Assert.Equal(1, rc2);
            Assert.Contains("missing data file(s): GAMES.DB", e2);
        }
    }

    [Fact]
    public void Build_makes_a_working_card_and_refuses_the_system_volume()
    {
        var t = new TempCard();
        using (t)
        {
            t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(Card.BuildCheck.Case("card.json"))).RootElement);
            string card = Path.Combine(t.Dir, "card"), release = Card.ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
            string[] args = ["build", card, "--release", release, "--covers-zip", Card.BuildCheck.Case("covers.zip"),
                             "--art-cache", Card.BuildCheck.Case("art_cache"), "--art-prefer", "GS-9001=boxtga", "--allow-fixed"];
            var vol = VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(card).Id!;
            var e1 = new StringWriter();
            Assert.Equal(1, Program.Run(args, new StringWriter(), e1, [vol]));       // the card's volume as a system volume
            Assert.Contains("system drive", e1.ToString());
            Assert.False(Directory.Exists(Path.Combine(card, "aether")));
            var o = new StringWriter();
            var e2 = new StringWriter();
            Assert.True(Program.Run(args, o, e2, []) == 0, e2.ToString());
            Assert.Contains("17 images: 16 matched", o.ToString());
            Assert.True(File.Exists(Path.Combine(card, "aether", "ART.PAK")));
            Assert.Equal(2, Program.Run(["build", card], new StringWriter(), new StringWriter(), []));   // --release is required
        }
    }

    // "tidy-saves" is removed for 0.1 (the user: "too broad and dangerous") -- its own tests went with it;
    // Core.Saves.SaveTidy's own tests are untouched.

    /// <summary>Final review I-1: every verb that writes goes through the one guard. With the temp card's volume as the
    /// system volume, install, scan --out and build are each refused, --allow-fixed or not, and nothing is written;
    /// without --allow-fixed a fixed disk is refused too.</summary>
    [Fact]
    public void Every_writing_verb_refuses_the_system_volume_and_a_fixed_disk_without_the_override()
    {
        var (t, src, target) = Install.InstallCheck.Realize("replace");
        using (t)
        {
            Directory.CreateDirectory(Path.Combine(target, "SAVES", "BRAM", "T-1"));
            Directory.CreateDirectory(Path.Combine(target, "aether"));
            var before = t.Manifest();
            var vol = VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(target).Id!;
            string release = Card.ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
            var verbs = new List<string[]>
            {
                new[] { "install", target, "--iso", src.Iso, "--launcher", src.Launcher, "--data", src.DataDir, "--vrtiles", src.VrTiles },
                new[] { "scan", target, "--gamesdb", Repo.P("contract/fixtures/tiny.games.db"), "--out", Path.Combine(target, "aether", "new") },
                new[] { "build", target, "--release", release },
            };
            before = t.Manifest();
            foreach (var v in verbs)
            {
                var e = new StringWriter();
                Assert.True(Program.Run([.. v, "--allow-fixed"], new StringWriter(), e, [vol]) == 1, string.Join(" ", v) + ": " + e);
                Assert.Contains("is on the system drive", e.ToString());
                if (VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(target).Kind == VibeMenuCardManager.Core.Card.VolumeKind.Fixed)
                {
                    var e2 = new StringWriter();
                    Assert.Equal(1, Program.Run(v, new StringWriter(), e2, []));
                    Assert.Contains("add --allow-fixed", e2.ToString());
                }
                Assert.Equal(before, t.Manifest());
            }
        }
    }

    /// <summary>The same with the real system volumes, when this machine's temp folder is on one (the reviewer's C:).</summary>
    [SkippableFact]
    public void The_real_system_drive_is_refused_by_every_writing_verb()
    {
        using var t = new TempCard();
        var vol = VibeMenuCardManager.Core.Card.VolumeResolver.Resolve(t.Dir).Id;
        Skip.IfNot(vol != null && VibeMenuCardManager.Core.Card.VolumeResolver.SystemVolumes().Contains(vol), "temp is not on a system volume here");
        Directory.CreateDirectory(Path.Combine(t.Dir, "SAVES", "BRAM", "T-1"));
        Directory.CreateDirectory(Path.Combine(t.Dir, "aether"));
        var before = t.Manifest();
        foreach (var v in new[] { new[] { "install", t.Dir, "--iso", "a", "--launcher", "b", "--data", "c", "--vrtiles", "d", "--allow-fixed" },
                                  new[] { "scan", t.Dir, "--gamesdb", Repo.P("contract/fixtures/tiny.games.db"), "--out", Path.Combine(t.Dir, "o"), "--allow-fixed" } })
        {
            var e = new StringWriter();
            Assert.Equal(1, Program.Run(v, new StringWriter(), e));
            Assert.Contains("is on the system drive", e.ToString());
        }
        Assert.Equal(before, t.Manifest());
    }

    [Fact]
    public void No_verb_prints_usage_and_exits_2() => Assert.Equal(2, Run().Rc);
}
