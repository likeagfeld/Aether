// Aether Card Manager, GPL-3.0-or-later.
// What a write to a card is about to touch (Task 8 and its re-review). The target folder is resolved to the
// volume it really lives on (VolumeResolver: SUBST drives, junctions, symbolic links and UNC loopbacks all resolved
// away), and refused outright when that volume holds Windows, the system folder or the user profile, or cannot be
// resolved to a local volume. A fixed (non-removable) disk is refused unless the user explicitly overrides for that
// resolved volume ("this is not my system drive"): some USB card readers present as fixed disks. The Aether version on
// the card (/autoboot.iso's System ID title and the menu's build string, menu/src/version.h OM_BUILD_STRING
// "Aether <version> <build id>") is shown in the confirmation; it never decides anything.
using System.Text;
using System.Text.RegularExpressions;
using VibeMenuCardManager.Core.Disc;

namespace VibeMenuCardManager.Core.Card;

/// <param name="Refusal">why no write may happen at all (a system volume, an unresolvable or network target); null otherwise.</param>
/// <param name="NeedsFixedOverride">a fixed disk not overridden for this volume: blocked until the user overrides.</param>
public sealed partial record CardTarget(string Root, ResolvedVolume Volume, string DriveRoot, string? VolumeLabel,
                                        bool IsSystemDrive, string? Refusal, bool NeedsFixedOverride,
                                        string? VibeMenuVersion, string MenuOnCard)
{
    [GeneratedRegex(CardPaths.BuildWord + " ([0-9]+\\.[0-9]+[0-9A-Za-z.]*)")] private static partial Regex BuildString();

    /// <param name="systemVolumes">the volume ids refused (default: VolumeResolver.SystemVolumes()).</param>
    /// <param name="fixedOverride">the volume id the user said is not the system drive (only that one is allowed).</param>
    public static CardTarget Inspect(string root, IReadOnlyList<string>? systemVolumes = null, string? fixedOverride = null)
    {
        string full = Path.GetFullPath(root);
        var vol = VolumeResolver.Resolve(full);
        string drive = Path.GetPathRoot(vol.FinalPath) is string r && r != "" ? r : vol.FinalPath;
        string? label = null;
        try { if (drive.Length <= 3) { var di = new DriveInfo(drive); if (di.IsReady) label = di.VolumeLabel; } }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException) { }
        bool sys = vol.Id != null && (systemVolumes ?? VolumeResolver.SystemVolumes()).Contains(vol.Id);
        string? refusal = !vol.Resolved
            ? $"{full} ({vol.FinalPath}): {vol.Error}; Aether Card Manager writes only to a local card"
            : sys ? $"{full} is on the system drive ({vol.FinalPath}, {vol.Id}: it holds Windows or your user profile): Aether Card Manager never writes to it"
            : null;
        bool needsOverride = refusal == null && vol.Kind == VolumeKind.Fixed && fixedOverride != vol.Id;
        var (version, menu) = Menu(full);
        return new CardTarget(full, vol, drive, label, sys, refusal, needsOverride, version, menu);
    }

    static (string? Version, string Menu) Menu(string root)
    {
        string iso = Path.Combine(root, "autoboot.iso");
        try
        {
            if (!File.Exists(iso)) return (null, "no /autoboot.iso (no Aether, Rings or Kai menu installed)");
            var sid = SystemIdReader.ReadImage(iso);
            if (sid == null || !CardPaths.IsMenuTitle(sid.Title))
                return (null, $"/autoboot.iso is not Aether ({(sid == null ? "no Saturn System ID" : "title " + sid.Title)}): it will be backed up");
            var buf = new byte[4 << 20];                   // the menu's 1st Read program sits early in the image
            int n;
            using (var f = File.OpenRead(iso)) n = f.Read(buf, 0, buf.Length);
            var m = BuildString().Match(Encoding.Latin1.GetString(buf, 0, n));
            return m.Success ? (m.Groups[1].Value, $"Aether {m.Groups[1].Value} (/autoboot.iso)") : (null, "Aether, version not found in /autoboot.iso");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)          // re-review Important
        {
            return (null, $"/autoboot.iso could not be read ({e.Message}): treated as not Aether");
        }
    }

    /// <summary>The confirmation's text: the resolved volume, its label, the menu on the card, and what will be written.</summary>
    public string Describe(string action, IEnumerable<string> writes) =>
        $"{action} on {DriveRoot} ({(string.IsNullOrEmpty(VolumeLabel) ? "no volume label" : "label \"" + VolumeLabel + "\"")}, {Volume.Kind})\n" +
        $"Folder: {Root}" + (string.Equals(Root.TrimEnd('\\'), Volume.FinalPath.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase) ? "" : $" (really {Volume.FinalPath})") + "\n" +
        $"Volume: {Volume.Id}\nMenu on the card: {MenuOnCard}\nIt will write:\n" + string.Join("\n", writes.Select(w => "  " + w));
}
