// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Disc;

namespace VibeMenuCardManager.Tests.Disc;

public static class HeaderCheck
{
    public sealed record Row(string Fixture, string File, byte[] Raw, Dictionary<string, string> Want);

    public static List<Row> Load()
    {
        var o = new List<Row>();
        foreach (var fx in new[] { "real_headers", "xband_headers" })
            foreach (var e in JsonDocument.Parse(File.ReadAllText(Repo.P($"contract/fixtures/{fx}.json"))).RootElement
                         .GetProperty("entries").EnumerateArray())
                o.Add(new Row(fx, e.GetProperty("file").GetString()!, Convert.FromHexString(e.GetProperty("raw_hex").GetString()!),
                    new[] { "product", "version", "date", "device", "area", "peripherals", "maker", "title" }
                        .ToDictionary(k => k, k => e.GetProperty(k).GetString()!)));
        return o;
    }

    public static List<string> Compare(IEnumerable<Row> rows)
    {
        var bad = new List<string>();
        foreach (var r in rows)
        {
            var s = SystemIdReader.Parse(r.Raw);
            if (s == null) { bad.Add($"{r.File}: not parsed"); continue; }
            var got = new Dictionary<string, string>
            {
                ["product"] = s.Product, ["version"] = s.Version, ["date"] = s.ReleaseDate, ["device"] = s.Device,
                ["area"] = s.Area, ["peripherals"] = s.Peripherals, ["maker"] = s.Maker, ["title"] = s.Title,
            };
            foreach (var (k, v) in r.Want)
                if (got[k] != v) bad.Add($"{r.File}: {k} {JsonSerializer.Serialize(got[k])} expect {JsonSerializer.Serialize(v)}");
        }
        return bad;
    }
}

public class SystemIdTests
{
    [Fact]
    public void Every_real_header_parses_to_the_fixtures_fields()
    {
        var rows = HeaderCheck.Load();
        Assert.Equal(51, rows.Count);
        var bad = HeaderCheck.Compare(rows);
        Assert.True(bad.Count == 0, $"{bad.Count} differences over {rows.Count} headers:\n" + string.Join("\n", bad.Take(20)));
    }

    [Fact]
    public void A_wrong_hardware_id_is_not_a_saturn_header()
    {
        var raw = HeaderCheck.Load()[0].Raw;
        raw[15] = (byte)'X';
        Assert.Null(SystemIdReader.Parse(raw));
        Assert.Null(SystemIdReader.Parse(raw.AsSpan(0, 255)));
    }

    [Fact]
    public void Sysid_fixtures_parse()
    {
        var s = SystemIdReader.Parse(File.ReadAllBytes(Repo.P("contract/fixtures/sysid_gs9001.bin")));
        Assert.NotNull(s);
        Assert.Equal("GS-9001", s!.Product);
        Assert.Equal("JTUE", s.Area);
    }

    [Theory]
    [InlineData("CD-2/3", 2, 3)]
    [InlineData("cd-1/1", 1, 1)]
    [InlineData("", 1, 1)]
    public void DiscNM(string dev, int n, int m) => Assert.Equal((n, m), SystemIdReader.DiscNM(dev));

    static byte[] Sector(byte[] sid, byte mode)
    {
        var b = new byte[2352];
        b[0] = 0; for (int i = 1; i <= 10; i++) b[i] = 0xFF; b[11] = 0;
        b[15] = mode;
        sid.AsSpan(0, 256).CopyTo(b.AsSpan(mode == 1 ? 16 : 24));
        return b;
    }

    [Fact]
    public void Images_iso_mode1_and_mode2_cue_offsets()
    {
        var sid = File.ReadAllBytes(Repo.P("contract/fixtures/sysid_gs9001.bin"));
        using var card = new TempCard();
        File.WriteAllBytes(Path.Combine(card.Dir, "a.iso"), [.. sid.AsSpan(0, 256), .. new byte[2048]]);
        File.WriteAllBytes(Path.Combine(card.Dir, "m1.bin"), Sector(sid, 1));
        File.WriteAllBytes(Path.Combine(card.Dir, "m2.bin"), Sector(sid, 2));
        File.WriteAllText(Path.Combine(card.Dir, "m1.cue"), "FILE \"m1.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n");
        File.WriteAllText(Path.Combine(card.Dir, "m2.cue"), "FILE m2.bin BINARY\n  TRACK 01 MODE2/2352\n");
        foreach (var f in new[] { "a.iso", "m1.cue", "m2.cue" })
            Assert.Equal("GS-9001", SystemIdReader.ReadImage(Path.Combine(card.Dir, f))?.Product);
    }

    /// <summary>Optional, read-only: the user's retail images (G:\Saturn Games), when this machine has them, read to
    /// the same products as real_headers.json records. Reports the denominator; asserts only what it could open.</summary>
    [SkippableFact]
    public void Retail_images_on_this_machine_read_as_the_fixture_says()
    {
        const string dir = @"G:\Saturn Games";
        Skip.IfNot(Directory.Exists(dir), $"{dir} is not on this machine: 0 retail images read");
        var want = HeaderCheck.Load().Where(r => r.Fixture == "real_headers").ToDictionary(r => r.File, r => r.Want["product"]);
        int seen = 0;
        foreach (var cue in Directory.EnumerateFiles(dir, "*.cue", SearchOption.AllDirectories))
        {
            if (!want.TryGetValue(Path.GetFileName(cue), out var p)) continue;
            Assert.Equal(p, SystemIdReader.ReadImage(cue)?.Product);
            seen++;
        }
        Assert.True(seen > 0, $"G:\\Saturn Games exists but 0 of {want.Count} fixture images were found by name");
    }
}
