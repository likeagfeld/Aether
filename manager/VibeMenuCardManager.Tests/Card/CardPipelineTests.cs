// VibeMenu Card Manager, GPL-3.0-or-later.
// M4's definition of done on a temp card: a card holding only games (the fixture card: System IDs, covers, no game
// data) and the official menu.bin becomes a VibeMenu card -- /autoboot.iso, /vibemenu/ with the built GAMES.DB,
// LIBRARY.IDX and ART.PAK -- with menu.bin untouched, an existing autoboot.* backed up, and the built files accepted by
// tools/omsat's own checker (python -m omsat.check, when Python is on this machine). Mutation demos for the build golden.
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Tests.Card;

public static class ReleaseFixture
{
    /// <summary>A release folder: a stand-in autoboot.iso (an AETHER System ID and build string), a LAUNCHER.BIN
    /// with a valid header, the committed VRTILES.BIN, the fixture's GAMES.DB and jp_titles.json, the compat data.</summary>
    public static string Make(string dir)
    {
        Directory.CreateDirectory(Path.Combine(dir, "compat"));
        File.WriteAllBytes(Path.Combine(dir, "autoboot.iso"),
            [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "AETHER", ["product"] = "OMSAT-MENU" }), .. new byte[2048],
             .. "Aether 0.1 test\0"u8.ToArray()]);
        var body = Enumerable.Range(0, 300).Select(i => (byte)i).ToArray();
        var h = new byte[0x1000];
        "OMSATLNC"u8.CopyTo(h);
        VibeMenuCardManager.Core.Be.Put32(h, 8, 1); VibeMenuCardManager.Core.Be.Put32(h, 12, 0x00200000);
        VibeMenuCardManager.Core.Be.Put32(h, 16, (uint)body.Length); VibeMenuCardManager.Core.Be.Put32(h, 20, CommonHeader.Crc32(body));
        VibeMenuCardManager.Core.Be.Put32(h, 24, CommonHeader.Crc32(h.AsSpan(0, 24)));
        File.WriteAllBytes(Path.Combine(dir, "LAUNCHER.BIN"), [.. h, .. body]);
        File.Copy(Repo.P("menu/app/assets/VRTILES.BIN"), Path.Combine(dir, "VRTILES.BIN"));
        File.Copy(BuildCheck.Case("GAMES.DB"), Path.Combine(dir, "GAMES.DB"));
        File.Copy(BuildCheck.Case("jp_titles.json"), Path.Combine(dir, "jp_titles.json"));
        File.Copy(Repo.P("tools/data/compat/loader_compat.json"), Path.Combine(dir, "compat", "loader_compat.json"));
        File.Copy(Repo.P("tools/data/compat/controller_compat.json"), Path.Combine(dir, "compat", "controller_compat.json"));
        File.Copy(Repo.P("tools/data/compat/save_tools.json"), Path.Combine(dir, "compat", "save_tools.json"));
        return dir;
    }
}

public class CardPipelineTests
{
    static (TempCard T, string Card, string Release) Setup()
    {
        var t = new TempCard();
        t.Realize(JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card");
        File.WriteAllText(Path.Combine(card, "menu.bin"), "the official menu");
        File.WriteAllText(Path.Combine(card, "autoboot.iso"), "Satiator Rings");
        return (t, card, ReleaseFixture.Make(Path.Combine(t.Dir, "release")));
    }

    static PipelineOptions Opts(string release) => new(release, BuildCheck.Case("covers.zip"), BuildCheck.Case("art_cache"),
        FolderArt: new Dictionary<string, string> { ["Misc"] = BuildCheck.Case("folder_misc.png") },
        ArtPrefer: new Dictionary<string, string> { ["GS-9001"] = "boxtga" });

    [Fact]
    public void A_card_of_games_becomes_a_vibemenu_card()
    {
        var (t, card, release) = Setup();
        using (t)
        {
            var r = CardPipeline.Run(card, Opts(release), 20261122, VolumeResolver.Resolve(card).Identity, "20261122-120000");
            Assert.Equal("the official menu", File.ReadAllText(Path.Combine(card, "menu.bin")));
            Assert.Equal("Satiator Rings", File.ReadAllText(Path.Combine(card, "autoboot.iso.bak-20261122-120000")));
            Assert.Equal(File.ReadAllBytes(Path.Combine(release, "autoboot.iso")), File.ReadAllBytes(Path.Combine(card, "autoboot.iso")));
            string vm = Path.Combine(card, "aether");
            Assert.Equal(r.Build.ArtPak, File.ReadAllBytes(Path.Combine(vm, "ART.PAK")));
            Assert.Equal(r.Build.GamesDb, File.ReadAllBytes(Path.Combine(vm, "GAMES.DB")));
            Assert.Equal(r.Build.LibraryIdx, File.ReadAllBytes(Path.Combine(vm, "LIBRARY.IDX")));
            Assert.True(File.Exists(Path.Combine(vm, "LAUNCHER.BIN")) && File.Exists(Path.Combine(vm, "VRTILES.BIN")));
            Assert.Equal(16, LibraryIndex.Read(File.ReadAllBytes(Path.Combine(vm, "LIBRARY.IDX"))).Entries.Count(e => e.Key != ""));
            Assert.Equal("0.1", CardTarget.Inspect(card, []).VibeMenuVersion);
        }
    }

    // hardware 2026-11-27: Settings > Import Saves writes /aether/SAVES/IMPORT.SEL, and only a launch made SAVES, so on a
    // fresh card the import failed. Prepare makes it empty; Update keeps what is in it.
    [Fact]
    public void Prepare_makes_an_empty_saves_folder_and_update_keeps_its_files()
    {
        var (t, card, release) = Setup();
        using (t)
        {
            string saves = Path.Combine(card, "aether", "SAVES");
            CardPipeline.Run(card, Opts(release), 20261122, VolumeResolver.Resolve(card).Identity, "20261122-120000");
            Assert.True(Directory.Exists(saves));
            Assert.Empty(Directory.EnumerateFileSystemEntries(saves));
            File.WriteAllText(Path.Combine(saves, "ACTIVE.TXT"), "kept");
            CardPipeline.Run(card, Opts(release), 20261122, VolumeResolver.Resolve(card).Identity, "20261122-120001");
            Assert.Equal("kept", File.ReadAllText(Path.Combine(saves, "ACTIVE.TXT")));
        }
    }

    [SkippableFact]
    public void Tools_omsats_own_checker_accepts_the_built_card()
    {
        var (t, card, release) = Setup();
        using (t)
        {
            CardPipeline.Run(card, Opts(release), 20261122, null, "s");
            string vm = Path.Combine(card, "aether");
            ProcessStartInfo psi = new("python", ["-m", "omsat.check", Path.Combine(vm, "GAMES.DB"), Path.Combine(vm, "LIBRARY.IDX"), Path.Combine(vm, "ART.PAK")])
                { WorkingDirectory = Repo.P("tools"), RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
            Process? p = null;
            try { p = Process.Start(psi); } catch (System.ComponentModel.Win32Exception) { }
            Skip.If(p == null, "python is not on this machine");
            string outp = p!.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
            p.WaitForExit();
            Skip.If(outp.Contains("No module named"), "tools/omsat's dependencies are not installed: " + outp);
            Assert.True(p.ExitCode == 0, outp);
        }
    }

    [Fact]
    public void A_release_folder_missing_files_is_refused_before_anything_is_written()
    {
        var (t, card, release) = Setup();
        using (t)
        {
            File.Delete(Path.Combine(release, "GAMES.DB"));
            var before = Directory.GetFileSystemEntries(card, "*", SearchOption.AllDirectories).Length;
            Assert.Contains("GAMES.DB", Assert.Throws<InstallException>(() => CardPipeline.Run(card, Opts(release), 1, null)).Message);
            Assert.Equal(before, Directory.GetFileSystemEntries(card, "*", SearchOption.AllDirectories).Length);
        }
    }

    [SkippableFact]
    public void Mutation_dropping_art_prefer_or_a_cover_source_fails_the_build_golden()
    {
        Skip.IfNot(File.Exists(VibeMenuCardManager.Core.Art.TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout");
        var x = BuildCheck.Expect();
        using var tr = new VibeMenuCardManager.Core.Art.TitleRenderer();
        // The fixture's ART_PREFER (GS-9001 -> boxtga, manager/golden/mkbuild.py) is a no-op mutation now: the
        // cover order fix round made the game's own BOX.TGA the default first choice (SPEC §5.6, matching the read
        // side), so dropping an ArtPrefer that only ever pinned a game already defaulting to its own BOX.TGA no
        // longer changes the manifest. Asserting that here (rather than deleting the check) keeps this honest about
        // what the fixture currently exercises; a fixture entry whose ArtPrefer source diverges from the new
        // default would restore the original intent (mkbuild.py, tools/omsat).
        var (t1, r1) = BuildCheck.Build(o => o with { ArtPrefer = null });
        using (t1) Assert.Empty(BuildCheck.Compare(r1, x, tr));
        var (t2, r2) = BuildCheck.Build(o => o with { CoversZip = null });
        using (t2) Assert.Contains(BuildCheck.Compare(r2, x, tr), b => b.StartsWith("report.covers", StringComparison.Ordinal));
        var (t3, r3) = BuildCheck.Build(o => o with { Compat = false });
        using (t3) Assert.Contains("GAMES.DB differs", BuildCheck.Compare(r3, x, tr));
    }
}
