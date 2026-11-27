// VibeMenu Card Manager, GPL-3.0-or-later.
// ART.PAK's container (contract/SPEC.md §5.2, §5.9) against tools/omsat/artpak.py: tiny.art.pak (written by
// omsat.artpak through contract/fixtures/make_fixtures.py) read and written back byte for byte, its entries field-equal
// to read_artpak's, folder-art keys equal to folder_art_key (manager/golden/expect/artpak.json).
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core.Formats;
using FormatException = VibeMenuCardManager.Core.Formats.FormatException;

namespace VibeMenuCardManager.Tests.Formats;

public static class ArtCheck
{
    public static byte[] Tiny() => File.ReadAllBytes(Repo.P("contract/fixtures/tiny.art.pak"));
    public static JsonElement Expect() => JsonDocument.Parse(File.ReadAllText(Repo.P("manager/golden/expect/artpak.json"))).RootElement;

    static string Sha(byte[]? b) => b == null ? "" : Convert.ToHexStringLower(SHA256.HashData(b));

    /// <summary>The pack's entries against read_artpak's fields and get_blob's blob hashes; the mismatches.</summary>
    public static List<string> CompareEntries(byte[] pak)
    {
        var got = ArtPak.Read(pak);
        var want = Expect().GetProperty("tiny_entries").EnumerateArray().ToList();
        var bad = new List<string>();
        if (got.Entries.Count != want.Count) bad.Add($"{got.Entries.Count} entries, expect {want.Count}");
        for (int i = 0; i < Math.Min(got.Entries.Count, want.Count); i++)
        {
            var g = got.Entries[i];
            var w = want[i];
            var fields = new (string Name, long Got)[]
            {
                ("disc", g.Disc), ("flags", g.Flags), ("thumb_w", g.ThumbW), ("thumb_h", g.ThumbH), ("hero_w", g.HeroW),
                ("hero_h", g.HeroH), ("jp_w", g.JpW), ("jp_h", g.JpH), ("thumb_sec", g.ThumbSec), ("hero_sec", g.HeroSec),
                ("jp_sec", g.JpSec), ("manifest_row", g.ManifestRow), ("blob_crc", g.BlobCrc),
            };
            if (g.Key != w.GetProperty("key").GetString()) bad.Add($"entry {i}: key {g.Key} expect {w.GetProperty("key").GetString()}");
            foreach (var (n, v) in fields)
                if (v != w.GetProperty(n).GetInt64()) bad.Add($"entry {i} ({g.Key}): {n} {v} expect {w.GetProperty(n).GetInt64()}");
            foreach (var kind in new[] { "thumb", "hero", "jp" })
                if (Sha(got.Blob(g, kind)) != w.GetProperty(kind + "_sha256").GetString()) bad.Add($"entry {i} ({g.Key}): {kind} blob differs");
        }
        return bad;
    }

    /// <summary>The pack's entries as writer input (the blobs as read back).</summary>
    public static List<ArtEntry> AsInput(ArtPakData d) =>
        d.Entries.Select(e => new ArtEntry(e.Key, e.Disc, (e.Flags & ArtPak.FSquare) != 0, d.Blob(e, "thumb"), d.Blob(e, "hero"),
            d.Blob(e, "jp") is byte[] j ? new ArtJpTitle(e.JpW, e.JpH, j) : null, e.ManifestRow)).ToList();

    public static string? FirstDiff(byte[] a, byte[] b) => Card.ScanFixture.FirstDiff(a, b);
}

public class ArtPakTests
{
    [Fact]
    public void Tiny_art_pak_entries_equal_omsat_read_artpak()
    {
        var bad = ArtCheck.CompareEntries(ArtCheck.Tiny());
        Assert.True(bad.Count == 0, string.Join("\n", bad));
    }

    [Fact]
    public void Tiny_art_pak_reads_and_writes_back_byte_for_byte()
    {
        var b = ArtCheck.Tiny();
        var d = ArtPak.Read(b);
        Assert.Equal(5, d.Entries.Count);
        Assert.Null(ArtCheck.FirstDiff(ArtPak.Write(ArtCheck.AsInput(d), d.BuildDate), b));
    }

    [Fact]
    public void Write_sorts_by_key_bytes_then_disc()
    {
        var d = ArtPak.Read(ArtCheck.Tiny());
        var shuffled = ArtCheck.AsInput(d);
        shuffled.Reverse();
        Assert.Null(ArtCheck.FirstDiff(ArtPak.Write(shuffled, d.BuildDate), ArtCheck.Tiny()));
    }

    [Fact]
    public void Folder_art_keys_equal_omsat_folder_art_key()
    {
        var keys = ArtCheck.Expect().GetProperty("folder_keys").EnumerateObject().ToList();
        Assert.Equal(23, keys.Count);                       // the exact count, not a floor
        foreach (var k in keys) Assert.Equal(k.Value.GetString(), ArtPak.FolderArtKey(k.Name));
    }

    [Fact]
    public void A_disc_number_that_does_not_fit_its_byte_is_refused()
    {
        // the entry's disc is one byte (SPEC 5.2); 256 would silently become disc 0
        Assert.Throws<ArgumentException>(() => ArtPak.Write([new ArtEntry("T-1", 256, false, null, null, null, 0)], 1));
        Assert.Throws<ArgumentException>(() => ArtPak.Write([new ArtEntry("T-1", -1, false, null, null, null, 0)], 1));
        Assert.Equal(255, ArtPak.Read(ArtPak.Write([new ArtEntry("T-1", 255, false, null, null, null, 0)], 1)).Entries.Single().Disc);
    }

    [Fact]
    public void A_title_only_entry_has_no_layout_and_no_square_flag()
    {
        // write_artpak: the layout (and bit3) comes from a thumb or hero only; a JP-title-only entry carries neither
        var pak = ArtPak.Write([new ArtEntry("T-1", 0, true, null, null, new ArtJpTitle(16, 16, new byte[128]), 7)], 1);
        var e = ArtPak.Read(pak).Entries.Single();
        Assert.Equal((ArtPak.FJp, 0, 0, 0, 0, 16, 16), (e.Flags, e.ThumbW, e.ThumbH, e.HeroW, e.HeroH, e.JpW, e.JpH));
    }

    [Fact]
    public void Every_blob_checks_against_its_entry_crc() => Assert.Empty(ArtPak.Read(ArtCheck.Tiny()).VerifyBlobs());

    [Fact]
    public void Writer_refuses_a_key_table_the_menu_cannot_hold_and_bad_blob_sizes()
    {
        var hero = new byte[160 * 160 * 2];
        var ok = Enumerable.Range(0, ArtPak.MaxEntries).Select(i => new ArtEntry($"K{i:D6}", 0, true, null, null, null, 0)).ToList();
        Assert.True(ArtPak.Write(ok, 1).Length > 0);
        ok.Add(new ArtEntry("ZZZ", 0, true, null, null, null, 0));
        Assert.Contains("2558", Assert.Throws<FormatException>(() => ArtPak.Write(ok, 1)).Message);
        Assert.Throws<ArgumentException>(() => ArtPak.Write([new ArtEntry("A", 0, true, null, hero[..100], null, 0)], 1));
        Assert.Throws<ArgumentException>(() => ArtPak.Write([new ArtEntry("A", 0, true, null, null, new ArtJpTitle(12, 16, new byte[96]), 0)], 1));
    }
}
