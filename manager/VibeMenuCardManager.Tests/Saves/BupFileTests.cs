// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public class BupFileTests
{
    [Fact]
    public void Corpus_decode_matches_omsat_bupfile_on_all_326_headers()
    {
        var rows = CorpusCheck.LoadRows();
        var (expect, _) = CorpusCheck.LoadExpect();
        Assert.Equal(326, rows.Count);
        var bad = CorpusCheck.CompareDecode(rows, expect);
        Assert.True(bad.Count == 0, $"{bad.Count} of {rows.Count} rows differ:\n" + string.Join("\n", bad.Take(20)));
    }

    [Fact]
    public void Corpus_prefer_matches_omsat_bupfile_on_every_duplicate_pair()
    {
        var rows = CorpusCheck.LoadRows();
        var (_, pairs) = CorpusCheck.LoadExpect();
        Assert.Equal(10, pairs.Count);        // 5 same-folder pairs (DAYTONA__00 1, PANDRA_ZWEI 1, POLICENAUTS 3), both orders
        var bad = CorpusCheck.ComparePrefer(rows, pairs);
        Assert.True(bad.Count == 0, $"{bad.Count} of {pairs.Count} pairs differ:\n" + string.Join("\n", bad));
    }

    [Fact]
    public void Spec_example_DAYTONA__00_wins_over_Daytona__01()
    {
        var rows = CorpusCheck.LoadRows().ToDictionary(r => r.Path);
        var a = rows["vbt/20200620/DAYTONA__00.BUP"];
        var b = rows["vbt/20200620/Daytona__01.BUP"];
        Assert.True(BupFile.Prefer(a.Hdr, "DAYTONA__00.BUP", b.Hdr, "Daytona__01.BUP"));
        Assert.False(BupFile.Prefer(b.Hdr, "Daytona__01.BUP", a.Hdr, "DAYTONA__00.BUP"));
    }

    [Fact]
    public void Save_Game_Copier_dot_at_0x1B_is_not_part_of_the_name()
    {
        var rows = CorpusCheck.LoadRows().Where(r => r.Hdr[0x1B] == (byte)'.').ToList();
        Assert.Equal(39, rows.Count);          // SPEC §5.16: 39 of the 326 files
        foreach (var r in rows)
        {
            Assert.Equal(BupError.None, BupFile.TryDecode(r.Hdr, (uint)r.Len, out var d));
            Assert.Equal(11, d!.Name.Length);
        }
    }

    internal static byte[] Hdr(string name, uint datasize, uint date = 1, uint saveId = 0)
    {
        var h = new byte[64];
        "Vmem"u8.CopyTo(h);
        Be.Put32(h, 0x04, saveId);
        System.Text.Encoding.ASCII.GetBytes(name).CopyTo(h, 0x10);
        Be.Put32(h, 0x28, date);
        Be.Put32(h, 0x2C, datasize);
        return h;
    }

    [Fact]
    public void Decode_rejections_follow_bupfile_c()
    {
        var h = Hdr("ABC", 10);
        Assert.Equal(BupError.Short, BupFile.TryDecode(h.AsSpan(0, 63), 63, out _));
        Assert.Equal(BupError.Short, BupFile.TryDecode(h, 73, out _));          // 64 + 10 needed
        Assert.Equal(BupError.None, BupFile.TryDecode(h, 74, out var d));
        Assert.Equal("ABC"u8.ToArray(), d!.Name);
        var m = (byte[])h.Clone(); m[0] = (byte)'X';
        Assert.Equal(BupError.Magic, BupFile.TryDecode(m, 74, out _));
        var e = (byte[])h.Clone(); e[0x10] = 0;
        Assert.Equal(BupError.Field, BupFile.TryDecode(e, 74, out _));
        var u = (byte[])h.Clone(); u[0x11] = 0x7F;
        Assert.Equal(BupError.Field, BupFile.TryDecode(u, 74, out _));
        var big = Hdr("ABC", 0xFFFFFFF0);                                        // no 64 + size wrap
        Assert.Equal(BupError.Short, BupFile.TryDecode(big, 100, out _));
    }

    [Fact]
    public void Prefer_order_is_date_then_save_id_then_own_name_then_bytes()
    {
        Assert.True(BupFile.Prefer(Hdr("R", 0, date: 2), "z.BUP", Hdr("R", 0, date: 1), "R.BUP"));
        Assert.True(BupFile.Prefer(Hdr("R", 0, saveId: 2), "z.BUP", Hdr("R", 0, saveId: 1), "R.BUP"));
        Assert.True(BupFile.Prefer(Hdr("R", 0), "r.bup", Hdr("R", 0), "A.BUP"));      // own name, case-insensitive
        Assert.True(BupFile.Prefer(Hdr("R", 0), "A.BUP", Hdr("R", 0), "B.BUP"));
        Assert.False(BupFile.Prefer(Hdr("R", 0), "B.BUP", Hdr("R", 0), "A.BUP"));
        Assert.True(BupFile.Prefer(Hdr("R", 0), "Z.BUP", Hdr("R", 0), "a.BUP"));      // byte order: 'Z' 0x5A < 'a' 0x61
    }

    [Fact]
    public void NameNorm_takes_up_to_a_NUL_or_byte_11()
    {
        var o = new byte[12];
        Assert.True(BupFile.NameNorm("ABCDEFGHIJK."u8, o));
        Assert.Equal("ABCDEFGHIJK\0"u8.ToArray(), o);
        Assert.True(BupFile.NameNorm("AB\0XYZ\0\0\0\0\0\0"u8, o));
        Assert.Equal("AB\0\0\0\0\0\0\0\0\0\0"u8.ToArray(), o);
        Assert.False(BupFile.NameNorm(new byte[12], o));
    }
}
