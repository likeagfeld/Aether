// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos for ART.PAK: a flipped blob byte fails its entry's CRC; entries out of order, a
// blob sector past the data, and a doctored expect are each caught.
using VibeMenuCardManager.Core;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Tests.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Mutation;

public class ArtPakMutationTests
{
    static void Resign(byte[] b)
    {
        var h = CommonHeader.Unpack(b);
        Be.Put32(b, 0x24, CommonHeader.Crc32(b.AsSpan((int)h.TableOff, (int)h.TableSize)));
    }

    [Fact]
    public void A_flipped_blob_byte_fails_that_entrys_crc()
    {
        var b = ArtCheck.Tiny();
        var d = ArtPak.Read(b);
        var e = d.Entries.First(x => x.Key == "T-8113H");
        b[(int)(d.DataOff + e.HeroSec * 2048) + 100] ^= 0x01;
        var bad = ArtPak.Read(b).VerifyBlobs();
        Assert.Equal(["T-8113H disc 0: blob CRC mismatch"], bad);
        Assert.NotEmpty(ArtCheck.CompareEntries(b));             // and the golden comparison sees it too
    }

    [Fact]
    public void Two_swapped_entries_are_rejected_even_with_a_valid_crc()
    {
        var b = ArtCheck.Tiny();
        var t = b.AsSpan(64, 48).ToArray();
        b.AsSpan(112, 48).CopyTo(b.AsSpan(64, 48));
        t.CopyTo(b.AsSpan(112, 48));
        Resign(b);
        Assert.StartsWith("order:", Assert.Throws<FormatException>(() => ArtPak.Read(b)).Message);
    }

    [Fact]
    public void Two_discs_of_one_key_out_of_order_are_rejected()
    {
        var b = ArtCheck.Tiny();
        var d = ArtPak.Read(b);
        int i = d.Entries.FindIndex(e => e.Key == "T-4507G");             // discs 1 and 2, adjacent
        int o1 = 64 + 48 * i, o2 = o1 + 48;
        var t = b.AsSpan(o1, 48).ToArray();
        b.AsSpan(o2, 48).CopyTo(b.AsSpan(o1, 48));
        t.CopyTo(b.AsSpan(o2, 48));
        Resign(b);
        Assert.Contains("disc 1", Assert.Throws<FormatException>(() => ArtPak.Read(b)).Message);
    }

    [Fact]
    public void A_blob_sector_past_the_data_is_rejected()
    {
        var b = ArtCheck.Tiny();
        Be.Put32(b, 64 + 0x20, 0x00FFFFFF);                      // entry 0's hero sector
        Resign(b);
        Assert.StartsWith("range:", Assert.Throws<FormatException>(() => ArtPak.Read(b)).Message);
    }

    [Fact]
    public void A_wrong_flag_in_the_written_pack_is_caught_by_the_byte_compare()
    {
        var d = ArtPak.Read(ArtCheck.Tiny());
        var input = ArtCheck.AsInput(d);
        input[1] = input[1] with { Square = !input[1].Square };
        // square and tall covers have the same pixel count (SPEC 5.2), so only the bytes show the wrong layout
        Assert.NotNull(ArtCheck.FirstDiff(ArtPak.Write(input, d.BuildDate), ArtCheck.Tiny()));
        input = ArtCheck.AsInput(d);
        input[1] = input[1] with { ManifestRow = 99 };
        Assert.NotNull(ArtCheck.FirstDiff(ArtPak.Write(input, d.BuildDate), ArtCheck.Tiny()));
    }
}
