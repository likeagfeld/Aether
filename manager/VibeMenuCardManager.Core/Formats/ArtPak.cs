// Aether Card Manager, GPL-3.0-or-later.
// ART.PAK, the container (contract/SPEC.md §5.2; §5.9's key-table limit). A port of tools/omsat/artpak.py
// write_artpak / read_artpak / get_blob / folder_art_key; the blobs (RGB555 covers, 4-bpp JP titles) are encoded by
// Art/ and are opaque here. Big-endian; blobs 2048-byte aligned and zero-padded (design spec §2).
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed record ArtJpTitle(int W, int H, byte[] Data);

/// <param name="Square">the layout: square (JP: 80x80 thumb, 160x160 hero) or tall (US/EU: 64x100, 128x200).</param>
/// <param name="Thumb">RGB555 pixels of the thumb, or null.</param>
/// <param name="Hero">RGB555 pixels of the hero cover, or null.</param>
public sealed record ArtEntry(string Key, int Disc, bool Square, byte[]? Thumb, byte[]? Hero, ArtJpTitle? JpTitle, uint ManifestRow);

public sealed record ArtKeyEntry(string Key, int Disc, int Flags, int ThumbW, int ThumbH, int HeroW, int HeroH, int JpW, int JpH,
                                 uint ThumbSec, uint HeroSec, uint JpSec, uint ManifestRow, uint BlobCrc);

public sealed class ArtPakData(byte[] data, uint dataOff, uint buildDate, List<ArtKeyEntry> entries)
{
    public uint DataOff { get; } = dataOff;
    public uint BuildDate { get; } = buildDate;
    public List<ArtKeyEntry> Entries { get; } = entries;

    static (uint Sec, int Len, int Flag) Where(ArtKeyEntry e, string kind) => kind switch
    {
        "thumb" => (e.ThumbSec, e.ThumbW * e.ThumbH * 2, ArtPak.FThumb),
        "hero" => (e.HeroSec, e.HeroW * e.HeroH * 2, ArtPak.FHero),
        "jp" => (e.JpSec, e.JpW / 2 * e.JpH, ArtPak.FJp),
        _ => throw new ArgumentException(kind),
    };

    /// <summary>get_blob: the entry's thumb / hero / jp blob, or null when its flag is clear.</summary>
    public byte[]? Blob(ArtKeyEntry e, string kind)
    {
        var (sec, len, flag) = Where(e, kind);
        if ((e.Flags & flag) == 0) return null;
        return data.AsSpan((int)(DataOff + sec * (long)ArtPak.Sector), len).ToArray();
    }

    /// <summary>Each entry's CRC-32 over its blobs (thumb, hero, jp, as written); the entries that fail.</summary>
    public List<string> VerifyBlobs()
    {
        var bad = new List<string>();
        foreach (var e in Entries)
        {
            var all = new List<byte>();
            foreach (var k in new[] { "thumb", "hero", "jp" }) if (Blob(e, k) is byte[] b) all.AddRange(b);
            if (CommonHeader.Crc32(all.ToArray()) != e.BlobCrc) bad.Add($"{e.Key} disc {e.Disc}: blob CRC mismatch");
        }
        return bad;
    }
}

public static class ArtPak
{
    public const int Ent = 48, Sector = 2048;
    public const int FThumb = 1, FHero = 2, FJp = 4, FSquare = 8;
    /// <summary>SPEC §5.9: header + key table at most 122,876 B, i.e. 2,558 entries at table offset 64.</summary>
    public const int MaxEntries = 2558;

    static (int W, int H) Thumb(bool square) => square ? (80, 80) : (64, 100);
    static (int W, int H) Hero(bool square) => square ? (160, 160) : (128, 200);

    /// <summary>SPEC §5.2 Folder art: "F" + the FNV-1a-32 (basis 0x811C9DC5, prime 0x01000193) of the UTF-8 path, as 8
    /// uppercase hex digits.</summary>
    public static string FolderArtKey(string path)
    {
        uint h = 0x811C9DC5;
        foreach (byte b in Encoding.UTF8.GetBytes(path)) { h ^= b; h *= 0x01000193; }
        return "F" + h.ToString("X8", System.Globalization.CultureInfo.InvariantCulture);
    }

    static long Align(long n) => (n + Sector - 1) / Sector * Sector;

    static int Cmp(ArtEntry a, ArtEntry b)
    {
        int c = CommonHeader.KeyBytes(a.Key).AsSpan().SequenceCompareTo(CommonHeader.KeyBytes(b.Key));
        return c != 0 ? c : a.Disc.CompareTo(b.Disc);
    }

    public static byte[] Write(IEnumerable<ArtEntry> entries, uint buildDate)
    {
        var ents = entries.ToList();
        if (ents.Count > MaxEntries)
            throw new FormatException($"ART.PAK would hold {ents.Count} entries, over the menu's {MaxEntries} (SPEC 5.9)");
        ents = [.. ents.OrderBy(e => e, Comparer<ArtEntry>.Create(Cmp))];      // stable: equal (key, disc) keep their order
        uint tableOff = CommonHeader.Size, tableSize = (uint)(Ent * ents.Count);
        uint dataOff = (uint)Align(tableOff + tableSize);
        var blobs = new List<byte>();
        var table = new byte[tableSize];
        uint Put(byte[] b)
        {
            uint sec = (uint)(blobs.Count / Sector);
            blobs.AddRange(b);
            blobs.AddRange(new byte[Align(b.Length) - b.Length]);
            return sec;
        }
        for (int i = 0; i < ents.Count; i++)
        {
            var e = ents[i];
            var (tw, th) = e.Thumb != null ? Thumb(e.Square) : (0, 0);
            var (hw, hh) = e.Hero != null ? Hero(e.Square) : (0, 0);
            if (e.Thumb != null && e.Thumb.Length != tw * th * 2) throw new ArgumentException($"{e.Key}: thumb is {e.Thumb.Length} B, not {tw}x{th} RGB555");
            if (e.Hero != null && e.Hero.Length != hw * hh * 2) throw new ArgumentException($"{e.Key}: hero is {e.Hero.Length} B, not {hw}x{hh} RGB555");
            if (e.JpTitle is { } j && (j.W % 8 != 0 || j.W is < 8 or > 320 || j.H is < 1 or > 32 || j.Data.Length != j.W / 2 * j.H))
                throw new ArgumentException($"{e.Key}: JP title {j.W}x{j.H} ({j.Data.Length} B) breaks SPEC 5.2 (width a multiple of 8, <= 320; height <= 32; 4 bpp)");
            if (e.Disc is < 0 or > 255) throw new ArgumentException($"{e.Key}: disc {e.Disc} does not fit the entry's byte (SPEC 5.2)");
            int flags = 0;
            uint ts = 0, hs = 0, js = 0;
            var crc = new List<byte>();
            if (e.Thumb != null) { ts = Put(e.Thumb); crc.AddRange(e.Thumb); flags |= FThumb; }
            if (e.Hero != null) { hs = Put(e.Hero); crc.AddRange(e.Hero); flags |= FHero; }
            if (e.JpTitle != null) { js = Put(e.JpTitle.Data); crc.AddRange(e.JpTitle.Data); flags |= FJp; }
            if (e.Square && (e.Thumb != null || e.Hero != null)) flags |= FSquare;
            var t = table.AsSpan(Ent * i, Ent);
            CommonHeader.KeyBytes(e.Key).CopyTo(t);
            t[0x10] = (byte)e.Disc; t[0x11] = (byte)flags;
            t[0x12] = (byte)tw; t[0x13] = (byte)th; t[0x14] = (byte)hw; t[0x15] = (byte)hh;
            Be.Put16(t, 0x16, (ushort)(e.JpTitle?.W ?? 0));
            t[0x18] = (byte)(e.JpTitle?.H ?? 0);                     // 0x19..0x1B reserved
            Be.Put32(t, 0x1C, ts); Be.Put32(t, 0x20, hs); Be.Put32(t, 0x24, js);
            Be.Put32(t, 0x28, e.ManifestRow);
            Be.Put32(t, 0x2C, CommonHeader.Crc32(crc.ToArray()));
        }
        var h = new CommonHeader(CommonHeader.Art, (uint)ents.Count, tableOff, tableSize, dataOff, (uint)blobs.Count, buildDate,
                                 CommonHeader.Crc32(table), [(0, 0), (0, 0), (0, 0)]);
        var o = new byte[dataOff + blobs.Count];
        h.Pack().CopyTo(o, 0);
        table.CopyTo(o, tableOff);
        blobs.CopyTo(o, (int)dataOff);
        return o;
    }

    public static ArtPakData Read(byte[] data) => CommonHeader.Guard(() => ReadCore(data));

    static ArtPakData ReadCore(byte[] data)
    {
        var h = CommonHeader.Validate(data, CommonHeader.Art);
        if (h.TableSize != Ent * h.Count) throw new FormatException("table size != 48 * count");
        if (h.Count > MaxEntries) throw new FormatException($"size: {h.Count} entries, over the menu's {MaxEntries} (SPEC 5.9)");
        if (h.DataOff % Sector != 0) throw new FormatException($"data offset {h.DataOff} is not 2048-aligned");
        var ents = new List<ArtKeyEntry>();
        for (int i = 0; i < h.Count; i++)
        {
            var t = data.AsSpan((int)h.TableOff + Ent * i, Ent);
            var e = new ArtKeyEntry(CommonHeader.KeyStr(t[..16]), t[0x10], t[0x11], t[0x12], t[0x13], t[0x14], t[0x15],
                                    Be.Get16(t, 0x16), t[0x18], Be.Get32(t, 0x1C), Be.Get32(t, 0x20), Be.Get32(t, 0x24),
                                    Be.Get32(t, 0x28), Be.Get32(t, 0x2C));
            if (i > 0)
            {
                var p = ents[^1];
                int c = CommonHeader.KeyBytes(p.Key).AsSpan().SequenceCompareTo(CommonHeader.KeyBytes(e.Key));
                if (c > 0 || (c == 0 && p.Disc > e.Disc))
                    throw new FormatException($"order: entry {i} ({e.Key} disc {e.Disc}) is before {p.Key} disc {p.Disc}");
            }
            foreach (var (flag, sec, len) in new[] { (FThumb, e.ThumbSec, e.ThumbW * e.ThumbH * 2), (FHero, e.HeroSec, e.HeroW * e.HeroH * 2),
                                                     (FJp, e.JpSec, e.JpW / 2 * e.JpH) })
                if ((e.Flags & flag) != 0 && (long)sec * Sector + len > h.DataSize)
                    throw new FormatException($"range: {e.Key} disc {e.Disc}: a blob at sector {sec} runs past the data");
            ents.Add(e);
        }
        return new ArtPakData(data, h.DataOff, h.BuildDate, ents);
    }
}
