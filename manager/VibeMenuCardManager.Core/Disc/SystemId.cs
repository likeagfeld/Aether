// Aether Card Manager, GPL-3.0-or-later.
// The Saturn System ID (the first 256 bytes of IP.BIN). Field layout per ST-040-R4 §4.2 (via contract/SPEC.md §6 and
// docs/research/research_cardmanager.md §5.2, which read it); raw-sector offsets per SPEC v1 clarification 4 (byte 16
// of a MODE1/2352 sector, 24 of MODE2/2352, after the 00 FF x10 00 sync; byte 15 is the mode). A port of
// tools/omsat/iphdr.py.
using System.Text;
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Disc;

public sealed record SystemId(string Maker, string Product, string Version, string ReleaseDate, string Device,
                              string Area, string Peripherals, string Title, byte[] Raw256)
{
    public ReadOnlySpan<byte> ProductField => Raw256.AsSpan(0x20, 10);
    public ReadOnlySpan<byte> AreaField => Raw256.AsSpan(0x40, 10);
}

public static partial class SystemIdReader
{
    public static ReadOnlySpan<byte> HardwareId => "SEGA SEGASATURN "u8;

    static readonly Encoding Ascii = Encoding.GetEncoding("us-ascii", EncoderFallback.ReplacementFallback,
                                                          new DecoderReplacementFallback("�"));

    static string Txt(ReadOnlySpan<byte> b) => Ascii.GetString(b).TrimEnd(' ', '\0');

    /// <summary>null unless the 256 bytes start with the hardware identifier "SEGA SEGASATURN ".</summary>
    public static SystemId? Parse(ReadOnlySpan<byte> b)
    {
        if (b.Length < 256 || !b[..16].SequenceEqual(HardwareId)) return null;
        return new SystemId(Txt(b.Slice(0x10, 16)), Txt(b.Slice(0x20, 10)), Txt(b.Slice(0x2A, 6)), Txt(b.Slice(0x30, 8)),
                            Txt(b.Slice(0x38, 8)), Txt(b.Slice(0x40, 10)), Txt(b.Slice(0x50, 16)), Txt(b.Slice(0x60, 112)),
                            b[..256].ToArray());
    }

    /// <summary>kai_ip_crc (tools/omsat/iphdr.py): the IP CRC Pseudo Saturn Kai matches its compat and cheat tables by
    /// (PSKai v6.545 ip_header.c:39-69): its Save Data Manager (title CRC 0x157E3779) is 0xCAFE0100, every other IP a
    /// CRC-32 over bytes 0x10-0x3F then 0x50-0xFF, skipping the hardware id and the area field (a region-patched disc
    /// keeps its CRC).</summary>
    public static uint KaiIpCrc(ReadOnlySpan<byte> raw)
    {
        if (Formats.CommonHeader.Crc32(raw.Slice(0x60, 24)) == 0x157E3779) return 0xCAFE0100;
        var b = new byte[48 + 176];
        raw.Slice(0x10, 48).CopyTo(b);
        raw.Slice(0x50, 176).CopyTo(b.AsSpan(48));
        return Formats.CommonHeader.Crc32(b);
    }

    static readonly (string Name, int Off, int Len)[] Fields =
    [("maker", 0x10, 16), ("product", 0x20, 10), ("version", 0x2A, 6), ("release_date", 0x30, 8), ("device", 0x38, 8),
     ("area", 0x40, 10), ("peripherals", 0x50, 16), ("title", 0x60, 112)];

    /// <summary>make_system_id (tools/omsat/iphdr.py): 256 bytes, spaces to 0xCF and NULs after, the hardware id, each
    /// named field left-justified and space-padded. Fixtures and tests use it.</summary>
    public static byte[] Make(IReadOnlyDictionary<string, string> fields)
    {
        var b = new byte[256];
        b.AsSpan(0, 0xD0).Fill((byte)' ');
        HardwareId.CopyTo(b);
        foreach (var (name, off, len) in Fields)
        {
            var v = Encoding.ASCII.GetBytes(fields.TryGetValue(name, out var s) ? s : "");
            if (v.Length > len) throw new ArgumentException($"{name} longer than {len}");
            b.AsSpan(off, len).Fill((byte)' ');
            v.CopyTo(b, off);
        }
        return b;
    }

    static readonly byte[] Sync =[0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];

    /// <summary>The System ID of a raw track-1 sector or a 2048-byte-sector image's start.</summary>
    public static SystemId? FromImageStart(ReadOnlySpan<byte> data)
    {
        if (data.Length >= 16 && data[..12].SequenceEqual(Sync))
        {
            int off = data[15] == 1 ? 16 : 24;
            return data.Length >= off + 256 ? Parse(data.Slice(off, 256)) : null;
        }
        return Parse(data);
    }

    /// <summary>Every FILE line's file name, path components stripped (SPEC v1 clarification 7, 2026-11-23): a line
    /// whose first non-blank word is FILE (any case, immediately followed by a blank), then a name in double quotes
    /// (no closing quote on that line: the line is skipped) or a bare word (ends at the next blank). A port of
    /// tools/omsat/cardscan.py cue_file_names / menu/src/sysid.c om_cue_files, byte for byte: used by the scanner to
    /// fold a cue's own data track(s) out of its folder's image list (LARRY 6's game.cue + game.iso is one game, not
    /// two), and by this reader to resolve a .cue's first track.</summary>
    public static List<string> CueFiles(string text)
    {
        var o = new List<string>();
        foreach (var raw in text.Split('\n'))
        {
            var t = raw.TrimStart(' ', '\t');
            string first4 = t.Length >= 4 ? t[..4] : t;
            if (!first4.Equals("file", StringComparison.OrdinalIgnoreCase)) continue;
            string after4 = t.Length > 4 ? t[4..5] : "";
            if (after4 != " " && after4 != "\t") continue;
            t = t[4..].TrimStart(' ', '\t');
            string name;
            if (t.StartsWith('"'))
            {
                int end = t.IndexOf('"', 1);
                if (end < 0) continue;      // no closing quote on this line: skipped
                name = t[1..end];
            }
            else
            {
                int end = 0;
                while (end < t.Length && t[end] is not (' ' or '\t' or '\r')) end++;
                name = t[..end];
            }
            name = name.Replace('\\', '/');
            int slash = name.LastIndexOf('/');
            if (slash >= 0) name = name[(slash + 1)..];
            if (name != "") o.Add(name);
        }
        return o;
    }

    /// <summary>.cue: the first FILE line's track file; anything else: the file itself. null when no System ID.</summary>
    public static SystemId? ReadImage(string path)
    {
        string p = path;
        if (path.EndsWith(".cue", StringComparison.OrdinalIgnoreCase))
        {
            string? f = CueFiles(File.ReadAllText(path, Encoding.Latin1)).FirstOrDefault();
            if (f == null) return null;
            p = Path.Combine(Path.GetDirectoryName(path)!, f);
        }
        if (!File.Exists(p)) return null;
        using var fh = File.OpenRead(p);
        var buf = new byte[2352];
        int n = fh.Read(buf, 0, buf.Length);
        return FromImageStart(buf.AsSpan(0, n));
    }

    [GeneratedRegex("CD-([0-9]+)/([0-9]+)")] private static partial Regex DiscRe();

    /// <summary>"CD-n/m" from the device information field (0x38); (1, 1) when absent.</summary>
    public static (int N, int M) DiscNM(string device)
    {
        var m = DiscRe().Match(device.ToUpperInvariant());
        return m.Success ? (int.Parse(m.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture), int.Parse(m.Groups[2].Value, System.Globalization.CultureInfo.InvariantCulture)) : (1, 1);
    }
}
