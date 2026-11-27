// Aether Card Manager, GPL-3.0-or-later.
// The card scan (contract/SPEC.md v1 clarification 7): how a card becomes a LIBRARY.IDX. A port of
// tools/omsat/cardscan.py; menu/src/scan.c implements the same rule on the console, and contract/fixtures/scan_tree.*
// gates all three.
using System.Text;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Core.Card;

public interface IDirSource
{
    /// <summary>(name, isDir) for the folder at path ("" = the root), in any order.</summary>
    IReadOnlyList<(string Name, bool IsDir)> List(string path);
    SystemId? SysId(string path);
    /// <summary>The data-track file name(s) the .cue at `path` lists in its FILE lines, read from at most the first
    /// 4,096 bytes (SPEC v1 clarification 7, 2026-11-23; matches the console's own cue reader): a .cue's own track,
    /// e.g. an .iso sharing its folder, is not a separate game -- LARRY 6's game.cue + game.iso. Never throws; a cue
    /// that cannot be read gives no names (nothing is folded, same as today).</summary>
    IReadOnlyList<string> CueFiles(string path);
}

/// <summary>A card on disk (read-only).</summary>
public sealed class FsSource(string root) : IDirSource
{
    string Host(string p) => p == "" ? root : Path.Combine(root, p.Replace('/', Path.DirectorySeparatorChar));

    public IReadOnlyList<(string Name, bool IsDir)> List(string path) =>
        new DirectoryInfo(Host(path)).EnumerateFileSystemInfos().Select(e => (e.Name, e is DirectoryInfo)).ToList();

    public SystemId? SysId(string path)
    {
        try { return SystemIdReader.ReadImage(Host(path)); }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }

    const int CueRead = 4096;   // the console's om_satfs_cue_files reads the same prefix

    public IReadOnlyList<string> CueFiles(string path)
    {
        try
        {
            using var fh = File.OpenRead(Host(path));
            var buf = new byte[Math.Min(CueRead, fh.Length)];
            fh.ReadExactly(buf);
            return SystemIdReader.CueFiles(Encoding.Latin1.GetString(buf));
        }
        catch (IOException) { return []; }
        catch (UnauthorizedAccessException) { return []; }
    }
}

public sealed record ScanResult(byte[] Data, List<LibEntry> Entries, List<LibFolder> Folders, uint Fingerprint,
                                int Matched, int Unreadable, int SkippedLongPaths, int SkippedFolders,
                                Dictionary<string, uint> Dates, Dictionary<string, SystemId?> SystemIds,
                                Dictionary<string, bool> OwnFolder,
                                Dictionary<string, List<string>> CueMissing)
{
    public string Denominator => $"{Entries.Count} images";
}

public static class CardScanner
{
    public const int MaxDepth = 8, MaxFoldersDefault = 1024;

    static int B(string s) => Encoding.UTF8.GetByteCount(s);
    static string Join(string a, string b) => a == "" ? b : a + "/" + b;
    static bool IsImage(string n) => n.EndsWith(".cue", StringComparison.OrdinalIgnoreCase) || n.EndsWith(".iso", StringComparison.OrdinalIgnoreCase);

    static (List<string> Dirs, List<string> Files) Split(IReadOnlyList<(string Name, bool IsDir)> listing, bool atRoot)
    {
        var d = listing.Where(x => x.IsDir && !LibraryIndex.IsHiddenName(x.Name, atRoot)).Select(x => x.Name).ToList();
        var f = listing.Where(x => !x.IsDir && !LibraryIndex.IsHiddenName(x.Name, atRoot)).Select(x => x.Name).ToList();
        d.Sort(Be.CompareBytes);
        f.Sort(Be.CompareBytes);
        return (d, f);
    }

    /// <summary>2026-11-23 (Satiator Rings' model, clarification 7): a single-game folder holds exactly one image
    /// (after the cue dedupe) and no subfolder; any other folder, a multi-disc one included, is a folder row.</summary>
    static bool IsGameDir(List<string> sdirs, List<string> images) => sdirs.Count == 0 && images.Count == 1;

    /// <summary>Folds out of `images` any name a sibling .cue's FILE line(s) name (manager fix round: a game's data
    /// track sharing its folder, like LARRY 6's game.cue + game.iso, is not a separate game). Never changes the
    /// result when no .cue in `images` references another name also in `images` (every contract/fixtures/scan_tree.*
    /// case: a referenced track is always a .bin, already excluded by IsImage, or there is no co-located reference
    /// at all), so this does not touch the golden LIBRARY.IDX fixture.</summary>
    static List<string> DedupedImages(IDirSource src, string folderPath, List<string> images) =>
        DedupedImages(src, folderPath, images, [], null);

    /// <summary>As above, plus (Problems view): any name a .cue's FILE line names that is not present anywhere in
    /// `allFiles` (the folder's whole listing, not just its images -- a referenced .bin is never an "image") is
    /// recorded into `missing` under the cue's own SD path, compared the same way the dedupe itself compares.</summary>
    static List<string> DedupedImages(IDirSource src, string folderPath, List<string> images, List<string> allFiles,
                                      Dictionary<string, List<string>>? missing)
    {
        var consumed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var present = new HashSet<string>(allFiles, StringComparer.OrdinalIgnoreCase);
        foreach (var img in images)
            if (img.EndsWith(".cue", StringComparison.OrdinalIgnoreCase))
                foreach (var f in src.CueFiles(Join(folderPath, img)))
                {
                    if (string.Equals(f, img, StringComparison.OrdinalIgnoreCase)) continue;   // a cue never excludes itself
                    consumed.Add(f);
                    if (missing != null && !present.Contains(f))
                    {
                        string cp = Join(folderPath, img);
                        (missing.TryGetValue(cp, out var l) ? l : missing[cp] = []).Add(f);
                    }
                }
        return consumed.Count == 0 ? images : images.Where(f => !consumed.Contains(f)).ToList();
    }

    public static ScanResult Scan(IDirSource src, IEnumerable<string> keys, IReadOnlyDictionary<string, string> aliases,
                                  uint buildDate, int maxFolders = MaxFoldersDefault, IReadOnlyDictionary<string, string>? forcedKeys = null)
    {
        var matcher = new Matcher(keys, aliases);
        var folders = new List<LibFolder> { new("", 0xFFFF) };
        var entries = new List<LibEntry>();
        var dates = new Dictionary<string, uint>();
        var sysIds = new Dictionary<string, SystemId?>();
        var ownFolder = new Dictionary<string, bool>();
        var cueMissing = new Dictionary<string, List<string>>();
        int unreadable = 0, skippedLong = 0, skippedFolders = 0;
        var rootListing = src.List("");

        // own (true) when the image's immediate folder belongs to this game alone (a game
        // directory per IsGameDir -- one image, or a multi-disc set of the same game); false for a loose image that
        // shares its folder with other things (a container, root, or a folder that also holds a subfolder --
        // "image beside subfolder"). Kept alongside SystemIds as a per-path side table (not in LibEntry) so
        // LIBRARY.IDX's binary format stays untouched; no longer read by the display-name rule (a game's
        // default name is always its own file name now), but still useful diagnostically.
        void Add(string path, SystemId? sid, int fid, bool box, bool own)
        {
            sysIds[path] = sid;
            ownFolder[path] = own;
            int area = 0, disc = 1, total = 1;
            if (sid != null)
            {
                area = GamesDb.RegionFlagsFrom(sid.Area);
                (disc, total) = SystemIdReader.DiscNM(sid.Device);
            }
            // "Match to game..." (M-art-pack §3): a staged match override wins over whatever the matcher finds (or
            // does not find -- it also applies when sid is null, so a homebrew/translation with no readable header
            // can still be forced to a key).
            string key = forcedKeys != null && forcedKeys.TryGetValue(path, out var fk) ? fk
                        : sid != null ? matcher.Match(sid.Product, sid.Area).Key ?? "" : "";
            string d = sid?.ReleaseDate ?? "";
            if (key != "" && d.Length == 8 && d.All(char.IsAsciiDigit) && uint.Parse(d, System.Globalization.CultureInfo.InvariantCulture) / 10000 is >= 1900 and <= 2099)
                dates[key] = dates.TryGetValue(key, out var old) ? Math.Min(old, uint.Parse(d, System.Globalization.CultureInfo.InvariantCulture)) : uint.Parse(d, System.Globalization.CultureInfo.InvariantCulture);
            entries.Add(new LibEntry(path, key, disc, total, area, path.EndsWith(".cue", StringComparison.OrdinalIgnoreCase) ? 1 : 2, fid, box));
        }

        var queue = new List<(int Fid, string Path, int Depth)> { (0, "", 0) };
        for (int qi = 0; qi < queue.Count; qi++)
        {
            var (fid, path, depth) = queue[qi];
            var (dirs, files) = Split(path == "" ? rootListing : src.List(path), path == "");
            bool hereHasBox = files.Contains("BOX.TGA");
            var hereImages = DedupedImages(src, path, files.Where(IsImage).ToList(), files, cueMissing);
            foreach (var name in hereImages)
            {
                string p = Join(path, name);
                if (B(p) > 255) { skippedLong++; continue; }
                var sid = src.SysId(p);
                if (sid == null) unreadable++;
                Add(p, sid, fid, hereHasBox, own: false);
            }
            foreach (var d in dirs)
            {
                string sub = Join(path, d);
                var (sdirs, sfiles) = Split(src.List(sub), false);
                var images = DedupedImages(src, sub, sfiles.Where(IsImage).ToList(), sfiles, cueMissing);
                var sids = IsGameDir(sdirs, images) ? images.Select(f => src.SysId(Join(sub, f))).ToList() : [];
                if (sids.Count > 0)
                {
                    bool box = sfiles.Contains("BOX.TGA");
                    for (int i = 0; i < images.Count; i++)
                    {
                        if (B(Join(sub, images[i])) > 255) { skippedLong++; continue; }
                        if (sids[i] == null) unreadable++;
                        Add(Join(sub, images[i]), sids[i], fid, box, own: true);
                    }
                }
                else if (B(sub) > 255) skippedLong++;
                else if (depth + 1 > MaxDepth) skippedFolders++;          // depth 9 or deeper: not entered
                else if (folders.Count >= maxFolders) skippedFolders++;   // the folder cap (root included)
                else
                {
                    folders.Add(new LibFolder(sub, fid));
                    queue.Add((folders.Count - 1, sub, depth + 1));
                }
            }
        }
        entries.Sort((a, b) => a.FolderId != b.FolderId ? a.FolderId.CompareTo(b.FolderId) : Be.CompareBytes(a.Path, b.Path));
        uint fp = LibraryIndex.CardFingerprint(rootListing.Select(x => x.Name));
        return new ScanResult(LibraryIndex.Write(entries, folders, fp, buildDate), entries, folders, fp,
                              entries.Count(e => e.Key != ""), unreadable, skippedLong, skippedFolders, dates, sysIds, ownFolder,
                              cueMissing);
    }
}
