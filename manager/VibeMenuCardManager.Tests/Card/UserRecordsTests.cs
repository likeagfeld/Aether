// VibeMenu Card Manager, GPL-3.0-or-later.
// "Add to database…" (the user, 2026-11-24): a disc GAMES.DB does not know gets a record of the user's own. Its key
// (the disc's own code, or a synthetic one for homebrew), its survival across a rescan, a Save and a Prepare with a
// newer GAMES.DB, and -- the point of it -- that the console menu finds it: LIBRARY.IDX gives the disc that key, and
// GAMES.DB's sorted table holds it with the user-title flag, which is all om_gamesdb_find and title.c read.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Card;

public class UserRecordsTests
{
    static byte[] Iso(string product, string title) =>
        [.. SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = "U", ["title"] = title, ["device"] = "CD-1/1" }), .. new byte[2048]];

    static void Game(string root, string folder, string product, string title)
    {
        Directory.CreateDirectory(Path.Combine(root, folder));
        File.WriteAllBytes(Path.Combine(root, folder, "game.iso"), Iso(product, title));
    }

    /// <summary>A card with one known game, one retail disc GAMES.DB lacks, two homebrew discs sharing "T-81" and one
    /// with a zero placeholder code.</summary>
    static CardLoad Card(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"),
            GamesDb.Write([new GameRecord { Key = "T-00001", TitleEn = "Alpha" }], [], new Dictionary<string, string>(), 20261124));
        Game(root, "Alpha", "T-00001", "ALPHA");
        Game(root, "Unknown", "T-99901G", "UNKNOWN RETAIL");
        Game(root, "Coup", "T-81", "COUP");
        Game(root, "Utenyaa", "T-81", "UTENYAA");
        Game(root, "Pools", "SG0000000", "SACRED POOLS");
        return CardEditSession.Load(root, 20261124);
    }

    static GameRow Row(CardLoad l, string folder) => l.Rows.Single(r => r.Folder == folder);

    [Fact]
    public void A_real_code_keys_by_itself_and_homebrew_gets_a_synthetic_key_that_never_collides()
    {
        using var t = new TempCard();
        var l = Card(t.Dir);
        Assert.Equal("T-99901G", UserRecords.KeyFor(l, Row(l, "Unknown")));
        string coup = UserRecords.KeyFor(l, Row(l, "Coup")), ute = UserRecords.KeyFor(l, Row(l, "Utenyaa"));
        Assert.Matches("^U[0-9A-F]{8}$", coup);
        Assert.Matches("^U[0-9A-F]{8}$", ute);
        Assert.NotEqual(coup, ute);
        Assert.Matches("^U[0-9A-F]{8}$", UserRecords.KeyFor(l, Row(l, "Pools")));
        Assert.True(UserRecords.IsPlaceholder("SG0000000") && UserRecords.IsPlaceholder("CD0000001") && UserRecords.IsPlaceholder("T-81"));
        Assert.False(UserRecords.IsPlaceholder("T-99901G"));
    }

    /// <summary>om_gamesdb_find: the 64-byte records sorted by their 16-byte key (memcmp), searched by the LIBRARY.IDX
    /// entry's key. This walks the bytes as the menu does, not through GamesDb.Read.</summary>
    static int MenuFind(byte[] gdb, string key)
    {
        var h = CommonHeader.Unpack(gdb);
        var want = CommonHeader.KeyBytes(key);
        int lo = 0, hi = (int)h.Count - 1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            int c = gdb.AsSpan((int)h.TableOff + GamesDb.Rec * mid, 16).SequenceCompareTo(want);
            if (c == 0) return (int)h.TableOff + GamesDb.Rec * mid;
            if (c < 0) lo = mid + 1; else hi = mid - 1;
        }
        return -1;
    }

    [Fact]
    public void A_user_record_reaches_the_menu_and_survives_a_rescan_and_a_newer_games_db()
    {
        using var t = new TempCard();
        var l = Card(t.Dir);
        string coupKey = UserRecords.Add(l, Row(l, "Coup"), new UserEditRecord { Title = "Coup", Genre = "Action", Year = 2025, Players = 4 });
        string retailKey = UserRecords.Add(l, Row(l, "Unknown"), new UserEditRecord { Title = "Unknown Retail", Region = "U" });
        Assert.Throws<ArgumentException>(() => UserRecords.Add(l, Row(l, "Pools"), new UserEditRecord { Title = " " }));
        Assert.True(CardEditSession.HasChanges(l));
        CardEditSession.Apply(new CardFs(t.Dir), l, 20261124, null, CancellationToken.None);

        void MenuSees(string folder, string key, string title)
        {
            var lib = LibraryIndex.Read(File.ReadAllBytes(Path.Combine(t.Dir, "aether", "LIBRARY.IDX")));
            Assert.Equal(key, lib.Entries.Single(e => e.Path == $"{folder}/game.iso").Key);
            var gdb = File.ReadAllBytes(Path.Combine(t.Dir, "aether", "GAMES.DB"));
            Assert.True(MenuFind(gdb, key) >= 0, $"{key} not found the menu's way");
            var rec = GamesDb.Read(gdb).Records.Single(r => r.Key == key);
            Assert.True(rec.IsUserTitle);                                // 0x35 bit0: title.c shows title_en
            Assert.Equal(title, rec.TitleEn);
        }
        MenuSees("Coup", coupKey, "Coup");
        MenuSees("Unknown", retailKey, "Unknown Retail");
        Assert.NotEqual(coupKey, LibraryIndex.Read(File.ReadAllBytes(Path.Combine(t.Dir, "aether", "LIBRARY.IDX")))
                                     .Entries.Single(e => e.Path == "Utenyaa/game.iso").Key);
        var coup = GamesDb.Read(File.ReadAllBytes(Path.Combine(t.Dir, "aether", "GAMES.DB"))).Records.Single(r => r.Key == coupKey);
        Assert.Equal((2025, 4), (coup.Year, coup.Players));

        // a rescan reads it back: the disc keeps its key and its title, and is no longer "not in database"
        var again = CardEditSession.Load(t.Dir, 20261124);
        Assert.Equal(coupKey, Row(again, "Coup").Key);
        Assert.Equal("Coup", Row(again, "Coup").DisplayTitle(again.Db.Records.ToDictionary(r => r.Key), again.UserEdits));
        Assert.False(CardEditSession.HasChanges(again));

        // a newer release's GAMES.DB (no user records in it) is installed by Prepare: the records come back
        string release = ReleaseFixture.Make(Path.Combine(t.Dir, "..", Path.GetFileName(t.Dir) + "-release"));
        try
        {
            CardPipeline.Run(t.Dir, new PipelineOptions(release, Compat: false), 20261125, null, "s");
            MenuSees("Coup", coupKey, "Coup");
            MenuSees("Unknown", retailKey, "Unknown Retail");
        }
        finally { Directory.Delete(release, recursive: true); }
    }
}
