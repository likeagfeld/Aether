// Aether Card Manager (vmcm), GPL-3.0-or-later. Derived from GD MENU Card Manager (sonik-br, Derek Pascarella /
// ateam; design spec §8). One verb per Core operation; the verbs arrive with their plan tasks
// (docs/superpowers/plans/2026-11-22-m4-card-manager.md).
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Install;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Cli;

public static class Program
{
    public const string Usage =
        "Aether Card Manager 0.1\n" +
        "usage: vmcm header <image.cue|.iso>     print the disc's System ID and its game id\n" +
        "       vmcm notice                      print NOTICE.txt (third-party attributions for a packaged release)\n" +
        "       vmcm scan <card-root> --gamesdb <GAMES.DB> --out <dir> [--allow-fixed]   write <dir>/LIBRARY.IDX (SPEC 5.4)\n" +
        "       vmcm install <card-root> --iso F --launcher F --data DIR --vrtiles F [--loadbg F] [--music F] [--theme NAME=DIR]... [--allow-fixed]\n" +
        "                                         write /autoboot.iso and /aether/ (menu.bin is never touched; the old autoboot.iso backed up)\n" +
        "       vmcm build <card-root> --release DIR [--covers-zip ZIP] [--art-cache DIR] [--download] [--folder-art PATH=IMAGE]...\n" +
        "                  [--art-prefer KEY=SOURCE]... [--no-compat] [--date YYYYMMDD] [--font OTF] [--allow-fixed]\n" +
        "                                         build LIBRARY.IDX, GAMES.DB and ART.PAK from the card's games and install Aether\n" +
        "       vmcm assemble-release --out DIR [--repo ROOT] [--from DIR] [--allow-stale]   the release folder build --release takes, from a\n" +
        "                                         Aether checkout's build outputs (ROOT default .; --from default ROOT/build/release)\n" +
        "       vmcm rollback-folder <card-root> --backup DIR [--allow-fixed]   undo the Aether rename's folder move: /aether back to\n" +
        "                                         /vibemenu, the menu files and autoboot.iso from before it (DIR: the move's PC backup)\n" +
        "every verb that writes refuses the system drive and a target it cannot resolve; a fixed (non-removable) disk needs\n" +
        "--allow-fixed; the card is re-checked before every file. The CLI asks no question: it is for scripts.\n";

    public static int Main(string[] args)
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;   // review-1 m-1
        return Run(args, Console.Out, Console.Error);
    }

    /// <summary>YYYYMMDD from the Gregorian date parts (DateTime's properties are always Gregorian), never from a
    /// culture's calendar (th-TH would give 2569xxxx: review-1 m-1).</summary>
    public static uint BuildDate(DateTime t) => (uint)(t.Year * 10000 + t.Month * 100 + t.Day);

    public static int Run(string[] args, TextWriter o, TextWriter err) => Run(args, o, err, null);

    /// <param name="systemVolumes">tests only: the volumes refused as system volumes (default: the real ones).</param>
    internal static int Run(string[] args, TextWriter o, TextWriter err, IReadOnlyList<string>? systemVolumes)
    {
        if (args.Length >= 2 && args[0] == "build") return BuildVerb(args, o, err, systemVolumes);
        if (args.Length >= 1 && args[0] == "assemble-release") return AssembleVerb(args, o, err);
        if (args.Length == 2 && args[0] == "header") return Header(args[1], o, err);
        if (args.Length == 1 && args[0] == "notice") { o.Write(Core.About.NoticeFile.Text()); return 0; }   // packaging: NOTICE.txt
        if (args.Length >= 2 && args[0] == "install") return InstallVerb(args, o, err, systemVolumes);
        if (args.Length is 4 or 5 && args[0] == "rollback-folder" && args[2] == "--backup" && (args.Length == 4 || args[4] == "--allow-fixed"))
        {
            if (Guard(args[1], args.Length == 5, systemVolumes, err, out int rrc) is not CardTarget rt) return rrc;
            try { foreach (var l in CardFolderMigration.Rollback(rt.Root, args[3], rt.Volume.Identity, Core.Backup.CardBackup.CardFolderName(rt))) o.WriteLine(l); return 0; }
            catch (Exception e) when (e is InstallException or IOException or UnauthorizedAccessException) { err.WriteLine(e.Message); return 1; }
        }
        if (args.Length is 6 or 7 && args[0] == "scan" && args[2] == "--gamesdb" && args[4] == "--out" && (args.Length == 6 || args[6] == "--allow-fixed"))
            return Scan(args[1], args[3], args[5], args.Length == 7, o, err, systemVolumes);
        err.Write(Usage);
        return 2;
    }

    /// <summary>The one gate before any CLI write, the CLI side of the UI's CardWriteGuard: the
    /// target resolved to its volume; a system volume or an unresolvable target refused; a fixed disk only with
    /// --allow-fixed. null (rc says why, already printed) or the target, whose Volume.Identity every write is pinned to
    /// (re-verified before each file).</summary>
    static CardTarget? Guard(string root, bool allowFixed, IReadOnlyList<string>? systemVolumes, TextWriter err, out int rc)
    {
        rc = 1;
        CardTarget t;
        try { t = CardTarget.Inspect(root, systemVolumes); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException) { err.WriteLine($"{root}: {e.Message}"); return null; }
        if (t.Refusal != null) { err.WriteLine(t.Refusal); return null; }
        if (t.NeedsFixedOverride && !allowFixed)
        {
            err.WriteLine($"{t.Root} is on a fixed (non-removable) disk, {t.Volume.FinalPath} ({t.Volume.Id}); if it really is your SD card, add --allow-fixed");
            return null;
        }
        if (t.Volume.Identity == null) { err.WriteLine($"{t.Root}: its volume could not be resolved; nothing was written"); return null; }
        rc = 0;
        return t;
    }

    static int Scan(string card, string gamesdb, string outDir, bool allowFixed, TextWriter o, TextWriter err, IReadOnlyList<string>? systemVolumes)
    {
        // --out may not exist yet: its nearest existing folder is checked before anything is created, then --out itself
        // is pinned once it exists (on the same volume)
        string probe = Path.GetFullPath(outDir);
        while (!Directory.Exists(probe) && Path.GetDirectoryName(probe) is string up) probe = up;
        if (Guard(probe, allowFixed, systemVolumes, err, out int grc) is not CardTarget pt) return grc;
        GamesDbData db;
        try { db = GamesDb.Read(File.ReadAllBytes(gamesdb)); }
        catch (Exception e) when (e is IOException or VibeMenuCardManager.Core.Formats.FormatException) { err.WriteLine($"{gamesdb}: {e.Message}"); return 1; }
        ScanResult r;
        try
        {
            uint date = BuildDate(DateTime.Now);
            r = DiscSets.Apply(CardScanner.Scan(new FsSource(card), db.Records.Select(x => x.Key), db.Aliases, date), date);   // SPEC §5.4 sets
            if (r.Data.Length > LibraryIndex.ConsoleMax) { err.WriteLine($"LIBRARY.IDX {r.Data.Length} B is over the menu's {LibraryIndex.ConsoleMax} B"); return 1; }
            Directory.CreateDirectory(outDir);
            if (Guard(outDir, allowFixed, systemVolumes, err, out grc) is not CardTarget ot) return grc;
            if (ot.Volume.Id != pt.Volume.Id) { err.WriteLine($"{outDir}: it is not on the volume that was checked ({pt.Volume.Id})"); return 1; }
            new CardFs(outDir, ot.Volume.Identity).WriteReplace("/LIBRARY.IDX", r.Data);   // write-then-replace: --out may be the card's /aether
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)   // a read-only --out, a pulled card
        {
            err.WriteLine($"scan stopped: {e.Message}");
            return 1;
        }
        o.WriteLine($"{r.Denominator}: {r.Matched} matched, {r.Entries.Count - r.Matched} unmatched, {r.Unreadable} unreadable System IDs; " +
                    $"{r.Folders.Count} folders, {r.SkippedFolders} folders skipped, {r.SkippedLongPaths} paths over 255 B skipped");
        return 0;
    }

    /// <summary>M4's "an empty SD card becomes a working card": the card's games scanned, matched and packed, then
    /// Aether installed -- after the same volume checks as the UI's CardWriteGuard (a system volume or an unresolvable
    /// target refused; a fixed disk only with --allow-fixed, which then names the volume it applies to).</summary>
    static int BuildVerb(string[] a, TextWriter o, TextWriter err, IReadOnlyList<string>? systemVolumes)
    {
        string? release = null, zip = null, cache = null, date = null, font = null;
        bool download = false, compat = true, allowFixed = false;
        var folderArt = new Dictionary<string, string>();
        var prefer = new Dictionary<string, string>();
        for (int i = 2; i < a.Length; i++)
        {
            string k = a[i];
            if (k is "--download") { download = true; continue; }
            if (k is "--no-compat") { compat = false; continue; }
            if (k is "--allow-fixed") { allowFixed = true; continue; }
            if (i + 1 >= a.Length) { err.Write(Usage); return 2; }
            string v = a[++i];
            switch (k)
            {
                case "--release": release = v; break;
                case "--covers-zip": zip = v; break;
                case "--art-cache": cache = v; break;
                case "--date": date = v; break;
                case "--font": font = v; break;
                case "--folder-art" or "--art-prefer":
                    int eq = v.IndexOf('=');
                    if (eq <= 0) { err.WriteLine($"{k} {v}: give NAME=VALUE"); return 2; }
                    if (k == "--art-prefer" && !CardBuilder.ArtPreferSources.Contains(v[(eq + 1)..]))
                    { err.WriteLine($"--art-prefer {v}: SOURCE is one of {string.Join("/", CardBuilder.ArtPreferSources)}"); return 2; }
                    (k == "--folder-art" ? folderArt : prefer)[v[..eq].Trim('/')] = v[(eq + 1)..];
                    break;
                default: err.Write(Usage); return 2;
            }
        }
        if (release == null) { err.WriteLine("build: --release DIR is required"); return 2; }
        uint d = date != null && uint.TryParse(date, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out var dd)
            ? dd : BuildDate(DateTime.Now);
        if (Guard(a[1], allowFixed, systemVolumes, err, out int grc) is not CardTarget t) return grc;
        if (CardFolderMigration.Detect(t.Root) == CardFolderState.Both) { err.WriteLine(CardFolderMigration.BothMessage(t.Root)); return 1; }
        o.WriteLine(t.Describe("Build an Aether card", CardFolderMigration.BuildWrites(t.Root)));
        try
        {
            var r = CardPipeline.Run(t.Root, new PipelineOptions(release, zip, cache, download, folderArt, prefer, compat, font), d, t.Volume.Identity);
            if (CardBuilder.JpWarning(r.Build) is string jw) err.WriteLine("warning: " + jw);
            var s = r.Build.Report["scan"]!;
            o.WriteLine($"{s["denominator"]}: {s["matched"]} matched; covers {r.Build.Report["covers"]!.ToJsonString()}; JP titles {r.Build.Report["jp"]!["rendered"]}");
            if (r.Folder is { Moved: true } mf)
                foreach (var l in mf.Log) o.WriteLine("folder: " + l);
            foreach (var b in r.Install.Backups) o.WriteLine("backed up: " + b);
            foreach (var w in r.Install.Warnings) err.WriteLine("warning: " + w);
            o.WriteLine("installed: " + string.Join(", ", r.Install.Copied));
            return 0;
        }
        catch (InstallException e) { err.WriteLine(e.Message); return 1; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or VibeMenuCardManager.Core.Formats.FormatException
                                       or System.Text.Json.JsonException or ArgumentException)   // a malformed release
        { err.WriteLine($"build failed: {e.Message}"); return 1; }
    }

    static int InstallVerb(string[] a, TextWriter o, TextWriter err, IReadOnlyList<string>? systemVolumes)
    {
        var opt = new Dictionary<string, string>();
        var themes = new Dictionary<string, string>();
        bool allowFixed = false;
        for (int i = 2; i < a.Length; i++)
        {
            if (a[i] == "--allow-fixed") { allowFixed = true; continue; }
            if (i + 1 >= a.Length || !a[i].StartsWith("--", StringComparison.Ordinal)) { err.Write(Usage); return 2; }
            if (a[i] == "--theme")
            {
                int eq = a[i + 1].IndexOf('=');
                if (eq <= 0) { err.WriteLine($"--theme {a[i + 1]}: give NAME=DIR"); return 2; }
                themes[a[i + 1][..eq]] = a[i + 1][(eq + 1)..];
            }
            else opt[a[i][2..]] = a[i + 1];
            i++;
        }
        foreach (var k in new[] { "iso", "launcher", "data", "vrtiles" })
            if (!opt.ContainsKey(k)) { err.WriteLine($"install: --{k} is required"); return 2; }
        if (Guard(a[1], allowFixed, systemVolumes, err, out int grc) is not CardTarget t) return grc;
        try
        {
            var r = Installer.Install(a[1], new InstallSources(opt["iso"], opt["launcher"], opt["data"], opt["vrtiles"],
                opt.GetValueOrDefault("loadbg"), opt.GetValueOrDefault("music"), themes), null, t.Volume.Identity);
            if (r.OldOpenMenuFolder)
                o.WriteLine("note: an old /openmenu/ folder exists on this card -- it is no longer used (Aether now reads /aether/) and was left untouched.");
            foreach (var b in r.Backups) o.WriteLine("backed up: " + b);
            foreach (var w in r.Warnings) err.WriteLine("warning: " + w);
            o.WriteLine("installed: " + string.Join(", ", r.Copied));
            o.WriteLine(r.MenuBinPresent ? "menu.bin: present, unchanged" : "menu.bin: not on this card");
            return 0;
        }
        catch (InstallException e) { err.WriteLine(e.Message); return 1; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { err.WriteLine($"install failed: {e.Message}"); return 1; }
    }

    static int Header(string image, TextWriter o, TextWriter err)
    {
        var s = File.Exists(image) ? SystemIdReader.ReadImage(image) : null;
        if (s == null) { err.WriteLine($"{image}: no Saturn System ID"); return 1; }
        o.WriteLine($"product={s.Product} version={s.Version} date={s.ReleaseDate} device={s.Device} area={s.Area} " +
                    $"peripherals={s.Peripherals} maker={s.Maker}");
        o.WriteLine($"title={s.Title}");
        o.WriteLine($"game id={GameId.Ini(s.ProductField, s.AreaField) ?? "(none: empty product)"}");
        return 0;
    }

    /// <summary>contract/SPEC.md §5.17 Import, run on the PC. 0 done, 1 stopped (run it again), 2 nothing to import.</summary>
    /// <summary>Final review I-2: the release folder Build card takes, from a checkout's build outputs (ReleaseAssembler).
    /// It writes only the new --out folder on this PC, never a card.</summary>
    static int AssembleVerb(string[] a, TextWriter o, TextWriter err)
    {
        string? outDir = null, repo = ".", from = null;
        bool allowStale = false;
        for (int i = 1; i < a.Length; i += 2)
        {
            if (a[i] == "--allow-stale") { allowStale = true; i--; continue; }
            if (i + 1 >= a.Length) { err.Write(Usage); return 2; }
            switch (a[i])
            {
                case "--out": outDir = a[i + 1]; break;
                case "--repo": repo = a[i + 1]; break;
                case "--from": from = a[i + 1]; break;
                default: err.Write(Usage); return 2;
            }
        }
        if (outDir == null) { err.WriteLine("assemble-release: --out DIR is required"); return 2; }
        try
        {
            var r = ReleaseAssembler.Assemble(repo, outDir, from, allowStale);
            o.WriteLine("menu: " + r.Build);
            foreach (var (f, src, _) in r.Files) o.WriteLine($"  {f} <- {src}");
            foreach (var w in r.Warnings) err.WriteLine("warning: " + w);
            o.WriteLine($"release folder {r.Dir}: {r.Files.Count} files, menu {r.Build}; use it as vmcm build --release, or in Build card");
            return 0;
        }
        catch (ReleaseException e) { err.WriteLine(e.Message); return 1; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { err.WriteLine($"assemble-release stopped: {e.Message}"); return 1; }
    }

    // "tidy-saves" and "import-saves" are removed for 0.1 (the user: too broad and dangerous / needs the same UI
    // sophistication as the in-menu save manager, same reasoning as the Card Manager UI's own removal of Tidy
    // saves/Import from Kai/Rings). Core.Saves.SaveTidy/SaveImporter are untouched, just unreached from here.
}
