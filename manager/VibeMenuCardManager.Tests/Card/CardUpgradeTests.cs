// VibeMenu Card Manager, GPL-3.0-or-later.
// An in-place card upgrade (Prepare on a card with an older VibeMenu, after a Card Manager update): the new menu and
// LAUNCHER.BIN go on as a pair, and everything the user made stays -- USEREDIT.INI (a user record and [MATCH]), FAVS,
// PERGAME, CHEATS.PAK (merged, the user's cheat kept), BOX.TGA and FOLDER.PNG, the whole SAVES tree, /menu.bin --
// with the version-to-version migration steps run once.
using System.Security.Cryptography;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class CardUpgradeTests
{
    static byte[] WithBuild(byte[] iso, string build)
    {
        var s = System.Text.Encoding.Latin1.GetString(iso);
        int at = s.IndexOf("Aether 0.1 test", StringComparison.Ordinal), n = "Aether 0.1 test".Length;
        if (at < 0) { at = s.IndexOf("VibeMenu 0.1 test", StringComparison.Ordinal); n = "VibeMenu 0.1 test".Length; }   // a pre-rename card's
        return [.. iso.Take(at), .. System.Text.Encoding.ASCII.GetBytes(build), .. iso.Skip(at + n)];
    }

    static Dictionary<string, string> Tree(string root, Func<string, bool> keep) =>
        Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Select(f => Path.GetRelativePath(root, f).Replace('\\', '/')).Where(keep)
            .ToDictionary(r => r, r => Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(Path.Combine(root, r)))));

    [Fact]
    public void An_upgrade_installs_the_new_pair_and_keeps_every_user_file()
    {
        using var t = new TempCard();
        string card = Path.Combine(t.Dir, "card"), vm = Path.Combine(card, "aether");
        string release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        File.WriteAllBytes(Path.Combine(release, "autoboot.iso"), WithBuild(File.ReadAllBytes(Path.Combine(release, "autoboot.iso")), "Aether 0.2 abc1234 2026-12-01"));
        var shipped = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak"));
        File.WriteAllBytes(Path.Combine(release, "CHEATS.PAK"), shipped);

        // a card set up by 0.1, then used: games, the user's covers and folder art, saves, favourites, edits, cheats
        Directory.CreateDirectory(vm);
        File.WriteAllBytes(Path.Combine(card, "autoboot.iso"), WithBuild(
            [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU", ["product"] = "OMSAT-MENU" }), .. new byte[2048],
             .. "VibeMenu 0.1 test\0"u8.ToArray()], "VibeMenu 0.1 1111111 2026-11-24"));
        File.WriteAllText(Path.Combine(vm, "LAUNCHER.BIN"), "the 0.1 launcher");
        File.Copy(Path.Combine(release, "GAMES.DB"), Path.Combine(vm, "GAMES.DB"));
        File.WriteAllText(Path.Combine(card, "menu.bin"), "the official menu");
        foreach (var (f, p) in new[] { ("Games/Alpha", "T-00001"), ("Games/Homebrew", "T-81") })
        {
            Directory.CreateDirectory(Path.Combine(card, f));
            File.WriteAllBytes(Path.Combine(card, f, "game.iso"), SystemIdReader.Make(new Dictionary<string, string> { ["product"] = p, ["area"] = "U", ["title"] = p, ["device"] = "CD-1/1" }));
        }
        File.WriteAllBytes(Path.Combine(card, "Games/Alpha/BOX.TGA"), Core.Art.TgaEncoder.Encode(new Core.Art.RgbaImage(4, 6, new byte[96])));
        File.WriteAllBytes(Path.Combine(card, "Games/FOLDER.PNG"), [137, 80, 78, 71, 1, 2, 3]);
        File.WriteAllText(Path.Combine(vm, "USEREDIT.INI"), "[U12345678]\ntitle=My Homebrew\nnew=1\n[MATCH]\nGames/Homebrew/game.iso=U12345678\n");
        File.WriteAllText(Path.Combine(vm, "FAVS.TXT"), "Games/Alpha/game.iso\n");
        File.WriteAllText(Path.Combine(vm, "PERGAME.INI"), "[T-00001@U]\nloader=jhl\n");
        Directory.CreateDirectory(Path.Combine(vm, "SAVES", "T-00001@U", "G000", "G00001"));
        File.WriteAllBytes(Path.Combine(vm, "SAVES", "T-00001@U", "G000", "G00001", "SAVE1.BUP"), [9, 9, 9]);
        var recs = CheatPak.Read(shipped);
        recs["T-8113H"].Cheats.Add(new Cheat("My own cheat", CheatPak.COn | CheatPak.CUser, [(0x06030000, 7, 3)]));
        File.WriteAllBytes(Path.Combine(vm, "CHEATS.PAK"), CheatPak.Write(recs));

        bool User(string r) => r == "menu.bin" || r.StartsWith("Games/", StringComparison.Ordinal) || r.StartsWith("aether/SAVES/", StringComparison.Ordinal)
                               || r is "aether/USEREDIT.INI" or "aether/FAVS.TXT" or "aether/PERGAME.INI";
        var before = Tree(card, User);
        var ran = new List<string>();
        var steps = new[]
        {
            new CardMigration("0.0", "0.1", "already on the card", (_, _) => ran.Add("0.1")),
            new CardMigration("0.1", "0.2", "the 0.2 step", (c, log) => { ran.Add("0.2"); log.Add("did it"); }),
            new CardMigration("0.2", "0.3", "a later release's", (_, _) => ran.Add("0.3")),
        };

        var r = CardPipeline.Run(card, new PipelineOptions(release, Compat: false), 20261201, null, "up", steps);

        Assert.Equal(before, Tree(card, User));                                            // every user file, byte for byte
        Assert.Equal(File.ReadAllBytes(Path.Combine(release, "autoboot.iso")), File.ReadAllBytes(Path.Combine(card, "autoboot.iso")));
        Assert.Equal(File.ReadAllBytes(Path.Combine(release, "LAUNCHER.BIN")), File.ReadAllBytes(Path.Combine(vm, "LAUNCHER.BIN")));
        Assert.Equal(CardPipeline.MenuPairState.Current, CardPipeline.MenuPair(card, release).State);
        Assert.Equal("My own cheat", CheatPak.Read(File.ReadAllBytes(Path.Combine(vm, "CHEATS.PAK")))["T-8113H"].Cheats[^1].Name);
        var gdb = GamesDb.Read(File.ReadAllBytes(Path.Combine(vm, "GAMES.DB")));
        Assert.True(gdb.Records.Single(x => x.Key == "U12345678").IsUserTitle);             // the user record survives
        Assert.Equal("U12345678", LibraryIndex.Read(File.ReadAllBytes(Path.Combine(vm, "LIBRARY.IDX"))).Entries.Single(e => e.Path == "Games/Homebrew/game.iso").Key);
        Assert.Equal(["0.2"], ran);                                                          // only the step this upgrade crosses
        Assert.Equal(["0.1 -> 0.2: the 0.2 step", "did it"], r.Migrations);
        Assert.Empty(CardMigrations.Run(card, null, "0.2", steps));                         // a fresh card has nothing to migrate
    }
}
