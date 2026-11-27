// VibeMenu Card Manager, GPL-3.0-or-later.
// The installer's CHEATS.PAK (SPEC §5.15: merged with the card's copy in the same transaction, an unreadable copy kept
// as CHEATS.BAK) and the menu/LAUNCHER.BIN pair (one transaction; CardPipeline.MenuPair tells a matched pair from one
// that needs the update). Temp-dir cards only.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Install;
using VibeMenuCardManager.Tests.Card;

namespace VibeMenuCardManager.Tests.Install;

public class CheatsAndPairInstallTests
{
    static readonly byte[] Shipped = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak"));

    static (TempCard T, InstallSources Src, string Target, string Vm) Setup()
    {
        var (t, src, target) = InstallCheck.Realize("fresh");
        string pak = Path.Combine(t.Dir, "src", "CHEATS.PAK");
        File.WriteAllBytes(pak, Shipped);
        string vm = Path.Combine(target, "aether");
        Directory.CreateDirectory(vm);
        return (t, src with { Cheats = pak }, target, vm);
    }

    [Fact]
    public void CHEATS_PAK_goes_on_merged_with_the_cards_copy()
    {
        var (t, src, target, vm) = Setup();
        using (t)
        {
            var recs = CheatPak.Read(Shipped);
            recs["T-8113H"].Cheats.Add(new Cheat("Card only", CheatPak.COn | CheatPak.CUser, [(0x06030000, 7, 3)]));
            File.WriteAllBytes(Path.Combine(vm, "CHEATS.PAK"), CheatPak.Write(recs));
            var rep = Installer.Install(target, src, "s1");
            Assert.Contains("CHEATS.PAK", rep.Copied);
            Assert.Equal(3, rep.Cheats!.Merged);
            Assert.Equal("Card only", CheatPak.Read(File.ReadAllBytes(Path.Combine(vm, "CHEATS.PAK")))["T-8113H"].Cheats[^1].Name);
            Assert.False(File.Exists(Path.Combine(vm, "CHEATS.BAK")));
            var once = File.ReadAllBytes(Path.Combine(vm, "CHEATS.PAK"));
            Installer.Install(target, src, "s2");
            Assert.Equal(once, File.ReadAllBytes(Path.Combine(vm, "CHEATS.PAK")));   // installing again changes nothing
        }
    }

    [Fact]
    public void An_unreadable_card_copy_is_kept_as_CHEATS_BAK()
    {
        var (t, src, target, vm) = Setup();
        using (t)
        {
            File.WriteAllBytes(Path.Combine(vm, "CHEATS.PAK"), "not a pak"u8.ToArray());
            var rep = Installer.Install(target, src, "s1");
            Assert.NotNull(rep.Cheats!.OldError);
            Assert.Equal(Shipped, File.ReadAllBytes(Path.Combine(vm, "CHEATS.PAK")));
            Assert.Equal("not a pak"u8.ToArray(), File.ReadAllBytes(Path.Combine(vm, "CHEATS.BAK")));
        }
    }

    /// <summary>The pair rule: whichever rename fails -- the menu's, LAUNCHER.BIN's or CHEATS.PAK's -- the card keeps
    /// its old menu AND its old launcher (and its cheats), never one new and one old.</summary>
    [Theory]
    [InlineData("autoboot.iso")]
    [InlineData("LAUNCHER.BIN")]
    [InlineData("CHEATS.PAK")]
    public void A_failed_install_never_leaves_a_new_menu_with_an_old_launcher(string failAt)
    {
        var (t, src, target, vm) = Setup();
        using (t)
        {
            File.WriteAllText(Path.Combine(target, "autoboot.iso"), "old menu");
            File.WriteAllText(Path.Combine(vm, "LAUNCHER.BIN"), "old launcher");
            File.WriteAllBytes(Path.Combine(vm, "CHEATS.PAK"), "old cheats"u8.ToArray());
            var inst = new Installer
            {
                BeforeRename = (a, b) =>
                {
                    if (a.EndsWith(Installer.StageSuffix, StringComparison.Ordinal) && Path.GetFileName(b) == failAt)
                        throw new IOException("injected");
                },
            };
            Assert.Contains("card restored", Assert.Throws<InstallException>(() => inst.Run(target, src, "s")).Message);
            Assert.Equal("old menu", File.ReadAllText(Path.Combine(target, "autoboot.iso")));
            Assert.Equal("old launcher", File.ReadAllText(Path.Combine(vm, "LAUNCHER.BIN")));
            Assert.Equal("old cheats", File.ReadAllText(Path.Combine(vm, "CHEATS.PAK")));
            Assert.False(File.Exists(Path.Combine(vm, "CHEATS.BAK")));
        }
    }

    [Fact]
    public void MenuPair_tells_a_matched_pair_from_one_that_needs_the_update()
    {
        using var t = new TempCard();
        string release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        string card = Path.Combine(t.Dir, "card"), vm = Path.Combine(card, "aether");
        Directory.CreateDirectory(vm);
        Assert.Equal(CardPipeline.MenuPairState.NotInstalled, CardPipeline.MenuPair(card, release).State);
        File.WriteAllText(Path.Combine(card, "autoboot.iso"), "Satiator Rings");
        Assert.Equal(CardPipeline.MenuPairState.NotInstalled, CardPipeline.MenuPair(card, release).State);
        File.Copy(Path.Combine(release, "autoboot.iso"), Path.Combine(card, "autoboot.iso"), overwrite: true);
        var (s, why) = CardPipeline.MenuPair(card, release);
        Assert.Equal(CardPipeline.MenuPairState.Mismatched, s);
        Assert.Contains("no /aether/LAUNCHER.BIN", why);
        File.WriteAllText(Path.Combine(vm, "LAUNCHER.BIN"), "an older launcher");
        Assert.Contains("LAUNCHER.BIN is not the one built with its menu", CardPipeline.MenuPair(card, release).Reason);
        File.Copy(Path.Combine(release, "LAUNCHER.BIN"), Path.Combine(vm, "LAUNCHER.BIN"), overwrite: true);
        // the user's card: menu and launcher current, but a GAMES.DB from an older release, so games stay "Not in database"
        var ship = GamesDb.Read(File.ReadAllBytes(Path.Combine(release, "GAMES.DB")));
        File.WriteAllBytes(Path.Combine(vm, "GAMES.DB"), GamesDb.Write(ship.Records.Skip(1), ship.Genres, ship.Aliases, ship.BuildDate));
        Assert.Contains("GAMES.DB", CardPipeline.MenuPair(card, release).Reason);
        File.WriteAllBytes(Path.Combine(vm, "GAMES.DB"), GamesDb.Write(ship.Records, ship.Genres, ship.Aliases, ship.BuildDate - 1));
        Assert.Contains("older than this release", CardPipeline.MenuPair(card, release).Reason);
        File.Copy(Path.Combine(release, "GAMES.DB"), Path.Combine(vm, "GAMES.DB"), overwrite: true);
        Assert.Equal((CardPipeline.MenuPairState.Current, (string?)null), CardPipeline.MenuPair(card, release));
    }

    [Fact]
    public void A_build_installs_the_releases_CHEATS_PAK_and_the_card_reads_its_cheats_on()
    {
        using var t = new TempCard();
        t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card");
        string release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        File.WriteAllBytes(Path.Combine(release, "CHEATS.PAK"), Shipped);
        var r = CardPipeline.Run(card, new PipelineOptions(release, null, null, false), 20261122, null, "s");
        Assert.Contains("CHEATS.PAK", r.Install.Copied);
        var load = CardEditSession.Load(card, 20261122);
        Assert.Equal(CheatPak.OnCounts(Shipped), load.CheatsOn);
        Assert.Equal(2, load.CheatsOn["GS-9001"]);
    }
}
