// Aether Card Manager, GPL-3.0-or-later.
// The 64-byte common header of every Aether binary file (contract/SPEC.md §5.1), big-endian. A port of
// tools/omsat/binfmt.py (pack_header, unpack_header, validate_common, key_bytes, key_str).
using System.IO.Hashing;
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed class FormatException(string message) : Exception(message);

public sealed record CommonHeader(byte[] Magic, uint Count, uint TableOff, uint TableSize, uint DataOff, uint DataSize,
                                  uint BuildDate, uint TableCrc, (uint Off, uint Size)[] Sections,
                                  ushort Version = 1, ushort HeaderSize = 64)
{
    public const int Size = 64;
    public static readonly byte[] Art = "OMSATART"u8.ToArray(), Gdb = "OMSATGDB"u8.ToArray(), Lib = "OMSATLIB"u8.ToArray();

    public static uint Crc32(ReadOnlySpan<byte> b) => System.IO.Hashing.Crc32.HashToUInt32(b);

    public byte[] Pack()
    {
        var h = new byte[Size];
        Magic.CopyTo(h, 0);
        Be.Put16(h, 8, Version);
        Be.Put16(h, 10, HeaderSize);
        uint[] v = [Count, TableOff, TableSize, DataOff, DataSize, BuildDate, TableCrc];
        for (int i = 0; i < 7; i++) Be.Put32(h, 12 + 4 * i, v[i]);
        for (int i = 0; i < 3; i++) { Be.Put32(h, 0x28 + 8 * i, Sections[i].Off); Be.Put32(h, 0x2C + 8 * i, Sections[i].Size); }
        return h;
    }

    public static CommonHeader Unpack(ReadOnlySpan<byte> b)
    {
        if (b.Length < Size) throw new FormatException("range: file shorter than header");
        var s = new (uint, uint)[3];
        for (int i = 0; i < 3; i++) s[i] = (Be.Get32(b, 0x28 + 8 * i), Be.Get32(b, 0x2C + 8 * i));
        return new CommonHeader(b[..8].ToArray(), Be.Get32(b, 12), Be.Get32(b, 16), Be.Get32(b, 20), Be.Get32(b, 24),
                                Be.Get32(b, 28), Be.Get32(b, 32), Be.Get32(b, 36), s, Be.Get16(b, 8), Be.Get16(b, 10));
    }

    static bool InRange(uint off, uint size, long total) => (long)off + size <= total;

    /// <summary>SPEC §5.1: reject wrong magic, an unknown version, a bad header size, a range past the end, a CRC
    /// mismatch (validate_common).</summary>
    public static CommonHeader Validate(ReadOnlySpan<byte> data, byte[] magic)
    {
        var h = Unpack(data);
        if (!h.Magic.AsSpan().SequenceEqual(magic))
            throw new FormatException($"magic: expected {Encoding.ASCII.GetString(magic)}, got {Encoding.Latin1.GetString(h.Magic)}");
        if (h.Version != 1) throw new FormatException($"version: unsupported {h.Version}");
        if (h.HeaderSize != Size) throw new FormatException($"header size: {h.HeaderSize}");
        if (!InRange(h.TableOff, h.TableSize, data.Length)) throw new FormatException("range: table beyond end of file");
        if (!InRange(h.DataOff, h.DataSize, data.Length)) throw new FormatException("range: data beyond end of file");
        for (int i = 0; i < 3; i++)
            if (h.Sections[i].Size != 0 && !InRange(h.Sections[i].Off, h.Sections[i].Size, data.Length))
                throw new FormatException($"range: section {i} beyond end of file");
        if (Crc32(data.Slice((int)h.TableOff, (int)h.TableSize)) != h.TableCrc) throw new FormatException("CRC: table CRC mismatch");
        return h;
    }

    public static byte[] KeyBytes(string key)
    {
        var raw = Encoding.ASCII.GetBytes(key);
        if (raw.Length > 16) throw new ArgumentException($"key longer than 16 bytes: {key}");
        var b = new byte[16];
        raw.CopyTo(b, 0);
        return b;
    }

    public static string KeyStr(ReadOnlySpan<byte> b16)
    {
        int n = b16.Length;
        while (n > 0 && b16[n - 1] == 0) n--;
        return Encoding.ASCII.GetString(b16[..n]);
    }

    /// <summary>A NUL-terminated string at off in a pool.</summary>
    /// <summary>Any out-of-range offset in a corrupt file is a FormatException, never a crash (SPEC §5.1).</summary>
    internal static T Guard<T>(Func<T> f)
    {
        try { return f(); }
        catch (Exception e) when (e is ArgumentOutOfRangeException or IndexOutOfRangeException or ArgumentException
                                  or OverflowException or DecoderFallbackException)
        { throw new FormatException("range: " + e.Message); }
    }

    public static string CStr(ReadOnlySpan<byte> pool, uint off, Encoding enc)
    {
        if (off >= pool.Length) throw new FormatException($"pool: offset {off} past the pool ({pool.Length} B)");
        var s = pool[(int)off..];
        int end = s.IndexOf((byte)0);
        if (end < 0) throw new FormatException("pool: string without a NUL");
        return enc.GetString(s[..end]);
    }
}
