// Aether Card Manager, GPL-3.0-or-later.
// LIBRARY.IDX (contract/SPEC.md §5.4; v1 clarification 6, the card fingerprint; §5.9's 212,988-byte console limit).
// A port of tools/omsat/libidx.py.
using System.Linq;
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed record LibEntry(string Path, string Key = "", int Disc = 1, int DiscTotal = 1, int AreaFlags = 0,
                              int ImageType = 1, int FolderId = 0, bool HasBox = false, string TitleOverride = "",
                              int Sets = 0);   // flags bits 2-15: a multi-disc set id << 4 | 4 for an alternate (DiscSets.cs)

public sealed record LibFolder(string Path, int Parent);

public sealed record LibraryIndexData(List<LibEntry> Entries, List<LibFolder> Folders, uint Fingerprint, uint BuildDate);

public static class LibraryIndex
{
    public const int Ent = 32;
    public const int ConsoleMax = 212_988;

    /// <summary>SPEC v1 clarifications 6-7: names dropped from listings (the §4 hidden set at the root only).</summary>
    // "saves", Pseudo Saturn Kai's /SAVES/BRAM/... save tree
    // (menu/src/libidx.c and tools/omsat/libidx.py already hide it; this port had not).
    static readonly string[] HiddenRoot = ["aether", "vibemenu", "openmenu", "menu.bin", "region.txt", "ar_backup.bin", "saves"];
    // 2026-11-23 (Satiator Rings' model): autoboot*, satiator-rings* and satiator-saves* at the root are prefixes,
    // not exact names (cardscan.py is_hidden_name).
    static readonly string[] HiddenRootPrefixes = ["autoboot", "satiator-rings", "satiator-saves"];

    static string AsciiLower(string s) => string.Create(s.Length, s, (d, x) => { for (int i = 0; i < x.Length; i++) d[i] = x[i] is >= 'A' and <= 'Z' ? (char)(x[i] + 32) : x[i]; });

    public static bool IsHiddenName(string name, bool atRoot)
    {
        if (name.StartsWith('.') || name.StartsWith('$') || AsciiLower(name) == "system volume information") return true;
        if (!atRoot) return false;
        string low = AsciiLower(name);
        return HiddenRoot.Contains(low) || HiddenRootPrefixes.Any(p => low.StartsWith(p, StringComparison.Ordinal))
            // LAUNCHER.BIN's launch descriptors: out.desc, and out1..out9.desc for the lid swap (libidx.py)
            || low == "out.desc" || (low.Length == 9 && low.StartsWith("out", StringComparison.Ordinal) && low[3] is >= '1' and <= '9'
                                     && low.EndsWith(".desc", StringComparison.Ordinal));
    }

    public static uint Fingerprint(IEnumerable<string> listing)
    {
        var sorted = listing.ToList();
        sorted.Sort(Be.CompareBytes);
        return CommonHeader.Crc32(Encoding.UTF8.GetBytes(string.Join("\n", sorted)));
    }

    /// <summary>SPEC v1 clarification 6: CRC-32 of the root listing's visible names, byte order, '\n'-joined.</summary>
    public static uint CardFingerprint(IEnumerable<string> rootNames) => Fingerprint(rootNames.Where(n => !IsHiddenName(n, true)));

    public static byte[] Write(IReadOnlyList<LibEntry> entries, IReadOnlyList<LibFolder> folders, uint fingerprint, uint buildDate)
    {
        var pool = new List<byte> { 0 };
        var index = new Dictionary<string, uint> { [""] = 0 };
        uint Add(string s)
        {
            var raw = Encoding.UTF8.GetBytes(s);
            if (raw.Length > 255) throw new ArgumentException($"path longer than 255 bytes: {s}");
            if (!index.TryGetValue(s, out var off)) { index[s] = off = (uint)pool.Count; pool.AddRange(raw); pool.Add(0); }
            return off;
        }
        var table = new byte[Ent * entries.Count];
        for (int i = 0; i < entries.Count; i++)
        {
            var e = entries[i];
            var t = table.AsSpan(Ent * i, Ent);
            CommonHeader.KeyBytes(e.Key).CopyTo(t);
            t[0x10] = (byte)e.Disc; t[0x11] = (byte)e.DiscTotal; t[0x12] = (byte)e.AreaFlags; t[0x13] = (byte)e.ImageType;
            Be.Put32(t, 0x14, Add(e.Path));
            Be.Put16(t, 0x18, (ushort)e.FolderId);
            Be.Put16(t, 0x1A, (ushort)((e.HasBox ? 1 : 0) | (e.TitleOverride != "" ? 2 : 0) | (e.Sets & 0xFFFC)));
            Be.Put32(t, 0x1C, Add(e.TitleOverride));
        }
        var ftab = new byte[8 * folders.Count];
        for (int i = 0; i < folders.Count; i++)
        {
            Be.Put32(ftab, 8 * i, Add(folders[i].Path));
            Be.Put16(ftab, 8 * i + 4, (ushort)folders[i].Parent);
        }
        var fp = new byte[4];
        Be.Put32(fp, 0, fingerprint);
        uint tOff = CommonHeader.Size, fOff = tOff + (uint)table.Length, pOff = fOff + (uint)ftab.Length, fpOff = pOff + (uint)pool.Count;
        var h = new CommonHeader(CommonHeader.Lib, (uint)entries.Count, tOff, (uint)table.Length, pOff, (uint)pool.Count, buildDate,
            CommonHeader.Crc32(table), [(fOff, (uint)ftab.Length), (pOff, (uint)pool.Count), (fpOff, 4)]);
        return [.. h.Pack(), .. table, .. ftab, .. pool, .. fp];
    }

    public static LibraryIndexData Read(byte[] data) => CommonHeader.Guard(() => ReadCore(data));

    static LibraryIndexData ReadCore(byte[] dataArr)
    {
        ReadOnlySpan<byte> data = dataArr;
        var h = CommonHeader.Validate(data, CommonHeader.Lib);
        if (h.Sections[2].Size != 4) throw new FormatException($"fingerprint section is {h.Sections[2].Size} B, not 4");
        if (h.TableSize != Ent * h.Count) throw new FormatException("table size != 32 * count");
        var (fOff, fSize) = h.Sections[0];
        var (pOff, pSize) = h.Sections[1];
        var (fpOff, _) = h.Sections[2];
        var pool = data.Slice((int)pOff, (int)pSize);
        var ents = new List<LibEntry>();
        for (int i = 0; i < h.Count; i++)
        {
            var t = data.Slice((int)h.TableOff + Ent * i, Ent);
            int fl = Be.Get16(t, 0x1A);
            ents.Add(new LibEntry(CommonHeader.CStr(pool, Be.Get32(t, 0x14), Encoding.UTF8), CommonHeader.KeyStr(t[..16]), t[0x10], t[0x11],
                t[0x12], t[0x13], Be.Get16(t, 0x18), (fl & 1) != 0, (fl & 2) != 0 ? CommonHeader.CStr(pool, Be.Get32(t, 0x1C), Encoding.UTF8) : "", fl & 0xFFFC));
        }
        var folders = new List<LibFolder>();
        for (int i = 0; i < fSize / 8; i++)
            folders.Add(new LibFolder(CommonHeader.CStr(pool, Be.Get32(data, (int)fOff + 8 * i), Encoding.UTF8), Be.Get16(data, (int)fOff + 8 * i + 4)));
        return new LibraryIndexData(ents, folders, Be.Get32(data, (int)fpOff), h.BuildDate);
    }
}
