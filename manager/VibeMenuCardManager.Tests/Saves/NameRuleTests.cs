// VibeMenu Card Manager, GPL-3.0-or-later.
// Every case here is ported from the menu's own host tests, so the C and C# rules face the same inputs:
// menu/host-tests/test_gameid.c:28-84, test_pgsplan.c:265-314, test_pgsbak.c:544-554 (at 3a88442).
using System.Text;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public class NameRuleTests
{
    static byte[] B(string s) => Encoding.Latin1.GetBytes(s);
    static byte[] N12(string s) { var b = new byte[12]; Encoding.ASCII.GetBytes(s).CopyTo(b, 0); return b; }

    [Theory]
    [InlineData("MK-81045  ", "U         ", "MK-81045@U")]
    [InlineData("MK-81214  ", "UT        ", "MK-81214@UT")]
    [InlineData("T-4507g   ", "JTUBKAEL  ", "T-4507G@JTUBKAEL")]
    [InlineData("GS-9001   ", "EU        ", "GS-9001@EU")]
    [InlineData("GS-9055 V1", "J         ", "GS-9055@J")]
    [InlineData("GS 9041/A ", "U         ", "GS@U")]
    [InlineData("T-1\0\0\0\0\0\0\0", "J         ", "T-1@J")]
    [InlineData("T-1.2/3_X ", "J         ", "T-1_2_3_X@J")]
    [InlineData("AB\x80" + "C      ", "J         ", "AB_C@J")]
    [InlineData("T-1       ", "      E   ", "T-1@E")]
    [InlineData("T-1       ", "  U       ", "T-1@U")]
    [InlineData("T-1       ", "1jE-      ", "T-1@E")]
    [InlineData("T-1       ", "J\0U       ", "T-1@J")]
    [InlineData("T-1       ", "JTUBKAELJT", "T-1@JTUBKAEL")]
    [InlineData("MK81045-50", "          ", "MK81045-50@")]
    [InlineData("ABCDEFGHIJ", "JTUBKAEL  ", "ABCDEFGHIJ@JTUBKAEL")]
    public void GameId_ini_matches_test_gameid_c(string product, string areas, string want)
        => Assert.Equal(want, GameId.Ini(B(product), B(areas)));

    [Theory]
    [InlineData("MK-81045@U", "MK-81045[U]")]
    [InlineData("MK-81214@UT", "MK-81214[UT]")]
    [InlineData("T-4507G@JTUBKAEL", "T-4507G[JTUBKAEL]")]
    [InlineData("MK81045-50@", "MK81045-50")]
    [InlineData("ABCDEFGHIJ@JTUBKAEL", "ABCDEFGHIJ[JTUBKAEL]")]
    [InlineData("NOAT", null)]
    [InlineData("@U", null)]
    [InlineData("A@1", null)]
    [InlineData("A@B@C", null)]
    [InlineData("a@U", null)]
    [InlineData("ABCDEFGHIJK@U", null)]
    [InlineData("A@JTUBKAELJ", null)]
    [InlineData("MK.1@U", null)]
    [InlineData("", null)]
    public void GameId_dir_matches_test_gameid_c(string id, string? want) => Assert.Equal(want, GameId.Dir(id));

    [Fact]
    public void GameId_empty_or_leading_space_product_has_no_id()
    {
        Assert.Null(GameId.Ini(B("          "), B("J         ")));
        Assert.Null(GameId.Ini(B(" T-1      "), B("J         ")));
    }

    [Fact]
    public void GameId_of_the_real_GS9001_header_ignores_the_disc_number()
    {
        var s1 = File.ReadAllBytes(Repo.P("contract/fixtures/sysid_gs9001.bin"));
        var s2 = (byte[])s1.Clone();
        Encoding.ASCII.GetBytes("CD-2/2  ").CopyTo(s2, 0x38);
        Assert.Equal("GS-9001@JTUE", GameId.Ini(s1.AsSpan(0x20, 10), s1.AsSpan(0x40, 10)));
        Assert.Equal("GS-9001@JTUE", GameId.Ini(s2.AsSpan(0x20, 10), s2.AsSpan(0x40, 10)));
    }

    [Theory]
    [InlineData("VIBEMENU_PG", true)]
    [InlineData("PSEUDO__SAT", true)]
    [InlineData("PSEUDO_XTRA", true)]
    [InlineData("PSEUDO_BRAM", true)]
    [InlineData("_CARTDMPHDR", true)]
    [InlineData("_CARTDMPDAT", true)]
    [InlineData("SAVRING_001", true)]
    [InlineData("GHEROES_001", false)]
    [InlineData("PSEUDO", false)]
    [InlineData("VIBEMENU", false)]
    public void Reserved_matches_test_pgsplan_c(string name, bool want) => Assert.Equal(want, SaveNames.Reserved(N12(name)));

    [Fact]
    public void FileName_matches_test_pgsplan_c()
    {
        Assert.Equal("SWAGMAN_DAT.BUP", SaveNames.FileName(N12("SWAGMAN.DAT"), []));
        Assert.Equal("SWAGMAN_DAT~1.BUP", SaveNames.FileName(N12("SWAGMAN_DAT"), ["swagman_dat.bup"]));
        Assert.Equal("G_____.BUP", SaveNames.FileName(N12("Gs a*b"), []));
        Assert.Equal("MK-81214_00.BUP", SaveNames.FileName(N12("MK-81214_00"), []));
        var taken = new List<string> { "A.BUP" };
        for (int i = 1; i <= 9; i++) taken.Add($"A~{i}.BUP");
        Assert.Equal("A~9.BUP", SaveNames.FileName(N12("A"), taken.Take(9).ToList()));
        Assert.Null(SaveNames.FileName(N12("A"), taken));
        Assert.Null(SaveNames.FileName(N12(""), []));
    }

    [Fact]
    public void Slot_and_product_dirs()
    {
        Assert.Equal("SAVES/MK-81214[UT]", SaveNames.SlotDir("MK-81214@UT", 0));
        Assert.Equal("SAVES/MK-81214[UT]/03", SaveNames.SlotDir("MK-81214@UT", 3));
        Assert.Equal("SAVES/MK-81214/09", SaveNames.SlotDir("MK-81214@UT", 9, productOnly: true));
        Assert.Null(SaveNames.SlotDir("MK-81214@UT", 10));
        Assert.Equal("SAVES/MK-81214", SaveNames.ProductDir("MK-81214@UT"));
        Assert.Null(SaveNames.ProductDir("MK-81214@"));
    }

    [Theory]
    [InlineData("MK-81214[UT]", "MK-81214[UT]")]
    [InlineData("mk-81214", "MK-81214")]
    [InlineData("T.1234", "T_1234")]
    [InlineData("_BACKUP", null)]
    [InlineData("[SLOT01]", null)]
    [InlineData("ABCDEFGHIJK", null)]
    [InlineData("MK-1[ut]", null)]
    [InlineData("MK-1[ABCDEFGHI]", null)]
    [InlineData("T-1 ", "T-1")]
    [InlineData("T 1", "T_1")]
    [InlineData("MK-1[]", null)]
    [InlineData("MK-1[U]x", null)]
    public void ImportFolder_matches_test_pgsbak_c(string name, string? want) => Assert.Equal(want, ImportFolders.VibeFolder(name));

    [Theory]
    [InlineData("[SLOT03]", 3)]
    [InlineData("[SLOT12]", 12)]
    [InlineData("[SLOT3]", 0)]
    [InlineData("[slot03]", 0)]
    [InlineData("[SLOT03]x", 0)]
    public void RingsSlot_matches_test_pgsbak_c(string name, int want) => Assert.Equal(want, ImportFolders.RingsSlot(name));
}
