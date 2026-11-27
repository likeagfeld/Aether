// Aether Card Manager, GPL-3.0-or-later.
// The Kai/Rings save import of contract/SPEC.md §5.17 "Import". A port of menu/launcher/pgsbak.c
// (BSD-3-Clause) at the commit manager/golden/README.md pins (M6 Task 24b: into the current set, savegen.h; fix round
// 3: sticky destinations; named skips, Rings [SLOT10]-[SLOT19] placement, Kai's SAVES/LATEST into
// _GENERAL) -- import_all, import_product, first_of, merge_slot, merge_class, fold_scan, put_new, same_rec, place_high,
// hs_match, match_set, skip_line, skip_file, skip_tree, gimp_note, genlist_add keep their names (PascalCase) so the two
// can be read side by side -- and checked against that file compiled for the host (manager/golden, SaveImporterGoldenTests).
// The PC port never implements the picker (IMPORT.SEL/om_pgs_import_sel): it always imports every game, as
// om_pgs_import does with no picker ("picked" always true with no selection list).
// Sources: Pseudo Saturn Kai's /SAVES/BRAM/<folder>/ and <folder>/0n/ (cafe-alpha, pseudo_bram.c:674-684, GPL-2.0) and
// Satiator Rings' /satiator-saves/<id>/ and /satiator-saves/[SLOTnn]/<id>/ (retrohead, save_functions.c
// getPathForGameId). The originals are never written, renamed or deleted.
namespace VibeMenuCardManager.Core.Saves;

public static class SaveImporter
{
    public const string KaiRoot = "/SAVES/BRAM";
    public const string KaiLatest = "/SAVES/LATEST";          // OM_PGS_KAI_LATEST: Kai's saves in no BRAM/<game> folder
    public const string RingsRoot = "/satiator-saves";
    public const string Saves = "/aether/SAVES";
    public const string ImportTxt = "/aether/SAVES/IMPORT.TXT";
    public const string General = "SAVES/_GENERAL";           // OM_PGS_GENERAL
    const string GenlistPath = "/aether/SAVES/GENLIST.TXT"; // OM_PGS_GENLIST
    const string QuarImport = "/aether/SAVES/_QUARANTINE/IMPORT/";

    // pgsrun.h / pgsplan.h / pgsbak.c limits
    const long Rec = BupFile.Hdr + 32768 + 4;        // OM_PGS_REC: one .BUP of a whole 32 KB record + read slack
    const int Ents = 256;                            // OM_PGS_ENTS: a source slot's .BUP files
    const int EName = 96;                            // OM_PGS_ENAME
    const int FName = 20;                            // OM_PGS_FNAME: "<11>~9.BUP" + NUL
    const int NameW = 24;                             // NAMEW: a folder name in the name lists
    const int DMax = 256;                            // DMAX: a destination or quarantine folder's files
    const int SrcMax = 12;                           // SRC_MAX
    const int NKa = 384, NRb = 192, NRc = 192, NVv = 256;
    const string Cart = "CART";                      // OM_PGS_CART (pgsbak.h)
    const int SkipCap = 4096;                        // sizeof s_skip
    const int BlCap = 48;                             // bl_t's n[48]
    const int GimpMax = 128;                          // GIMP_MAX
    static readonly int[] HighOrder = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0];   // place_high's order[10]
    const int HsSrcInDir = 1, HsDirInSrc = 2;

    sealed class Names { public readonly List<string> V = []; public int Cap; public int Over; }

    sealed class Src { public required string Path; public bool Kai; }

    sealed class Ent
    {
        public required string Name;
        public required int Tag;                     // 1 + source index
        public long Size;
        public byte[] Hdr = new byte[BupFile.Hdr];
        public int Win;                              // 0 dropped, 1 for the internal memory, 2 cart-only
    }

    sealed class Fold
    {
        public readonly List<string> File = [];      // "" = a name of FName bytes or more (never ours)
        public readonly List<byte[]?> Rec = [];      // the record name (12 bytes, NUL-padded), null = unknown
        public bool Scanned, Over;
    }

    sealed class Ctx
    {
        public required CardFs Fs;
        public required ImportResult R;
        public Action<string>? Say;
        public Names Ka = new() { Cap = NKa }, Rb = new() { Cap = NRb }, Rc = new() { Cap = NRc }, Vv = new() { Cap = NVv };
        public Fold Dst = new(), Q = new(), Alt = new();
        public bool ReversePrefer;                    // mutation-demo hook (tests only)
        public CancellationToken Cancel;
        // every save file left out is counted and named ("skipped <why> <path>" lines for IMPORT.TXT)
        public readonly List<byte> SkipBuf = [];
        public int SkipMore;
        // fix I9: the versions imported into _GENERAL (Kai's LATEST), never loaded (GENLIST.TXT's "I" line)
        public readonly List<uint> Gimp = [];
        public bool GenImp;
    }

    /// <summary>Runs the import on a card (a directory root). The card must be an installed Aether card (/aether
    /// exists). Writes IMPORT.TXT last. Throws ImportStoppedException when it stops part-way; run it again.</summary>
    /// <param name="cancel">checked between files (before each record is copied): a cancelled import throws
    /// ImportCancelledException with the counts so far. Every file already on the card is whole (each is written as
    /// ".IMP" and renamed), and IMPORT.TXT is not written; the import is idempotent, so running it again finishes the job.</param>
    public static ImportResult Run(CardFs fs, Action<string>? progress = null, CancellationToken cancel = default) =>
        Run(fs, progress, reversePrefer: false, cancel);

    /// <summary>reversePrefer: the mutation demo's broken importer (the duplicate rule turned around).</summary>
    internal static ImportResult Run(CardFs fs, Action<string>? progress, bool reversePrefer, CancellationToken cancel = default)
    {
        var r = new ImportResult();
        var c = new Ctx { Fs = fs, R = r, Say = progress, ReversePrefer = reversePrefer, Cancel = cancel };
        try
        {
            if (fs.Absent("/aether") || (File.GetAttributes(fs.Host("/aether")) & FileAttributes.Directory) == 0)
                throw new ImportStoppedException("not an Aether card: /aether is missing (install Aether first)", r);
            fs.Verify();                                  // at entry: the card the user confirmed
            ImportAll(c);
            fs.Verify();                                  // and before IMPORT.TXT
            fs.MkDir(Saves);
            fs.WriteReplace(ImportTxt, ImportTxtBytes(c));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)   // a read-only card or locked file too
        {
            throw new ImportStoppedException("IMPORT STOPPED: " + e.Message, r, e);
        }
        return r;
    }

    /// <summary>om_pgs_import's write: line 1 (the counts), then every skipped save named, then "skipped-more N" when
    /// the 4,096-byte buffer overflowed.</summary>
    static byte[] ImportTxtBytes(Ctx c)
    {
        var o = new List<byte>(System.Text.Encoding.ASCII.GetBytes(c.R.ImportTxtLine()));
        o.AddRange(c.SkipBuf);
        if (c.SkipMore > 0) o.AddRange(System.Text.Encoding.ASCII.GetBytes("skipped-more " + c.SkipMore.ToString(Inv) + "\n"));
        return o.ToArray();
    }

    static void AddDir(Names l, CardFs.Entry e)
    {
        if (!e.IsDir) return;
        if (e.NameBytes >= NameW || l.V.Count >= l.Cap) { l.Over++; return; }
        l.V.Add(e.Name);
    }

    static string ProductOf(string f)
    {
        string? o = ImportFolders.VibeFolder(f);
        if (o == null) return "";
        int b = o.IndexOf('[');
        return b >= 0 ? o[..b] : o;
    }

    // --- M6 Task 24c: every skipped save file left out, counted and named -------------------------------------------

    /// <summary>skip_line: "skipped &lt;why&gt; &lt;path&gt;[ x&lt;n&gt;]\n", in a 4,096-byte buffer; past it,
    /// "skipped-more" only. Always counts (RI-&gt;skipped += n), whether or not it is named.</summary>
    static void SkipLine(Ctx c, string why, string path, int n)
    {
        c.R.Skipped += n;
        int a = System.Text.Encoding.UTF8.GetByteCount(why), b = System.Text.Encoding.UTF8.GetByteCount(path);
        if (c.SkipBuf.Count + a + b + 24 >= SkipCap) { c.SkipMore += n; return; }
        string line = "skipped " + why + " " + path + (n != 1 ? " x" + n.ToString(Inv) : "") + "\n";
        c.SkipBuf.AddRange(System.Text.Encoding.UTF8.GetBytes(line));
    }

    static void SkipFile(Ctx c, string path, string why) => SkipLine(c, why, path, 1);

    /// <summary>skip_tree: every "*.BUP" of a folder (and, one level deep only, of its sub-folders) left out: each
    /// counted and named.</summary>
    static void SkipTree(Ctx c, string dir, string why, bool deep)
    {
        var subs = new List<string>();
        int more = 0;
        foreach (var e in c.Fs.List(dir))
        {
            if (e.IsDir)
            {
                if (e.NameBytes < NameW && subs.Count < 16) subs.Add(e.Name);
                else more++;
                continue;
            }
            if (!IsBup(e.Name)) continue;
            SkipFile(c, Jp(dir, e.Name), why);
        }
        if (more > 0) SkipLine(c, "too-many-folders", dir, more);
        if (deep) foreach (var s in subs) SkipTree(c, Jp(dir, s), why, false);
    }

    static void ImportAll(Ctx c)
    {
        var r = c.R;
        c.Say?.Invoke("SAVES: READING KAI/RINGS FOLDERS");
        foreach (var e in c.Fs.List(KaiRoot)) AddDir(c.Ka, e);
        var ka = new List<string>();                  // Kai writes its folders sanitized: anything else is not one
        foreach (var n in c.Ka.V)
            if (ImportFolders.VibeFolder(n) is string f && f == n) ka.Add(n);
            else SkipTree(c, Jp(KaiRoot, n), n[0] == '_' ? "general-set-backup" : "not-a-game-folder", true);
        c.Ka.V.Clear(); c.Ka.V.AddRange(ka);

        foreach (var e in c.Fs.List(RingsRoot)) AddDir(c.Rb, e);
        var rb = new List<string>();
        foreach (var n in c.Rb.V)
        {
            int slot = ImportFolders.RingsSlot(n);
            if (slot != 0)
            {
                if (slot <= 19)                        // its game folders, listed now; 24c: 10..19 too (place_high)
                {
                    var t = new Names { Cap = NVv };     // its game folders, listed now
                    string sp = Jp(RingsRoot, n);
                    foreach (var e in c.Fs.List(sp)) AddDir(t, e);
                    if (t.Over > 0) SkipLine(c, "too-many-folders", sp, t.Over);
                    foreach (var g in t.V)
                    {
                        string? f = ImportFolders.VibeFolder(g);
                        if (f == null || f.Contains('[') || c.Rc.V.Count >= c.Rc.Cap ||
                            System.Text.Encoding.UTF8.GetByteCount(g) > NameW - 2)
                        { SkipTree(c, Jp(sp, g), "not-a-game-folder", true); continue; }
                        c.Rc.V.Add((char)('0' + slot) + g);
                    }
                }
                else SkipTree(c, Jp(RingsRoot, n), "no-such-slot", true);   // Rings has 20 slots, Aether 10
                continue;
            }
            string? ff = ImportFolders.VibeFolder(n);
            if (ff == null || ff.Contains('['))
            { SkipTree(c, Jp(RingsRoot, n), n[0] == '_' ? "general-set-backup" : "not-a-game-folder", true); continue; }
            rb.Add(n);
        }
        c.Rb.V.Clear(); c.Rb.V.AddRange(rb);

        foreach (var e in c.Fs.List(Saves)) AddDir(c.Vv, e);
        if (c.Ka.Over > 0) SkipLine(c, "too-many-folders", KaiRoot, c.Ka.Over);
        if (c.Rb.Over + c.Rc.Over > 0) SkipLine(c, "too-many-folders", RingsRoot, c.Rb.Over + c.Rc.Over);
        // c.Vv.Over is never counted: an unseen Aether folder changes nothing already imported into it, never loses one

        for (int i = 0; i < c.Ka.V.Count; i++) { var d = ProductOf(c.Ka.V[i]); if (d != "" && FirstOf(c, d, 0, i)) ImportProduct(c, d); }
        for (int i = 0; i < c.Rb.V.Count; i++) { var d = ProductOf(c.Rb.V[i]); if (d != "" && FirstOf(c, d, 1, i)) ImportProduct(c, d); }
        for (int i = 0; i < c.Rc.V.Count; i++) { var d = ProductOf(c.Rc.V[i][1..]); if (d != "" && FirstOf(c, d, 2, i)) ImportProduct(c, d); }

        {   // Kai's SAVES/LATEST -- saves in no BRAM/<game> folder -- copy-only into _GENERAL (no
            // game id; R1-safe). SAVES/2025 and the other SAVES/ folders (raw BRAM dumps) are never read.
            c.Say?.Invoke("SAVES: KAI'S LATEST SAVES");
            c.Gimp.Clear();
            c.GenImp = true;
            MergeSlot(c, General, 0, [new Src { Path = KaiLatest, Kai = true }]);
            c.GenImp = false;
            if (c.Gimp.Count > 0) GenlistAdd(c);
        }
    }

    /// <summary>true when no earlier source entry (KA, then RB, then RC) names product p.</summary>
    static bool FirstOf(Ctx c, string p, int listI, int idx)
    {
        for (int i = 0; i < c.Ka.V.Count && (listI > 0 || i < idx); i++) if (ProductOf(c.Ka.V[i]) == p) return false;
        if (listI == 0) return true;
        for (int i = 0; i < c.Rb.V.Count && (listI > 1 || i < idx); i++) if (ProductOf(c.Rb.V[i]) == p) return false;
        if (listI == 1) return true;
        for (int i = 0; i < c.Rc.V.Count && i < idx; i++) if (ProductOf(c.Rc.V[i][1..]) == p) return false;
        return true;
    }

    static string SlotSub(string p, int slot) => slot > 0 ? p + "/0" + slot.ToString(Inv) : p;

    /// <summary>import_product1. A folder Aether already uses for this product is sticky. A
    /// Kai region folder D goes to SAVES/D when Aether has D; else, when Aether reads its product-only SAVES/&lt;P&gt;
    /// (it has P and not D), into P -- never creating D, which would change the launcher's pick (pgsplan.h want_po);
    /// else to a new SAVES/D. Product-only sources (Rings' id folder, Kai's &lt;P&gt;) go into every folder a region of
    /// the game reads. Rings' [SLOT10]-[SLOT19] (place_high) too, once per destination.</summary>
    static void ImportProduct(Ctx c, string P)
    {
        var dl = new List<(string Name, List<int> Kr)>();   // the destinations, each with its Kai region sources (<= 8)
        int kaiPo = -1, ringsPo = -1;
        int rcmask = 0;                               // Rings slots 1..19 of this product
        bool vvPo = c.Vv.V.Contains(P);               // Aether's own product-only folder
        foreach (var v in c.Vv.V)                     // Aether's own region folders: each one a destination
        {
            if (ProductOf(v) != P || !v.Contains('[') || ImportFolders.VibeFolder(v) != v) continue;
            if (dl.Count < 64) dl.Add((v, []));
        }
        if (vvPo && dl.Count < 64) dl.Add((P, []));
        for (int i = 0; i < c.Ka.V.Count; i++)        // Kai: the region folders and the product-only one
        {
            string? f = ImportFolders.VibeFolder(c.Ka.V[i]);
            if (f == null) continue;
            if (f == P) { kaiPo = i; continue; }
            if (ProductOf(f) != P) continue;
            int k = dl.FindIndex(x => x.Name == c.Ka.V[i]);
            string to = k >= 0 ? c.Ka.V[i] : vvPo ? P : c.Ka.V[i];   // Aether's D; its P; or a new D
            k = dl.FindIndex(x => x.Name == to);
            if (k < 0) { if (dl.Count == 64) continue; dl.Add((to, [])); k = dl.Count - 1; }
            if (dl[k].Kr.Count < 8) dl[k].Kr.Add(i);
        }
        for (int i = 0; i < c.Rb.V.Count; i++) if (ProductOf(c.Rb.V[i]) == P && !c.Rb.V[i].Contains('[')) ringsPo = i;
        foreach (var e in c.Rc.V) if (ProductOf(e[1..]) == P) rcmask |= 1 << (e[0] - '0');
        if (dl.Count == 0 && kaiPo < 0 && ringsPo < 0 && rcmask == 0) return;
        if (dl.Count == 0) dl.Add((P, []));           // nothing yet: the product folder

        foreach (var (name, kr) in dl)                // every folder Aether reads for this product
        {
            if (kr.Count == 0 && kaiPo < 0 && ringsPo < 0 && rcmask == 0) continue;   // Aether's alone
            string dest = "SAVES/" + name;
            int subs = 0;
            c.Say?.Invoke("IMPORT: " + dest[6..]);
            if (!c.R.Destinations.Contains(dest)) c.R.Destinations.Add(dest);
            for (int slot = 0; slot <= 9; slot++)
            {
                if (slot > 0 && ((subs | rcmask) & (1 << slot)) == 0) continue;
                var src = new List<Src>();
                foreach (int j in kr)
                {
                    if (src.Count >= SrcMax) break;
                    src.Add(new Src { Path = SlotSub(KaiRoot + "/" + c.Ka.V[j], slot), Kai = true });
                }
                if (kaiPo >= 0 && src.Count < SrcMax) src.Add(new Src { Path = SlotSub(KaiRoot + "/" + c.Ka.V[kaiPo], slot), Kai = true });
                if (slot == 0 && ringsPo >= 0 && src.Count < SrcMax) src.Add(new Src { Path = RingsRoot + "/" + c.Rb.V[ringsPo], Kai = false });
                if (slot > 0 && (rcmask & (1 << slot)) != 0)
                    foreach (var e in c.Rc.V)
                    {
                        if (src.Count >= SrcMax) break;
                        if (e[0] - '0' != slot || ProductOf(e[1..]) != P) continue;
                        src.Add(new Src { Path = RingsRoot + "/[SLOT0" + slot.ToString(Inv) + "]/" + e[1..], Kai = false });
                    }
                if (src.Count == 0) continue;
                int got = MergeSlot(c, dest, slot, src);
                if (slot == 0) subs = got;
            }
            // Rings [SLOT10]-[SLOT19], in slot order, then in list order within a slot
            for (int hs = 10; hs <= 19; hs++)
                foreach (var e in c.Rc.V)
                {
                    if (e[0] - '0' != hs || ProductOf(e[1..]) != P) continue;
                    var h = new Src { Path = RingsRoot + "/[SLOT" + hs.ToString(Inv) + "]/" + e[1..], Kai = false };
                    PlaceHigh(c, dest, h);
                }
        }
    }

    static bool IsBup(string n) =>
        n.Length > 4 && n[^4] == '.' && (n[^3] | 32) == 'b' && (n[^2] | 32) == 'u' && (n[^1] | 32) == 'p';

    static string Jp(string a, string b) => a + "/" + b;

    /// <summary>same_rec: the same record version -- the same date and datasize and byte-identical data.</summary>
    static bool SameRec(Ctx c, string pa, byte[] ha, string pb, byte[] hb)
    {
        uint ds = Be.Get32(ha, 0x2C);
        if (Be.Get32(ha, 0x28) != Be.Get32(hb, 0x28) || ds != Be.Get32(hb, 0x2C)) return false;
        for (long x = 0; x < ds; x += 2048)
        {
            int k = (int)Math.Min(2048, ds - x);
            var a = c.Fs.ReadAt(pa, BupFile.Hdr + x, k);
            var b = c.Fs.ReadAt(pb, BupFile.Hdr + x, k);
            if (!a.AsSpan().SequenceEqual(b)) return false;
        }
        return true;
    }

    /// <summary>fold_scan: a destination or quarantine folder's .BUP names and the records they hold.</summary>
    static void FoldScan(Ctx c, Fold f, string abs)
    {
        f.File.Clear(); f.Rec.Clear();
        f.Scanned = true; f.Over = false;
        foreach (var e in c.Fs.List(abs))
        {
            if (e.IsDir || !IsBup(e.Name)) continue;
            if (f.File.Count >= DMax) { f.Over = true; continue; }
            f.File.Add(e.NameBytes >= FName ? "" : e.Name);
            f.Rec.Add(null);
        }
        for (int i = 0; i < f.File.Count; i++)
        {
            if (f.File[i] == "") continue;
            byte[] h;
            try { h = c.Fs.ReadAt(Jp(abs, f.File[i]), 0, BupFile.Hdr); }
            catch (ShortReadException) { continue; }
            if (BupFile.TryDecode(h, unchecked((uint)BupFile.Hdr + Be.Get32(h, 0x2C)), out var d) == BupError.None)
            {
                var nm = new byte[12];
                d!.Name.CopyTo(nm, 0);
                f.Rec[i] = nm;
            }
        }
    }

    static int FoldFind(Fold f, byte[] nm, int from = 0)
    {
        for (int k = from; k < f.File.Count; k++) if (f.Rec[k] is byte[] r && r.AsSpan().SequenceEqual(nm)) return k;
        return -1;
    }

    /// <summary>put_new: data as a new file of folder abs (made on demand) under its record name, ".IMP" first.</summary>
    static void PutNew(Ctx c, Fold f, string abs, byte[] nm, byte[] data)
    {
        if (c.Cancel.IsCancellationRequested) throw new ImportCancelledException(c.R);      // between files, never inside one
        c.Fs.Verify();                                    // before the file's folders and data reach the card
        MkDirs(c, abs);
        string fname = SaveNames.FileName(nm, f.File) ?? throw new IOException($"no free file name in {abs}");
        string p = Jp(abs, fname);
        c.Fs.WriteVerifyRename(p + ".IMP", p, data);
        if (f.File.Count < DMax) { f.File.Add(fname); f.Rec.Add(nm); }
    }

    static void MkDirs(Ctx c, string abs)
    {
        for (int i = Card.CardPaths.Root.Length + 1; i <= abs.Length; i++)   // past "/aether/"
            if (i == abs.Length || abs[i] == '/') c.Fs.MkDir(abs[..i]);
    }

    /// <summary>merge_slot: SAVES/&lt;dest&gt;[/0n] from every source; returns the Kai slot folders 01..09 seen (a bit
    /// mask), for slot 0. alt: 0 normal; 1 every placement into the current set's ALT/ (place_high, every slot full);
    /// 2 the normal destination, but a conflict goes to that set's ALT/ instead of being dropped (place_high, a sticky
    /// or newly-claimed slot).</summary>
    static int MergeSlot(Ctx c, string dest, int slot, List<Src> src, int alt = 0)
    {
        var r = c.R;
        var ents = new List<Ent>();
        int subs = 0, over = 0;
        for (int i = 0; i < src.Count; i++)
            foreach (var e in c.Fs.List(src[i].Path))
            {
                if (e.IsDir)
                {
                    if (e.Name.Length == 2 && e.Name[0] == '0' && e.Name[1] is >= '1' and <= '9') subs |= 1 << (e.Name[1] - '0');
                    continue;
                }
                if (!IsBup(e.Name)) continue;
                if (e.NameBytes >= EName || ents.Count >= Ents) { over++; continue; }
                ents.Add(new Ent { Name = e.Name, Tag = 1 + i, Size = e.Size });
            }
        if (over > 0) SkipLine(c, "too-many-files", src[0].Path, over);
        if (ents.Count == 0) return subs;
        foreach (var e in ents)                        // decode; reserved and undecodable drop out
        {
            string p = Jp(src[e.Tag - 1].Path, e.Name);
            byte[] h;
            try { h = c.Fs.ReadAt(p, 0, BupFile.Hdr); }
            catch (ShortReadException) { SkipFile(c, p, "not-a-save"); continue; }
            if (BupFile.TryDecode(h, (uint)e.Size, out var d) != BupError.None) { SkipFile(c, p, "not-a-save"); continue; }
            if (SaveNames.Reserved(PadName(d!.Name))) { SkipFile(c, p, "reserved"); continue; }
            e.Hdr = h;
            // a save over 32 KB (a cartridge or floppy save) cannot go into the internal
            // memory: class 2, copied cart-only into the folder's CART sub-folder, which no load reads
            e.Win = e.Size + 4 > Rec ? 2 : 1;
        }
        string dst = "/aether/" + dest, qd = QuarImport + dest[6..];
        if (slot > 0) { dst += "/0" + slot.ToString(Inv); qd += "/0" + slot.ToString(Inv); }
        // into the folder's current set (SaveGen, savegen.h), so the launcher loads what was imported; new
        // names only -- nothing on the card is written over. CART and the quarantine stay at the folder itself.
        string cur = SaveGen.Cur(c.Fs, dst).Path;
        string confAlt = cur + "/ALT";
        string mergeDst = alt == 1 ? cur + "/ALT" : cur;      // kept as alternates (never loaded)
        c.Alt.Scanned = false;
        MergeClass(c, 1, mergeDst, qd, src, ents, alt, confAlt);
        MergeClass(c, 2, dst + "/" + Cart, qd + "/" + Cart, src, ents, 0, "");
        return subs;
    }

    static readonly System.Globalization.CultureInfo Inv = System.Globalization.CultureInfo.InvariantCulture;

    /// <summary>merge_class: one class of merge_slot's decoded entries (1: for the internal memory, 2: cart-only)
    /// into dst, the losers into qd.</summary>
    static void MergeClass(Ctx c, int cls, string dst, string qd, List<Src> src, List<Ent> ents, int alt, string confAlt)
    {
        var r = c.R;
        c.Dst.Scanned = c.Q.Scanned = false;
        for (int i = 0; i < ents.Count; i++)          // each record once: the group of i, if i leads it
        {
            if (c.Cancel.IsCancellationRequested) throw new ImportCancelledException(r);
            if (ents[i].Win != cls) continue;
            int k;
            for (k = 0; k < i; k++) if (ents[k].Win == cls && Same11(ents[k], ents[i])) break;
            if (k < i) continue;                      // not the group's first member
            int best = i;
            for (k = i + 1; k < ents.Count; k++)
                if (ents[k].Win == cls && Same11(ents[k], ents[i]) &&
                    BupFile.Prefer(ents[k].Hdr, ents[k].Name, ents[best].Hdr, ents[best].Name) != c.ReversePrefer)
                    best = k;
            var w = ents[best];
            var nm = new byte[12];                    // the 11 bytes at 0x10 + NUL, normalized
            w.Hdr.AsSpan(0x10, 11).CopyTo(nm);
            string p = Jp(src[w.Tag - 1].Path, w.Name);
            if (!BupFile.NameNorm(nm, nm)) { SkipFile(c, p, "bad-name"); continue; }
            long wlen = BupFile.Hdr + (long)Be.Get32(w.Hdr, 0x2C);
            for (k = i; k < ents.Count; k++)          // the losers: quarantined when they differ from the winner
            {
                var l = ents[k];
                if (k == best || l.Win != cls || !Same11(l, w)) continue;
                long llen = BupFile.Hdr + (long)Be.Get32(l.Hdr, 0x2C);
                string q = Jp(src[l.Tag - 1].Path, l.Name);
                if (SameRec(c, p, w.Hdr, q, l.Hdr)) continue;
                if (!c.Q.Scanned) FoldScan(c, c.Q, qd);
                if (c.Q.Over) { SkipFile(c, q, "quarantine-full"); continue; }       // the loser stays where it is (its source)
                bool same = false;
                for (int qi = FoldFind(c.Q, nm); qi >= 0 && !same; qi = FoldFind(c.Q, nm, qi + 1))
                {
                    string r2 = Jp(qd, c.Q.File[qi]);
                    same = SameRec(c, q, l.Hdr, r2, c.Fs.ReadAt(r2, 0, BupFile.Hdr));
                }
                if (same) continue;
                PutNew(c, c.Q, qd, nm, cls == 2 ? ReadBig(c, q, llen) : ReadWhole(c, q));
                r.Older++;
            }
            if (!c.Dst.Scanned) FoldScan(c, c.Dst, dst);       // the winner
            if (c.Dst.Over) { SkipFile(c, p, "destination-full"); continue; }       // over 256 saves: not touched
            {
                bool same = false, any = false;
                for (int qi = FoldFind(c.Dst, nm); qi >= 0 && !same; qi = FoldFind(c.Dst, nm, qi + 1))
                {
                    any = true;
                    string r2 = Jp(dst, c.Dst.File[qi]);
                    same = SameRec(c, p, w.Hdr, r2, c.Fs.ReadAt(r2, 0, BupFile.Hdr));
                }
                if (same) { r.Present++; GimpNote(c, p); continue; }
                if (any && alt == 2)              // fix I8: a high slot's conflicting save -> ALT/, never dropped
                {
                    if (!c.Alt.Scanned) FoldScan(c, c.Alt, confAlt);
                    bool inAlt = false;
                    for (int qi2 = FoldFind(c.Alt, nm); qi2 >= 0 && !inAlt; qi2 = FoldFind(c.Alt, nm, qi2 + 1))
                    {
                        string r2 = Jp(confAlt, c.Alt.File[qi2]);
                        inAlt = SameRec(c, p, w.Hdr, r2, c.Fs.ReadAt(r2, 0, BupFile.Hdr));
                    }
                    if (inAlt) { r.Present++; continue; }
                    PutNew(c, c.Alt, confAlt, nm, ReadWhole(c, p));
                    r.Alternates++;
                    continue;
                }
                if (any && alt != 1) { r.Conflicts++; continue; }       // never over an Aether save
            }
            if (cls == 2)                             // cart-only: copied in pieces, whatever its size
            {
                PutNew(c, c.Dst, dst, nm, ReadBig(c, p, wlen));
                r.Cart++;
                continue;
            }
            PutNew(c, c.Dst, dst, nm, ReadWhole(c, p));
            GimpNote(c, p);
            if (alt == 1) r.Alternates++;
            else if (src[w.Tag - 1].Kai) r.Kai++; else r.Rings++;
            if (((r.Kai + r.Rings) & 7) == 0) c.Say?.Invoke("SAVES: COPYING KAI/RINGS SAVES " + (r.Kai + r.Rings).ToString(Inv));
        }
    }

    // --- M6 Task 24c: Rings [SLOT10]-[SLOT19] placement --------------------------------------------------------------
    // a Rings [SLOT10]..[SLOT19] folder of a game (Aether has 10 slots) goes into the game's slot that already holds
    // one of its saves byte for byte (a run again, a resume after a cut); else into the lowest empty slot, 1..9, then
    // 0; else, every slot full, into slot 0's current set's ALT/ as alternates (never loaded, never lost). Inside the
    // slot the usual merge rules apply. Hardware 2026-11-25 (Policenauts, Panzer Dragoon Saga: their only saves in
    // Rings' [SLOT19]): a game with no saves in any of its 10 slots gets the high slot in slot 0, the slot a launch
    // uses by default.

    /// <summary>bl_ent's listing cap: up to 48 ".bup" names; a name of FName bytes or more is "" (never ours); once 48
    /// are seen, every further file overwrites slot 47 with "" (the console's own cap behavior, ported literally).</summary>
    static List<string> BlList(Ctx c, string dir)
    {
        var n = new List<string>();
        foreach (var e in c.Fs.List(dir))
        {
            if (e.IsDir || !IsBup(e.Name)) continue;
            if (n.Count < BlCap) n.Add(e.NameBytes < FName ? e.Name : "");
            else n[BlCap - 1] = "";
        }
        return n;
    }

    static byte[]? ReadHeadOrNull(Ctx c, string p)
    {
        try { return c.Fs.ReadAt(p, 0, BupFile.Hdr); }
        catch (IOException) { return null; }
    }

    /// <summary>match_set: 1 when every file of x has a same-version copy in y. An unreadable or
    /// over-length name in x fails the whole match; one in y is just skipped.</summary>
    static bool MatchSet(Ctx c, List<string> x, string xd, List<string> y, string yd)
    {
        foreach (var xn in x)
        {
            if (xn == "") return false;
            byte[]? ha = ReadHeadOrNull(c, Jp(xd, xn));
            if (ha == null) return false;
            bool hit = false;
            foreach (var yn in y)
            {
                if (hit) break;
                if (yn == "") continue;
                byte[]? hb = ReadHeadOrNull(c, Jp(yd, yn));
                if (hb == null) continue;
                if (!ha.AsSpan(0x10, 11).SequenceEqual(hb.AsSpan(0x10, 11))) continue;
                hit = SameRec(c, Jp(xd, xn), ha, Jp(yd, yn), hb);
            }
            if (!hit) return false;
        }
        return true;
    }

    /// <summary>hs_match: which of a high Rings slot's (src) and an Aether slot's (dir) saves match, by record
    /// version: OURS when every save of src is in dir (a run again), or dir holds saves and every one of them is one
    /// of src's (a resume after a cut placed part of it).</summary>
    static (int Flags, int DirCount) HsMatch(Ctx c, string dir, string src)
    {
        var a = BlList(c, src);
        var b = BlList(c, dir);
        int m = 0;
        if (a.Count > 0 && MatchSet(c, a, src, b, dir)) m |= HsSrcInDir;
        if (b.Count > 0 && MatchSet(c, b, dir, a, src)) m |= HsDirInSrc;
        return (m, b.Count);
    }

    static void PlaceHigh(Ctx c, string dest, Src h)
    {
        int empty = -1;
        bool used = false;
        foreach (int slot in HighOrder)               // a slot that is this high slot's already, else the first empty
        {
            string sd = "/aether/" + dest + (slot != 0 ? "/0" + slot.ToString(Inv) : "");
            string cur = SaveGen.Cur(c.Fs, sd).Path;
            var (flags, nd) = HsMatch(c, cur, h.Path);
            if (flags != 0) { MergeSlot(c, dest, slot, [h], 2); return; }
            if (nd == 0 && empty < 0) empty = slot;
            if (nd != 0) used = true;
        }
        if (!used) empty = 0;                         // no saves in any slot: slot 0, the launch's default
        {
            string cur = SaveGen.Cur(c.Fs, "/aether/" + dest).Path + "/ALT";
            var (flags, _) = HsMatch(c, cur, h.Path);
            if ((flags & HsSrcInDir) == 0 && empty >= 0) { MergeSlot(c, dest, empty, [h], 2); return; }
            MergeSlot(c, dest, 0, [h], 1);            // every slot full (or placed there before): alternates
        }
    }

    // --- fix I9: Kai's SAVES/LATEST into _GENERAL; GENLIST.TXT's "I" line ----------------------------------------------

    static uint RecId(byte[] nm12, uint date, uint size, uint dataCrc)
    {
        var b = new byte[24];
        nm12.AsSpan(0, 12).CopyTo(b);
        Be.Put32(b, 12, date);
        Be.Put32(b, 16, size);
        Be.Put32(b, 20, dataCrc);
        var hh = new System.IO.Hashing.Crc32();
        hh.Append(b);
        return hh.GetCurrentHashAsUInt32();
    }

    /// <summary>gimp_note: path's version id (a winner of merge_class, into _GENERAL only) -- noted so the next
    /// archive keeps it as an alternate if it differs from the console's (pgsrun.c cand_act).</summary>
    static void GimpNote(Ctx c, string path)
    {
        if (!c.GenImp || c.Gimp.Count >= GimpMax) return;
        byte[] rec;
        try { rec = c.Fs.ReadAll(path); }
        catch (IOException) { return; }
        if (rec.Length < BupFile.Hdr) return;
        var nm = new byte[12];
        rec.AsSpan(0x10, 11).CopyTo(nm);
        if (!BupFile.NameNorm(nm, nm)) return;
        uint date = Be.Get32(rec, 0x28), size = Be.Get32(rec, 0x2C);
        if (rec.Length < BupFile.Hdr + size) return;
        var hh = new System.IO.Hashing.Crc32();
        hh.Append(rec.AsSpan(BupFile.Hdr, (int)size));
        uint id = RecId(nm, date, size, hh.GetCurrentHashAsUInt32());
        if (!c.Gimp.Contains(id)) c.Gimp.Add(id);
    }

    /// <summary>genlist_add: GENLIST.TXT's "R"/"A" lines kept, the "I" line = the old one plus c.Gimp (deduped); a cut
    /// before it: the next import notes the same versions as present.</summary>
    static void GenlistAdd(Ctx c)
    {
        byte[] rec;
        try { rec = c.Fs.ReadAll(GenlistPath); }
        catch (FileNotFoundException) { rec = []; }
        catch (DirectoryNotFoundException) { rec = []; }
        var o = new List<byte> { (byte)'\n' };
        int i = 0;
        int cap = (int)(Rec / 2);
        while (i < rec.Length)
        {
            int a = i;
            while (i < rec.Length && rec[i] != (byte)'\n') i++;
            if (i > a && rec[a] != (byte)'I' && o.Count + (i - a) + 1 < cap)
            {
                for (int k = a; k < i; k++) o.Add(rec[k]);
                o.Add((byte)'\n');
            }
            if (i > a && rec[a] == (byte)'I')         // its ids join this run's
            {
                int k = a + 1;
                while (k + 9 <= i && rec[k] == (byte)' ' && c.Gimp.Count < GimpMax)
                {
                    uint v = 0;
                    for (int j = 0; j < 8; j++)
                    {
                        char ch = (char)rec[k + 1 + j];
                        v = v << 4 | (uint)(ch >= 'A' ? ch - 'A' + 10 : ch - '0');
                    }
                    if (!c.Gimp.Contains(v)) c.Gimp.Add(v);
                    k += 9;
                }
            }
            i++;
        }
        o.Add((byte)'I');
        foreach (var id in c.Gimp)
        {
            o.Add((byte)' ');
            foreach (var ch in id.ToString("X8", Inv)) o.Add((byte)ch);
        }
        o.Add((byte)'\n');
        c.Fs.MkDir(Saves);
        c.Fs.WriteReplace(GenlistPath, o.ToArray());
    }

    /// <summary>put_big's source side: the record file's first 64 + datasize bytes (the header and the data; bytes after
    /// the data are not copied, unlike put_new's whole-file copy), whatever the size. Short: an error (read_span).</summary>
    static byte[] ReadBig(Ctx c, string p, long len) => c.Fs.ReadAt(p, 0, BigLen(p, len));

    /// <summary>review-1 n-2: a record file the PC cannot hold in one buffer (over 2 GB; never on a real card, whose
    /// largest save is a 512 KB cartridge) stops the import as an I/O error, never an unhandled overflow.</summary>
    internal static int BigLen(string p, long len) =>
        len <= Array.MaxLength ? (int)len : throw new IOException($"{p}: {len} B is too large to copy");

    static bool Same11(Ent a, Ent b) => a.Hdr.AsSpan(0x10, 11).SequenceEqual(b.Hdr.AsSpan(0x10, 11));

    static byte[] PadName(byte[] name) { var b = new byte[12]; name.CopyTo(b, 0); return b; }

    /// <summary>om_sdw_read_file with cap OM_PGS_REC: the whole file, verbatim.</summary>
    static byte[] ReadWhole(Ctx c, string p)
    {
        var b = c.Fs.ReadAll(p);
        if (b.Length + 4 > Rec) throw new IOException($"{p}: {b.Length} B is over the {Rec - 4} B a record file can hold");
        return b;
    }
}
