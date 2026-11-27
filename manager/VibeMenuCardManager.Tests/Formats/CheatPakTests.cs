// VibeMenu Card Manager, GPL-3.0-or-later.
// CHEATS.PAK (contract/SPEC.md §5.15) against tools/omsat/cheatpak.py: the fixture and the shipped file read and
// rewrite byte for byte, identical records are stored once in key order, the card-update merge keeps the user's
// choices (tools/tests/test_cheatpak.py's cases), a card-only record survives a merge as it is, and a record emptied of
// cheats loses its row (the editor's rule).
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Formats;

public class CheatPakTests
{
    static CheatRecord Rec(params Cheat[] cheats) => new(0x06000914, 0xC305, CheatPak.GMaster | CheatPak.GCwx, 0x06002800, 39, [.. cheats]);
    static Cheat Kai(string name, bool on = false) => new(name, on ? CheatPak.COn : (byte)0, [(0x06010000, 1, 1)]);
    static Cheat User(string name) => new(name, CheatPak.COn | CheatPak.CUser, [(0x06030000, 7, 3)]);
    static IEnumerable<(string, bool)> Shape(CheatRecord r) => r.Cheats.Select(c => (c.Name, (c.Flags & CheatPak.COn) != 0));

    [Fact]
    public void The_fixture_reads_back_and_rewrites_byte_for_byte()
    {
        var data = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak"));
        var recs = CheatPak.Read(data);
        Assert.Equal(["GS-9001", "T-8113H", "T-8113H-50"], recs.Keys.Order(StringComparer.Ordinal));
        Assert.Equal([("PLAYER 1", CheatPak.CHeader), ("UNLIMITED LIVES", 0), ("MAX MONEY", CheatPak.COn), ("MY CODE", CheatPak.COn | CheatPak.CUser)],
                     recs["GS-9001"].Cheats.Select(c => (c.Name, (int)c.Flags)));
        Assert.Equal(2, recs["GS-9001"].OnCount);
        Assert.Equal(data, CheatPak.Write(recs, CommonHeader.Unpack(data).BuildDate));
    }

    /// <summary>The shipped file (tools/data/cheats/CHEATS.PAK, built by omsat.kaicheats): every key reads, and the C#
    /// writer gives back the same bytes -- write_pak's layout, sharing and date.</summary>
    [Fact]
    public void The_shipped_file_reads_and_rewrites_byte_for_byte()
    {
        var data = File.ReadAllBytes(Repo.P("tools/data/cheats/CHEATS.PAK"));
        var recs = CheatPak.Read(data);
        Assert.Equal(CheatPak.ReadIndex(data).Count, recs.Count);
        Assert.InRange(recs.Count, 100, 130);           // 111 at e009d69
        Assert.Equal(data, CheatPak.Write(recs));
        Assert.Equal(recs.Count, CheatPak.OnCounts(data).Count);
    }

    [Fact]
    public void Identical_records_are_stored_once_in_key_order_and_an_empty_record_loses_its_row()
    {
        var a = Rec(Kai("A", true), Kai("B"));
        var b = Rec(Kai("C")) with { Location = 0x060FF000 };
        var data = CheatPak.Write(new Dictionary<string, CheatRecord> { ["K2"] = a, ["K1"] = a, ["K3"] = b, ["K0"] = Rec() });
        var idx = CheatPak.ReadIndex(data);
        Assert.Equal(["K1", "K2", "K3"], idx.Select(i => i.Key));
        Assert.Equal(idx[0].Off, idx[1].Off);
        Assert.NotEqual(idx[0].Off, idx[2].Off);
        Assert.Equal(64 + 24 * 3, idx[0].Off);          // the first key's copy is the stored one
        Assert.True(idx.All(i => i.Off % 4 == 0 && i.Len % 4 == 0));
    }

    [Fact]
    public void Bad_files_and_records_are_refused()
    {
        var data = CheatPak.Write(new Dictionary<string, CheatRecord> { ["K1"] = Rec(Kai("A")), ["K2"] = Rec(Kai("B")) });
        var bad = (byte[])data.Clone(); bad[^6] ^= 1;
        Assert.Contains("record CRC", Assert.Throws<FormatException>(() => CheatPak.Read(bad)).Message);
        bad = (byte[])data.Clone(); bad[64 + 3] ^= 1;
        Assert.Contains("table CRC", Assert.Throws<FormatException>(() => CheatPak.Read(bad)).Message);
        Assert.ThrowsAny<Exception>(() => CheatPak.Read(data[..100]));
        Assert.Throws<FormatException>(() => CheatPak.EncodeRecord(Rec(new Cheat("H", CheatPak.CHeader | CheatPak.COn, []))));
        Assert.Throws<FormatException>(() => CheatPak.EncodeRecord(Rec(new Cheat(new string('x', 31), 0, [(0, 0, 0)]))));
        Assert.Throws<FormatException>(() => CheatPak.EncodeRecord(Rec([.. Enumerable.Range(0, 65).Select(i => Kai($"C{i}"))])));
        Assert.Empty(CheatPak.OnCounts(bad));
        Assert.Empty(CheatPak.OnCounts(null));
    }

    [Fact]
    public void A_merge_keeps_the_users_choices_and_card_only_games()
    {
        var cardOnly = new CheatRecord(0x06000914, 0xC305, 0, 0, 8, [User("Kept")]);   // the editor's default master
        var old = CheatPak.Write(new Dictionary<string, CheatRecord>
        {
            ["K1"] = Rec(Kai("Lives", true), Kai("Lives"), Kai("Gone", true), User("Mine")),
            ["OLDONLY"] = cardOnly,
        });
        var nw = CheatPak.Write(new Dictionary<string, CheatRecord>
        {
            ["K1"] = Rec(new Cheat("Group", CheatPak.CHeader, []), Kai("Lives"), Kai("Lives", true), Kai("Fresh", true)),
            ["NEWONLY"] = Rec(Kai("X")),
        });
        var (outb, rep) = CheatPak.Merge(old, nw);
        var r = CheatPak.Read(outb);
        Assert.Equal([("Group", false), ("Lives", true), ("Lives", false), ("Fresh", true), ("Mine", true)], Shape(r["K1"]));
        Assert.True((r["K1"].Cheats[^1].Flags & CheatPak.CUser) != 0);
        Assert.Equal(CheatPak.EncodeRecord(cardOnly), CheatPak.EncodeRecord(r["OLDONLY"]));   // master fields and all
        Assert.Contains("NEWONLY", r.Keys);
        Assert.Equal((1, 1, null), (rep.Merged, rep.KeptCardOnly, rep.OldError));
        Assert.Equal(outb, CheatPak.Merge(outb, nw).Data);                                   // merging again changes nothing
        Assert.Equal(outb, CheatPak.Merge(outb, outb).Data);                                 // user cheats never doubled
    }

    [Fact]
    public void A_merge_with_no_or_a_corrupt_card_copy_gives_the_shipped_file()
    {
        var nw = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak"));
        Assert.Equal(nw, CheatPak.Merge(null, nw).Data);
        var (outb, rep) = CheatPak.Merge("OMSATCHT garbage"u8.ToArray(), nw);
        Assert.Equal(nw, outb);
        Assert.NotNull(rep.OldError);
    }

    [Fact]
    public void A_merge_that_does_not_fit_keeps_the_card_record()
    {
        var old = CheatPak.Write(new Dictionary<string, CheatRecord> { ["K1"] = Rec([.. Enumerable.Range(0, 40).Select(i => User($"U{i}"))]) });
        var nw = CheatPak.Write(new Dictionary<string, CheatRecord> { ["K1"] = Rec([.. Enumerable.Range(0, 30).Select(i => Kai($"C{i}"))]) });
        var (outb, rep) = CheatPak.Merge(old, nw);
        Assert.Equal(["K1"], rep.KeptUnfitted);
        Assert.Equal(CheatPak.EncodeRecord(CheatPak.Read(old)["K1"]), CheatPak.EncodeRecord(CheatPak.Read(outb)["K1"]));
    }

    [Fact]
    public void The_card_copy_is_CHEATS_PAK_else_its_NEW()
    {
        using var t = new TempCard();
        string vm = Path.Combine(t.Dir, "aether");
        Directory.CreateDirectory(vm);
        Assert.Null(CheatPak.ReadCardCopy(vm));
        File.WriteAllBytes(Path.Combine(vm, CheatPak.NewName), [2]);
        Assert.Equal([2], CheatPak.ReadCardCopy(vm));
        File.WriteAllBytes(Path.Combine(vm, CheatPak.Name), [1]);
        Assert.Equal([1], CheatPak.ReadCardCopy(vm));
    }
}
