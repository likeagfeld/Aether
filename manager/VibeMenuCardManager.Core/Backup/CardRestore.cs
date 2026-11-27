// Aether Card Manager, GPL-3.0-or-later.
// Staging a PC backup snapshot back onto the open card ("PC backup/restore"). This never writes
// to the card by itself: Plan() is a read-only diff (for "pick a snapshot, see what differs"), and Stage() only
// mutates the in-memory CardLoad session -- the same staged-edit model every other edit in this app uses -- so
// nothing reaches the card until the user runs Save changes through CardWriteGuard, same as any other edit.
// USEREDIT.INI/FAVS.TXT/PERGAME.INI merge into the live session fields Apply() already regenerates those files
// from (so a restore and an unrelated in-progress edit combine correctly); BOX.TGA/FOLDER.* restore into the same
// PendingCover fields the cover picker uses; CHEATS.PAK and the /aether/SAVES tree have no staged-edit model of
// their own, so they are queued as raw tracked writes (CardLoad.PendingRestoreRawFiles) that Apply() writes verbatim
// -- after explicitly refusing to ever write "/menu.bin" or any path outside what a snapshot can contain.
using System.Text;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Card;

namespace VibeMenuCardManager.Core.Backup;

public enum RestoreAction { WillRestore, Unchanged, Skipped }

public sealed record RestoreDiffEntry(string CardPath, RestoreAction Action, string Reason);

public static class CardRestore
{
    static string Host(string root, string sdPath) => Path.Combine(root, sdPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));

    static string Sha256Hex(byte[] data) => Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(data));

    /// <summary>Whether `cardPath` (one of CardBackup.CollectFiles' own shapes) has a safe place to stage into right
    /// now, and why not when it doesn't -- a shared (multi-game) folder's BOX.TGA has no single owning GameRow, the
    /// same rule Save changes itself enforces (CardEditSession.Apply, "never overwrite a shared or root cover").</summary>
    static (bool Ok, string Reason) CanStage(CardLoad load, string cardPath)
    {
        if (CardBackup.FixedFiles.Contains(cardPath)) return (true, "USEREDIT.INI/FAVS.TXT/PERGAME.INI/CHEATS.PAK");
        if (cardPath.StartsWith("/aether/SAVES/", StringComparison.Ordinal)) return (true, "a save file");
        string rel = cardPath.TrimStart('/');
        if (Path.GetFileName(rel).Equals("BOX.TGA", StringComparison.OrdinalIgnoreCase))
        {
            string folder = rel.Length > "BOX.TGA".Length ? rel[..^"/BOX.TGA".Length] : "";
            bool has = load.Rows.Any(r => !r.Removed && r.OwnFolder && r.Folder == folder);
            return has ? (true, "this folder's own cover") : (false, "a shared folder's BOX.TGA belongs to no single game: no safe place to stage it");
        }
        string name = Path.GetFileName(rel).ToUpperInvariant();
        if (name is "FOLDER.PNG" or "FOLDER.JPG" or "FOLDER.TGA")
        {
            string folder = rel.Length > name.Length ? rel[..^(name.Length + 1)] : "";
            bool has = load.Folders.Any(f => f.Path == folder);
            return has ? (true, "this folder's art") : (false, "this folder no longer exists on the card");
        }
        return (false, "not a file this app restores");
    }

    /// <summary>A read-only diff of a snapshot against the card as it stands right now (CardLoad's own Rows/Folders,
    /// for the OwnFolder/FolderRow lookups CanStage needs -- the raw bytes are still read straight off disk, since
    /// GAMES.DB/LIBRARY.IDX only ever describe games, never the USEREDIT.INI/FAVS.TXT/SAVES bytes themselves).</summary>
    public static List<RestoreDiffEntry> Plan(CardLoad load, string root, SnapshotInfo snap)
    {
        var o = new List<RestoreDiffEntry>();
        foreach (var f in snap.Manifest.Files.OrderBy(f => f.CardPath, StringComparer.Ordinal))
        {
            string host = Host(root, f.CardPath);
            string? curHash = null;
            try { if (File.Exists(host)) curHash = Sha256Hex(File.ReadAllBytes(host)); }
            catch (IOException) { } catch (UnauthorizedAccessException) { }
            if (curHash == f.Sha256) { o.Add(new(f.CardPath, RestoreAction.Unchanged, "already matches the card")); continue; }
            var (ok, reason) = CanStage(load, f.CardPath);
            o.Add(new(f.CardPath, ok ? RestoreAction.WillRestore : RestoreAction.Skipped, reason));
        }
        return o;
    }

    /// <summary>Stages every WillRestore entry from Plan() into `load` -- merged into the normal staged-edit fields
    /// where one exists, else queued as a raw tracked write -- and returns the same diff for the confirmation/summary.
    /// Never touches the card; the caller still runs Save changes (CardWriteGuard) to actually write anything.</summary>
    public static List<RestoreDiffEntry> Stage(CardLoad load, string root, SnapshotInfo snap)
    {
        var plan = Plan(load, root, snap);
        int staged = 0;
        foreach (var e in plan.Where(e => e.Action == RestoreAction.WillRestore))
        {
            byte[] data = CardBackup.ReadSnapshotFile(snap.Dir, e.CardPath);
            switch (e.CardPath)
            {
                case "/aether/USEREDIT.INI":
                    StageUserEdit(load, data);
                    break;
                case "/aether/FAVS.TXT":
                    StageFavs(load, data);
                    break;
                case "/aether/PERGAME.INI":
                    load.PerGameText = Encoding.ASCII.GetString(data);
                    break;
                case "/aether/CHEATS.PAK":
                    StageRaw(load, e.CardPath, data);
                    break;
                default:
                    if (e.CardPath.StartsWith("/aether/SAVES/", StringComparison.Ordinal)) { StageRaw(load, e.CardPath, data); break; }
                    StageCoverOrFolderArt(load, e.CardPath, data);
                    break;
            }
            staged++;
        }
        if (staged > 0) load.PendingRestoreFrom = snap.Name;
        return plan;
    }

    /// <summary>True when `key`'s live USEREDIT.INI record already differs from what Load() actually read off the
    /// card -- an edit staged in this session but not yet saved, which a restore must never silently discard.</summary>
    static bool HasUnsavedUserEdit(CardLoad load, string key)
    {
        var now = load.UserEdits.GetValueOrDefault(key) ?? new Formats.UserEditRecord();
        var was = load.OriginalUserEdits.GetValueOrDefault(key) ?? new Formats.UserEditRecord();
        return !now.SameAs(was);
    }

    static void StageUserEdit(CardLoad load, byte[] data)
    {
        string text = Encoding.ASCII.GetString(data);
        var backupEdits = Formats.UserEdits.Parse(text);
        // Replace from the backup, key by key, EXCEPT a key with an unsaved edit right now (HasUnsavedUserEdit):
        // a plain wholesale replace would otherwise silently discard an edit the user made after opening the card
        // and before choosing Restore, just because the backup predates it.
        var keys = new HashSet<string>(backupEdits.Keys);
        keys.UnionWith(load.UserEdits.Keys);
        var merged = new Dictionary<string, Formats.UserEditRecord>();
        foreach (var key in keys)
        {
            if (HasUnsavedUserEdit(load, key)) { merged[key] = load.UserEdits[key]; continue; }
            if (backupEdits.TryGetValue(key, out var be)) merged[key] = be.Clone();
        }
        load.UserEdits = merged;
        var matchByPath = Formats.MatchOverrides.Parse(text);
        foreach (var row in load.Rows.Where(r => !r.Removed && r.MatchOverride == r.OriginalMatchOverride))
            row.MatchOverride = matchByPath.GetValueOrDefault(row.Path, "");
    }

    static void StageFavs(CardLoad load, byte[] data)
    {
        string text = Encoding.ASCII.GetString(data);
        var favs = Formats.PathList.Parse(text, Formats.PathList.FavsMax).Paths.ToHashSet();
        // Same rule as MatchOverride above: a row whose Favorite is already staged differently from what Load()
        // read keeps that unsaved choice; only a row nobody has touched this session is replaced from the backup.
        foreach (var row in load.Rows.Where(r => !r.Removed && r.Favorite == r.OriginalFavorite))
            row.Favorite = favs.Contains(row.Path);
    }

    static void StageCoverOrFolderArt(CardLoad load, string cardPath, byte[] data)
    {
        string rel = cardPath.TrimStart('/');
        RgbaImage img;
        try { img = RgbaImage.Decode(data); } catch (Formats.FormatException) { return; }   // corrupt backup: skip, don't throw away the rest
        if (Path.GetFileName(rel).Equals("BOX.TGA", StringComparison.OrdinalIgnoreCase))
        {
            string folder = rel.Length > "BOX.TGA".Length ? rel[..^"/BOX.TGA".Length] : "";
            var row = load.Rows.FirstOrDefault(r => !r.Removed && r.OwnFolder && r.Folder == folder);
            if (row != null) { row.PendingCover = img; row.RestoreCoverStaged = false; }
        }
        else
        {
            string name = Path.GetFileName(rel).ToUpperInvariant();
            string folder = rel.Length > name.Length ? rel[..^(name.Length + 1)] : "";
            var fr = load.Folders.FirstOrDefault(f => f.Path == folder);
            if (fr != null) { fr.PendingCover = img; fr.RemoveArtStaged = false; }
        }
    }

    static void StageRaw(CardLoad load, string cardPath, byte[] data)
    {
        if (cardPath.Equals("/menu.bin", StringComparison.OrdinalIgnoreCase)) return;   // never, even from a corrupt manifest
        load.PendingRestoreRawFiles.RemoveAll(f => f.CardPath == cardPath);
        load.PendingRestoreRawFiles.Add((cardPath, data));
    }
}
