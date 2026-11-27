// Aether Card Manager, GPL-3.0-or-later.
// Pre-save checks: run before any write, so Save changes blocks with a clear
// message instead of failing part-way through the transaction. Two checks: is there enough free space on the card
// for the whole change set, and is every file or folder Apply() is about to touch actually writable (not read-only,
// not held open by another program). Advisory, not a volume-identity guard (that is CardWriteGuard's job): a
// best-effort estimate is the right amount of certainty for "will this fit and will this write succeed".
namespace VibeMenuCardManager.Core.Card;

public static class PreSaveChecks
{
    /// <summary>A conservative estimate of the bytes this save will add to the card: each staged cover's encoded
    /// size (BOX.TGA / FOLDER.PNG), and the three rebuilt files (when a preview rebuild is given -- the caller runs
    /// CardRebuilder.Run once to get the real sizes, the same bytes Apply() will write).</summary>
    public static long EstimateBytesNeeded(CardLoad load, RebuildResult? rebuild)
    {
        long needed = 0;
        foreach (var row in load.Rows.Where(r => !r.Removed && r.PendingCover != null))
            needed += Art.TgaEncoder.Encode(row.PendingCover!).LongLength;
        foreach (var folder in load.Folders.Where(f => f.PendingCover != null))
            needed += Art.CoverEncoder.EncodePng(folder.PendingCover!).LongLength;
        if (rebuild != null) needed += rebuild.GamesDb.LongLength + rebuild.LibraryIdx.LongLength + rebuild.ArtPak.LongLength;
        return needed;
    }

    /// <summary>Every existing path Apply() is about to write to -- a path that does not exist yet has nothing on
    /// it to be locked or read-only.</summary>
    public static IEnumerable<string> TargetPaths(string root, CardLoad load)
    {
        string H(string sd) => Path.Combine(root, sd.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));
        foreach (var row in load.Rows.Where(r => !r.Removed && r.PendingCover != null))
            yield return H((row.Folder == "" ? "" : row.Folder + "/") + "BOX.TGA");
        foreach (var folder in load.Folders.Where(f => f.PendingCover != null || f.RemoveArtStaged))
            yield return H(folder.Path);
        yield return H("aether/FAVS.TXT");
        yield return H("aether/PERGAME.INI");
        yield return H("aether/USEREDIT.INI");
        yield return H("aether/GAMES.DB");
        yield return H("aether/LIBRARY.IDX");
        yield return H("aether/ART.PAK");
    }

    /// <summary>Clear, blocking problems found before any write; empty means it is safe to proceed. `freeBytes`:
    /// the target volume's available free space (the caller resolves it, e.g. DriveInfo).</summary>
    public static List<string> Run(string root, CardLoad load, RebuildResult? rebuild, long freeBytes)
    {
        var problems = new List<string>();
        long needed = EstimateBytesNeeded(load, rebuild);
        long margin = Math.Max(1_000_000, needed / 20);     // 5%, at least 1 MB headroom
        long required = needed + margin;
        if (freeBytes < required)
            problems.Add($"not enough free space: this save needs about {required / 1_048_576} MB, {freeBytes / 1_048_576} MB free on the card");

        foreach (var path in TargetPaths(root, load).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            bool isDir = Directory.Exists(path);
            bool isFile = !isDir && File.Exists(path);
            if (!isDir && !isFile) continue;
            try
            {
                var attr = File.GetAttributes(path);
                if ((attr & FileAttributes.ReadOnly) != 0) { problems.Add($"{path} is read-only"); continue; }
                if (isFile)
                    using (File.Open(path, FileMode.Open, FileAccess.ReadWrite, FileShare.Read)) { }
            }
            catch (UnauthorizedAccessException) { problems.Add($"{path}: access denied (read-only or permissions)"); }
            catch (IOException) { problems.Add($"{path} is open in another program"); }
        }
        return problems;
    }
}
