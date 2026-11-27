// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Formats;

public class GamesDbTests
{
    static byte[] Tiny() => File.ReadAllBytes(Repo.P("contract/fixtures/tiny.games.db"));

    static byte[] RoundTrip(byte[] b)
    {
        var d = GamesDb.Read(b);
        return GamesDb.Write(d.Records, d.Genres, d.Aliases, d.BuildDate);
    }

    [Fact]
    public void Tiny_games_db_reads_and_writes_back_byte_for_byte()
    {
        var b = Tiny();
        var d = GamesDb.Read(b);
        Assert.Equal(4, d.Records.Count);
        var gs = d.Records.Single(r => r.Key == "GS-9001");
        Assert.Equal(("Daytona USA", "SEGA", 1, 3, "JHL loader required", 0x04), (gs.TitleEn, gs.Maker, gs.LoaderHint, gs.CompatFlags, gs.CompatNote, gs.CompatRegions));
        Assert.Equal("GS-7103", d.Aliases["610-6342"]);
        Assert.Equal(b, RoundTrip(b));
    }

    /// <summary>The full card DB this machine built (build/db/GAMES.DB), when present: 2,112 records and 28 aliases --
    /// the sheet plus the libretro DAT's 183 (coverproject.py --libretro-dat; without it those are silently dropped).</summary>
    [SkippableFact]
    public void A_full_games_db_round_trips_when_this_machine_has_one()
    {
        var p = Repo.P("build/db/GAMES.DB");
        Skip.IfNot(File.Exists(p), "build/db/GAMES.DB is not on this checkout (a build artifact)");
        var b = File.ReadAllBytes(p);
        Assert.True(b.Length <= GamesDb.ConsoleMax, $"{b.Length} B over the console's {GamesDb.ConsoleMax}");
        var db = GamesDb.Read(b);
        Assert.True(db.Records.Count == 2112, $"{db.Records.Count} records, not 2,112: was it built with --libretro-dat build/thirdparty/libretro-saturn-redump.dat?");
        Assert.Equal(28, db.Aliases.Count);
        Assert.Equal(b, RoundTrip(b));
    }

    [Fact]
    public void FillDates_equals_omsat_gamesdb_fill_dates()
    {
        var x = JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/gamesdb_fill.json"))).RootElement;
        var dates = x.GetProperty("dates").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetUInt32());
        var (o, n) = GamesDb.FillDates(Tiny(), dates);
        Assert.Equal(x.GetProperty("filled").GetInt32(), n);
        Assert.Equal(x.GetProperty("len").GetInt32(), o.Length);
        Assert.Equal(x.GetProperty("sha256").GetString(), Convert.ToHexStringLower(SHA256.HashData(o)));
    }

    [Fact]
    public void Nothing_to_fill_returns_the_input_unchanged()
    {
        var b = Tiny();
        var (o, n) = GamesDb.FillDates(b, new Dictionary<string, uint> { ["GS-9001"] = 19990101 });
        Assert.Equal(0, n);
        Assert.Same(b, o);
    }

    [Fact]
    public void FillDates_leaves_a_record_with_a_year_alone()
    {
        var recs = new[] { new GameRecord { Key = "A-1", TitleEn = "A", Year = 1996 }, new GameRecord { Key = "B-1", TitleEn = "B" } };
        var b = GamesDb.Write(recs, ["X"], new Dictionary<string, string>(), 20261122);
        var (o, n) = GamesDb.FillDates(b, new Dictionary<string, uint> { ["A-1"] = 19970101, ["B-1"] = 19980202 });
        Assert.Equal(1, n);
        var d = GamesDb.Read(o).Records.ToDictionary(r => r.Key);
        Assert.Equal((1996, 0u), (d["A-1"].Year, d["A-1"].Date));
        Assert.Equal((1998, 19980202u), (d["B-1"].Year, d["B-1"].Date));
    }

    [Fact]
    public void Write_sorts_records_and_aliases_by_key_bytes()
    {
        var recs = new[] { new GameRecord { Key = "T-2", TitleEn = "b" }, new GameRecord { Key = "GS-1", TitleEn = "a" } };
        var aliases = new Dictionary<string, string> { ["Z9"] = "T-2", ["A1"] = "GS-1" };
        var d = GamesDb.Read(GamesDb.Write(recs, [], aliases, 1));
        Assert.Equal(["GS-1", "T-2"], d.Records.Select(r => r.Key));
        Assert.Equal(["A1", "Z9"], d.Aliases.Keys);
    }

    [Theory]
    [InlineData("JTUE", 0b01111)]
    [InlineData("UB", 0b10100)]
    [InlineData("", 0)]
    public void RegionFlags(string a, int want) => Assert.Equal(want, GamesDb.RegionFlagsFrom(a));

    [Fact]
    public void AsciiFold_drops_accents_and_marks_the_rest()
    {
        Assert.Equal("Pokemon", GamesDb.AsciiFold("Pokémon"));
        Assert.Equal("??", GamesDb.AsciiFold("日本"));
    }

    [Fact]
    public void Readers_reject_corruption()
    {
        var b = Tiny();
        var crc = (byte[])b.Clone(); crc[64 + 20] ^= 1;                 // a table byte: CRC mismatch
        Assert.Contains("CRC", Assert.Throws<FormatException>(() => GamesDb.Read(crc)).Message);
        var mag = (byte[])b.Clone(); mag[0] = (byte)'X';
        Assert.Contains("magic", Assert.Throws<FormatException>(() => GamesDb.Read(mag)).Message);
        Assert.Contains("range", Assert.Throws<FormatException>(() => GamesDb.Read(b[..^10])).Message);
        var ver = (byte[])b.Clone(); ver[9] = 2;
        Assert.Contains("version", Assert.Throws<FormatException>(() => GamesDb.Read(ver)).Message);
    }
}
