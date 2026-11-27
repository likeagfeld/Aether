// Aether Card Manager, GPL-3.0-or-later.
// Tidy saves (contract/SPEC.md §5.17) on the PC: a port of menu/launcher/tidy.c and of its one choke
// point menu/launcher/r1.c om_r1_drop (both BSD-3-Clause) at the commit manager/golden/README.md pins, checked against
// them compiled for the host (manager/golden, tidy_main.c; SaveTidyGoldenTests). tidy_dir, tidy_bkp, tidy_backups,
// rm_empty_gen, rm_if_empty, covered and drop keep their names (PascalCase), so the files can be read side by side.
//
// It removes only provable duplicates: in each save folder (SAVES/<game>, <game>/01..09, _GENERAL) every *.BUP of
// the folder and of its generations is grouped by CRC over the record's fields and data; every file of the current
// set is kept, a group without one keeps its copy in the newest generation, and each other member goes only when R1's
// strict check finds the kept copy byte for byte at that moment. Emptied generation folders (never the current one)
// and then empty shards go. A BRAM backup other than the newest goes when each of its records has a byte-identical
// kept copy. Never touched: the current sets, CART, _QUARANTINE, IMPORT.TXT, Kai's and Rings' folders, a file that
// does not decode, a name of 20 bytes or more, a folder whose files do not fit the tables. A JOURNAL.TXT (a save swap
// not finished) refuses the whole run. The preview makes the very same checks and removes nothing. A generation's
// sub-folder ALT/ (alternate imports, never loaded) is deduped the same way, but only against other
// ALT/ members: the current generation's copies are kept, an emptied ALT/ is removed before its
// generation.
//
// PC additions (none change what is removed): the confirmed card is re-verified before every removal (CardFs), and a
// cancel is honored before every removal and every folder; either stop leaves a subset done, as a power cut does, and
// the next run finishes it.
namespace VibeMenuCardManager.Core.Saves;

/// <summary>om_tidy_res.</summary>
public sealed class TidyResult
{
    public long Files;          // duplicate copies: found (preview) or removed (run)
    public long Bytes;          // their size
    public int Bups, Bkps;      // of them: save files, BRAM backup generations
    public int Folders;         // empty generation and shard folders removed (run)
    public int Slots;           // save folders examined
    public int Skipped;         // folders or files left alone (too many entries, too long a name)
}

/// <summary>OM_TIDY_BUSY: a save swap's JOURNAL.TXT is on the card; nothing was read or removed.</summary>
public sealed class TidyBusyException()
    : IOException("A save swap is unfinished -- launch a game first (the swap finishes), then tidy.");

public sealed class TidyCancelledException(TidyResult soFar) : OperationCanceledException("tidy cancelled")
{
    public TidyResult SoFar { get; } = soFar;
}

public sealed class TidyStoppedException(string message, TidyResult soFar, Exception inner) : Exception(message, inner)
{
    public TidyResult SoFar { get; } = soFar;
}

/// <summary>Mutation-demo hooks (tests only): each breaks one Tidy rule.</summary>
[Flags]
internal enum TidyMutation { None = 0, CurrentNotKept = 1, NewestBackupNotKept = 2, NoRecheck = 4 }

public static class SaveTidy
{
    const string SavesDir = "/aether/SAVES";
    const string Journal = "/aether/SAVES/JOURNAL.TXT";
    const string BakDir = "/aether/SAVES/_BACKUP";
    const int Hdr = BupFile.Hdr;
    const long Rec = Hdr + 32768 + 4;                 // OM_PGS_REC
    const int FName = 20;                             // OM_PGS_FNAME
    const int SDir = 32, NSDir = 512, NFp = 16, Fpw = 80, Bkw = 32;
    /// <summary>tidy.c's table sizes on the host build the golden runs (tidy_main.c prints them): one folder's files
    /// (OM_PGS_REC / sizeof(tent)) and the kept copies (the arena from lib + NSDIR x SDIR up to rec, / sizeof(kent)).</summary>
    public const int TCap = 912;
    public const int KCap = 2448;
    const int NamesCap = (int)(Rec / Bkw);            // a backup listing's names (in E->chk)

    sealed record TEnt(uint Gen, long Size, string Fn, bool Alt = false) { public uint Crc; public bool Skip, Keep; }
    sealed record KEnt(uint Crc, uint Gen, int Dir, string Fn, bool Alt = false);

    sealed class Ctx
    {
        public required CardFs Fs;
        public required TidyResult R;
        public bool Run;
        public Action<string>? Say;
        public CancellationToken Cancel;
        public TidyMutation M;
        public readonly List<string> Dirs = [];
        public readonly List<KEnt> Kept = [];
    }

    /// <summary>run false: the preview (nothing removed; its counts are what a run removes). Throws TidyBusyException,
    /// TidyCancelledException, or TidyStoppedException on an SD error (run it again).</summary>
    public static TidyResult Run(CardFs fs, bool run, Action<string>? progress = null, CancellationToken cancel = default) =>
        Run(fs, run, progress, cancel, TidyMutation.None);

    internal static TidyResult Run(CardFs fs, bool run, Action<string>? progress, CancellationToken cancel, TidyMutation m)
    {
        var r = new TidyResult();
        var c = new Ctx { Fs = fs, R = r, Run = run, Say = progress, Cancel = cancel, M = m };
        try
        {
            fs.Verify();
            if (JournalPending(fs)) throw new TidyBusyException();
            foreach (var e in fs.List(SavesDir)) OnSaves(c, e);
            for (int i = 0; i < c.Dirs.Count; i++) TidyDir(c, i);    // slot folders are appended as their game is seen
            TidyBackups(c);
        }
        catch (Exception e) when (e is (IOException or UnauthorizedAccessException) and not TidyBusyException)
        {
            throw new TidyStoppedException("TIDY STOPPED: " + e.Message, r, e);
        }
        return r;
    }

    /// <summary>om_tidy's journal check through om_sdw_read_file: JOURNAL.TXT, else JOURNAL.TXT.NEW; a non-empty one is
    /// a swap not finished. One too big to read is an error, as on the console.</summary>
    static bool JournalPending(CardFs fs)
    {
        foreach (var p in new[] { Journal, Journal + ".NEW" })
        {
            if (fs.Absent(p)) continue;
            long n = new FileInfo(fs.Host(p)).Length;
            if (n + 4 > Rec) throw new IOException($"{p}: {n} B is over the {Rec - 4} B the journal check reads");
            return n > 0;
        }
        return false;
    }

    static void Check(Ctx c)
    {
        if (c.Cancel.IsCancellationRequested) throw new TidyCancelledException(c.R);
    }

    static bool Ext(string n, char a, char b, char x) =>
        n.Length > 4 && n[^4] == '.' && (n[^3] | 32) == a && (n[^2] | 32) == b && (n[^1] | 32) == x;

    static int Bytes(string s) => System.Text.Encoding.UTF8.GetByteCount(s);

    /// <summary>fpath2: "/aether/&lt;dir&gt;[/Gsss/Gnnnnn][/ALT][/&lt;fn&gt;]" (alt: a generation's alternates, M6
    /// Task 24c).</summary>
    static string FPath2(string dir, uint gen, bool alt, string? fn)
    {
        string o = "/aether/" + dir;
        if (gen != 0) o = SaveGen.Dir(o, gen);
        if (alt) o += "/ALT";
        return fn != null ? o + "/" + fn : o;
    }

    /// <summary>fpath: "/aether/&lt;dir&gt;[/Gsss/Gnnnnn][/&lt;fn&gt;]".</summary>
    static string FPath(string dir, uint gen, string? fn) => FPath2(dir, gen, false, fn);

    static void OnSaves(Ctx c, CardFs.Entry e)
    {
        if (!e.IsDir || (e.Name[0] == '_' && e.Name != "_GENERAL")) return;   // _BACKUP, _QUARANTINE: not save folders
        if (e.NameBytes + 7 > SDir - 3 || c.Dirs.Count == NSDir) { c.R.Skipped++; return; }
        c.Dirs.Add("SAVES/" + e.Name);
    }

    static uint Crc(byte[] rec, uint ds)
    {
        var h = new System.IO.Hashing.Crc32();
        h.Append(rec.AsSpan(0x10, 0x20));
        h.Append(rec.AsSpan(Hdr, (int)ds));
        return h.GetCurrentHashAsUInt32();
    }

    // --- one save folder -------------------------------------------------------------------------------------------
    static void TidyDir(Ctx c, int di)
    {
        Check(c);
        string dir = c.Dirs[di];
        c.R.Slots++;
        c.Say?.Invoke("TIDY: " + dir[6..]);
        string p = FPath(dir, 0, null);
        uint curg = SaveGen.Cur(c.Fs, p).Gen;
        var t = new List<TEnt>();
        bool over = false;
        var sh = new bool[1000];
        int subs = 0;
        void OnFile(CardFs.Entry e, uint gen, bool alt = false)
        {
            if (!Ext(e.Name, 'b', 'u', 'p')) return;
            if (e.NameBytes >= FName) { c.R.Skipped++; return; }      // never ours: left alone
            if (t.Count == TCap) { over = true; return; }
            t.Add(new TEnt(gen, e.Size, e.Name, alt));
        }
        foreach (var e in c.Fs.List(p))                               // its own files, shards and slot folders
        {
            int v;
            if (!e.IsDir) { OnFile(e, 0); continue; }
            if ((v = SaveGen.Num(e.Name, 3)) >= 0) sh[v] = true;
            else if (e.Name.Length == 2 && e.Name[0] == '0' && e.Name[1] is >= '1' and <= '9') subs |= 1 << (e.Name[1] - '0');
        }
        if (!dir[6..].Contains('/'))                                  // a game folder: its slots 1-9 come later
            for (int k = 1; k <= 9; k++)
                if ((subs & (1 << k)) != 0)
                {
                    if (c.Dirs.Count == NSDir) { c.R.Skipped++; continue; }
                    c.Dirs.Add(dir + "/0" + (char)('0' + k));
                }
        for (int s = 0; s < 1000; s++)
        {
            if (!sh[s]) continue;
            var g = GenBits(c, SaveGen.Shard(p, (uint)s * 100), (uint)s);
            for (int k = 0; k < 100; k++)
            {
                if (!g[k]) continue;
                uint gen = (uint)(s * 100 + k);
                foreach (var e in c.Fs.List(FPath(dir, gen, null))) if (!e.IsDir) OnFile(e, gen);
                foreach (var e in c.Fs.List(FPath2(dir, gen, true, null))) if (!e.IsDir) OnFile(e, gen, alt: true);
            }
        }
        if (over) { c.R.Skipped++; return; }                          // cannot see it all: not touched
        foreach (var x in t)                                          // each file's record: its fields and data, as a CRC
        {
            string fp = FPath2(dir, x.Gen, x.Alt, x.Fn);
            byte[]? rec = ReadFile(c.Fs, fp);
            if (rec == null || BupFile.TryDecode(rec, (uint)rec.Length, out var d) != BupError.None) { x.Skip = true; continue; }
            x.Crc = Crc(rec, d!.DataSize);
            x.Keep = x.Gen == curg && (c.M & TidyMutation.CurrentNotKept) == 0;   // the current set (and its alternates): kept
        }
        for (int i = 0; i < t.Count; i++)                             // a group with no current copy keeps its newest
        {                                                              // (an alternates group only with alternates)
            if (t[i].Skip || t[i].Keep) continue;
            int best = i;
            for (int j = 0; j < t.Count; j++)
            {
                if (t[j].Skip || t[j].Crc != t[i].Crc || t[j].Alt != t[i].Alt) continue;
                if (t[j].Gen == curg && (c.M & TidyMutation.CurrentNotKept) == 0) { best = j; break; }
                if (t[j].Gen > t[best].Gen || (t[j].Gen == t[best].Gen && j < best)) best = j;
            }
            t[best].Keep = true;
        }
        for (int i = 0; i < t.Count; i++)                             // every other copy: removed through R1's check
        {
            if (t[i].Skip || t[i].Keep) continue;
            int j = t.FindIndex(y => y.Keep && !y.Skip && y.Crc == t[i].Crc && y.Alt == t[i].Alt);
            if (j < 0) continue;
            string kp = FPath2(dir, t[j].Gen, t[j].Alt, t[j].Fn), fp = FPath2(dir, t[i].Gen, t[i].Alt, t[i].Fn);
            if (Drop(c, fp, [kp], out long size)) { c.R.Files++; c.R.Bups++; c.R.Bytes += size; }
        }
        if (c.Run)                                                    // emptied generations, then empty shards
            for (int s = 0; s < 1000; s++)
            {
                if (!sh[s]) continue;
                string shp = SaveGen.Shard(p, (uint)s * 100);
                var g = GenBits(c, shp, (uint)s);
                for (int k = 0; k < 100; k++)
                {
                    if (!g[k] || s * 100 + k == curg) continue;
                    RmIfEmpty(c, FPath2(dir, (uint)(s * 100 + k), true, null));   // an emptied ALT/ (24c), then the generation
                    RmEmptyGen(c, FPath(dir, (uint)(s * 100 + k), null));
                }
                RmIfEmpty(c, shp);
            }
        foreach (var x in t)                                          // the kept copies, for the backups' check
        {
            if (x.Skip || !x.Keep) continue;
            if (c.Kept.Count == KCap) { c.R.Skipped++; break; }
            c.Kept.Add(new KEnt(x.Crc, x.Gen, di, x.Fn, x.Alt));
        }
    }

    /// <summary>A shard's generations (mode 1): folders "G" + 5 digits, n &gt; 0, n / 100 == s.</summary>
    static bool[] GenBits(Ctx c, string shard, uint s)
    {
        var g = new bool[100];
        foreach (var e in c.Fs.List(shard))
        {
            int v;
            if (e.IsDir && (v = SaveGen.Num(e.Name, 5)) > 0 && (uint)v / 100 == s) g[v % 100] = true;
        }
        return g;
    }

    /// <summary>om_sdw_read_file with cap OM_PGS_REC: null when the file is too big for it (OM_E_FULL).</summary>
    static byte[]? ReadFile(CardFs fs, string p)
    {
        long n = new FileInfo(fs.Host(p)).Length;
        if (n + 4 > Rec) return null;
        var b = fs.ReadAll(p);
        return b.Length + 4 > Rec ? null : b;
    }

    /// <summary>mode 3: what a folder holds -- record files, up to 8 other names (shorter than 20 bytes), or "too
    /// much" (other = 9: a folder in it, a ninth or a long name).</summary>
    static (int Bups, int Other, List<string> Names) Left(Ctx c, string p)
    {
        int bups = 0, other = 0;
        var names = new List<string>();
        foreach (var e in c.Fs.List(p))
        {
            if (e.IsDir) { other = 9; continue; }
            if (Ext(e.Name, 'b', 'u', 'p') || Ext(e.Name, 'b', 'k', 'p')) bups++;     // a record file: kept
            else if (other < 8 && e.NameBytes < FName) { names.Add(e.Name); other++; }
            else other = 9;
        }
        return (bups, other, names);
    }

    /// <summary>rm_empty_gen: a generation folder other than the current one, left without a record file: its
    /// DONE.TXT / .TMP files, then itself.</summary>
    static void RmEmptyGen(Ctx c, string gp)
    {
        var (bups, other, names) = Left(c, gp);
        if (bups > 0 || other > 8) return;
        foreach (var n in names) { Check(c); c.Fs.Delete(gp + "/" + n); }
        Check(c);
        c.Fs.RemoveDir(gp);
        c.R.Folders++;
    }

    static void RmIfEmpty(Ctx c, string p)
    {
        if (c.Fs.Absent(p)) return;                   // called speculatively on a generation's ALT/, too
        var (bups, other, _) = Left(c, p);
        if (bups > 0 || other > 0) return;
        Check(c);
        c.Fs.RemoveDir(p);
        c.R.Folders++;
    }

    // --- the BRAM backups ------------------------------------------------------------------------------------------
    /// <summary>om_pgs_bak_seq: the seq of "&lt;5 digits&gt;[-...].BKP|.TMP" (complete: .BKP); 0 = not one.</summary>
    public static uint BakSeq(string name, out bool complete)
    {
        var f = System.Text.Encoding.UTF8.GetBytes(name);
        uint v = 0;
        int k = 0, n = f.Length;
        complete = false;
        while (k < n && k < 5 && f[k] >= '0' && f[k] <= '9') v = v * 10 + (uint)(f[k++] - '0');
        if (k != 5 || v == 0 || n < 9) return 0;
        if (f[k] != '-' && f[k] != '.') return 0;
        int x = n - 4;
        if (f[x] != '.') return 0;
        if ((f[x + 1] | 32) == 'b' && (f[x + 2] | 32) == 'k' && (f[x + 3] | 32) == 'p') complete = true;
        else if (!((f[x + 1] | 32) == 't' && (f[x + 2] | 32) == 'm' && (f[x + 3] | 32) == 'p')) return 0;
        return v;
    }

    sealed class Bk
    {
        public readonly bool[] Sh = new bool[1000];
        public List<string>? Names;
        public uint Best;
        public string BestName = "";
        public int Ents;
    }

    static Bk ListBk(Ctx c, string p, bool names)
    {
        var b = new Bk { Names = names ? [] : null };
        foreach (var e in c.Fs.List(p))
        {
            b.Ents++;
            if (e.IsDir)
            {
                if (e.Name.Length == 3 && e.Name.All(ch => ch is >= '0' and <= '9')) b.Sh[int.Parse(e.Name, System.Globalization.CultureInfo.InvariantCulture)] = true;
                continue;
            }
            uint s;
            if (e.NameBytes >= Bkw || (s = BakSeq(e.Name, out bool complete)) == 0 || !complete) continue;
            if (s > b.Best) { b.Best = s; b.BestName = e.Name; }
            if (b.Names != null)
            {
                if (b.Names.Count == NamesCap) { c.R.Skipped++; continue; }
                b.Names.Add(e.Name);
            }
        }
        return b;
    }

    static string BkShard(int s) => BakDir + "/" + s.ToString("000", System.Globalization.CultureInfo.InvariantCulture);

    static void TidyBackups(Ctx c)
    {
        Check(c);
        c.Say?.Invoke("TIDY: BRAM BACKUPS");
        var root = ListBk(c, BakDir, names: true);
        string newest = "";
        for (int x = 999; x >= 0 && newest == ""; x--)               // the newest generation: never removed
        {
            if (!root.Sh[x]) continue;
            var sh = ListBk(c, BkShard(x), names: false);
            if (sh.Best != 0) newest = BkShard(x) + "/" + sh.BestName;
        }
        if (newest == "" && root.Best != 0) newest = BakDir + "/" + root.BestName;
        if ((c.M & TidyMutation.NewestBackupNotKept) != 0) newest = "";
        foreach (var n in root.Names!)                                // the flat generations from before Task 24b
        {
            string p = BakDir + "/" + n;
            if (p != newest) TidyBkp(c, p);
        }
        for (int s = 0; s < 1000; s++)
        {
            if (!root.Sh[s]) continue;
            string sp = BkShard(s);
            var sh = ListBk(c, sp, names: true);
            foreach (var n in sh.Names!)
            {
                string p = sp + "/" + n;
                if (p != newest) TidyBkp(c, p);
            }
            if (c.Run && ListBk(c, sp, names: false).Ents == 0)
            {
                Check(c);
                c.Fs.RemoveDir(sp);
                c.R.Folders++;
            }
        }
    }

    /// <summary>read_span / read_at: n bytes at off; null when the file is shorter (OM_E_SHORT).</summary>
    static byte[]? Span(CardFs fs, string p, long off, int n)
    {
        try { return fs.ReadAt(p, off, n); }
        catch (ShortReadException) { return null; }
    }

    /// <summary>tidy_bkp: one generation, removed when each record in it has a kept byte-identical copy (Drop re-checks
    /// each).</summary>
    static void TidyBkp(Ctx c, string p)
    {
        if (c.Fs.Absent(p)) return;
        long fsz = new FileInfo(c.Fs.Host(p)).Length;
        var h = Span(c.Fs, p, 0, 64);
        if (h == null || !h.AsSpan(0, 8).SequenceEqual("OMSATBKP"u8)) return;
        int cnt = (h[10] << 8) | h[11];
        long off = 64 + cnt * 32L;
        var fp = new List<string>();
        for (int k = 0; k < cnt; k++)
        {
            if (off + Hdr > fsz) return;
            var rh = Span(c.Fs, p, off, Hdr);
            if (rh == null || BupFile.TryDecode(rh, (uint)Math.Min(uint.MaxValue, fsz - off), out var d) != BupError.None) return;
            uint ds = d!.DataSize;
            if (Hdr + ds > Rec) return;
            var rec = new byte[Hdr + ds];
            rh.CopyTo(rec, 0);
            if (ds > 0)
            {
                var data = Span(c.Fs, p, off + Hdr, (int)ds);
                if (data == null) return;
                data.CopyTo(rec, Hdr);
            }
            uint crc = Crc(rec, ds);
            var kept = c.Kept.Find(x => x.Crc == crc);
            if (kept == null) return;                                 // a record with no kept copy: the generation stays
            string t = FPath2(c.Dirs[kept.Dir], kept.Gen, kept.Alt, kept.Fn);
            if (Bytes(t) >= Fpw) return;
            if (!fp.Contains(t))
            {
                if (fp.Count == NFp) return;
                fp.Add(t);
            }
            off += Hdr + ds + ((4 - (ds & 3)) & 3);
        }
        if (Drop(c, p, fp, out long size)) { c.R.Files++; c.R.Bkps++; c.R.Bytes += size; }
    }

    // --- r1.c, strict (Tidy is its one caller) ----------------------------------------------------------------------
    /// <summary>om_r1_drop: true when path (a .BUP or a .BKP) was removed -- or, in the preview, would be: each record
    /// it holds has a byte-identical intact copy (name, comment, language, date, size and data) in one of files, read
    /// now. size: the file's size (0 when absent).</summary>
    static bool Drop(Ctx c, string path, IReadOnlyList<string> files, out long size)
    {
        Check(c);
        size = 0;
        if (c.Fs.Absent(path)) return false;                          // gone: nothing to drop
        size = new FileInfo(c.Fs.Host(path)).Length;
        if ((c.M & TidyMutation.NoRecheck) != 0) { if (c.Run) c.Fs.Delete(path); return true; }
        if (Ext(path, 'b', 'u', 'p'))
        {
            if (size < Hdr) return false;
            var r = c.Fs.ReadAt(path, 0, Hdr);
            if (BupFile.TryDecode(r, (uint)Math.Min(uint.MaxValue, size), out _) != BupError.None) return false;   // not a record
            if (!Covered(c, r, path, Hdr, files)) return false;
        }
        else if (Ext(path, 'b', 'k', 'p'))                            // a generation: every record in it needs a copy
        {
            var h = Span(c.Fs, path, 0, 64);
            if (h == null || !h.AsSpan(0, 8).SequenceEqual("OMSATBKP"u8)) return false;
            int cnt = (h[10] << 8) | h[11];
            long off = 64 + cnt * 32L;
            for (int k = 0; k < cnt; k++)                             // each record's own .BUP image header, then its data
            {
                if (off + Hdr > size) return false;
                var r = Span(c.Fs, path, off, Hdr);
                if (r == null || BupFile.TryDecode(r, (uint)Math.Min(uint.MaxValue, size - off), out var d) != BupError.None) return false;
                if (!Covered(c, r, path, off + Hdr, files)) return false;
                off += Hdr + d!.DataSize + ((4 - (d.DataSize & 3)) & 3);
            }
        }
        else return false;                                            // not a record file: never through here
        if (!c.Run) return true;
        c.Fs.Delete(path);
        return true;
    }

    /// <summary>covered: the record (r: its 64-byte header; its data at self + doff) is held byte for byte by one of
    /// files.</summary>
    static bool Covered(Ctx c, byte[] r, string self, long doff, IReadOnlyList<string> files)
    {
        uint ds = Be.Get32(r, 0x2C);
        foreach (var p in files)
        {
            if (p == self || !Ext(p, 'b', 'u', 'p')) continue;
            if (c.Fs.Absent(p)) continue;
            long size = new FileInfo(c.Fs.Host(p)).Length;
            if (size < Hdr) continue;
            var h = Span(c.Fs, p, 0, Hdr);
            if (h == null) continue;
            if (BupFile.TryDecode(h, (uint)Math.Min(uint.MaxValue, size), out var d) != BupError.None) continue;   // not intact
            if (!h.AsSpan(0x10, 11).SequenceEqual(r.AsSpan(0x10, 11)) || !h.AsSpan(0x1C, 11).SequenceEqual(r.AsSpan(0x1C, 11)) ||
                h[0x27] != r[0x27] || d!.Date != Be.Get32(r, 0x28) || d.DataSize != ds)
                continue;
            if (SameBytes(c.Fs, self, doff, p, Hdr, ds)) return true;
        }
        return false;
    }

    static bool SameBytes(CardFs fs, string pa, long oa, string pb, long ob, uint len)
    {
        for (long x = 0; x < len; x += 2048)
        {
            int k = (int)Math.Min(2048, len - x);
            if (!fs.ReadAt(pa, oa + x, k).AsSpan().SequenceEqual(fs.ReadAt(pb, ob + x, k))) return false;
        }
        return true;
    }
}
