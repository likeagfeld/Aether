// Aether Card Manager, GPL-3.0-or-later.
// Big-endian field access: every multi-byte field in every Aether and Kai format is big-endian (SH-2 native; design
// spec §2, contract/SPEC.md §5.16).
using System.Buffers.Binary;

namespace VibeMenuCardManager.Core;

public static class Be
{
    public static uint Get32(ReadOnlySpan<byte> b, int off) => BinaryPrimitives.ReadUInt32BigEndian(b[off..]);
    public static ushort Get16(ReadOnlySpan<byte> b, int off) => BinaryPrimitives.ReadUInt16BigEndian(b[off..]);
    public static void Put32(Span<byte> b, int off, uint v) => BinaryPrimitives.WriteUInt32BigEndian(b[off..], v);
    public static void Put16(Span<byte> b, int off, ushort v) => BinaryPrimitives.WriteUInt16BigEndian(b[off..], v);

    /// <summary>Unsigned byte order of two strings' UTF-8 encodings (C strcmp on the bytes a card holds).</summary>
    public static int CompareBytes(string a, string b) =>
        System.Text.Encoding.UTF8.GetBytes(a).AsSpan().SequenceCompareTo(System.Text.Encoding.UTF8.GetBytes(b));

    public static readonly IComparer<string> ByteOrder = Comparer<string>.Create(CompareBytes);

    /// <summary>ASCII-only case-insensitive equality (bupfile.c / pgsplan.c ieq): only a-z fold.</summary>
    public static bool AsciiIEq(string a, string b)
    {
        if (a.Length != b.Length) return false;
        for (int i = 0; i < a.Length; i++)
        {
            char x = a[i] is >= 'a' and <= 'z' ? (char)(a[i] - 32) : a[i];
            char y = b[i] is >= 'a' and <= 'z' ? (char)(b[i] - 32) : b[i];
            if (x != y) return false;
        }
        return true;
    }
}
