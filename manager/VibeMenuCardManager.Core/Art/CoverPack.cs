// Aether Card Manager, GPL-3.0-or-later.
// COVERS.PAK (tools/omsat/coverpack.py write_coverpack/read_coverpack/read_cover): the PC-side cover pack the Card
// Manager ships next to its exe, built offline from the Rings Cover Project zip, retrohead2d/3d and libretro.
// Deliberately not an OMSAT on-disc format (no sector alignment, no CommonHeader): a simple 32-byte header, a UTF-8
// JSON index, then the cover blobs. Blobs are read lazily, one Stream.Seek per request, so opening a pack never reads
// its ~90 MB of image data; only the header and the (small) JSON index are read up front.
using System.Text;
using System.Text.Json;

namespace VibeMenuCardManager.Core.Art;

/// <param name="FallbackRegion">set when the code has no cover of its own regions and this entry shows the same game's
/// cover from another region (coverpack.py): the area letter U, J or E of that cover; else "".</param>
/// <param name="FallbackFrom">with FallbackRegion: the GAMES.DB key the shown cover belongs to.</param>
public sealed record CoverPackEntry(string Source, string Format, int Offset, int Size, int Width, int Height, uint Crc32,
                                    string FallbackRegion = "", string FallbackFrom = "")
{
    /// <summary>"US", "JP" or "EU" for a region-fallback cover (the "&lt;REGION&gt; cover" tag), else null.</summary>
    public string? FallbackLabel => FallbackRegion switch { "U" => "US", "J" => "JP", "E" => "EU", "" => null, var x => x };
}

public sealed class CoverPackData : IDisposable
{
    readonly Stream _stream;
    readonly long _dataOff;
    readonly Dictionary<string, List<CoverPackEntry>> _codes;
    public uint BuildDate { get; }
    public IReadOnlyDictionary<string, List<CoverPackEntry>> Codes => _codes;

    internal CoverPackData(Stream stream, long dataOff, uint buildDate, Dictionary<string, List<CoverPackEntry>> codes)
    {
        _stream = stream; _dataOff = dataOff; BuildDate = buildDate; _codes = codes;
    }

    /// <summary>Every cover kept for `code` (coverproject/retrohead2d/retrohead3d/libretro, any that exist), else empty.</summary>
    public IReadOnlyList<CoverPackEntry> For(string code) => _codes.TryGetValue(code, out var l) ? l : [];

    /// <summary>Reads and CRC-checks one entry's blob, one seek, decoded only when the caller actually wants that
    /// cover (lazy: a 1,217-code pack is not decoded up front).</summary>
    public byte[] Blob(CoverPackEntry e)
    {
        var buf = new byte[e.Size];
        lock (_stream)
        {
            _stream.Seek(_dataOff + e.Offset, SeekOrigin.Begin);
            int got = 0;
            while (got < buf.Length)
            {
                int n = _stream.Read(buf, got, buf.Length - got);
                if (n == 0) throw new Formats.FormatException($"range: blob at offset {e.Offset} runs past the end of the pack");
                got += n;
            }
        }
        if (Formats.CommonHeader.Crc32(buf) != e.Crc32) throw new Formats.FormatException("CRC: cover blob CRC mismatch");
        return buf;
    }

    public void Dispose() => _stream.Dispose();
}

public static class CoverPack
{
    static readonly byte[] Magic = "OMSATCVR"u8.ToArray();
    const int HeaderSize = 32;
    const ushort Version = 1;

    /// <summary>Where a packaged build keeps COVERS.PAK: "covers/COVERS.PAK" next to the running exe.</summary>
    public static string DefaultPath() => Path.Combine(AppContext.BaseDirectory, "covers", "COVERS.PAK");

    /// <summary>Opens the default path, or null when it is not there (packaging has not shipped it yet, or a dev
    /// build has no copy) -- never throws for a missing file; a corrupt one still throws Formats.FormatException.</summary>
    public static CoverPackData? OpenDefault()
    {
        string p = DefaultPath();
        return File.Exists(p) ? Open(p) : null;
    }

    public static CoverPackData Open(string path) => ReadHeaderAndIndex(File.OpenRead(path));

    /// <summary>For tests: a whole in-memory pack (small synthetic packs only -- a real ~90 MB pack should use
    /// Open(path) so its blobs are not all loaded into managed memory at once).</summary>
    public static CoverPackData Read(byte[] data) => ReadHeaderAndIndex(new MemoryStream(data, writable: false));

    static CoverPackData ReadHeaderAndIndex(Stream s)
    {
        try
        {
            var header = new byte[HeaderSize];
            if (s.ReadAtLeast(header, HeaderSize, throwOnEndOfStream: false) < HeaderSize)
                throw new Formats.FormatException("range: file shorter than the 32 B header");
            if (!header.AsSpan(0, 8).SequenceEqual(Magic))
                throw new Formats.FormatException("magic: not an OMSATCVR cover pack");
            ushort version = Be.Get16(header, 8);
            if (version != Version) throw new Formats.FormatException($"version: unsupported {version}");
            uint indexLen = Be.Get32(header, 12), indexCrc = Be.Get32(header, 16), dataOff = Be.Get32(header, 20), buildDate = Be.Get32(header, 24);

            var indexBytes = new byte[indexLen];
            if (indexLen > 0 && s.ReadAtLeast(indexBytes, (int)indexLen, throwOnEndOfStream: false) < indexLen)
                throw new Formats.FormatException("range: index runs past the end of the file");
            if (Formats.CommonHeader.Crc32(indexBytes) != indexCrc) throw new Formats.FormatException("CRC: index CRC mismatch");
            if (dataOff != HeaderSize + indexLen) throw new Formats.FormatException("range: data_off does not follow the index");

            var codes = ParseIndex(indexBytes);
            return new CoverPackData(s, dataOff, buildDate, codes);
        }
        catch
        {
            s.Dispose();
            throw;
        }
    }

    static Dictionary<string, List<CoverPackEntry>> ParseIndex(byte[] indexBytes)
    {
        using var doc = JsonDocument.Parse(indexBytes);
        var root = doc.RootElement;
        if (!root.TryGetProperty("version", out var v) || v.GetInt32() != 1)
            throw new Formats.FormatException("index: unsupported version");
        var codes = new Dictionary<string, List<CoverPackEntry>>();
        if (!root.TryGetProperty("codes", out var codesEl)) return codes;
        foreach (var kv in codesEl.EnumerateObject())
        {
            var list = new List<CoverPackEntry>();
            foreach (var e in kv.Value.EnumerateArray())
                list.Add(new CoverPackEntry(
                    e.GetProperty("source").GetString() ?? "", e.GetProperty("format").GetString() ?? "",
                    e.GetProperty("offset").GetInt32(), e.GetProperty("size").GetInt32(),
                    e.GetProperty("width").GetInt32(), e.GetProperty("height").GetInt32(),
                    e.GetProperty("crc32").GetUInt32(),
                    e.TryGetProperty("fallback_region", out var fr) ? fr.GetString() ?? "" : "",
                    e.TryGetProperty("fallback_from", out var ff) ? ff.GetString() ?? "" : ""));
            codes[kv.Name] = list;
        }
        return codes;
    }
}
