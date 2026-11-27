// Aether Card Manager, GPL-3.0-or-later.
// A Satiator SD card as a directory root, with the operations the save import needs, shaped like the console's
// (libsatiator over FatFs, as menu/launcher/pgsbak.c uses it):
// - listings in unsigned byte order of the UTF-8 name (the golden harness lists the console's import the same way,
//   manager/golden/README.md); only "not found" is an absent folder -- any other error stops the caller, as pgsbak.c
//   list() stops on anything but FR_NO_FILE / FR_NO_PATH;
// - whole-file reads;
// - a write that reaches the device before it is read back, compared and renamed into place (om_sdw_write_file with
//   verify on, then s_rename, which never overwrites);
// - a write-then-replace (om_sdw_replace, menu/src/sdwrite.h) whose step 1 never truncates.
// Durability: data is written with FileOptions.WriteThrough and FileStream.Flush(flushToDisk: true)
// (FlushFileBuffers on Windows) before the read-back and before any rename, so a final-named file never exists whose
// bytes were only in the OS cache. Limits, stated honestly: the read-back is an ordinary buffered read, so it proves the
// OS's view of the file (as the console's verify proves the firmware's), not the media's; and the rename itself (a FAT
// directory entry) is not separately flushed -- after a cut at the rename, the next import sees the ".IMP" (never
// taken) or the final name, whose data was already flushed.
namespace VibeMenuCardManager.Core.Saves;

public sealed class CardFs
{
    public string Root { get; }

    /// <summary>Test hooks (internal): called with the final SD path just before a rename into place; a throw there is
    /// a simulated power cut between the write and the rename. FailWrite: the SD path whose data write fails.</summary>
    internal Action<string>? BeforeRename;
    internal string? FailWrite;

    public CardFs(string root) => Root = Path.GetFullPath(root);

    /// <param name="confirmedIdentity">the ResolvedVolume.Identity the user confirmed (CardWriteGuard): Verify() then
    /// re-resolves the root before each committed file and refuses to go on when it changed.</param>
    public CardFs(string root, string? confirmedIdentity) : this(root) => ConfirmedIdentity = confirmedIdentity;

    public string? ConfirmedIdentity { get; }

    /// <summary>Throws CardChangedException when a confirmed card's root no longer resolves to the confirmed volume and
    /// path (no-op without a confirmed identity).</summary>
    public void Verify()
    {
        if (ConfirmedIdentity == null) return;
        string now = Card.VolumeResolver.Resolve(Root).Identity ?? "unresolvable";
        if (now != ConfirmedIdentity) throw new Card.CardChangedException(Root, ConfirmedIdentity, now);
    }

    /// <summary>"/aether/SAVES/X" -> the host path under Root.</summary>
    public string Host(string sdPath) =>
        Path.Combine(Root, sdPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar));

    public readonly record struct Entry(string Name, bool IsDir, long Size)
    {
        public int NameBytes => System.Text.Encoding.UTF8.GetByteCount(Name);
    }

    /// <summary>True only when the path does not exist ("not found"); any other error (access denied, a device error)
    /// is thrown.</summary>
    public bool Absent(string sdPath)
    {
        try { File.GetAttributes(Host(sdPath)); return false; }
        catch (FileNotFoundException) { return true; }
        catch (DirectoryNotFoundException) { return true; }
    }

    /// <summary>A folder's entries ("." and ".." never appear), in byte order of name. An absent folder, or a path that
    /// is a file, lists nothing (s_opendir's FR_NO_PATH / FR_NO_FILE); any other error is thrown.</summary>
    public IReadOnlyList<Entry> List(string sdPath)
    {
        string h = Host(sdPath);
        FileAttributes a;
        try { a = File.GetAttributes(h); }
        catch (FileNotFoundException) { return []; }
        catch (DirectoryNotFoundException) { return []; }
        if ((a & FileAttributes.Directory) == 0) return [];
        var o = new List<Entry>();
        foreach (var e in new DirectoryInfo(h).EnumerateFileSystemInfos())
            o.Add(e is DirectoryInfo ? new Entry(e.Name, true, 0) : new Entry(e.Name, false, ((FileInfo)e).Length));
        o.Sort((x, y) => Be.CompareBytes(x.Name, y.Name));
        return o;
    }

    public byte[] ReadAll(string sdPath) => File.ReadAllBytes(Host(sdPath));

    /// <summary>n bytes at off; fewer available is a short read (read_at's OM_E_SHORT).</summary>
    public byte[] ReadAt(string sdPath, long off, int n)
    {
        using var f = File.OpenRead(Host(sdPath));
        f.Seek(off, SeekOrigin.Begin);
        var b = new byte[n];
        int got = 0, r;
        while (got < n && (r = f.Read(b, got, n - got)) > 0) got += r;
        if (got != n) throw new ShortReadException(sdPath, off, n, got);
        return b;
    }

    public void MkDir(string sdPath) => Directory.CreateDirectory(Host(sdPath));

    /// <summary>Create/truncate, write through to the device and flush it, then read back and compare.</summary>
    void WriteDurable(string sdPath, byte[] data)
    {
        using (var f = new FileStream(Host(sdPath), FileMode.Create, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough))
        {
            if (FailWrite == sdPath)                       // a device error part-way: half the data is on the card
            {
                f.Write(data.AsSpan(0, data.Length / 2));
                throw new IOException($"injected write failure: {sdPath}");
            }
            f.Write(data);
            f.Flush(flushToDisk: true);
        }
        if (!File.ReadAllBytes(Host(sdPath)).AsSpan().SequenceEqual(data))
            throw new IOException($"verify failed: {sdPath}");
    }

    void Rename(string fromSd, string toSd)
    {
        BeforeRename?.Invoke(toSd);
        Verify();                                         // the card is still the one the user confirmed
        File.Move(Host(fromSd), Host(toSd), overwrite: false);   // FatFs f_rename refuses an existing name (FR_EXIST)
    }

    /// <summary>Test hook (internal): called with the SD path just before a Tidy removal (Delete / RemoveDir); a throw
    /// there is a simulated power cut before that mutation.</summary>
    internal Action<string>? BeforeRemove;

    /// <summary>s_unlink of a file: an absent one is not an error (FR_NO_FILE is ignored by every caller). The card is
    /// re-verified first.</summary>
    public void Delete(string sdPath)
    {
        BeforeRemove?.Invoke(sdPath);
        Verify();
        try { File.Delete(Host(sdPath)); }
        catch (DirectoryNotFoundException) { }
    }

    /// <summary>s_unlink of a folder: only an empty one goes (FatFs denies a non-empty one: an IOException here); an
    /// absent one is not an error.</summary>
    public void RemoveDir(string sdPath)
    {
        BeforeRemove?.Invoke(sdPath);
        Verify();
        try { Directory.Delete(Host(sdPath), recursive: false); }
        catch (DirectoryNotFoundException) { }
    }

    /// <summary>The temporary file written durably and verified, then renamed to final -- never over an existing file.</summary>
    public void WriteVerifyRename(string tmpSdPath, string finalSdPath, byte[] data)
    {
        WriteDurable(tmpSdPath, data);
        Rename(tmpSdPath, finalSdPath);
    }

    /// <summary>om_sdw_replace (sdwrite.h): 0. path absent and "&lt;path&gt;.NEW" present: finish that replace;
    /// 1. make sure path exists without truncating it (FA_OPEN_ALWAYS); 2. write and verify .NEW (a failure removes it,
    /// path untouched); 3. remove path; 4. rename .NEW to path. Only "not found" counts as absent.</summary>
    public void WriteReplace(string sdPath, byte[] data)
    {
        string nw = sdPath + ".NEW";
        if (Absent(sdPath) && !Absent(nw)) Rename(nw, sdPath);
        using (new FileStream(Host(sdPath), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None)) { }
        try { WriteDurable(nw, data); }
        catch
        {
            // the cleanup never hides the write's own error
            try { File.Delete(Host(nw)); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            throw;
        }
        File.Delete(Host(sdPath));
        Rename(nw, sdPath);
    }
}

public sealed class ShortReadException(string path, long off, int want, int got)
    : IOException($"short read: {path} at {off}: {got} of {want} bytes");
