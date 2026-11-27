// Aether Card Manager, GPL-3.0-or-later.
// A build's identity: its version, the UTC time it was built (yyyyMMdd.HHmm, which orders as text), its commit and
// whether it is a pre-release. The running app reads its own from the assembly (Directory.Build.props writes
// InformationalVersion "0.1+20261127.1532.c33b131" and an AetherPrerelease metadata attribute); a release describes
// its build in a version.json asset (package.ps1), so two builds of the same version can be told apart.
using System.Reflection;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Update;

/// <param name="Stamp">the UTC build time, yyyyMMdd.HHmm; null when unknown (a Debug build, an older release).</param>
public sealed partial record BuildInfo(string Version, string? Stamp, string? Commit, bool Prerelease)
{
    public const string AssetName = "version.json";

    [GeneratedRegex(@"^\d{8}\.\d{4}$")] private static partial Regex StampShape();

    static string? CleanStamp(string? s) => s != null && StampShape().IsMatch(s) ? s : null;
    static string? CleanCommit(string? s) => string.IsNullOrWhiteSpace(s) ? null : s.Trim();

    /// <summary>"0.1+20261127.1532.c33b131" (either part may be missing: "0.1+.c33b131", "0.1+20261127.1532.", "0.1").</summary>
    public static BuildInfo FromInformationalVersion(string info, bool prerelease)
    {
        int plus = info.IndexOf('+');
        string version = plus < 0 ? info : info[..plus];
        string meta = plus < 0 ? "" : info[(plus + 1)..];
        var m = Regex.Match(meta, @"^(\d{8}\.\d{4})?\.?(.*)$");
        return new BuildInfo(version, CleanStamp(m.Groups[1].Value), CleanCommit(m.Groups[2].Value), prerelease);
    }

    /// <summary>This app's own build (Core's assembly carries the same identity as every project of the build).</summary>
    public static BuildInfo Running()
    {
        var asm = typeof(BuildInfo).Assembly;
        string info = asm.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? About.Credits.Version;
        bool pre = !string.Equals(asm.GetCustomAttributes<AssemblyMetadataAttribute>().FirstOrDefault(a => a.Key == "AetherPrerelease")?.Value,
                                  "false", StringComparison.OrdinalIgnoreCase);
        return FromInformationalVersion(info, pre);
    }

    /// <summary>A release's version.json ({"version","build","commit","prerelease"}), else null.</summary>
    public static BuildInfo? FromJson(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var r = doc.RootElement;
            if (r.ValueKind != JsonValueKind.Object || !r.TryGetProperty("version", out var v) || v.GetString() is not string ver) return null;
            string? S(string k) => r.TryGetProperty(k, out var e) && e.ValueKind == JsonValueKind.String ? e.GetString() : null;
            bool pre = r.TryGetProperty("prerelease", out var p) && p.ValueKind == JsonValueKind.True;
            return new BuildInfo(ver, CleanStamp(S("build")), CleanCommit(S("commit")), pre);
        }
        catch (JsonException) { return null; }
    }

    /// <summary>"Build 20261127.1532 · c33b131 · pre-release" for About; "" when nothing is known.</summary>
    public string Describe()
    {
        var parts = new List<string>();
        if (Stamp != null) parts.Add($"Build {Stamp[..4]}-{Stamp[4..6]}-{Stamp[6..8]} {Stamp[9..11]}:{Stamp[11..]} UTC");
        if (Commit != null) parts.Add(Commit);
        if (Prerelease) parts.Add("pre-release");
        return string.Join(" · ", parts);
    }
}
