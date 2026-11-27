// Aether Card Manager, GPL-3.0-or-later.
// The installer (design spec §8.1 Installer): writes /autoboot.iso and /aether/, never touches /menu.bin (its SHA-256
// is compared before and after), and backs up the existing /autoboot.iso (Satiator Rings, Pseudo Saturn Kai) first --
// only that file: every other root file, other autoboot.* names included (a Rings autoboot.rings.iso, a Satiator
// autobootdesc.txt), is left exactly as it is, as tools/omsat/install.py does. A port of tools/omsat/install.py, transactional
// in its three phases:
//   1. stage: every new file copied (written through and flushed) under a transient name next to its destination;
//   2. commit: one flat plan of plain renames -- /autoboot.iso to a unique ".bak-<stamp>" name, then for each staged
//      file its existing destination moved aside (".omsat-old"), then the staged file into place;
//   3. undo on failure: every rename done so far reversed, each independently; if all succeed the card is exactly as
//      it was ("install failed, card restored"), else the error names every stranded file and the command to fix it.
// An old /openmenu/ folder (the project's former name) is reported, never touched.
using System.Security.Cryptography;

namespace VibeMenuCardManager.Core.Install;

/// <param name="Cheats">the shipped CHEATS.PAK (SPEC §5.15): what goes to /aether/CHEATS.PAK is it merged with the
/// card's own copy (the user's on/off choices, added cheats and card-only games kept), in the same transaction.</param>
public sealed record InstallSources(string Iso, string Launcher, string DataDir, string VrTiles, string? LoadBg = null,
                                    string? Music = null, IReadOnlyDictionary<string, string>? Themes = null, string? Cheats = null);

/// <param name="Warnings">things left over after a committed install (a moved-aside original that could not be
/// deleted): the install itself succeeded.</param>
/// <param name="Cheats">the CHEATS.PAK merge, when one was installed.</param>
public sealed record InstallReport(List<string> Backups, List<string> Copied, bool MenuBinPresent, bool OldOpenMenuFolder, int Renames,
                                   List<string> Warnings, Formats.CheatMergeReport? Cheats = null);

public sealed class InstallException(string message, Exception? inner = null) : Exception(message, inner);

public sealed class Installer
{
    public static readonly string[] DataFiles = ["GAMES.DB", "ART.PAK", "LIBRARY.IDX"];
    public static readonly string[] ThemeFiles = ["THEME.INI", "BG.BIN"];
    public const string StageSuffix = ".omsat-new", HoldSuffix = ".omsat-old";

    /// <summary>Test hooks (fault injection): before every rename (commit and undo), before every staging copy, and
    /// right after the commit (before the menu.bin check).</summary>
    internal Action<string, string>? BeforeRename;
    internal Action<string>? BeforeCopy;
    internal Action? AfterCommit;
    internal Action? BeforeCleanup;

    /// <param name="confirmedIdentity">the ResolvedVolume.Identity the user confirmed: the target is re-resolved before
    /// staging, before every staged copy and before every rename (commit and undo), and the install stops when it changed
    /// (a SUBST or junction retargeted during a write); a rename that would land on the changed target is never
    /// made, so an undo that cannot run there is reported as stranded.</param>
    public static InstallReport Install(string target, InstallSources src, string? stamp = null, string? confirmedIdentity = null) =>
        new Installer { ConfirmedIdentity = confirmedIdentity }.Run(target, src, stamp);

    public string? ConfirmedIdentity { get; init; }
    string _target = "";

    void Verify()
    {
        if (ConfirmedIdentity == null) return;
        string now = Core.Card.VolumeResolver.Resolve(_target).Identity ?? "unresolvable";
        if (now != ConfirmedIdentity) throw new Core.Card.CardChangedException(_target, ConfirmedIdentity, now);
    }

    static string? Sha(string p) => File.Exists(p) ? Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(p))) : null;

    void Rename(string a, string b)
    {
        BeforeRename?.Invoke(a, b);
        Verify();
        if (Directory.Exists(a)) Directory.Move(a, b);
        else File.Move(a, b, overwrite: false);
    }

    /// <summary>Delete a transient file of ours; a read-only one gets its attribute cleared and one retry (review-2: an
    /// UnauthorizedAccessException is not an IOException). Returns the error, or null when the file is gone.</summary>
    internal static string? TryDelete(string p)
    {
        for (int attempt = 0; ; attempt++)
        {
            try { File.Delete(p); return null; }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                if (attempt > 0 || !File.Exists(p)) return e.Message;
                try { File.SetAttributes(p, File.GetAttributes(p) & ~FileAttributes.ReadOnly); }
                catch (Exception e2) when (e2 is IOException or UnauthorizedAccessException) { return e2.Message; }
            }
        }
    }

    static void CopyDurable(string from, string to)
    {
        using var i = File.OpenRead(from);
        using var o = new FileStream(to, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16, FileOptions.WriteThrough);
        i.CopyTo(o);
        o.Flush(flushToDisk: true);
    }

    public InstallReport Run(string target, InstallSources src, string? stamp = null)
    {
        var temps = new List<string>();
        try { return RunWith(target, src, stamp, temps); }
        finally { foreach (var t in temps) TryDelete(t); }
    }

    // The menu (autoboot.iso) and LAUNCHER.BIN always go together, in one plan: the hand-off block between them is
    // versioned (menu/src/handoff.h) and each refuses the other's from another build, so a card never gets one alone.
    InstallReport RunWith(string target, InstallSources src, string? stamp, List<string> temps)
    {
        var themes = (src.Themes ?? new Dictionary<string, string>()).ToDictionary(kv => kv.Key, kv => kv.Value);
        foreach (var (n, d) in themes)
            if (n == "" || n.Contains('/') || n.Contains('\\') || n.Length > 31 || !File.Exists(Path.Combine(d, "THEME.INI")))
                throw new InstallException($"theme '{n}': needs a plain name (31 chars at most) and {Path.Combine(d, "THEME.INI")}");
        if (!Directory.Exists(target)) throw new InstallException($"target {target} is not a directory");
        if (Directory.Exists(Path.Combine(target, Core.Card.CardPaths.OldFolder)))   // the Aether rename: never beside it
            throw new InstallException($"{target} still has /{Core.Card.CardPaths.OldFolder}, the menu's folder from before the Aether rename: " +
                                       "build or update the card (vmcm build / Update card), which backs it up and moves it to " +
                                       $"/{Core.Card.CardPaths.Folder} first; nothing was written");
        _target = target;
        try { Verify(); }
        catch (Core.Card.CardChangedException e) { throw new InstallException(e.Message, e); }
        foreach (var p in new[] { src.Iso, src.Launcher, src.VrTiles, src.LoadBg, src.Music, src.Cheats }.OfType<string>())
            if (!File.Exists(p)) throw new InstallException($"missing {p}");
        var missing = DataFiles.Where(n => !File.Exists(Path.Combine(src.DataDir, n))).ToList();
        if (missing.Count > 0) throw new InstallException($"missing data file(s): {string.Join(", ", missing)}");
        var lb = File.ReadAllBytes(src.Launcher);
        try { LauncherHeader.Parse(lb); }
        catch (FormatException e) { throw new InstallException($"LAUNCHER.BIN rejected: {e.Message}"); }

        string menu = Path.Combine(target, "menu.bin");     // never staged, never planned, never renamed: read only
        string? before = Sha(menu);
        stamp ??= DateTime.Now.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture);
        bool oldOm = Directory.Exists(Path.Combine(target, "openmenu"));

        string vm = Path.Combine(target, "aether");
        bool vmExisted = Directory.Exists(vm);
        Directory.CreateDirectory(vm);
        var sources = new List<(string Label, string Final, string Source)> { ("autoboot.iso", Path.Combine(target, "autoboot.iso"), src.Iso) };
        foreach (var n in DataFiles) sources.Add(($"aether/{n}", Path.Combine(vm, n), Path.Combine(src.DataDir, n)));
        sources.Add(("aether/LAUNCHER.BIN", Path.Combine(vm, "LAUNCHER.BIN"), src.Launcher));
        sources.Add(("aether/VRTILES.BIN", Path.Combine(vm, "VRTILES.BIN"), src.VrTiles));
        if (src.LoadBg != null) sources.Add(("aether/LOADBG.BIN", Path.Combine(vm, "LOADBG.BIN"), src.LoadBg));
        Formats.CheatMergeReport? cheatsRep = null;
        if (src.Cheats != null)
        {
            // merged, never a plain overwrite; an unreadable card copy is kept beside it as CHEATS.BAK
            byte[]? old = Formats.CheatPak.ReadCardCopy(vm);
            byte[] merged;
            try { (merged, cheatsRep) = Formats.CheatPak.Merge(old, File.ReadAllBytes(src.Cheats)); }
            catch (Formats.FormatException e) { throw new InstallException($"CHEATS.PAK rejected: {e.Message}"); }
            string Temp(byte[] b) { string t = Path.GetTempFileName(); temps.Add(t); File.WriteAllBytes(t, b); return t; }
            sources.Add(($"aether/{Formats.CheatPak.Name}", Path.Combine(vm, Formats.CheatPak.Name), Temp(merged)));
            if (cheatsRep.OldError != null && old != null)
                sources.Add(($"aether/{Formats.CheatPak.BakName}", Path.Combine(vm, Formats.CheatPak.BakName), Temp(old)));
        }
        string mus = Path.Combine(vm, "MUSIC");
        bool musExisted = Directory.Exists(mus);
        if (src.Music != null)
        {
            Directory.CreateDirectory(mus);
            sources.Add(("aether/MUSIC/BGM.MUS", Path.Combine(mus, "BGM.MUS"), src.Music));
        }
        string thRoot = Path.Combine(vm, "THEMES");
        var newDirs = new List<(string Dir, bool Existed)> { (thRoot, Directory.Exists(thRoot)) };
        if (themes.Count > 0) Directory.CreateDirectory(thRoot);
        foreach (var (n, d) in themes.OrderBy(kv => kv.Key, StringComparer.Ordinal))
        {
            string td = Path.Combine(thRoot, n);
            newDirs.Insert(0, (td, Directory.Exists(td)));
            Directory.CreateDirectory(td);
            foreach (var f in ThemeFiles)
                if (File.Exists(Path.Combine(d, f))) sources.Add(($"aether/THEMES/{n}/{f}", Path.Combine(td, f), Path.Combine(d, f)));
        }
        void DropNewDirs()
        {
            foreach (var (d, existed) in newDirs.Append((mus, musExisted)).Append((vm, vmExisted)))
                if (!existed)
                    try { Directory.Delete(d); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        // 1. stage: every new file copied under a transient name; nothing old is touched yet
        var staged = new List<string>();
        foreach (var (label, final, source) in sources)
        {
            string tmp = final + StageSuffix;
            try
            {
                BeforeCopy?.Invoke(label);
                Verify();
                CopyDurable(source, tmp);
            }
            catch (Core.Card.CardChangedException e)
            {
                // the target now resolves to another card: touch nothing more there (no cleanup through the changed path)
                throw new InstallException($"install stopped while staging: {e.Message}; the staged {StageSuffix} files and the folders " +
                                           "staging made stay on the card you confirmed (transient, never read); nothing there was replaced", e);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                foreach (var s in staged.Append(tmp)) TryDelete(s);
                DropNewDirs();
                throw new InstallException($"install failed while staging: {label}: {e.Message}", e);
            }
            staged.Add(tmp);
        }

        // 2. commit: a flat plan of plain renames (backups, then each staged file into place)
        var (plan, backups, held) = PlanCommit(target, stamp, sources, staged);
        var done = new List<(string Src, string Dst)>();
        try
        {
            foreach (var (a, b) in plan) { Rename(a, b); done.Add((a, b)); }
        }
        catch (Exception commitErr) when (commitErr is IOException or UnauthorizedAccessException)
        {
            // 3. undo: reverse every rename done, each independently
            var failures = new List<(string Src, string Dst, Exception Err)>();
            for (int i = done.Count - 1; i >= 0; i--)
                try { Rename(done[i].Dst, done[i].Src); }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException) { failures.Add((done[i].Src, done[i].Dst, e)); }
            if (failures.Count == 0)
            {
                foreach (var s in staged) TryDelete(s);
                DropNewDirs();
                throw new InstallException($"install failed, card restored: {commitErr.Message}", commitErr);
            }
            string stranded = string.Join("; ", failures.Select(f => $"{f.Dst} should be renamed back to {f.Src} ({f.Err.Message})"));
            string manual = string.Join("; ", failures.Select(f => $"mv \"{f.Dst}\" \"{f.Src}\""));
            throw new InstallException($"install failed AND rollback incomplete: {commitErr.Message}. Stranded: {stranded}. Manual fix: {manual}", commitErr);
        }
        var warnings = new List<string>();
        BeforeCleanup?.Invoke();
        foreach (var o in held)                                   // the moved-aside originals: a leftover is a warning
            if (TryDelete(o) is string err) warnings.Add($"could not delete {o} (the old copy, set aside): {err}; delete it by hand");
        AfterCommit?.Invoke();
        if (Sha(menu) != before) throw new InstallException("menu.bin changed during install -- this must never happen");
        // an empty SAVES (2026-11-27): Settings > Import Saves writes IMPORT.SEL there before any launch has made it
        try { Directory.CreateDirectory(Path.Combine(vm, "SAVES")); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        { warnings.Add($"could not make /aether/SAVES: {e.Message}; the menu makes it when it needs it"); }

        if (Directory.EnumerateFiles(target).Any(f => string.Equals(Path.GetFileName(f), "autoboot.elf", StringComparison.OrdinalIgnoreCase)))
            warnings.Add("/autoboot.elf is on the card: the Satiator menu may start it before /autoboot.iso (unverified); it was left as it is");
        var copied = DataFiles.Concat(["LAUNCHER.BIN", "VRTILES.BIN"]).ToList();
        if (src.LoadBg != null) copied.Add("LOADBG.BIN");
        if (src.Cheats != null) copied.Add(Formats.CheatPak.Name);
        if (src.Music != null) copied.Add("MUSIC/BGM.MUS");
        foreach (var (n, d) in themes.OrderBy(kv => kv.Key, StringComparer.Ordinal))
            foreach (var f in ThemeFiles) if (File.Exists(Path.Combine(d, f))) copied.Add($"THEMES/{n}/{f}");
        copied.Add("autoboot.iso");
        backups.Sort(StringComparer.Ordinal);
        return new InstallReport(backups, copied, before != null, oldOm, plan.Count, warnings, cheatsRep);
    }

    /// <summary>_plan_commit: the renames, the backup names and the held-aside paths. Existence is tracked virtually,
    /// because earlier steps of the same plan have not run when later ones are planned.</summary>
    static (List<(string, string)> Plan, List<string> Backups, List<string> Held) PlanCommit(
        string target, string stamp, List<(string Label, string Final, string Source)> sources, List<string> staged)
    {
        var willExist = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
        bool Exists(string p) => willExist.TryGetValue(p, out var e) ? e : File.Exists(p) || Directory.Exists(p);
        var plan = new List<(string, string)>();
        var claimed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void Add(string a, string b) { plan.Add((a, b)); willExist[a] = false; willExist[b] = true; }
        string Unique(string path)
        {
            string c = path;
            for (int n = 1; File.Exists(c) || Directory.Exists(c) || claimed.Contains(Path.GetFileName(c)); n++)
                c = path + "." + n.ToString(System.Globalization.CultureInfo.InvariantCulture);
            claimed.Add(Path.GetFileName(c));
            return c;
        }
        var backups = new List<string>();
        // sorted(target.iterdir()) on Windows (PureWindowsPath orders case-insensitively)
        // Only /autoboot.iso itself, the one file this install replaces (2026-11-25, the user's card: autoboot.rings.iso,
        // the Rings menu their swap scripts use, and autobootdesc.txt, a Satiator autoboot descriptor, were renamed to
        // .bak-<stamp> by the old "every autoboot.*" rule). No other root file is ever renamed, moved or removed.
        foreach (var p in Directory.EnumerateFiles(target).OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase))
        {
            string name = Path.GetFileName(p);
            if (string.Equals(name, "autoboot.iso", StringComparison.OrdinalIgnoreCase))
            {
                string dst = Unique(p + ".bak-" + stamp);
                Add(p, dst);
                backups.Add(Path.GetFileName(dst));
            }
        }
        var held = new List<string>();
        for (int i = 0; i < sources.Count; i++)
        {
            string final = sources[i].Final;
            if (Exists(final))
            {
                string old = Unique(final + HoldSuffix);
                Add(final, old);
                held.Add(old);
            }
            Add(staged[i], final);
        }
        return (plan, backups, held);
    }
}

/// <summary>LAUNCHER.BIN's 4 KB header (menu/src/handoff.h; a port of tools/omsat/lnchhdr.py parse_header):
/// "OMSATLNC", u32 version 1, u32 load address 0x00200000, u32 body size, u32 body CRC-32, u32 CRC-32 of bytes 0-0x17.</summary>
public static class LauncherHeader
{
    public const int Hdr = 0x1000, MaxBody = 0x7F000;

    public static (uint BodySize, uint BodyCrc) Parse(byte[] file)
    {
        if (file.Length < Hdr || !file.AsSpan(0, 8).SequenceEqual("OMSATLNC"u8)) throw new FormatException("magic");
        if (Be.Get32(file, 24) != Formats.CommonHeader.Crc32(file.AsSpan(0, 24))) throw new FormatException("header CRC");
        if (Be.Get32(file, 8) != 1 || Be.Get32(file, 12) != 0x00200000) throw new FormatException("version/load");
        uint size = Be.Get32(file, 16);
        if (size == 0 || size > MaxBody || file.Length != Hdr + size) throw new FormatException("size");
        return (size, Be.Get32(file, 20));
    }
}
