// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos for GAMES.DB: each corruption must make the reader or the writer
// refuse. Corruptions that keep the CRC valid (re-signed tables) prove the order and range checks, not only the CRC.
using VibeMenuCardManager.Core;
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Mutation;

public class GamesDbMutationTests
{
    static byte[] Tiny() => File.ReadAllBytes(Repo.P("contract/fixtures/tiny.games.db"));

    /// <summary>Recompute the table CRC after a table edit, so only the edit itself can be what the reader rejects.</summary>
    static void Resign(byte[] b)
    {
        var h = CommonHeader.Unpack(b);
        Be.Put32(b, 0x24, CommonHeader.Crc32(b.AsSpan((int)h.TableOff, (int)h.TableSize)));
    }

    [Fact]
    public void Unmutated_reads() => Assert.Equal(4, GamesDb.Read(Tiny()).Records.Count);

    [Fact]
    public void Two_swapped_records_are_rejected_even_with_a_valid_CRC()
    {
        var b = Tiny();
        var r0 = b.AsSpan(64, 64).ToArray();
        b.AsSpan(128, 64).CopyTo(b.AsSpan(64, 64));
        r0.CopyTo(b.AsSpan(128, 64));
        Resign(b);
        Assert.StartsWith("order: record 1", Assert.Throws<FormatException>(() => GamesDb.Read(b)).Message);
    }

    [Fact]
    public void A_flipped_table_byte_fails_the_CRC()
    {
        var b = Tiny();
        b[64 + 0x11] ^= 0x01;
        Assert.Contains("CRC", Assert.Throws<FormatException>(() => GamesDb.Read(b)).Message);
    }

    [Fact]
    public void A_title_offset_past_the_pool_is_a_format_error_not_a_crash()
    {
        var b = Tiny();
        Be.Put32(b, 64 + 0x1C, 0x00FFFFFF);
        Resign(b);
        Assert.StartsWith("pool:", Assert.Throws<FormatException>(() => GamesDb.Read(b)).Message);
    }

    [Fact]
    public void A_file_over_the_console_limit_is_refused_by_reader_and_writer()
    {
        // a GAMES.DB padded past 245,756 B (SPEC 5.9): the reader refuses it before parsing
        var big = new byte[GamesDb.ConsoleMax + 1];
        Tiny().CopyTo(big, 0);
        Assert.StartsWith("size:", Assert.Throws<FormatException>(() => GamesDb.Read(big)).Message);
        // 3,900 records with long titles make a file over the limit: the writer refuses to produce it
        var recs = Enumerable.Range(0, 3900).Select(i => new GameRecord { Key = $"T-{i:D6}", TitleEn = $"TITLE {i:D6} " + new string('X', 30) });
        Assert.Contains("over the menu's 245756 B", Assert.Throws<FormatException>(() => GamesDb.Write(recs, [], new Dictionary<string, string>(), 1)).Message);
    }

    [Fact]
    public void A_fingerprint_section_of_the_wrong_size_is_rejected()
    {
        var b = File.ReadAllBytes(Repo.P("contract/fixtures/tiny.library.idx"));
        Be.Put32(b, 0x3C, 3);
        Assert.Contains("fingerprint", Assert.Throws<FormatException>(() => LibraryIndex.Read(b)).Message);
    }
}
