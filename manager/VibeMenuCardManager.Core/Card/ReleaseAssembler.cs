// Aether Card Manager, GPL-3.0-or-later.
// The release folder "Build card" takes (CardPipeline's layout), assembled from an Aether checkout's build outputs
// (tools/ is not edited). tools/m3_release.sh leaves the menu in build/release/, but GAMES.DB and
// jp_titles.json only in build/db/ (cardbuild's inputs) and the compat tables in tools/data/compat; this puts them
// where CardPipeline.ReleaseRequired and BuildOptionsFor read them:
//
//   autoboot.iso      build/release/autoboot.iso   the release image (m3_release.sh copies it before its --app-fake
//                                                  build overwrites menu/app/out/autoboot.iso; a AETHER System ID)
//   LAUNCHER.BIN      build/release/LAUNCHER.BIN only (its header checked): it pairs with that autoboot.iso through the
//                     versioned hand-off block, so a launcher built apart from the packaged menu is never taken
//   VRTILES.BIN       build/release/VRTILES.BIN, else menu/app/assets/VRTILES.BIN
//   LOADBG.BIN        build/release/LOADBG.BIN, else menu/app/assets/LOADBG.BIN (optional)
//   BGM.MUS, THEMES/  build/release/ (optional; a theme needs THEME.INI)
//   GAMES.DB          build/db/GAMES.DB (read and checked: SPEC 5.1)
//   jp_titles.json    build/db/jp_titles.json (a JSON object of key -> title)
//   compat/           tools/data/compat/{loader_compat,save_tools,controller_compat}.json
//   CHEATS.PAK        build/release/CHEATS.PAK, else tools/data/cheats/CHEATS.PAK (read and checked: SPEC 5.15)
//   SOURCES.txt       written here: each file's SHA-256 and where it came from
// The output folder must be new or empty: an assembly never mixes with an older one.
// Final re-review I-7: the packaged menu is named by its build string ("Aether <version> <commit> <date>", in the
// image), in the output and in SOURCES.txt; a file taken from a fallback location instead of the release folder is a
// warning; and a menu built before the newest commit touching menu/ or contract/SPEC.md (git: that commit is not an
// ancestor of, or equal to, the build's) is refused unless allowStale -- as is one whose build commit cannot be checked.
using System.Security.Cryptography;
using System.Text.Json;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Core.Card;

public sealed class ReleaseException(string message) : Exception(message);

public sealed record AssembledRelease(string Dir, IReadOnlyList<(string File, string From, string Sha256)> Files, string Title,
                                      string Build, IReadOnlyList<string> Warnings);

public static partial class ReleaseAssembler
{
    [System.Text.RegularExpressions.GeneratedRegex(CardPaths.BuildWord + @" [0-9]+\.[0-9]+[0-9A-Za-z.]* ([0-9a-f]{7,40})[^\x00\s]*(?: [0-9]{4}-[0-9]{2}-[0-9]{2})?")]
    private static partial System.Text.RegularExpressions.Regex BuildRe();

    /// <summary>The image's build string ("Aether 0.1 ba8a8cd 2026-11-19") and its commit, from the first 4 MB (where the
    /// menu's first program sits, as CardTarget reads it); nulls when there is none.</summary>
    public static (string? Build, string? Commit) BuildOf(string iso)
    {
        var buf = new byte[4 << 20];
        int n;
        using (var f = File.OpenRead(iso)) n = f.Read(buf, 0, buf.Length);
        var m = BuildRe().Match(System.Text.Encoding.Latin1.GetString(buf, 0, n));
        return m.Success ? (m.Value, m.Groups[1].Value) : (null, null);
    }

    static (int Rc, string Out) Git(string repo, params string[] args)
    {
        var psi = new System.Diagnostics.ProcessStartInfo("git", ["-C", repo, .. args])
            { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
        try
        {
            using var p = System.Diagnostics.Process.Start(psi)!;
            string o = p.StandardOutput.ReadToEnd();
            p.StandardError.ReadToEnd();
            p.WaitForExit();
            return (p.ExitCode, o.Trim());
        }
        catch (System.ComponentModel.Win32Exception) { return (-1, "git is not installed"); }
    }

    /// <summary>null when the build contains the newest commit touching menu/ or contract/SPEC.md; else why it is stale
    /// (or cannot be checked).</summary>
    public static string? Staleness(string repo, string? build, string? commit)
    {
        if (commit == null) return $"the menu image names no build commit ({build ?? "no Aether build string"}), so its age cannot be checked";
        var (rc, newest) = Git(repo, "log", "-1", "--format=%h %cs %s", "--", "menu", "contract/SPEC.md");
        if (rc != 0 || newest == "") return $"{repo} is not a git checkout with menu/ history ({newest}), so the age of {build} cannot be checked";
        string nsha = newest.Split(' ')[0];
        var (rc2, _) = Git(repo, "cat-file", "-e", commit + "^{commit}");
        if (rc2 != 0) return $"the menu was built from {commit} ({build}), which this checkout does not have, so its age cannot be checked; the newest commit touching menu/ or contract/SPEC.md is {newest}";
        var (rc3, _) = Git(repo, "merge-base", "--is-ancestor", nsha, commit);
        if (rc3 == 0) return null;
        var (_, behind) = Git(repo, "rev-list", "--count", $"{commit}..{nsha}", "--", "menu", "contract/SPEC.md");
        return $"the menu is {build}, built before {newest} ({behind} newer commits touch menu/ or contract/SPEC.md)";
    }

    /// <param name="releaseDir">the menu's build outputs (default: &lt;repo&gt;/build/release)</param>
    /// <param name="allowStale">package a menu older than the checkout's menu sources anyway (it is still named and warned)</param>
    public static AssembledRelease Assemble(string repo, string outDir, string? releaseDir = null, bool allowStale = false)
    {
        repo = Path.GetFullPath(repo);
        string rel = Path.GetFullPath(releaseDir ?? Path.Combine(repo, "build", "release"));
        string R(params string[] p) => Path.Combine([repo, .. p]);
        string? First(params string[] c) => c.FirstOrDefault(File.Exists);

        var plan = new List<(string To, string? From, bool Required)>
        {
            ("autoboot.iso", First(Path.Combine(rel, "autoboot.iso")), true),
            ("LAUNCHER.BIN", First(Path.Combine(rel, "LAUNCHER.BIN")), true),
            ("VRTILES.BIN", First(Path.Combine(rel, "VRTILES.BIN"), R("menu", "app", "assets", "VRTILES.BIN")), true),
            ("LOADBG.BIN", First(Path.Combine(rel, "LOADBG.BIN"), R("menu", "app", "assets", "LOADBG.BIN")), false),
            ("BGM.MUS", First(Path.Combine(rel, "BGM.MUS")), false),
            ("GAMES.DB", First(R("build", "db", "GAMES.DB")), true),
            ("jp_titles.json", First(R("build", "db", "jp_titles.json")), true),
            ("compat/loader_compat.json", First(R("tools", "data", "compat", "loader_compat.json")), true),
            ("compat/save_tools.json", First(R("tools", "data", "compat", "save_tools.json")), true),
            ("compat/controller_compat.json", First(R("tools", "data", "compat", "controller_compat.json")), true),
            ("CHEATS.PAK", First(Path.Combine(rel, "CHEATS.PAK"), R("tools", "data", "cheats", "CHEATS.PAK")), false),
        };
        var missing = plan.Where(p => p.Required && p.From == null).Select(p => p.To).ToList();
        if (missing.Count > 0)
            throw new ReleaseException($"the checkout at {repo} is missing {string.Join(", ", missing)} " +
                                       "(build the menu with tools/m3_release.sh, or pass --from the folder holding its outputs)");

        // the inputs checked before anything is written
        var iso = plan[0].From!;
        var sid = SystemIdReader.ReadImage(iso);
        if (sid == null || !CardPaths.IsMenuTitle(sid.Title))
            throw new ReleaseException($"{iso} is not an Aether image ({(sid == null ? "no Saturn System ID" : "title " + sid.Title)})");
        var (build, commit) = BuildOf(iso);
        var warnings = new List<string>();
        if (Staleness(repo, build, commit) is string stale)
        {
            if (!allowStale)
                throw new ReleaseException($"refusing to package a stale menu: {stale}. Rebuild build/release with tools/m3_release.sh " +
                                           "(or pass --from the folder of a current build), or add --allow-stale to package it anyway");
            warnings.Add("STALE MENU: " + stale);
        }
        foreach (var (to, from, _) in plan)
            if (from != null && !Path.GetFullPath(from).StartsWith(rel + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            {
                string rf = Path.GetRelativePath(repo, from).Replace('\\', '/');
                if (to is not ("GAMES.DB" or "jp_titles.json" or "CHEATS.PAK") && !to.StartsWith("compat/", StringComparison.Ordinal))
                    warnings.Add($"{to} is not in {rel}: taken from {rf} (the current tree, not necessarily the build of {build ?? "the packaged menu"})");
            }
        try { LauncherHeader.Parse(File.ReadAllBytes(plan[1].From!)); }
        catch (Exception e) when (e is System.FormatException or Formats.FormatException) { throw new ReleaseException($"{plan[1].From}: not a launcher image ({e.Message})"); }
        try { GamesDb.Read(File.ReadAllBytes(plan[5].From!)); }
        catch (Formats.FormatException e) { throw new ReleaseException($"{plan[5].From}: {e.Message}"); }
        if (plan[10].From is string cheats)
            try { CheatPak.Read(File.ReadAllBytes(cheats)); }
            catch (Formats.FormatException e) { throw new ReleaseException($"{cheats}: {e.Message}"); }
        foreach (var j in new[] { 6, 7, 8, 9 })
        {
            try
            {
                using var d = JsonDocument.Parse(File.ReadAllText(plan[j].From!));
                if (j == 6 && d.RootElement.ValueKind != JsonValueKind.Object) throw new JsonException("not an object of key -> title");
            }
            catch (JsonException e) { throw new ReleaseException($"{plan[j].From}: {e.Message}"); }
        }

        outDir = Path.GetFullPath(outDir);
        if (Directory.Exists(outDir) && Directory.EnumerateFileSystemEntries(outDir).Any())
            throw new ReleaseException($"{outDir} is not empty: assemble into a new folder");
        Directory.CreateDirectory(Path.Combine(outDir, "compat"));
        var files = new List<(string, string, string)>();
        void Put(string to, string from)
        {
            string dst = Path.Combine(outDir, to.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
            File.Copy(from, dst);
            string rf = Path.GetRelativePath(repo, from).Replace('\\', '/');
            files.Add((to, rf.StartsWith("..", StringComparison.Ordinal) ? from : rf, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(dst)))));
        }
        foreach (var (to, from, _) in plan) if (from != null) Put(to, from);
        string themes = Path.Combine(rel, "THEMES");
        if (Directory.Exists(themes))
            foreach (var t in Directory.GetDirectories(themes).Order(StringComparer.Ordinal))
            {
                if (!File.Exists(Path.Combine(t, "THEME.INI"))) continue;
                foreach (var f in Directory.GetFiles(t).Order(StringComparer.Ordinal))
                    Put($"THEMES/{Path.GetFileName(t)}/{Path.GetFileName(f)}", f);
            }

        File.WriteAllText(Path.Combine(outDir, "SOURCES.txt"),
            "# Aether release folder for Aether Card Manager (vmcm assemble-release): SHA-256, file, source\n" +
            $"# menu: {build ?? "no build string"}\n" +
            string.Concat(warnings.Select(w => "# warning: " + w + "\n")) +
            string.Concat(files.Select(f => $"{f.Item3} {f.Item1} <- {f.Item2}\n")));
        var missingNow = CardPipeline.CheckRelease(outDir, compat: true);
        if (missingNow.Count > 0) throw new ReleaseException($"assembled, but {outDir} is missing {string.Join(", ", missingNow)}");
        return new AssembledRelease(outDir, files, sid.Title.Trim(), build ?? "no build string", warnings);
    }
}
