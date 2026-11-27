// VibeMenu Card Manager, GPL-3.0-or-later.
// COVERS.PAK (tools/omsat/coverpack.py's format): a small synthetic pack built here, byte for byte to the module
// header's spec (manager/VibeMenuCardManager.Core/Art/CoverPack.cs), not via CoverPack's own writer (it has none --
// only tools/omsat/coverpack.py writes the real pack), so these tests exercise the reader against an independently
// built artifact.
using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Art;

public sealed record PackEntryIn(string Code, string Source, string Format, byte[] Data, int Width, int Height, uint? BadCrc = null,
                                 string FallbackRegion = "", string FallbackFrom = "");

public static class CoverPackFixture
{
    public static byte[] Build(IReadOnlyList<PackEntryIn> entries, uint buildDate = 20261123)
    {
        var blobs = new List<byte>();
        var codes = new Dictionary<string, List<Dictionary<string, object>>>();
        foreach (var e in entries)
        {
            int off = blobs.Count;
            blobs.AddRange(e.Data);
            uint crc = e.BadCrc ?? CommonHeader.Crc32(e.Data);
            if (!codes.TryGetValue(e.Code, out var list)) codes[e.Code] = list = [];
            var ent = new Dictionary<string, object> { ["source"] = e.Source, ["format"] = e.Format, ["offset"] = off, ["size"] = e.Data.Length,
                                                       ["width"] = e.Width, ["height"] = e.Height, ["crc32"] = crc };
            if (e.FallbackRegion != "") { ent["fallback_region"] = e.FallbackRegion; ent["fallback_from"] = e.FallbackFrom; }
            list.Add(ent);
        }
        var index = JsonSerializer.Serialize(new { version = 1, codes });
        var indexBytes = Encoding.UTF8.GetBytes(index);
        uint indexCrc = CommonHeader.Crc32(indexBytes);
        uint dataOff = (uint)(32 + indexBytes.Length);

        var header = new byte[32];
        "OMSATCVR"u8.ToArray().CopyTo(header, 0);
        BinaryPrimitives.WriteUInt16BigEndian(header.AsSpan(8), 1);
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(12), (uint)indexBytes.Length);
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(16), indexCrc);
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(20), dataOff);
        BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(24), buildDate);

        return [.. header, .. indexBytes, .. blobs];
    }
}

public class CoverPackTests
{
    static byte[] Jpeg(byte b = 1) => [0xFF, 0xD8, b, b, b, 0xFF, 0xD9];   // content does not matter: never decoded here

    [Fact]
    public void Reads_the_header_and_index_and_every_entrys_blob()
    {
        var pak = CoverPackFixture.Build([
            new("T-00001", "coverproject", "jpeg", Jpeg(1), 100, 140),
            new("T-00001", "retrohead2d", "jpeg", Jpeg(2), 90, 130),
            new("T-00002", "libretro", "png", Jpeg(3), 80, 120),
        ], buildDate: 20261124);

        using var data = CoverPack.Read(pak);
        Assert.Equal(20261124u, data.BuildDate);
        Assert.Equal(2, data.Codes.Count);
        var t1 = data.For("T-00001");
        Assert.Equal(2, t1.Count);
        Assert.Equal(Jpeg(1), data.Blob(t1.Single(e => e.Source == "coverproject")));
        Assert.Equal(Jpeg(2), data.Blob(t1.Single(e => e.Source == "retrohead2d")));
        Assert.Equal(Jpeg(3), data.Blob(data.For("T-00002").Single()));
    }

    [Fact]
    public void A_region_fallback_entry_carries_its_region_and_source_key()
    {
        var pack = CoverPack.Read(CoverPackFixture.Build([
            new("T-1234G", "coverproject", "jpeg", Jpeg(), 300, 512),
            new("T-1234G-50", "coverproject", "jpeg", Jpeg(2), 300, 512, FallbackRegion: "U", FallbackFrom: "T-1234H"),
        ]));
        var own = pack.For("T-1234G")[0];
        Assert.Equal(("", "", null), (own.FallbackRegion, own.FallbackFrom, own.FallbackLabel));
        var fb = pack.For("T-1234G-50")[0];
        Assert.Equal(("U", "T-1234H", "US"), (fb.FallbackRegion, fb.FallbackFrom, fb.FallbackLabel));
        Assert.Equal("JP", (fb with { FallbackRegion = "J" }).FallbackLabel);
        Assert.Equal("EU", (fb with { FallbackRegion = "E" }).FallbackLabel);
    }

    /// <summary>The real pack (build/coverpack/COVERS.PAK, ~180 MB, not committed): only its header and index are read.
    /// coverpack.py's d51022a run: 1,919 codes, 117 by region fallback (U 112, J 3, E 2).</summary>
    [SkippableFact]
    public void The_real_pack_has_its_region_fallbacks()
    {
        string p = Repo.P("build/coverpack/COVERS.PAK");
        Skip.IfNot(File.Exists(p), "build/coverpack/COVERS.PAK is not built");
        using var pack = CoverPack.Open(p);
        var fb = pack.Codes.Values.SelectMany(l => l).Where(e => e.FallbackRegion != "").ToList();
        Assert.True(pack.Codes.Count >= 1900, $"{pack.Codes.Count} codes");
        Assert.True(fb.Count >= 100, $"{fb.Count} fallbacks");
        Assert.All(fb, e => Assert.NotNull(e.FallbackLabel));
        Assert.All(fb, e => Assert.NotEqual("", e.FallbackFrom));
    }

    [Fact]
    public void For_an_unknown_code_is_empty()
    {
        using var data = CoverPack.Read(CoverPackFixture.Build([new("T-00001", "coverproject", "jpeg", Jpeg(), 1, 1)]));
        Assert.Empty(data.For("T-99999"));
    }

    [Fact]
    public void A_crc_with_the_top_bit_set_still_round_trips_as_unsigned()
    {
        // JSON crc32 values can exceed int32.MaxValue (Python masks with & 0xFFFFFFFF); GetUInt32 must handle that.
        var bytes = new byte[] { 0xFF, 0xFF, 0xFF, 0xFE };    // crc32 of these 4 bytes: whatever it is, just needs to round-trip
        var pak = CoverPackFixture.Build([new("T-HIBIT", "coverproject", "jpeg", bytes, 1, 1)]);
        using var data = CoverPack.Read(pak);
        var e = data.For("T-HIBIT").Single();
        Assert.Equal(bytes, data.Blob(e));
    }

    [Fact]
    public void A_corrupt_blob_fails_its_own_CRC_check()
    {
        var pak = CoverPackFixture.Build([new("T-00001", "coverproject", "jpeg", Jpeg(), 1, 1, BadCrc: 0xDEADBEEF)]);
        using var data = CoverPack.Read(pak);
        var ex = Assert.Throws<FormatException>(() => data.Blob(data.For("T-00001").Single()));
        Assert.Contains("CRC", ex.Message);
    }

    [Fact]
    public void Wrong_magic_is_rejected()
    {
        var pak = CoverPackFixture.Build([new("T-1", "coverproject", "jpeg", Jpeg(), 1, 1)]);
        pak[0] = (byte)'X';
        var ex = Assert.Throws<FormatException>(() => CoverPack.Read(pak));
        Assert.Contains("magic", ex.Message);
    }

    [Fact]
    public void A_bad_index_crc_is_rejected()
    {
        var pak = CoverPackFixture.Build([new("T-1", "coverproject", "jpeg", Jpeg(), 1, 1)]);
        BinaryPrimitives.WriteUInt32BigEndian(pak.AsSpan(16), 0x12345678);
        var ex = Assert.Throws<FormatException>(() => CoverPack.Read(pak));
        Assert.Contains("CRC", ex.Message);
    }

    [Fact]
    public void A_file_shorter_than_the_header_is_rejected()
    {
        var ex = Assert.Throws<FormatException>(() => CoverPack.Read(new byte[10]));
        Assert.Contains("range", ex.Message);
    }

    [Fact]
    public void An_unsupported_version_is_rejected()
    {
        var pak = CoverPackFixture.Build([new("T-1", "coverproject", "jpeg", Jpeg(), 1, 1)]);
        BinaryPrimitives.WriteUInt16BigEndian(pak.AsSpan(8), 2);
        var ex = Assert.Throws<FormatException>(() => CoverPack.Read(pak));
        Assert.Contains("version", ex.Message);
    }

    [Fact]
    public void Open_reads_lazily_from_a_real_file_and_can_be_disposed_and_deleted()
    {
        var pak = CoverPackFixture.Build([new("T-00001", "coverproject", "jpeg", Jpeg(7), 10, 10)]);
        string path = Path.Combine(Path.GetTempPath(), "cmtest_" + Guid.NewGuid().ToString("N") + ".pak");
        File.WriteAllBytes(path, pak);
        try
        {
            using (var data = CoverPack.Open(path))
                Assert.Equal(Jpeg(7), data.Blob(data.For("T-00001").Single()));
        }
        finally { File.Delete(path); }     // throws if Open/Dispose left the file locked
    }

    [Fact]
    public void OpenDefault_is_null_when_the_pack_is_not_installed()
    {
        // CoverPack.DefaultPath() is next to the test runner's own exe -- never present there.
        Assert.Null(CoverPack.OpenDefault());
    }
}
