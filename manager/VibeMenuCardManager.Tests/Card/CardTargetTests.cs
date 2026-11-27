// VibeMenu Card Manager, GPL-3.0-or-later.
// The card-write target: the system-drive refusal is by VOLUME, so the same volume reached
// through a SUBST drive letter, an NTFS junction or a UNC loopback is refused too; a fixed disk needs the explicit
// override for that resolved volume; an unreadable autoboot.iso degrades to "not VibeMenu".
using System.Diagnostics;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;

namespace VibeMenuCardManager.Tests.Card;

public class CardTargetTests
{
    static int Cmd(string args)
    {
        using var p = Process.Start(new ProcessStartInfo("cmd.exe", "/c " + args) { UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true })!;
        p.WaitForExit(15000);
        return p.ExitCode;
    }

    static string SystemVolume() => VolumeResolver.Resolve(Environment.SystemDirectory).Id!;

    [SkippableFact]
    public void A_temp_card_on_the_system_volume_is_refused()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "the volume test needs Windows");
        using var card = new TempCard();
        var v = VolumeResolver.Resolve(card.Dir);
        Skip.IfNot(v.Id == SystemVolume(), $"the temp folder is not on the system volume here ({v.Id})");
        var t = CardTarget.Inspect(card.Dir);
        Assert.True(t.IsSystemDrive);
        Assert.Contains("system drive", t.Refusal);
    }

    [SkippableFact]
    public void A_SUBST_drive_letter_onto_the_system_volume_is_refused()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "SUBST is Windows-only");
        using var card = new TempCard();
        Skip.IfNot(VolumeResolver.Resolve(card.Dir).Id == SystemVolume(), "the temp folder is not on the system volume here");
        var used = DriveInfo.GetDrives().Select(d => char.ToUpperInvariant(d.Name[0])).ToHashSet();
        char letter = "VUTSRQPONMLKJIH".FirstOrDefault(c => !used.Contains(c));
        Skip.If(letter == '\0', "no free drive letter for SUBST");
        Skip.IfNot(Cmd($"subst {letter}: \"{card.Dir}\"") == 0, "subst failed on this machine");
        try
        {
            string root = $"{letter}:\\";
            Assert.Equal($"{letter}:\\", Path.GetPathRoot(root));      // a path-string check sees only the new letter
            var t = CardTarget.Inspect(root);
            Assert.True(t.IsSystemDrive, $"SUBST {letter}: was not seen as the system volume: {t.Volume}");
            Assert.NotNull(t.Refusal);
        }
        finally { Cmd($"subst {letter}: /d"); }
    }

    [SkippableFact]
    public void A_junction_is_resolved_to_its_target_volume_and_path()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "junctions are Windows-only");
        using var card = new TempCard();
        string target = Path.Combine(card.Dir, "real");
        string link = Path.Combine(card.Dir, "link");
        Directory.CreateDirectory(target);
        Skip.IfNot(Cmd($"mklink /J \"{link}\" \"{target}\"") == 0, "mklink /J failed on this machine");
        try
        {
            var v = VolumeResolver.Resolve(link);
            Assert.Equal(VolumeResolver.Resolve(target).Id, v.Id);
            Assert.EndsWith("real", v.FinalPath);                      // the final path is the junction's target
            Assert.Equal(VolumeResolver.Resolve(target).Id == SystemVolume(), CardTarget.Inspect(link).IsSystemDrive);
        }
        finally { Directory.Delete(link); }                            // removes the junction only, never its target
    }

    [SkippableFact]
    public void A_UNC_loopback_is_refused()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "UNC paths are Windows-only");
        string unc = @"\\localhost\" + Environment.SystemDirectory[0] + @"$\";
        Skip.IfNot(Directory.Exists(unc), "the administrative share is not reachable here");
        var t = CardTarget.Inspect(unc);
        Assert.NotNull(t.Refusal);
    }

    [SkippableFact]
    public void A_fixed_disk_needs_the_override_for_that_volume()
    {
        using var card = new TempCard();
        var v = VolumeResolver.Resolve(card.Dir);
        Skip.IfNot(v.Kind == VolumeKind.Fixed, $"the temp folder is not on a fixed disk here ({v.Kind})");
        Assert.True(CardTarget.Inspect(card.Dir, []).NeedsFixedOverride);
        Assert.True(CardTarget.Inspect(card.Dir, [], "\\\\?\\Volume{another}\\ serial 00000000").NeedsFixedOverride);
        var ok = CardTarget.Inspect(card.Dir, [], v.Id);
        Assert.False(ok.NeedsFixedOverride);
        Assert.Null(ok.Refusal);
    }

    [Fact]
    public void The_menu_on_the_card_is_named()
    {
        using var card = new TempCard();
        Assert.StartsWith("no /autoboot.iso", CardTarget.Inspect(card.Dir, []).MenuOnCard);
        var iso = Path.Combine(card.Dir, "autoboot.iso");
        File.WriteAllBytes(iso, [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "SATIATOR RINGS" }), .. new byte[2048]]);
        Assert.StartsWith("/autoboot.iso is not Aether (title SATIATOR RINGS)", CardTarget.Inspect(card.Dir, []).MenuOnCard);
        File.WriteAllBytes(iso, [.. SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU" }), .. new byte[5000],
                                 .. "...VibeMenu 0.1 20261122-ab12\0"u8.ToArray()]);
        var t = CardTarget.Inspect(card.Dir, []);
        Assert.Equal("0.1", t.VibeMenuVersion);
        Assert.Contains("Aether 0.1 (/autoboot.iso)", t.Describe("Import saves", ["/aether/SAVES/..."]));
    }

    [SkippableFact]
    [SupportedOSPlatform("windows")]
    public void An_unreadable_autoboot_iso_is_not_vibemenu_never_a_crash()
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "the access-denied file is made with a Windows ACL");
        using var card = new TempCard();
        var iso = Path.Combine(card.Dir, "autoboot.iso");
        File.WriteAllBytes(iso, SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "VIBEMENU" }));
        var fi = new FileInfo(iso);
        var acl = fi.GetAccessControl();
        var rule = new FileSystemAccessRule(WindowsIdentity.GetCurrent().User!, FileSystemRights.ReadData, AccessControlType.Deny);
        acl.AddAccessRule(rule);
        fi.SetAccessControl(acl);
        try { Assert.StartsWith("/autoboot.iso could not be read", CardTarget.Inspect(card.Dir, []).MenuOnCard); }
        finally { acl.RemoveAccessRule(rule); fi.SetAccessControl(acl); }
    }
}
