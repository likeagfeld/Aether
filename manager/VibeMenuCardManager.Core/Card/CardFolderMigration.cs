// Aether Card Manager, GPL-3.0-or-later.
// The Aether rename's card move (2026-11-25): a card set up before the rename keeps the menu's files AND the user's own
// (SAVES, FAVS.TXT, RECENT.TXT, SETTINGS.INI, PERGAME.INI, USEREDIT.INI, CHEATS.PAK, IMPORT) in /vibemenu; the new menu
// reads /aether. Update card moves the folder, never copies it:
//   1. Detect: only /vibemenu (Old) moves; /aether alone (New) or neither (None) has nothing to move; BOTH is refused --
//      merging two folders of saves is never guessed at.
//   2. Back up the whole /vibemenu tree to the PC (Migrations/<card>/<stamp>/vibemenu, outside the snapshot folders
//      CardBackup prunes; enough free space checked first), re-read every copy and compare, and only then write the
//      manifest (every file's size and SHA-256): any failure deletes the partial copy and stops, the card untouched.
//   3. One Directory.Move(/vibemenu, /aether) on the confirmed volume: a same-volume rename of one directory entry; no
//      file data moves. The file system's own write of that entry is not guaranteed atomic against a power cut or an
//      unplug, so the PC backup is the real guarantee: every later failure names it.
//   4. Verify /aether against the manifest (every path, size and hash; MUSIC/BGM.MUS, the ~1 GB music the release
//      reinstalls, by size). A difference moves the folder back and stops.
// Rollback (after a failed install, or on request) moves /aether back to /vibemenu, puts back the menu files an install
// replaced (from the backup) and the VibeMenu autoboot.iso the installer set aside as autoboot.iso.bak-<stamp>.
// The journal, ACTIVE.TXT and GENLIST.TXT hold names relative to the folder, and the console's marker (VIBEMENU_PG) and the
// slot stamps (VIBEMENU.TXT) keep their names, so the saves match after the move with nothing rewritten.
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Core.Card;

public enum CardFolderState { None, New, Old, Both }

/// <param name="Moved">the folder was moved (false: nothing to move).</param>
/// <param name="BackupDir">the PC backup of the old folder (null when nothing moved).</param>
public sealed record FolderMigrationReport(bool Moved, string? BackupDir, int Files, long Bytes, List<string> Log);

public static class CardFolderMigration
{
    const string ManifestFile = "manifest.json";
    const string BigMusic = "MUSIC/BGM.MUS";

    /// <summary>The menu files an install replaces (Installer: data, LAUNCHER.BIN, VRTILES.BIN, LOADBG.BIN, the merged
    /// CHEATS.PAK, the music and THEMES/): what Rollback puts back from the backup.</summary>
    static readonly string[] InstallOwned = ["GAMES.DB", "ART.PAK", "LIBRARY.IDX", "LAUNCHER.BIN", "VRTILES.BIN", "LOADBG.BIN", "CHEATS.PAK"];

    /// <summary>Tests: fault injection right before the move and right after it.</summary>
    internal static Action? BeforeMove, AfterMove;

    public sealed record Entry(string Path, long Size, string? Sha256);

    public static CardFolderState Detect(string card)
    {
        bool o = Directory.Exists(System.IO.Path.Combine(card, CardPaths.OldFolder));
        bool n = Directory.Exists(System.IO.Path.Combine(card, CardPaths.Folder));
        return o && n ? CardFolderState.Both : o ? CardFolderState.Old : n ? CardFolderState.New : CardFolderState.None;
    }

    /// <summary>Where a card's migration backups live: beside CardBackup's snapshot folders, never pruned with them.</summary>
    public static string BackupRoot() => System.IO.Path.Combine(System.IO.Path.GetDirectoryName(Backup.CardBackup.Root())!, "Migrations");

    /// <summary>What Build/Update card writes, for the confirmation: on a card from before the Aether rename, the folder
    /// move comes first (CardFolderMigration).</summary>
    public static string[] BuildWrites(string root)
    {
        var w = new List<string>();
        if (Detect(root) == CardFolderState.Old)
            w.Add("/vibemenu is renamed to /aether (your saves, favorites and settings move with it; it is backed up to this PC and checked first)");
        w.Add("/autoboot.iso (the old one kept as autoboot.iso.bak-<stamp>; no other root file is touched; /menu.bin is never touched)");
        w.Add("/aether/GAMES.DB, LIBRARY.IDX, ART.PAK (built from the games on the card), LAUNCHER.BIN, VRTILES.BIN and the release's extras");
        return [.. w];
    }

    public static string BothMessage(string card) =>
        $"{card} has both /{CardPaths.OldFolder} (the menu's folder before the Aether rename) and /{CardPaths.Folder}. Nothing was " +
        $"changed. Keep the one with your saves: if /{CardPaths.Folder} only holds what a release copied in, delete it and run " +
        "Update card again; otherwise move your files across by hand.";

    static string Sha(string p)
    {
        using var f = File.OpenRead(p);
        return Convert.ToHexStringLower(SHA256.HashData(f));
    }

    static string Rel(string root, string full) => System.IO.Path.GetRelativePath(root, full).Replace(System.IO.Path.DirectorySeparatorChar, '/');
    static string Host(string root, string rel) => System.IO.Path.Combine(root, rel.Replace('/', System.IO.Path.DirectorySeparatorChar));

    /// <summary>Every file under dir, its size and hash (BGM.MUS: size only).</summary>
    public static List<Entry> Inventory(string dir, Action<string>? progress = null)
    {
        var o = new List<Entry>();
        foreach (var f in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal))
        {
            string rel = Rel(dir, f);
            progress?.Invoke(rel);
            long size = new FileInfo(f).Length;
            o.Add(new Entry(rel, size, rel.Equals(BigMusic, StringComparison.OrdinalIgnoreCase) ? null : Sha(f)));
        }
        return o;
    }

    /// <summary>The differences between dir and the manifest (empty: every file there, the same size and hash). A file
    /// the manifest does not name (Explorer's Thumbs.db or desktop.ini, macOS's .DS_Store appearing meanwhile) loses
    /// nothing, so it is not a difference; extras, when given, collects them.</summary>
    public static List<string> Compare(string dir, IReadOnlyList<Entry> want, Action<string>? progress = null, List<string>? extras = null)
    {
        var diffs = new List<string>();
        var have = Inventory(dir, progress).ToDictionary(e => e.Path, StringComparer.OrdinalIgnoreCase);
        foreach (var w in want)
        {
            if (!have.Remove(w.Path, out var h)) { diffs.Add($"missing {w.Path}"); continue; }
            if (h.Size != w.Size) diffs.Add($"{w.Path}: {h.Size} B, expected {w.Size} B");
            else if (w.Sha256 != null && h.Sha256 != w.Sha256) diffs.Add($"{w.Path}: content differs");
        }
        extras?.AddRange(have.Keys);
        return diffs;
    }

    internal static void VerifyVolume(string card, string? confirmedIdentity)
    {
        if (confirmedIdentity == null) return;
        string now = VolumeResolver.Resolve(card).Identity ?? "unresolvable";
        if (now != confirmedIdentity) throw new CardChangedException(card, confirmedIdentity, now);
    }

    /// <summary>Moves a pre-rename card's /vibemenu to /aether (backup, verify, move, verify). Old only; New and None
    /// return Moved=false; Both throws (nothing touched). cardName: the backup's folder name (CardBackup.CardFolderName).</summary>
    public static FolderMigrationReport Migrate(string card, string? confirmedIdentity, string cardName, DateTime nowUtc,
                                                Action<string>? progress = null, string? backupRoot = null)
    {
        var log = new List<string>();
        var state = Detect(card);
        if (state == CardFolderState.Both) throw new InstallException(BothMessage(card));
        if (state != CardFolderState.Old) return new FolderMigrationReport(false, null, 0, 0, log);
        try { VerifyVolume(card, confirmedIdentity); }
        catch (CardChangedException e) { throw new InstallException(e.Message, e); }
        string old = System.IO.Path.Combine(card, CardPaths.OldFolder), neu = System.IO.Path.Combine(card, CardPaths.Folder);

        // 2. the PC backup, then every copy read back
        string dir = System.IO.Path.Combine(backupRoot ?? BackupRoot(), cardName, nowUtc.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture));
        string copy = System.IO.Path.Combine(dir, CardPaths.OldFolder);
        if (Directory.Exists(dir)) throw new InstallException($"backup folder {dir} already exists: nothing was changed, try again");
        progress?.Invoke($"Backing up /{CardPaths.OldFolder} to the PC");
        List<Entry> man;
        try { man = Inventory(old, f => progress?.Invoke($"Backing up {f}")); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            throw new InstallException($"/{CardPaths.OldFolder} could not be read ({e.Message}); nothing on the card was changed", e);
        }
        long need = man.Sum(e => e.Size) + (64L << 20);
        if (FreeBytes(backupRoot ?? BackupRoot()) is long free && free < need)
            throw new InstallException($"this PC needs {need >> 20} MB free for the backup of /{CardPaths.OldFolder} and has {free >> 20} MB; " +
                                       "nothing on the card was changed");
        try
        {
            Directory.CreateDirectory(copy);
            foreach (var e in man)
            {
                string dst = Host(copy, e.Path);
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(dst)!);
                File.Copy(Host(old, e.Path), dst, overwrite: false);
            }
            var bad = Compare(copy, man, f => progress?.Invoke($"Checking the backup: {f}"));
            if (bad.Count > 0) throw new IOException($"the copy does not match the card ({string.Join("; ", bad.Take(5))})");
            File.WriteAllText(System.IO.Path.Combine(dir, ManifestFile), JsonSerializer.Serialize(man));   // only for a checked copy
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            try { Directory.Delete(dir, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }   // our own partial copy
            throw new InstallException($"the PC backup of /{CardPaths.OldFolder} failed ({e.Message}); nothing on the card was changed", e);
        }
        log.Add($"backed up /{CardPaths.OldFolder}: {man.Count} files, {man.Sum(e => e.Size)} B, to {dir} (every copy checked)");

        // 3. the move: one rename on the confirmed volume
        try { VerifyVolume(card, confirmedIdentity); }
        catch (CardChangedException e) { throw new InstallException(e.Message + "; nothing on the card was changed", e); }
        if (Directory.Exists(neu)) throw new InstallException(BothMessage(card));
        progress?.Invoke($"Moving /{CardPaths.OldFolder} to /{CardPaths.Folder}");
        BeforeMove?.Invoke();
        try { Directory.Move(old, neu); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            throw new InstallException($"could not move /{CardPaths.OldFolder} to /{CardPaths.Folder} ({e.Message}); state now: {Detect(card)}. " +
                                       $"Backup: {dir}", e);
        }
        AfterMove?.Invoke();
        log.Add($"moved /{CardPaths.OldFolder} to /{CardPaths.Folder}");

        // 4. verify, else move back (only on the confirmed card)
        var extras = new List<string>();
        List<string> diff;
        try { diff = Directory.Exists(old) ? ["/" + CardPaths.OldFolder + " still exists"] : Compare(neu, man, f => progress?.Invoke($"Checking {f}"), extras); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { diff = [$"/{CardPaths.Folder} could not be read ({e.Message})"]; }
        if (diff.Count > 0)
        {
            string back;
            try { VerifyVolume(card, confirmedIdentity); if (!Directory.Exists(old)) Directory.Move(neu, old); back = $"moved back to /{CardPaths.OldFolder}"; }
            catch (CardChangedException e) { back = $"NOT moved back: {e.Message}"; }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { back = $"could NOT move it back ({e.Message})"; }
            throw new InstallException($"after the move /{CardPaths.Folder} does not match the backup ({string.Join("; ", diff.Take(5))}); {back}. " +
                                       $"Backup: {dir}");
        }
        log.Add($"checked /{CardPaths.Folder}: every file as backed up" + (extras.Count > 0 ? $" (and {extras.Count} new: {string.Join(", ", extras.Take(5))})" : ""));
        File.WriteAllLines(System.IO.Path.Combine(dir, "migration.log"), log);
        return new FolderMigrationReport(true, dir, man.Count, man.Sum(e => e.Size), log);
    }

    static long? FreeBytes(string path)
    {
        try
        {
            string full = System.IO.Path.GetFullPath(path);
            return System.IO.Path.GetPathRoot(full) is { Length: > 0 } r ? new DriveInfo(r).AvailableFreeSpace : null;
        }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException) { return null; }
    }

    static bool IsVibeMenuImage(string p) =>
        Disc.SystemIdReader.ReadImage(p) is { } s && s.Title.StartsWith("VIBEMENU", StringComparison.Ordinal);

    /// <summary>Undoes a migration: /aether back to /vibemenu, the menu files an install replaced back from the backup
    /// (each current one kept beside it as .aether-&lt;stamp&gt;, never overwritten), and the pre-rename autoboot.iso back
    /// from the newest autoboot.iso.bak-* that is a VibeMenu image. The user's saves, favorites and settings stay as they
    /// are now. Refused, with nothing changed, unless backupDir is a completed move's backup (migration.log; cardName, when
    /// given, must be its card folder's name) and a VibeMenu autoboot.iso is there to go back to. A rollback stopped after
    /// the folder move can be run again. Returns what it did.</summary>
    public static List<string> Rollback(string card, string backupDir, string? confirmedIdentity, string? cardName = null)
    {
        var log = new List<string>();
        try { VerifyVolume(card, confirmedIdentity); }
        catch (CardChangedException e) { throw new InstallException(e.Message, e); }
        string old = System.IO.Path.Combine(card, CardPaths.OldFolder), neu = System.IO.Path.Combine(card, CardPaths.Folder);
        string full = System.IO.Path.TrimEndingDirectorySeparator(System.IO.Path.GetFullPath(backupDir));
        string copy = System.IO.Path.Combine(full, CardPaths.OldFolder);
        if (!Directory.Exists(copy) || !File.Exists(System.IO.Path.Combine(full, "migration.log")))
            throw new InstallException($"{backupDir} is not a completed move's backup (no /{CardPaths.OldFolder} copy or migration.log); nothing was changed");
        if (cardName != null && !string.Equals(System.IO.Path.GetFileName(System.IO.Path.GetDirectoryName(full)), cardName, StringComparison.OrdinalIgnoreCase))
            throw new InstallException($"{backupDir} is another card's backup (this card is {cardName}); nothing was changed");
        string iso = System.IO.Path.Combine(card, "autoboot.iso");
        var bak = Directory.EnumerateFiles(card, "autoboot.iso.bak-*").OrderByDescending(p => p, StringComparer.Ordinal).FirstOrDefault(IsVibeMenuImage);
        bool isoBack = File.Exists(iso) && IsVibeMenuImage(iso);
        if (bak == null && !isoBack)
            throw new InstallException("no VibeMenu autoboot.iso.bak-* on the card to go back to; nothing was changed");
        var state = Detect(card);
        if (state == CardFolderState.New) { Directory.Move(neu, old); log.Add($"moved /{CardPaths.Folder} back to /{CardPaths.OldFolder}"); }
        else if (state != CardFolderState.Old)                                    // Old: a rollback stopped after its move
            throw new InstallException($"rollback needs /{CardPaths.Folder} alone on the card (now: {state}); nothing was changed");
        string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture);
        foreach (var f in Directory.EnumerateFiles(copy, "*", SearchOption.AllDirectories))
        {
            string rel = Rel(copy, f);
            bool owned = InstallOwned.Contains(rel, StringComparer.OrdinalIgnoreCase) || rel.StartsWith("THEMES/", StringComparison.OrdinalIgnoreCase)
                         || rel.Equals(BigMusic, StringComparison.OrdinalIgnoreCase);
            if (!owned) continue;
            string dst = Host(old, rel), tmp = dst + Installer.StageSuffix;
            bool music = rel.Equals(BigMusic, StringComparison.OrdinalIgnoreCase);
            if (File.Exists(dst) && new FileInfo(dst).Length == new FileInfo(f).Length && (music || Sha(dst) == Sha(f))) continue;   // already back
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(dst)!);
            File.Copy(f, tmp, overwrite: true);
            File.SetAttributes(tmp, File.GetAttributes(tmp) & ~FileAttributes.ReadOnly);
            if (File.Exists(dst))
            {
                File.SetAttributes(dst, File.GetAttributes(dst) & ~FileAttributes.ReadOnly);
                if (music) File.Delete(dst);                                      // the release's music: reinstalled anyway, ~1 GB
                else { File.Move(dst, dst + ".aether-" + stamp); log.Add($"kept the newer /{CardPaths.OldFolder}/{rel} as {System.IO.Path.GetFileName(dst)}.aether-{stamp}"); }
            }
            File.Move(tmp, dst);
            log.Add($"restored /{CardPaths.OldFolder}/{rel}");
        }
        if (bak != null && !isoBack)
        {
            if (File.Exists(iso)) File.Move(iso, iso + ".aether-" + stamp);
            File.Move(bak, iso);
            log.Add($"restored /autoboot.iso from {System.IO.Path.GetFileName(bak)}");
        }
        return log;
    }
}
