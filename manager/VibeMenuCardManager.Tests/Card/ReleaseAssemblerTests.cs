// VibeMenu Card Manager, GPL-3.0-or-later.
// Final review I-2: the release folder Build card takes, assembled from a checkout's build outputs, then used end to end:
// assemble -> build the fixture card -> the card's files equal tools/omsat cardbuild's (the Task 9 golden) and the card
// reads as VibeMenu 0.1. On a synthetic checkout (always) and on this machine's real one (when its build outputs exist).
using VibeMenuCardManager.Cli;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class ReleaseAssemblerTests
{
    /// <summary>A checkout's layout with only what the assembler reads: the menu's outputs in build/release (as
    /// tools/m3_release.sh leaves them), cardbuild's inputs in build/db, the compat tables where the repo keeps them.</summary>
    static string Git(string repo, params string[] a)
    {
        var psi = new System.Diagnostics.ProcessStartInfo("git", ["-C", repo, "-c", "user.name=test", "-c", "user.email=test@example.invalid",
                                                                  "-c", "commit.gpgsign=false", .. a])
            { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
        using var p = System.Diagnostics.Process.Start(psi)!;
        string o = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit();
        Assert.True(p.ExitCode == 0, string.Join(" ", a) + ": " + o);
        return o.Trim();
    }

    /// <summary>A menu image whose build string names commit (as the menu's version.h build writes it).</summary>
    static void Iso(string path, string build)
    {
        var b = File.ReadAllBytes(path).ToList();
        int at = System.Text.Encoding.Latin1.GetString([.. b]).IndexOf("Aether 0.1 test", StringComparison.Ordinal);
        b.RemoveRange(at, "Aether 0.1 test".Length);
        b.InsertRange(at, System.Text.Encoding.ASCII.GetBytes(build));
        File.WriteAllBytes(path, [.. b]);
    }

    /// <summary>A git checkout with menu/ and contract/SPEC.md history; its build/release menu built from HEAD.</summary>
    static string FakeRepo(string dir)
    {
        string repo = Path.Combine(dir, "repo");
        Directory.CreateDirectory(Path.Combine(repo, "contract"));
        Directory.CreateDirectory(Path.Combine(repo, "menu", "src"));
        File.WriteAllText(Path.Combine(repo, "contract", "SPEC.md"), "spec\n");
        File.WriteAllText(Path.Combine(repo, "menu", "src", "main.c"), "int main;\n");
        Git(repo, "init", "-q");
        Git(repo, "add", "-A");
        Git(repo, "commit", "-q", "-m", "menu");
        string head = Git(repo, "rev-parse", "--short=7", "HEAD");
        FakeOutputs(dir);
        Iso(Path.Combine(repo, "build", "release", "autoboot.iso"), $"Aether 0.1 {head} 2026-11-22");
        return repo;
    }

    static void FakeOutputs(string dir)
    {
        string fx = ReleaseFixture.Make(Path.Combine(dir, "fx"));
        void Cp(string from, params string[] to)
        {
            string p = Path.Combine([dir, "repo", .. to]);
            Directory.CreateDirectory(Path.GetDirectoryName(p)!);
            File.Copy(from, p);
        }
        Cp(Path.Combine(fx, "autoboot.iso"), "build", "release", "autoboot.iso");
        Cp(Path.Combine(fx, "LAUNCHER.BIN"), "build", "release", "LAUNCHER.BIN");
        Cp(Path.Combine(fx, "VRTILES.BIN"), "menu", "app", "assets", "VRTILES.BIN");
        Cp(Path.Combine(fx, "GAMES.DB"), "build", "db", "GAMES.DB");
        Cp(Path.Combine(fx, "jp_titles.json"), "build", "db", "jp_titles.json");
        Cp(Repo.P("tools/data/compat/loader_compat.json"), "tools", "data", "compat", "loader_compat.json");
        Cp(Repo.P("tools/data/compat/save_tools.json"), "tools", "data", "compat", "save_tools.json");
        Cp(Repo.P("tools/data/compat/controller_compat.json"), "tools", "data", "compat", "controller_compat.json");
        Directory.CreateDirectory(Path.Combine(dir, "repo", "build", "release", "THEMES", "SAMPLE"));
        File.WriteAllText(Path.Combine(dir, "repo", "build", "release", "THEMES", "SAMPLE", "THEME.INI"), "name=SAMPLE\n");
    }

    static PipelineOptions Opts(string release) => new(release, BuildCheck.Case("covers.zip"), BuildCheck.Case("art_cache"),
        FolderArt: new Dictionary<string, string> { ["Misc"] = BuildCheck.Case("folder_misc.png") },
        ArtPrefer: new Dictionary<string, string> { ["GS-9001"] = "boxtga" });

    [SkippableFact]
    public void Assemble_then_build_gives_the_cardbuild_golden_card()
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout");
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir), rel = Path.Combine(t.Dir, "release");
        var o = new StringWriter();
        var e = new StringWriter();
        Assert.True(Program.Run(["assemble-release", "--repo", repo, "--out", rel], o, e) == 0, e.ToString());
        Assert.Empty(CardPipeline.CheckRelease(rel, compat: true));
        Assert.True(File.Exists(Path.Combine(rel, "THEMES", "SAMPLE", "THEME.INI")));
        Assert.Contains("GAMES.DB <- build/db/GAMES.DB", File.ReadAllText(Path.Combine(rel, "SOURCES.txt")));
        // final re-review I-7: the menu named, and VRTILES.BIN (not in build/release here) warned as a fallback
        string head = Git(repo, "rev-parse", "--short=7", "HEAD");
        Assert.Contains($"menu: Aether 0.1 {head} 2026-11-22", o.ToString());
        Assert.Contains($"# menu: Aether 0.1 {head} 2026-11-22", File.ReadAllText(Path.Combine(rel, "SOURCES.txt")));
        Assert.Contains("warning: VRTILES.BIN is not in", e.ToString());
        Assert.Contains("taken from menu/app/assets/VRTILES.BIN", File.ReadAllText(Path.Combine(rel, "SOURCES.txt")));
        Assert.DoesNotContain("STALE", e.ToString());
        t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card");
        var r = CardPipeline.Run(card, Opts(rel), BuildCheck.Expect().GetProperty("date").GetUInt32(), VolumeResolver.Resolve(card).Identity);
        using var tr = new TitleRenderer();
        // every file and report field as cardbuild's; manifest.csv's folder-art row names the image as passed (absolute
        // here, relative to its folder in the golden), so it is the one line allowed to differ
        var bad = BuildCheck.Compare(r.Build, BuildCheck.Expect(), tr).Where(b => !b.StartsWith("manifest.csv", StringComparison.Ordinal)).ToList();
        var want = BuildCheck.Expect().GetProperty("manifest_csv").GetString()!.Split('\n');
        var got = r.Build.ManifestCsv.Split('\n');
        Assert.Equal(want.Length, got.Length);
        Assert.Single(want.Zip(got).Where(x => x.First != x.Second), x => x.First.Contains("folder_misc.png", StringComparison.Ordinal));
        Assert.True(bad.Count == 0, string.Join("\n", bad));
        Assert.Equal(r.Build.ArtPak, File.ReadAllBytes(Path.Combine(card, "aether", "ART.PAK")));
        Assert.Equal("0.1", CardTarget.Inspect(card, []).VibeMenuVersion);
        Assert.Contains(r.Install.Copied, c => c.Contains("THEMES/SAMPLE", StringComparison.Ordinal));
    }

    [Fact]
    public void A_checkout_without_the_outputs_or_a_used_out_folder_is_refused_before_anything_is_written()
    {
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir), rel = Path.Combine(t.Dir, "release");
        File.Delete(Path.Combine(repo, "build", "db", "jp_titles.json"));
        var e = new StringWriter();
        Assert.Equal(1, Program.Run(["assemble-release", "--repo", repo, "--out", rel], new StringWriter(), e));
        Assert.Contains("missing jp_titles.json", e.ToString());
        Assert.False(Directory.Exists(rel));
        File.WriteAllText(Path.Combine(repo, "build", "db", "jp_titles.json"), "{}");
        File.WriteAllBytes(Path.Combine(repo, "build", "release", "autoboot.iso"), File.ReadAllBytes(Repo.P("contract/fixtures/sysid_gs9001.bin")));
        Assert.Contains("is not an Aether image", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(repo, rel)).Message);
        Assert.False(Directory.Exists(rel));
        Directory.CreateDirectory(rel);
        File.WriteAllText(Path.Combine(rel, "old.txt"), "x");
        Assert.Contains("is not empty", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(FakeRepo(Path.Combine(t.Dir, "b")), rel)).Message);
        Assert.Equal(2, Program.Run(["assemble-release"], new StringWriter(), new StringWriter()));
    }

    /// <summary>Final re-review I-7: a menu built before the newest commit touching menu/ or contract/SPEC.md is refused,
    /// naming its build string and that commit; --allow-stale packages it with a STALE warning; a build string with no
    /// commit, or one this checkout does not have, cannot be checked and is refused the same way. A commit elsewhere
    /// (not menu/ or the SPEC) does not make it stale.</summary>
    [Fact]
    public void A_menu_older_than_the_checkouts_menu_sources_is_refused_unless_allow_stale()
    {
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir);
        string built = Git(repo, "rev-parse", "--short=7", "HEAD");
        File.WriteAllText(Path.Combine(repo, "README"), "docs only\n");
        Git(repo, "add", "README");
        Git(repo, "commit", "-q", "-m", "docs");
        Assert.Equal("Aether 0.1 " + built + " 2026-11-22", ReleaseAssembler.Assemble(repo, Path.Combine(t.Dir, "r0")).Build);   // not stale
        File.WriteAllText(Path.Combine(repo, "menu", "src", "main.c"), "int main2;\n");
        Git(repo, "commit", "-q", "-am", "menu change");
        string newest = Git(repo, "rev-parse", "--short=7", "HEAD");
        var e = new StringWriter();
        Assert.Equal(1, Program.Run(["assemble-release", "--repo", repo, "--out", Path.Combine(t.Dir, "r1")], new StringWriter(), e));
        Assert.Contains("refusing to package a stale menu", e.ToString());
        Assert.Contains($"Aether 0.1 {built} 2026-11-22", e.ToString());
        Assert.Contains(newest, e.ToString());
        Assert.Contains("1 newer commits touch menu/ or contract/SPEC.md", e.ToString());
        Assert.False(Directory.Exists(Path.Combine(t.Dir, "r1")));
        var e2 = new StringWriter();
        Assert.True(Program.Run(["assemble-release", "--repo", repo, "--out", Path.Combine(t.Dir, "r2"), "--allow-stale"], new StringWriter(), e2) == 0, e2.ToString());
        Assert.Contains("warning: STALE MENU:", e2.ToString());
        Assert.Contains("# warning: STALE MENU:", File.ReadAllText(Path.Combine(t.Dir, "r2", "SOURCES.txt")));
        // the SPEC counts too
        File.WriteAllText(Path.Combine(repo, "contract", "SPEC.md"), "spec 2\n");
        Git(repo, "commit", "-q", "-am", "spec");
        Assert.Contains("2 newer commits", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(repo, Path.Combine(t.Dir, "r3"))).Message);
    }

    [Fact]
    public void A_menu_whose_build_commit_cannot_be_checked_is_refused_unless_allow_stale()
    {
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir);
        string iso = Path.Combine(repo, "build", "release", "autoboot.iso");
        var b = File.ReadAllBytes(iso);
        string text = System.Text.Encoding.Latin1.GetString(b);
        int at = text.IndexOf("Aether 0.1 ", StringComparison.Ordinal);
        System.Text.Encoding.ASCII.GetBytes("Aether 0.1 abcdef1").CopyTo(b, at);             // a commit this checkout lacks
        File.WriteAllBytes(iso, b);
        Assert.Contains("which this checkout does not have", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(repo, Path.Combine(t.Dir, "a"))).Message);
        Assert.Contains("STALE MENU", ReleaseAssembler.Assemble(repo, Path.Combine(t.Dir, "b"), allowStale: true).Warnings[0]);
        string plain = Path.Combine(t.Dir, "plain");                                           // not a git checkout at all
        Directory.CreateDirectory(plain);
        FakeOutputs(plain);
        Assert.Contains("cannot be checked", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(Path.Combine(plain, "repo"), Path.Combine(t.Dir, "c"))).Message);
    }

    /// <summary>This machine's checkout, when tools/m3_release.sh has left its outputs: the real release folder builds a
    /// card whose files the menu's formats accept.</summary>
    [SkippableFact]
    public void The_real_checkouts_outputs_assemble_into_a_release_that_builds_a_card()
    {
        Skip.IfNot(File.Exists(Repo.P("build/release/autoboot.iso")) && File.Exists(Repo.P("build/db/GAMES.DB")) &&
                   File.Exists(Repo.P("build/db/jp_titles.json")), "no build outputs in this checkout (run tools/m3_release.sh)");
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout");
        using var t = new TempCard();
        string rel = Path.Combine(t.Dir, "release");
        // I-7: this machine's build/release may hold an old menu: then it is refused by name, and packaged only with
        // allowStale (and a STALE warning)
        AssembledRelease a;
        try { a = ReleaseAssembler.Assemble(Repo.Root, rel); }
        catch (ReleaseException e) when (e.Message.StartsWith("refusing to package a stale menu", StringComparison.Ordinal))
        {
            Assert.Contains(ReleaseAssembler.BuildOf(Repo.P("build/release/autoboot.iso")).Build!, e.Message);
            a = ReleaseAssembler.Assemble(Repo.Root, rel, allowStale: true);
            Assert.Contains(a.Warnings, w => w.StartsWith("STALE MENU:", StringComparison.Ordinal));
        }
        Assert.StartsWith("Aether ", a.Build);
        Assert.StartsWith("AETHER", a.Title);
        Assert.Empty(CardPipeline.CheckRelease(rel, compat: true));
        t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card");
        var r = CardPipeline.Run(card, new PipelineOptions(rel, BuildCheck.Case("covers.zip"), BuildCheck.Case("art_cache")), 20261122,
                                 VolumeResolver.Resolve(card).Identity);
        Assert.Empty(r.Build.Report["check_errors"]!.AsObject());
        var vm = Path.Combine(card, "aether");
        int matched = r.Build.Report["scan"]!["matched"]!.GetValue<int>();        // the real GAMES.DB lacks the fixture's synthetic keys
        Assert.True(matched > 0);
        Assert.Equal(matched, LibraryIndex.Read(File.ReadAllBytes(Path.Combine(vm, "LIBRARY.IDX"))).Entries.Count(x => x.Key != ""));
        GamesDb.Read(File.ReadAllBytes(Path.Combine(vm, "GAMES.DB")));
        ArtPak.Read(File.ReadAllBytes(Path.Combine(vm, "ART.PAK")));
        Assert.Equal(File.ReadAllBytes(Repo.P("build/release/autoboot.iso")), File.ReadAllBytes(Path.Combine(card, "autoboot.iso")));
        Assert.NotNull(CardTarget.Inspect(card, []).VibeMenuVersion);
    }

    /// <summary>The menu and LAUNCHER.BIN are a pair (hand-off block v3+): a launcher only in menu/launcher/out (built
    /// apart from build/release's menu) is never packaged with it.</summary>
    [Fact]
    public void A_launcher_built_apart_from_the_packaged_menu_is_refused()
    {
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir), rel = Path.Combine(t.Dir, "release");
        string lnc = Path.Combine(repo, "menu", "launcher", "out", "LAUNCHER.BIN");
        Directory.CreateDirectory(Path.GetDirectoryName(lnc)!);
        File.Move(Path.Combine(repo, "build", "release", "LAUNCHER.BIN"), lnc);
        Assert.Contains("missing LAUNCHER.BIN", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(repo, rel, allowStale: true)).Message);
        Assert.False(Directory.Exists(rel));
    }

    [Fact]
    public void The_shipped_CHEATS_PAK_is_packaged_and_checked()
    {
        using var t = new TempCard();
        string repo = FakeRepo(t.Dir), rel = Path.Combine(t.Dir, "release");
        string pak = Path.Combine(repo, "tools", "data", "cheats", "CHEATS.PAK");
        Directory.CreateDirectory(Path.GetDirectoryName(pak)!);
        File.WriteAllBytes(pak, [1, 2, 3]);
        Assert.Contains("CHEATS.PAK", Assert.Throws<ReleaseException>(() => ReleaseAssembler.Assemble(repo, rel, allowStale: true)).Message);
        File.Copy(Repo.P("tools/data/cheats/CHEATS.PAK"), pak, overwrite: true);
        var a = ReleaseAssembler.Assemble(repo, rel, allowStale: true);
        Assert.Equal(File.ReadAllBytes(pak), File.ReadAllBytes(Path.Combine(rel, "CHEATS.PAK")));
        Assert.DoesNotContain(a.Warnings, w => w.Contains("CHEATS.PAK", StringComparison.Ordinal));
    }
}
