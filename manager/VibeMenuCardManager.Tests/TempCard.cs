// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core;

namespace VibeMenuCardManager.Tests;

/// <summary>A card in a fresh temp directory (never the user's SD card), deleted on Dispose. Realize() lays out a
/// manager/golden/cases/*.json spec exactly as manager/golden/realize.py does.</summary>
public sealed class TempCard : IDisposable
{
    public string Dir { get; } = Path.Combine(Path.GetTempPath(), "vmcm-test-" + Guid.NewGuid().ToString("N"));

    public TempCard() => Directory.CreateDirectory(Dir);

    public void Dispose()
    {
        try { Directory.Delete(Dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    public static byte[] DataBytes(int seed, int n)
    {
        var b = new byte[n];
        for (int i = 0; i < n; i++) b[i] = (byte)((seed * 131 + i * 7 + (i >> 8)) & 0xFF);
        return b;
    }

    /// <summary>realize.py bkp_bytes: a BRAM backup generation (menu/launcher/pgsbak.h layout).</summary>
    public static byte[] Bkp(JsonElement k)
    {
        var imgs = k.GetProperty("recs").EnumerateArray().Select(Content).ToList();
        var body = imgs.SelectMany(i => i.Concat(new byte[(4 - i.Length % 4) % 4])).ToArray();
        var h = new byte[64];
        "OMSATBKP"u8.CopyTo(h);
        Be.Put16(h, 8, 1);
        Be.Put16(h, 10, (ushort)imgs.Count);
        Be.Put32(h, 12, (uint)body.Length);
        Be.Put32(h, 16, System.IO.Hashing.Crc32.HashToUInt32(body));
        Be.Put32(h, 20, k.GetProperty("seq").GetUInt32());
        System.Text.Encoding.ASCII.GetBytes(k.GetProperty("date").GetString()!).CopyTo(h, 24);
        var m = System.Text.Encoding.ASCII.GetBytes(k.GetProperty("marker").GetString()!.PadRight(24));
        m.CopyTo(h, 40);
        byte[] raw = [.. h, .. imgs.SelectMany(i => i[0x10..0x30]), .. body];
        int cut = k.GetProperty("cut").GetInt32();
        return raw[..(raw.Length - cut)];
    }

    public static byte[] Content(JsonElement f)
    {
        if (f.TryGetProperty("bkp", out var bk)) return Bkp(bk);
        if (f.TryGetProperty("raw", out var raw)) return Convert.FromHexString(raw.GetString()!);
        if (f.TryGetProperty("head", out var head))
        {
            var h = Convert.FromHexString(head.GetString()!);
            var b = new byte[f.GetProperty("len").GetInt32()];
            h.CopyTo(b, 0);
            return b;
        }
        var d = f.GetProperty("data");
        return [.. Convert.FromHexString(f.GetProperty("hdr").GetString()!),
                .. DataBytes(d.GetProperty("seed").GetInt32(), d.GetProperty("len").GetInt32()),
                .. Convert.FromHexString(f.TryGetProperty("tail", out var t) ? t.GetString()! : "")];
    }

    public void Realize(JsonElement spec)
    {
        if (spec.TryGetProperty("dirs", out var dirs))
            foreach (var d in dirs.EnumerateArray()) Directory.CreateDirectory(Path.Combine(Dir, d.GetString()!));
        foreach (var f in spec.GetProperty("files").EnumerateArray())
        {
            var p = Path.Combine(Dir, f.GetProperty("path").GetString()!.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(p)!);
            File.WriteAllBytes(p, Content(f));
        }
    }

    public sealed record Row(string Path, long Size, string Sha256);

    /// <summary>Every directory (path + "/") and file, sorted by the UTF-8 bytes of the path (realize.py manifest).</summary>
    public List<Row> Manifest()
    {
        var o = new List<Row>();
        foreach (var e in new DirectoryInfo(Dir).EnumerateFileSystemInfos("*", SearchOption.AllDirectories))
        {
            string rel = Path.GetRelativePath(Dir, e.FullName).Replace(Path.DirectorySeparatorChar, '/');
            if (e is DirectoryInfo) o.Add(new Row(rel + "/", 0, ""));
            else o.Add(new Row(rel, ((FileInfo)e).Length, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(e.FullName)))));
        }
        o.Sort((a, b) => Be.CompareBytes(a.Path, b.Path));
        return o;
    }
}
