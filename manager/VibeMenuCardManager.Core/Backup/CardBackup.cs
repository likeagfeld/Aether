// Aether Card Manager, GPL-3.0-or-later.
// A quiet PC-side backup of everything a card rebuild cannot recreate, taken whenever a card is opened (coordinator
// round, "PC backup/restore"): USEREDIT.INI, every user BOX.TGA and FOLDER.PNG/.JPG/.TGA (card-relative paths kept),
// FAVS.TXT, PERGAME.INI, /aether/CHEATS.PAK (when present) and the whole /aether/SAVES tree. Reads the card only,
// never writes to it; everything it writes lives under the per-user app-data folder, never on the card. GAMES.DB,
// LIBRARY.IDX and ART.PAK are deliberately excluded -- a rebuild always recreates them from the files above.
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace VibeMenuCardManager.Core.Backup;

/// <summary>One file as a snapshot's manifest records it: its card-relative path (forward slashes, no leading '/')
/// and its content hash, which is what "nothing changed since the last snapshot" and the restore diff both compare.</summary>
public sealed record ManifestEntry(string CardPath, string Sha256, long Size);

public sealed record Manifest(List<ManifestEntry> Files)
{
    /// <summary>The same manifest, keyed by path, for a diff or a "does this match the last snapshot" check.</summary>
    public Dictionary<string, ManifestEntry> ByPath => Files.ToDictionary(f => f.CardPath, f => f);

    /// <summary>Order-independent: two manifests with the same (path, hash, size) set are the same snapshot in
    /// everything that matters, even if Files was built by walking the disk in a different order.</summary>
    public bool SameContentAs(Manifest other)
    {
        if (Files.Count != other.Files.Count) return false;
        var a = ByPath; var b = other.ByPath;
        return a.Count == b.Count && a.All(kv => b.TryGetValue(kv.Key, out var o) && o.Sha256 == kv.Value.Sha256 && o.Size == kv.Value.Size);
    }
}

public sealed record SnapshotInfo(string Dir, DateTime Time, Manifest Manifest)
{
    public string Name => Path.GetFileName(Dir);
}

/// <summary>The result of opening a card: either a new snapshot was written, or the last one already matches (no-op).</summary>
public sealed record BackupResult(bool Created, string CardFolder, string? SnapshotDir, int Files, long Bytes);

public static class CardBackup
{
    const string ManifestFile = "manifest.json";
    public const int KeepSnapshots = 10;

    /// <summary>Every file a snapshot copies when present on the card, besides the BOX.TGA/FOLDER.* walk and the
    /// SAVES tree (below) -- GAMES.DB/LIBRARY.IDX/ART.PAK are not here on purpose: a rebuild always recreates them.</summary>
    public static readonly string[] FixedFiles =
        ["/aether/USEREDIT.INI", "/aether/FAVS.TXT", "/aether/PERGAME.INI", "/aether/CHEATS.PAK"];

    static readonly string[] FolderArtNames = ["FOLDER.PNG", "FOLDER.JPG", "FOLDER.TGA"];

    /// <summary>Tests: a folder under the scratch/temp area, never the real per-user app-data folder.</summary>
    public static string? RootOverrideForTests;

    public static string DefaultRoot() =>
        Path.Combine(AppDataDir.Get(), "Backups");

    public static string Root() => RootOverrideForTests ?? DefaultRoot();

    /// <summary>"Backups/&lt;label&gt;-&lt;volume serial&gt;" (Card.CardTarget.Inspect's own label/serial, so this
    /// never duplicates the volume-identification logic CardWriteGuard already relies on). No label -> "card".</summary>
    public static string CardFolderName(Card.CardTarget target)
    {
        string label = string.IsNullOrWhiteSpace(target.VolumeLabel) ? "card" : target.VolumeLabel!;
        string serial = "unknown";
        if (target.Volume.Id is string id)
        {
            int i = id.IndexOf("serial ", StringComparison.Ordinal);
            if (i >= 0) serial = id[(i + 7)..].Trim();
        }
        return Sanitize(label) + "-" + Sanitize(serial);
    }

    static string Sanitize(string s)
    {
        var sb = new StringBuilder();
        foreach (char c in s) sb.Append(Array.IndexOf(Path.GetInvalidFileNameChars(), c) >= 0 || c == ' ' ? '_' : c);
        return sb.Length == 0 ? "_" : sb.ToString();
    }

    /// <summary>Every card-relative path ("/aether/FAVS.TXT", "Games/Foo/BOX.TGA", "/aether/SAVES/.../x") this
    /// backup copies, in the order they will be hashed/copied. Walking the disk directly (not CardLoad's in-memory
    /// model) so a file Explorer dropped onto the card is still backed up even before the next Refresh.</summary>
    public static List<string> CollectFiles(string root)
    {
        var o = new List<string>();
        foreach (var f in FixedFiles)
            if (File.Exists(Host(root, f))) o.Add(f);
        o.AddRange(WalkArt(root));
        string saves = Host(root, "/aether/SAVES");
        if (Directory.Exists(saves))
            foreach (var p in Directory.EnumerateFiles(saves, "*", SearchOption.AllDirectories))
                o.Add("/" + Rel(root, p));
        return o;
    }

    /// <summary>Every BOX.TGA and FOLDER.PNG/.JPG/.TGA anywhere on the card except inside /aether (that tree's own
    /// files are handled by FixedFiles/the SAVES walk, and GAMES.DB's own art lives in ART.PAK, not loose files).</summary>
    static IEnumerable<string> WalkArt(string root)
    {
        var o = new List<string>();
        void Walk(string dir)
        {
            IEnumerable<string> entries;
            try { entries = Directory.EnumerateFileSystemEntries(dir); }
            catch (IOException) { return; }
            catch (UnauthorizedAccessException) { return; }
            foreach (var p in entries)
            {
                if (Directory.Exists(p))
                {
                    if (dir == root && Card.CardPaths.IsMenuFolderName(Path.GetFileName(p))) continue;
                    Walk(p);
                }
                else
                {
                    string name = Path.GetFileName(p).ToUpperInvariant();
                    if (name == "BOX.TGA" || FolderArtNames.Contains(name)) o.Add("/" + Rel(root, p));
                }
            }
        }
        Walk(root);
        return o;
    }

    static string Host(string root, string sdPath) => Path.Combine(root, sdPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
    static string Rel(string root, string full) => Path.GetRelativePath(root, full).Replace(Path.DirectorySeparatorChar, '/');

    static string Sha256Hex(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));

    /// <summary>Hashes every file CollectFiles names, right now, on the card.</summary>
    public static Manifest BuildManifest(string root)
    {
        var files = new List<ManifestEntry>();
        foreach (var p in CollectFiles(root))
        {
            byte[] data;
            try { data = File.ReadAllBytes(Host(root, p)); }
            catch (IOException) { continue; }       // vanished between the listing and the read: skip, not fatal
            catch (UnauthorizedAccessException) { continue; }
            files.Add(new ManifestEntry(p, Sha256Hex(data), data.LongLength));
        }
        return new Manifest(files);
    }

    static JsonSerializerOptions JsonOpts => new() { WriteIndented = false };

    public static Manifest? ReadManifest(string snapshotDir)
    {
        string p = Path.Combine(snapshotDir, ManifestFile);
        if (!File.Exists(p)) return null;
        try
        {
            var m = JsonSerializer.Deserialize<Manifest>(File.ReadAllText(p));
            // a snapshot taken before the Aether rename names the menu's folder /vibemenu: read as /aether (the card's
            // folder now); ReadSnapshotFile finds its files under the old name
            return m == null ? null : new Manifest(m.Files.Select(f => f with { CardPath = Renamed(f.CardPath) }).ToList());
        }
        catch (JsonException) { return null; }
    }

    /// <summary>Every snapshot under a card's own backup folder, newest first (the timestamp name sorts correctly).</summary>
    public static List<SnapshotInfo> ListSnapshots(string cardFolder)
    {
        if (!Directory.Exists(cardFolder)) return [];
        var o = new List<SnapshotInfo>();
        foreach (var dir in Directory.EnumerateDirectories(cardFolder).OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal))
        {
            var m = ReadManifest(dir);
            if (m == null) continue;
            string name = Path.GetFileName(dir);
            DateTime t = DateTime.TryParseExact(name, "yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture,
                System.Globalization.DateTimeStyles.None, out var dt) ? dt : Directory.GetCreationTimeUtc(dir);
            o.Add(new SnapshotInfo(dir, t, m));
        }
        return o;
    }

    /// <summary>Takes a new snapshot of `root` under its own card folder, unless the last snapshot already has the
    /// exact same files/hashes (then nothing is written: BackupResult.Created is false). Prunes down to the newest
    /// KeepSnapshots afterward. Read-only on the card: every byte it touches there is a read.</summary>
    public static BackupResult Snapshot(string root, Card.CardTarget target, DateTime nowUtc)
    {
        string cardFolder = Path.Combine(Root(), CardFolderName(target));
        var now = BuildManifest(root);
        var last = ListSnapshots(cardFolder).FirstOrDefault();
        if (last != null && now.SameContentAs(last.Manifest))
            return new BackupResult(false, cardFolder, null, now.Files.Count, now.Files.Sum(f => f.Size));

        string snap = Path.Combine(cardFolder, nowUtc.ToString("yyyyMMdd-HHmmss"));
        Directory.CreateDirectory(snap);
        foreach (var f in now.Files)
        {
            string dest = Path.Combine(snap, "files", f.CardPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            File.Copy(Host(root, f.CardPath), dest, overwrite: true);
        }
        File.WriteAllText(Path.Combine(snap, ManifestFile), JsonSerializer.Serialize(now, JsonOpts));
        Prune(cardFolder);
        return new BackupResult(true, cardFolder, snap, now.Files.Count, now.Files.Sum(f => f.Size));
    }

    /// <summary>Keeps only the newest KeepSnapshots snapshot folders under a card's backup folder (local pruning
    /// only -- this never touches the card).</summary>
    public static void Prune(string cardFolder)
    {
        if (!Directory.Exists(cardFolder)) return;
        var dirs = Directory.EnumerateDirectories(cardFolder).OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal).ToList();
        foreach (var d in dirs.Skip(KeepSnapshots))
            try { Directory.Delete(d, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    /// <summary>A snapshot file's bytes, by its card-relative path as the manifest recorded it.</summary>
    public static byte[] ReadSnapshotFile(string snapshotDir, string cardPath)
    {
        string F(string cp) => Path.Combine(snapshotDir, "files", cp.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
        string p = F(cardPath), old = Card.CardPaths.Root + "/";
        if (!File.Exists(p) && cardPath.StartsWith(old, StringComparison.Ordinal))
            p = F("/" + Card.CardPaths.OldFolder + "/" + cardPath[old.Length..]);   // a pre-rename snapshot
        return File.ReadAllBytes(p);
    }

    /// <summary>"/vibemenu/X" (a pre-rename snapshot's path) as "/aether/X"; anything else unchanged.</summary>
    public static string Renamed(string cardPath)
    {
        string old = "/" + Card.CardPaths.OldFolder + "/";
        return cardPath.StartsWith(old, StringComparison.OrdinalIgnoreCase) ? Card.CardPaths.Root + "/" + cardPath[old.Length..] : cardPath;
    }
}
