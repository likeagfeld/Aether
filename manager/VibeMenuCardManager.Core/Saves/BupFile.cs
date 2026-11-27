// Aether Card Manager, GPL-3.0-or-later.
// The per-game save file (contract/SPEC.md §5.16): one backup-RAM record as a Pseudo Saturn Kai ".BUP" file, a 64-byte
// big-endian "Vmem" header then the record's data. The layout is Kai's vmem_bup_header_t (cafe-alpha, Pseudo Saturn Kai,
// pseudosat_kai/vmem/vmem_defs.h:82-135, GPL-2.0); the directory fields are the BIOS backup library's BupDir
// (per NOV96_DTS EXAMPLES/BACKRAM/GNU/SEGA_BUP.H BupDir: filename[12], comment[11], language, date, datasize, blocksize).
// A port of menu/src/bupfile.c (decode, name rules, prefer); tools/omsat/bupfile.py implements the same rules.
namespace VibeMenuCardManager.Core.Saves;

/// <param name="Name">1..11 printable ASCII bytes, no padding.</param>
/// <param name="Comment">The 11 raw comment bytes (Shift-JIS in real files).</param>
public sealed record BupDir(byte[] Name, byte[] Comment, byte Language, uint Date, uint DataSize, ushort Blocks);

public enum BupError { None, Short, Magic, Field }

public static class BupFile
{
    public const int Hdr = 64;

    /// <summary>1..11 printable bytes (0x20-0x7E) up to a NUL or byte 11, which is never part of the name (Save Game
    /// Copier writes '.' there after an 11-character name); 0 for an empty or unprintable name. bupfile.c name_len.</summary>
    public static int NameLen(ReadOnlySpan<byte> n)
    {
        int i;
        for (i = 0; i < 11 && i < n.Length && n[i] != 0; i++)
            if (n[i] < 0x20 || n[i] > 0x7E)
                return 0;
        return i;
    }

    /// <summary>om_bup_name_norm: out = the name, NUL-padded to 12; false for an empty or unprintable name.</summary>
    public static bool NameNorm(ReadOnlySpan<byte> in12, Span<byte> out12)
    {
        int n = NameLen(in12);
        if (n == 0) return false;
        Span<byte> t = stackalloc byte[12];
        in12[..n].CopyTo(t);
        t.CopyTo(out12);
        return true;
    }

    /// <summary>om_bup_decode_hdr. len = the file length the caller vouches for (the bytes need only hold the 64-byte
    /// header). Short: under 64, or datasize past len - 64; Magic: not "Vmem"; Field: empty or unprintable name.</summary>
    public static BupError TryDecode(ReadOnlySpan<byte> file, uint len, out BupDir? dir)
    {
        dir = null;
        if (len < Hdr || file.Length < Hdr) return BupError.Short;
        if (!file[..4].SequenceEqual("Vmem"u8)) return BupError.Magic;
        int n = NameLen(file.Slice(0x10, 12));
        if (n == 0) return BupError.Field;
        uint size = Be.Get32(file, 0x2C);
        if (size > len - Hdr) return BupError.Short;          // no 64 + size: it could wrap
        dir = new BupDir(file.Slice(0x10, n).ToArray(), file.Slice(0x1C, 11).ToArray(), file[0x27],
                         Be.Get32(file, 0x28), size, Be.Get16(file, 0x30));
        return BupError.None;
    }

    /// <summary>SPEC §5.16 Duplicates (om_bup_prefer): true when file a (header a, file name fa) wins over b. The later
    /// record date (0x28); then the higher save_id (0x04); then the file named "&lt;record&gt;.BUP" (ASCII
    /// case-insensitive); then the smaller file name in byte order. Both headers must have decoded.</summary>
    public static bool Prefer(ReadOnlySpan<byte> a, string fa, ReadOnlySpan<byte> b, string fb)
    {
        uint da = Be.Get32(a, 0x28), db = Be.Get32(b, 0x28), sa = Be.Get32(a, 0x04), sb = Be.Get32(b, 0x04);
        if (da != db) return da > db;
        if (sa != sb) return sa > sb;
        string own = System.Text.Encoding.ASCII.GetString(a.Slice(0x10, NameLen(a.Slice(0x10, 12)))) + ".BUP";
        bool ma = Be.AsciiIEq(fa, own), mb = Be.AsciiIEq(fb, own);
        if (ma != mb) return ma;
        return Be.CompareBytes(fa, fb) < 0;
    }
}
