// Aether Card Manager, GPL-3.0-or-later.
// The cover sources tools/omsat/cardbuild.py draws on, ported from tools/omsat/coverzip.py and artsrc.py:
// - CoverZip: the Rings Cover Project zip (Felinni Marinas and the Rings Cover Project contributors), indexed by
//   normalised member stem and read one member at a time (the real archive is 1.6 GB);
// - RemoteArt: the retrohead Satiator box server and libretro-thumbnails, through a cache directory whose layout is
//   artsrc's (sha256(url)[:24] + ".bin" for a hit, ".miss" for a 404, ".url"), so a cache tools/omsat filled works here
//   and a build can run offline (download: false = the cache only).
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Art;

public sealed partial class CoverZip : IDisposable
{
    static readonly HashSet<string> Regions = ["JAPAN", "US", "EU"];
    static readonly string[] Ext = [".png", ".jpg", ".jpeg"];
    readonly ZipArchive _zip;
    readonly Dictionary<string, List<string>> _index = [];
    public int Indexed { get; }
    public int Ambiguous { get; private set; }
    public int Resolved { get; private set; }
    static readonly Dictionary<string, string> RegionOfDir = new() { ["JAPAN"] = "(japan)", ["US"] = "(usa)", ["EU"] = "(europe)" };

    [GeneratedRegex("\\s*\\[[^\\]]*\\]")] private static partial Regex Brackets();
    [GeneratedRegex("\\s*\\([^)]*\\)$")] private static partial Regex LastParen();
    [GeneratedRegex("\\(disc (\\d+)\\)", RegexOptions.IgnoreCase)] private static partial Regex Disc();

    public CoverZip(string path)
    {
        _zip = ZipFile.OpenRead(path);
        foreach (var e in _zip.Entries)
        {
            var parts = e.FullName.Split('/');
            if (parts.Length >= 2 && Regions.Contains(parts[^2]) && Ext.Any(x => e.FullName.EndsWith(x, StringComparison.OrdinalIgnoreCase)))
            {
                string k = NormStem(e.FullName);
                if (!_index.TryGetValue(k, out var l)) _index[k] = l = [];
                l.Add(e.FullName);
            }
        }
        Indexed = _index.Values.Sum(v => v.Count);
    }

    /// <summary>norm_stem: the file name without .png/.jpg/.jpeg/.cue/.iso, [bracketed] groups dropped, lower-cased,
    /// whitespace collapsed.</summary>
    public static string NormStem(string name)
    {
        string stem = name[(name.LastIndexOf('/') + 1)..];
        foreach (var x in Ext.Concat([".cue", ".iso"]))
            if (stem.EndsWith(x, StringComparison.OrdinalIgnoreCase)) { stem = stem[..^x.Length]; break; }
        stem = Brackets().Replace(stem, "");
        return string.Join(' ', stem.ToLowerInvariant().Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }

    /// <summary>One name under several region folders (d51022a; on the real archive every one a "(Europe)" file in both
    /// EU/ and US/): the copy in the name's own region folder wins; else, when every copy has the same CRC, any copy
    /// (the sorted first); else none.</summary>
    string? Pick(List<string> hits)
    {
        foreach (var h in hits)
        {
            var parts = h.Split('/');
            if (RegionOfDir.TryGetValue(parts[^2], out var tag) && parts[^1].Contains(tag, StringComparison.OrdinalIgnoreCase)) return h;
        }
        var crcs = hits.Select(h => _zip.GetEntry(h)!.Crc32).Distinct().Count();
        return crcs == 1 ? hits.Order(StringComparer.Ordinal).First() : null;
    }

    /// <summary>find: the one member whose stem is the image's, dropping the last parenthesised group while more than
    /// one is left (the region group is never dropped); null when absent or ambiguous.</summary>
    public string? Find(string imageStem)
    {
        string s = NormStem(imageStem);
        while (s != "")
        {
            if (_index.TryGetValue(s, out var hits))
            {
                if (hits.Count == 1) return hits[0];
                if (Pick(hits) is string pick) { Resolved++; return pick; }
                Ambiguous++;
                return null;
            }
            if (s.Count(c => c == '(') <= 1) return null;
            string next = LastParen().Replace(s, "");
            if (next == s) return null;
            s = next;
        }
        return null;
    }

    public byte[] Read(string member)
    {
        using var s = _zip.GetEntry(member)!.Open();
        using var m = new MemoryStream();
        s.CopyTo(m);
        return m.ToArray();
    }

    public RgbaImage Image(string member) => RgbaImage.Decode(Read(member));

    public static int DiscOf(string member) => Disc().Match(member) is { Success: true } m ? int.Parse(m.Groups[1].Value, System.Globalization.CultureInfo.InvariantCulture) : 0;

    public void Dispose() => _zip.Dispose();
}

public sealed partial class RemoteArt(string cache, bool download = false, Func<string, byte[]?>? fetch = null)
{
    public const string Retrohead = "https://files-ds-scene.net/retrohead/satiator/box/2d/";
    public const string Libretro = "https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Saturn/master/Named_Boxarts/";
    static readonly (char Code, string Name)[] RegionName = [('U', "USA"), ('J', "Japan"), ('E', "Europe"), ('B', "Brazil"), ('T', "Taiwan")];

    /// <summary>artsrc RETROHEAD_REJECT: a retrohead 2d/ cover that is the wrong release for its product number.</summary>
    public static readonly Dictionary<string, string> RetroheadReject = new()
    {
        ["MK-81070"] = "2d/ holds the PAL Saturn Bomberman front (black spine) for the US product number; 3d/ and libretro " +
                       "show the US release (white spine, NetLink Playable) -- checked 2026-11-19",
    };

    public List<string> Log { get; } = [];

    [GeneratedRegex("[&*/:`<>?\\\\|\"]")] private static partial Regex LibretroBad();
    [GeneratedRegex("^[0-9]+-[0-9]+$")] private static partial Regex Dashed();

    public static List<string> RegionNames(int flags) =>
        RegionName.Where(r => (flags & (1 << "JTUEB".IndexOf(r.Code))) != 0).Select(r => r.Name).ToList();

    public static string ZipTitleStem(string title, string region) => $"{title.Replace('\'', '_')} ({region})";

    public static List<string> RetroheadNames(string key)
    {
        var o = new List<string> { key };
        if (key.All(char.IsAsciiDigit) || Dashed().IsMatch(key)) o.Add("MK-" + key);
        return o;
    }

    /// <summary>urllib.parse.quote with its default safe "/": RFC 3986 unreserved bytes kept, everything else %XX.</summary>
    public static string Quote(string s)
    {
        var o = new StringBuilder();
        foreach (byte b in Encoding.UTF8.GetBytes(s))
            if (b is >= (byte)'A' and <= (byte)'Z' or >= (byte)'a' and <= (byte)'z' or >= (byte)'0' and <= (byte)'9' || "_.-~/".Contains((char)b)) o.Append((char)b);
            else o.Append('%').Append(b.ToString("X2", System.Globalization.CultureInfo.InvariantCulture));
        return o.ToString();
    }

    public byte[]? Get(string url)
    {
        string h = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(url)))[..24];
        string hit = Path.Combine(cache, h + ".bin"), miss = Path.Combine(cache, h + ".miss");
        if (File.Exists(hit)) return File.ReadAllBytes(hit);
        if (File.Exists(miss) || !download) return null;
        var data = (fetch ?? Fetch)(url);
        Directory.CreateDirectory(cache);
        File.WriteAllText(Path.Combine(cache, h + ".url"), url + "\n");
        if (data == null) { File.WriteAllBytes(miss, []); Log.Add($"404 {url}"); return null; }
        File.WriteAllBytes(hit, data);
        Log.Add($"200 {url} ({data.Length} B)");
        return data;
    }

    static byte[]? Fetch(string url)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("Aether-vmcm/0.1 (cover fallback)");
        using var r = http.GetAsync(url).GetAwaiter().GetResult();
        if (r.StatusCode == System.Net.HttpStatusCode.NotFound) return null;
        r.EnsureSuccessStatusCode();
        return r.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult();
    }

    public (RgbaImage? Img, string? Src) RetroheadCover(string key)
    {
        foreach (var name in RetroheadNames(key))
        {
            string url = Retrohead + Quote(name + ".TGA");
            var data = Get(url);
            if (data == null) continue;
            if (RetroheadReject.TryGetValue(name, out var why)) return (null, $"{url} REJECTED: {why}");
            return (RgbaImage.Decode(data), url);
        }
        return (null, null);
    }

    public (RgbaImage? Img, string? Src) LibretroCover(string title, int regionFlags)
    {
        foreach (var region in RegionNames(regionFlags))
        {
            string url = Libretro + Quote(LibretroBad().Replace($"{title} ({region})", "_") + ".png");
            var data = Get(url);
            if (data != null) return (RgbaImage.Decode(data), url);
        }
        return (null, null);
    }
}
