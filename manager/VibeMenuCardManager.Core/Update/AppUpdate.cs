// Aether Card Manager, GPL-3.0-or-later.
// The Card Manager's own update (0.1): ask GitHub for the latest release of likeagfeld/Aether (public REST API, no
// token, short timeout -- offline, a 404 while the repo is private, or no release at all all read as "can't check",
// never an error), compare versions, download this platform's package (manager/release/package.ps1's asset names) and
// verify it against its size and the release's SHA256SUMS.txt, then hand the swap to a small updater script that waits
// for the app to exit, swaps the folder (the old one kept for rollback) and relaunches it.
using System.IO.Compression;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Update;

public sealed record ReleaseAsset(string Name, string Url, long Size);

/// <param name="Prerelease">GitHub's own pre-release flag on the release.</param>
/// <param name="Build">the release's version.json, when it has one (null: compare by version alone).</param>
public sealed record ReleaseInfo(string Version, string Tag, string Notes, string HtmlUrl, IReadOnlyList<ReleaseAsset> Assets,
                                 bool Prerelease = false, BuildInfo? Build = null);

/// <param name="Newer">the build to offer, else null.</param>
/// <param name="Reached">GitHub answered at all (false: offline, a 404, no release -- "can't check").</param>
public sealed record UpdateCheck(ReleaseInfo? Newer, bool Reached);

public sealed class UpdateException(string message) : Exception(message);

public static partial class AppVersion
{
    [GeneratedRegex(@"\d+(\.\d+)*")] private static partial Regex Numbers();

    /// <summary>The dotted version in a tag or string ("v0.1.1", "cardmanager-0.2", "0.1"), else null.</summary>
    public static string? Parse(string s) => Numbers().Match(s) is { Success: true } m ? m.Value : null;

    /// <summary>Dotted numeric compare, missing parts as 0: 0.1 &lt; 0.1.1 &lt; 0.2 &lt; 0.10.</summary>
    public static int Compare(string a, string b)
    {
        var pa = (Parse(a) ?? "0").Split('.'); var pb = (Parse(b) ?? "0").Split('.');
        for (int i = 0; i < Math.Max(pa.Length, pb.Length); i++)
        {
            long x = i < pa.Length && long.TryParse(pa[i], out var u) ? u : 0, y = i < pb.Length && long.TryParse(pb[i], out var v) ? v : 0;
            if (x != y) return x.CompareTo(y);
        }
        return 0;
    }
}

public static class AppUpdate
{
    public const string LatestUrl = "https://api.github.com/repos/likeagfeld/Aether/releases/latest";
    /// <summary>Every release, pre-releases included, newest first (only a pre-release build asks for this).</summary>
    public const string ListUrl = "https://api.github.com/repos/likeagfeld/Aether/releases?per_page=20";
    public const string ReleasesPage = "https://github.com/likeagfeld/Aether/releases";
    public const string SumsName = "SHA256SUMS.txt";
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(6);

    /// <summary>The latest release, or null whenever it cannot be known (offline, timeout, 404, private repo, rate
    /// limit, a body that does not parse) -- the caller shows "up to date / can't check", never an error.</summary>
    public static async Task<ReleaseInfo?> LatestAsync(HttpClient http, CancellationToken token = default) =>
        await GetJsonAsync(http, LatestUrl, token).ConfigureAwait(false) is string json ? ParseOne(json) : null;

    /// <summary>Every published release (drafts skipped), pre-releases included; null when it cannot be known.</summary>
    public static async Task<List<ReleaseInfo>?> ListAsync(HttpClient http, CancellationToken token = default)
    {
        if (await GetJsonAsync(http, ListUrl, token).ConfigureAwait(false) is not string json) return null;
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.ValueKind != JsonValueKind.Array) return null;
            return [.. doc.RootElement.EnumerateArray()
                        .Where(e => !(e.TryGetProperty("draft", out var d) && d.ValueKind == JsonValueKind.True))
                        .Select(Parse).OfType<ReleaseInfo>()];
        }
        catch (JsonException) { return null; }
    }

    static async Task<string?> GetJsonAsync(HttpClient http, string url, CancellationToken token)
    {
        try
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(token);
            cts.CancelAfter(Timeout);
            using var req = new HttpRequestMessage(HttpMethod.Get, url);
            req.Headers.UserAgent.Add(new ProductInfoHeaderValue("AetherCardManager", About.Credits.Version));
            req.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            using var resp = await http.SendAsync(req, cts.Token).ConfigureAwait(false);
            if (!resp.IsSuccessStatusCode) return null;
            return await resp.Content.ReadAsStringAsync(cts.Token).ConfigureAwait(false);
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException or OperationCanceledException or InvalidOperationException)
        {
            return null;
        }
    }

    static ReleaseInfo? ParseOne(string json)
    {
        try { using var doc = JsonDocument.Parse(json); return Parse(doc.RootElement); }
        catch (JsonException) { return null; }
    }

    static ReleaseInfo? Parse(JsonElement r)
    {
        try
        {
            if (r.ValueKind != JsonValueKind.Object) return null;
            string tag = r.GetProperty("tag_name").GetString() ?? "";
            if (AppVersion.Parse(tag) is not string ver) return null;
            var assets = r.TryGetProperty("assets", out var a) && a.ValueKind == JsonValueKind.Array
                ? a.EnumerateArray().Select(x => new ReleaseAsset(x.GetProperty("name").GetString() ?? "", x.GetProperty("browser_download_url").GetString() ?? "",
                                                                  x.TryGetProperty("size", out var sz) ? sz.GetInt64() : -1)).ToList()
                : [];
            return new ReleaseInfo(ver, tag, r.TryGetProperty("body", out var b) ? b.GetString() ?? "" : "",
                                   r.TryGetProperty("html_url", out var h) ? h.GetString() ?? ReleasesPage : ReleasesPage, assets,
                                   r.TryGetProperty("prerelease", out var p) && p.ValueKind == JsonValueKind.True);
        }
        catch (Exception e) when (e is KeyNotFoundException or InvalidOperationException or FormatException) { return null; }
    }

    /// <summary>The release with its version.json read (its build), else unchanged (compared by version alone).</summary>
    static async Task<ReleaseInfo> WithBuildAsync(HttpClient http, ReleaseInfo r, CancellationToken token)
    {
        if (r.Assets.FirstOrDefault(a => a.Name == BuildInfo.AssetName) is not { } asset) return r;
        try
        {
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(token);
            cts.CancelAfter(Timeout);
            using var resp = await http.GetAsync(asset.Url, cts.Token).ConfigureAwait(false);
            if (!resp.IsSuccessStatusCode) return r;
            string json = await resp.Content.ReadAsStringAsync(cts.Token).ConfigureAwait(false);
            return BuildInfo.FromJson(json) is { } b && AppVersion.Compare(b.Version, r.Version) == 0 ? r with { Build = b } : r;
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException or OperationCanceledException or InvalidOperationException)
        {
            return r;
        }
    }

    /// <summary>Whether `r` is a newer build than `current`: a higher version, or the same version built later (both
    /// build times known). A final build is only ever offered finals.</summary>
    public static bool IsNewer(ReleaseInfo r, BuildInfo current)
    {
        if (!current.Prerelease && (r.Prerelease || r.Build?.Prerelease == true)) return false;
        int c = AppVersion.Compare(r.Version, current.Version);
        if (c != 0) return c > 0;
        return r.Build?.Stamp is string remote && current.Stamp is string mine && string.CompareOrdinal(remote, mine) > 0;
    }

    /// <summary>The update check: /releases/latest (the newest final); a pre-release build also reads the release
    /// list, so it hears of a newer pre-release (and of the final when /latest has none yet). Each candidate's
    /// version.json gives its build, so the final 0.1 is offered to a pre-release 0.1 built before it; a release
    /// without one is compared by version alone.</summary>
    public static async Task<UpdateCheck> CheckAsync(HttpClient http, BuildInfo current, CancellationToken token = default)
    {
        var found = new List<ReleaseInfo>();
        bool reached = false;
        if (await LatestAsync(http, token).ConfigureAwait(false) is { } latest) { reached = true; found.Add(latest); }
        if (current.Prerelease && await ListAsync(http, token).ConfigureAwait(false) is { } list) { reached = true; found.AddRange(list); }
        ReleaseInfo? best = null;
        foreach (var r0 in found.DistinctBy(r => r.Tag).Where(r => AppVersion.Compare(r.Version, current.Version) >= 0))
        {
            var r = await WithBuildAsync(http, r0, token).ConfigureAwait(false);
            if (!IsNewer(r, current)) continue;
            int byVersion = best == null ? 1 : AppVersion.Compare(r.Version, best.Version);
            if (byVersion > 0 || (byVersion == 0 && string.CompareOrdinal(r.Build?.Stamp ?? "", best!.Build?.Stamp ?? "") > 0)) best = r;
        }
        return new UpdateCheck(best, reached);
    }

    /// <summary>A release newer than `current`, else null.</summary>
    public static ReleaseInfo? Newer(ReleaseInfo? latest, string current) =>
        latest != null && AppVersion.Compare(latest.Version, current) > 0 ? latest : null;

    /// <summary>The package this platform installs in place (package.ps1: AetherCardManager-&lt;ver&gt;-&lt;rid&gt;.zip on
    /// Windows, .tar.gz on Linux); null on macOS (the .app zip is installed by the user: the release page opens) or when
    /// the release lacks it.</summary>
    public static ReleaseAsset? AssetFor(ReleaseInfo r, string rid)
    {
        string? name = rid.StartsWith("win-", StringComparison.Ordinal) ? $"AetherCardManager-{r.Version}-{rid}.zip"
                     : rid.StartsWith("linux-", StringComparison.Ordinal) ? $"AetherCardManager-{r.Version}-{rid}.tar.gz" : null;
        return name == null ? null : r.Assets.FirstOrDefault(a => a.Name == name);
    }

    /// <summary>SHA256SUMS.txt ("&lt;hex&gt;  &lt;name&gt;" lines, sha256sum's format): the hash for `name`, else null.</summary>
    public static string? SumFor(string sums, string name)
    {
        foreach (var line in sums.Replace("\r", "").Split('\n'))
        {
            var parts = line.Trim().Split((char[]?)null, 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2 && parts[1].TrimStart('*') == name && parts[0].Length == 64) return parts[0].ToLowerInvariant();
        }
        return null;
    }

    /// <summary>Downloads `asset` into `dir` and checks it: its size against the release's, and its SHA-256 against
    /// the release's SHA256SUMS.txt when the release has one. Any mismatch deletes the file and throws.</summary>
    public static async Task<string> DownloadVerifiedAsync(HttpClient http, ReleaseInfo r, ReleaseAsset asset, string dir, CancellationToken token = default)
    {
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, asset.Name);
        byte[] data;
        try { data = await http.GetByteArrayAsync(asset.Url, token).ConfigureAwait(false); }
        catch (HttpRequestException e) { throw new UpdateException($"the download failed: {e.Message}"); }
        if (asset.Size >= 0 && data.LongLength != asset.Size)
            throw new UpdateException($"{asset.Name} is {data.LongLength} B, the release says {asset.Size} B: not installed");
        if (r.Assets.FirstOrDefault(a => a.Name == SumsName) is { } sumsAsset)
        {
            string sums;
            try { sums = await http.GetStringAsync(sumsAsset.Url, token).ConfigureAwait(false); }
            catch (HttpRequestException e) { throw new UpdateException($"{SumsName} could not be read: {e.Message}"); }
            string? want = SumFor(sums, asset.Name);
            string got = Convert.ToHexStringLower(SHA256.HashData(data));
            if (want == null) throw new UpdateException($"{SumsName} has no line for {asset.Name}: not installed");
            if (want != got) throw new UpdateException($"{asset.Name}'s SHA-256 is {got}, {SumsName} says {want}: not installed");
        }
        await File.WriteAllBytesAsync(path, data, token).ConfigureAwait(false);
        return path;
    }

    /// <summary>Unpacks a verified package into a new staging folder beside the app (same volume, so the swap is two
    /// renames). Returns that folder.</summary>
    public static string Stage(string package, string appDir)
    {
        string staged = appDir.TrimEnd(Path.DirectorySeparatorChar, '/') + ".update";
        if (Directory.Exists(staged)) Directory.Delete(staged, recursive: true);
        Directory.CreateDirectory(staged);
        if (package.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) ZipFile.ExtractToDirectory(package, staged);
        else if (package.EndsWith(".tar.gz", StringComparison.OrdinalIgnoreCase))
        {
            using var gz = new GZipStream(File.OpenRead(package), CompressionMode.Decompress);
            System.Formats.Tar.TarFile.ExtractToDirectory(gz, staged, overwriteFiles: true);
            // package.ps1 tars the <rid>/ folder itself: lift its contents to the top
            var subs = Directory.GetDirectories(staged);
            if (subs.Length == 1 && Directory.GetFiles(staged).Length == 0)
            {
                string inner = subs[0], lifted = staged + ".lift";
                Directory.Move(inner, lifted);
                Directory.Delete(staged);
                Directory.Move(lifted, staged);
            }
        }
        else throw new UpdateException($"{Path.GetFileName(package)}: not a package this updater unpacks");
        return staged;
    }

    /// <summary>The swap itself, as the updater script runs it: app -> app.old, staged -> app; when the second rename
    /// fails, app.old goes back (rollback) and the error is rethrown. On success app.old is left for a manual rollback
    /// until the next update.</summary>
    public static void Swap(string appDir, string staged, Action<string, string>? beforeMove = null)
    {
        appDir = appDir.TrimEnd(Path.DirectorySeparatorChar, '/');
        string old = appDir + ".old";
        if (Directory.Exists(old)) Directory.Delete(old, recursive: true);
        beforeMove?.Invoke(appDir, old);
        Directory.Move(appDir, old);
        try
        {
            beforeMove?.Invoke(staged, appDir);
            Directory.Move(staged, appDir);
        }
        catch
        {
            if (Directory.Exists(old))
            {
                if (Directory.Exists(appDir)) Directory.Delete(appDir, recursive: true);
                Directory.Move(old, appDir);
            }
            throw;
        }
    }

    /// <summary>The updater script for this OS: waits for `pid` to exit, does Swap's two renames (rolling back on a
    /// failed second one), then starts `exe` again. PowerShell on Windows, sh elsewhere.</summary>
    public static string Script(bool windows, int pid, string appDir, string staged, string exe)
    {
        appDir = appDir.TrimEnd('\\', '/');
        string old = appDir + ".old";
        if (windows)
        {
            static string Q(string s) => "'" + s.Replace("'", "''") + "'";
            return new StringBuilder()
                .AppendLine("$ErrorActionPreference = 'Stop'")
                .AppendLine($"try {{ Wait-Process -Id {pid} -Timeout 60 -ErrorAction SilentlyContinue }} catch {{ }}")
                .AppendLine($"$app = {Q(appDir)}; $old = {Q(old)}; $new = {Q(staged)}; $exe = {Q(exe)}")
                .AppendLine("if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }")
                .AppendLine("Move-Item -LiteralPath $app -Destination $old")
                .AppendLine("try { Move-Item -LiteralPath $new -Destination $app }")
                .AppendLine("catch { if (Test-Path -LiteralPath $app) { Remove-Item -LiteralPath $app -Recurse -Force }; Move-Item -LiteralPath $old -Destination $app; Start-Process -FilePath $exe; exit 1 }")
                .AppendLine("Start-Process -FilePath $exe")
                .ToString();
        }
        static string S(string s) => "'" + s.Replace("'", "'\\''") + "'";
        return new StringBuilder()
            .AppendLine("#!/bin/sh")
            .AppendLine($"while kill -0 {pid} 2>/dev/null; do sleep 0.5; done")
            .AppendLine($"app={S(appDir)}; old={S(old)}; new={S(staged)}; exe={S(exe)}")
            .AppendLine("rm -rf \"$old\"")
            .AppendLine("mv \"$app\" \"$old\" || exit 1")
            .AppendLine("if ! mv \"$new\" \"$app\"; then rm -rf \"$app\"; mv \"$old\" \"$app\"; \"$exe\" & exit 1; fi")
            .AppendLine("\"$exe\" &")
            .ToString();
    }
}
