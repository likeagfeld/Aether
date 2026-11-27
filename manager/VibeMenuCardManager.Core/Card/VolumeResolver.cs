// Aether Card Manager, GPL-3.0-or-later.
// Which volume a folder really is: a SUBST drive letter, a junction, a symbolic
// link or a UNC loopback (\\localhost\C$) all name the system drive under another path, so the system-drive refusal
// compares VOLUME IDENTITY, never path strings. On Windows the folder is opened (FILE_FLAG_BACKUP_SEMANTICS, which
// follows every reparse point and device mapping to the object the OS will actually write) and asked for its final path
// as a volume GUID path (GetFinalPathNameByHandle, VOLUME_NAME_GUID) and for its volume serial number
// (GetVolumeInformationByHandleW); the drive type comes from the resolved volume (GetDriveTypeW). A target that cannot be
// resolved to a local volume (a network path, an error) is reported as such and refused.
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace VibeMenuCardManager.Core.Card;

public enum VolumeKind { Removable, Fixed, Remote, CdRom, RamDisk, Unknown }

/// <param name="Id">the volume's identity: "\\?\Volume{GUID}\" plus its serial number (Windows); null when unresolved.</param>
/// <param name="FinalPath">the folder's final path after every mapping and reparse point (the DOS form when there is one).</param>
public sealed record ResolvedVolume(string? Id, string FinalPath, VolumeKind Kind, string? Error)
{
    public bool Resolved => Id != null;

    /// <summary>What a confirmed write is pinned to: the volume and the folder's final path on it (a junction or SUBST
    /// retargeted within the same volume changes the path, onto another volume the id).</summary>
    public string? Identity => Id == null ? null : Id + " | " + FinalPath.TrimEnd('\\', '/').ToUpperInvariant();
}

/// <summary>The card's folder no longer resolves to the volume and path the user confirmed (review-2: a SUBST or junction
/// retargeted during a write). Nothing more is written.</summary>
public sealed class CardChangedException(string root, string confirmed, string now)
    : IOException($"the card changed under {root} after it was confirmed (confirmed {confirmed}, now {now}): stopped, nothing more written");

public static partial class VolumeResolver
{
    const uint FILE_SHARE_ALL = 0x7, OPEN_EXISTING = 3, FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
    const uint VOLUME_NAME_DOS = 0x0, VOLUME_NAME_GUID = 0x1;

    [LibraryImport("kernel32.dll", EntryPoint = "CreateFileW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    private static partial SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disposition, uint flags, IntPtr template);

    [LibraryImport("kernel32.dll", EntryPoint = "GetFinalPathNameByHandleW", SetLastError = true)]
    private static unsafe partial uint GetFinalPathNameByHandle(SafeFileHandle h, char* buf, uint len, uint flags);

    [LibraryImport("kernel32.dll", EntryPoint = "GetVolumeInformationByHandleW", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetVolumeInformationByHandle(SafeFileHandle h, IntPtr name, uint nameLen, out uint serial,
                                                             out uint maxComp, out uint flags, IntPtr fs, uint fsLen);

    [LibraryImport("kernel32.dll", EntryPoint = "GetDriveTypeW", StringMarshalling = StringMarshalling.Utf16)]
    private static partial uint GetDriveType(string root);

    static unsafe string? FinalPath(SafeFileHandle h, uint flags)
    {
        var buf = new char[1024];
        uint n;
        fixed (char* p = buf) n = GetFinalPathNameByHandle(h, p, (uint)buf.Length, flags);
        return n == 0 || n >= buf.Length ? null : new string(buf, 0, (int)n);
    }

    static VolumeKind Kind(uint t) => t switch
    {
        2 => VolumeKind.Removable, 3 => VolumeKind.Fixed, 4 => VolumeKind.Remote, 5 => VolumeKind.CdRom, 6 => VolumeKind.RamDisk,
        _ => VolumeKind.Unknown,
    };

    /// <summary>The volume a folder (or file) really lives on.</summary>
    public static ResolvedVolume Resolve(string path)
    {
        string full = Path.GetFullPath(path);
        if (!OperatingSystem.IsWindows()) return ResolveUnix(full);
        using var h = CreateFile(full, 0, FILE_SHARE_ALL, IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
        if (h.IsInvalid) return new ResolvedVolume(null, full, VolumeKind.Unknown, new Win32Exception(Marshal.GetLastWin32Error()).Message);
        string dos = FinalPath(h, VOLUME_NAME_DOS) ?? full;
        if (dos.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
            return new ResolvedVolume(null, dos, VolumeKind.Remote, "a network path, not a local card");
        string? guid = FinalPath(h, VOLUME_NAME_GUID);
        if (guid == null || !guid.StartsWith(@"\\?\Volume{", StringComparison.OrdinalIgnoreCase))
            return new ResolvedVolume(null, dos, VolumeKind.Unknown, "the folder's volume could not be resolved");
        string volRoot = guid[..(guid.IndexOf('}') + 2)];                                // "\\?\Volume{...}\"
        if (!GetVolumeInformationByHandle(h, IntPtr.Zero, 0, out uint serial, out _, out _, IntPtr.Zero, 0))
            return new ResolvedVolume(null, dos, VolumeKind.Unknown, new Win32Exception(Marshal.GetLastWin32Error()).Message);
        string display = dos.StartsWith(@"\\?\", StringComparison.Ordinal) ? dos[4..] : dos;
        return new ResolvedVolume($"{volRoot} serial {serial:X8}", display, Kind(GetDriveType(volRoot)), null);
    }

    /// <summary>Elsewhere: the final path (every symlink followed) and the mount it falls under, by longest prefix.</summary>
    static ResolvedVolume ResolveUnix(string full)
    {
        try
        {
            string final = new DirectoryInfo(full).ResolveLinkTarget(returnFinalTarget: true)?.FullName ?? full;
            var d = DriveInfo.GetDrives().Where(x => final.StartsWith(x.RootDirectory.FullName, StringComparison.Ordinal))
                                         .OrderByDescending(x => x.RootDirectory.FullName.Length).FirstOrDefault();
            if (d == null) return new ResolvedVolume(null, final, VolumeKind.Unknown, "no mount found");
            var kind = d.DriveType switch { DriveType.Removable => VolumeKind.Removable, DriveType.Fixed => VolumeKind.Fixed,
                DriveType.Network => VolumeKind.Remote, _ => VolumeKind.Unknown };
            return new ResolvedVolume(d.RootDirectory.FullName, final, kind, null);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return new ResolvedVolume(null, full, VolumeKind.Unknown, e.Message); }
    }

    /// <summary>The volumes holding Windows, the system folder and the user profile, resolved the same way.</summary>
    public static IReadOnlyList<string> SystemVolumes()
    {
        var o = new List<string>();
        foreach (var p in new[] { Environment.SystemDirectory, Environment.GetFolderPath(Environment.SpecialFolder.Windows),
                                  Environment.GetFolderPath(Environment.SpecialFolder.UserProfile) })
            if (!string.IsNullOrEmpty(p) && Resolve(p).Id is string id && !o.Contains(id)) o.Add(id);
        return o;
    }
}
