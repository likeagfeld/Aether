// Aether Card Manager, GPL-3.0-or-later.
// CHEATS.PAK (contract/SPEC.md §5.15, magic OMSATCHT): every game's cheats, the user's on/off choices and user-added
// cheats in one file. A port of tools/omsat/cheatpak.py (encode/decode_record, write_pak, read_index, merge) plus the
// editor's one extra write rule (menu/launcher/chedit.c): a record emptied of cheats loses its row.
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed record Cheat(string Name, byte Flags, List<(uint Addr, ushort Value, byte Cmd)> Codes)
{
    public bool On => (Flags & CheatPak.COn) != 0 && (Flags & CheatPak.CHeader) == 0;
}

public sealed record CheatRecord(uint MasterAddr, ushort MasterValue, byte Flags, uint Location, ushort Cache4, List<Cheat> Cheats)
{
    /// <summary>How many cheats are switched on (label rows never are): the "N cheats on" pill.</summary>
    public int OnCount => Cheats.Count(c => c.On);
}

/// <param name="Merged">keys in both files (the shipped record with the card's choices)</param>
/// <param name="KeptCardOnly">keys only on the card, kept as they are</param>
/// <param name="KeptUnfitted">keys whose merge broke the 64-cheat/256-code limits: the card's record kept</param>
/// <param name="OldError">why the card's copy did not read (it is then not used, and kept as CHEATS.BAK)</param>
public sealed record CheatMergeReport(int Merged, int KeptCardOnly, List<string> KeptUnfitted, string? OldError);

public static class CheatPak
{
    public static readonly byte[] Magic = "OMSATCHT"u8.ToArray();
    public const string Name = "CHEATS.PAK", NewName = "CHEATS.PAK.NEW", BakName = "CHEATS.BAK";
    public const int Row = 24, RecHdr = 20, MaxCheats = 64, MaxCodes = 256, MaxName = 30;
    public const int MaxRec = RecHdr + MaxCheats * (3 + MaxName) + MaxCodes * 8;   // 4,180 B
    public const int MaxFile = 0x1C000;                                             // 114,688 B
    public const uint PakDate = 20261124;
    public const byte GMaster = 0x01, GCwx = 0x02, GJhl = 0x04;
    public const byte COn = 0x01, CUser = 0x02, CHeader = 0x04;

    static bool Printable(ReadOnlySpan<byte> b) { foreach (var x in b) if (x < 0x20 || x > 0x7E) return false; return true; }

    public static byte[] EncodeRecord(CheatRecord r)
    {
        if (r.Cheats.Count > MaxCheats) throw new FormatException($"{r.Cheats.Count} cheats, over {MaxCheats}");
        int ncodes = r.Cheats.Sum(c => c.Codes.Count);
        if (ncodes > MaxCodes) throw new FormatException($"{ncodes} codes, over {MaxCodes}");
        var body = new List<byte>(256);
        var h = new byte[16];
        Be.Put32(h, 0, r.MasterAddr); Be.Put16(h, 4, r.MasterValue); h[6] = r.Flags; h[7] = (byte)r.Cheats.Count;
        Be.Put32(h, 8, r.Location); Be.Put16(h, 12, (ushort)ncodes); Be.Put16(h, 14, r.Cache4);
        body.AddRange(h);
        foreach (var c in r.Cheats)
        {
            var nm = Encoding.ASCII.GetBytes(c.Name);
            if (nm.Length is < 1 or > MaxName || !Printable(nm) || Encoding.ASCII.GetString(nm) != c.Name)
                throw new FormatException($"cheat name '{c.Name}': 1-{MaxName} printable ASCII bytes");
            if ((c.Flags & CHeader) != 0 && (c.Codes.Count > 0 || (c.Flags & COn) != 0))
                throw new FormatException($"header row '{c.Name}' has codes or is on");
            if ((c.Flags & CHeader) == 0 && c.Codes.Count is < 1 or > 255)
                throw new FormatException($"cheat '{c.Name}': 1-255 codes");
            body.Add(c.Flags); body.Add((byte)c.Codes.Count); body.Add((byte)nm.Length); body.AddRange(nm);
        }
        var code = new byte[8];
        foreach (var c in r.Cheats)
            foreach (var (a, v, cmd) in c.Codes)
            {
                Be.Put32(code, 0, a); Be.Put16(code, 4, v); code[6] = cmd; code[7] = 0;
                body.AddRange(code);
            }
        while ((body.Count + 4) % 4 != 0) body.Add(0);
        var b = body.ToArray();
        var out_ = new byte[4 + b.Length];
        Be.Put32(out_, 0, CommonHeader.Crc32(b));
        b.CopyTo(out_, 4);
        return out_;
    }

    public static CheatRecord DecodeRecord(ReadOnlySpan<byte> b)
    {
        if (b.Length < RecHdr || b.Length % 4 != 0 || b.Length > MaxRec) throw new FormatException($"record length {b.Length}");
        if (CommonHeader.Crc32(b[4..]) != Be.Get32(b, 0)) throw new FormatException("record CRC");
        uint ma = Be.Get32(b, 4); ushort mv = Be.Get16(b, 8); byte fl = b[10]; int nch = b[11];
        uint loc = Be.Get32(b, 12); int ncodes = Be.Get16(b, 16); ushort cache4 = Be.Get16(b, 18);
        if (nch > MaxCheats || ncodes > MaxCodes) throw new FormatException("record counts");
        var cheats = new List<Cheat>();
        var counts = new List<int>();
        int p = RecHdr;
        for (int i = 0; i < nch; i++)
        {
            if (p + 3 > b.Length) throw new FormatException("cheat row past the record");
            byte cf = b[p]; int cn = b[p + 1], ln = b[p + 2];
            if (ln is < 1 or > MaxName || p + 3 + ln > b.Length || !Printable(b.Slice(p + 3, ln))) throw new FormatException("cheat name");
            if ((cn != 0 && (cf & CHeader) != 0) || (cn == 0 && (cf & (CHeader | COn)) != CHeader))
                throw new FormatException("cheat code count / header flag");
            cheats.Add(new Cheat(Encoding.ASCII.GetString(b.Slice(p + 3, ln)), cf, []));
            counts.Add(cn);
            p += 3 + ln;
        }
        if (counts.Sum() != ncodes || p + 8 * ncodes > b.Length || b.Length - (p + 8 * ncodes) > 3) throw new FormatException("code table");
        for (int i = 0; i < cheats.Count; i++)
            for (int n = 0; n < counts[i]; n++, p += 8)
                cheats[i].Codes.Add((Be.Get32(b, p), Be.Get16(b, p + 4), b[p + 6]));
        for (; p < b.Length; p++) if (b[p] != 0) throw new FormatException("non-zero padding");
        return new CheatRecord(ma, mv, fl, loc, cache4, cheats);
    }

    static int KeyCmp(string a, string b) => CommonHeader.KeyBytes(a).AsSpan().SequenceCompareTo(CommonHeader.KeyBytes(b));

    /// <summary>write_pak: rows in key order (memcmp of the padded key), identical records stored once (the first
    /// key's copy, in key order), and -- the editor's rule -- a record with no cheats left gets no row.</summary>
    public static byte[] Write(IReadOnlyDictionary<string, CheatRecord> records, uint date = PakDate)
    {
        var keys = records.Where(kv => kv.Value.Cheats.Count > 0).Select(kv => kv.Key).ToList();
        keys.Sort(KeyCmp);
        int dataOff = 64 + Row * keys.Count;
        var rows = new byte[Row * keys.Count];
        var data = new List<byte>();
        var at = new Dictionary<string, int>();
        for (int i = 0; i < keys.Count; i++)
        {
            var blob = EncodeRecord(records[keys[i]]);
            string id = Convert.ToBase64String(blob);
            if (!at.TryGetValue(id, out int off)) { at[id] = off = dataOff + data.Count; data.AddRange(blob); }
            CommonHeader.KeyBytes(keys[i]).CopyTo(rows, i * Row);
            Be.Put32(rows, i * Row + 16, (uint)off);
            Be.Put32(rows, i * Row + 20, (uint)blob.Length);
        }
        var hdr = new CommonHeader(Magic, (uint)keys.Count, 64, (uint)rows.Length, (uint)dataOff, (uint)data.Count, date,
                                   CommonHeader.Crc32(rows), [(0, 0), (0, 0), (0, 0)]);
        byte[] outb = [.. hdr.Pack(), .. rows, .. data];
        if (outb.Length > MaxFile) throw new FormatException($"CHEATS.PAK would be {outb.Length} B, over {MaxFile}");
        return outb;
    }

    /// <summary>The header and index, checked as the menu checks them: (key, offset, length) rows.</summary>
    public static List<(string Key, int Off, int Len)> ReadIndex(byte[] data)
    {
        if (data.Length > MaxFile) throw new FormatException($"file is {data.Length} B, over {MaxFile}");
        var h = CommonHeader.Unpack(data);
        if (!h.Magic.AsSpan().SequenceEqual(Magic)) throw new FormatException("magic: not OMSATCHT");
        if (h.Version != 1 || h.HeaderSize != 64) throw new FormatException("version / header size");
        if (h.TableOff != 64 || h.TableSize != (long)Row * h.Count || h.DataOff != 64 + h.TableSize) throw new FormatException("table layout");
        if ((long)h.DataOff + h.DataSize > data.Length) throw new FormatException("range: data past the end of the file");
        if (h.Sections.Any(s => s.Off != 0 || s.Size != 0)) throw new FormatException("section table not zero");
        var rows = data.AsSpan(64, (int)h.TableSize);
        if (CommonHeader.Crc32(rows) != h.TableCrc) throw new FormatException("table CRC");
        var outl = new List<(string, int, int)>();
        byte[]? prev = null;
        for (int i = 0; i < h.Count; i++)
        {
            var kb = rows.Slice(i * Row, 16).ToArray();
            uint off = Be.Get32(rows, i * Row + 16), ln = Be.Get32(rows, i * Row + 20);
            if (prev != null && kb.AsSpan().SequenceCompareTo(prev) <= 0) throw new FormatException($"index not sorted / duplicate key at row {i}");
            if (off % 4 != 0 || off < h.DataOff || (long)off + ln > (long)h.DataOff + h.DataSize) throw new FormatException($"row {i}: record out of range");
            prev = kb;
            outl.Add((CommonHeader.KeyStr(kb), (int)off, (int)ln));
        }
        return outl;
    }

    public static Dictionary<string, CheatRecord> Read(byte[] data) =>
        ReadIndex(data).ToDictionary(r => r.Key, r => DecodeRecord(data.AsSpan(r.Off, r.Len)));

    /// <summary>Each key's "cheats on" count; an unreadable file is no cheats (the menu refuses it too).</summary>
    public static Dictionary<string, int> OnCounts(byte[]? data)
    {
        if (data == null) return [];
        try { return Read(data).ToDictionary(kv => kv.Key, kv => kv.Value.OnCount); }
        catch (Exception e) when (e is FormatException or ArgumentException) { return []; }
    }

    /// <summary>A Kai cheat's identity in its record: (name, how many earlier Kai cheats share it); label and user
    /// rows have none.</summary>
    static List<(string, int)?> Ident(List<Cheat> cheats)
    {
        var seen = new Dictionary<string, int>();
        var outl = new List<(string, int)?>();
        foreach (var c in cheats)
        {
            if ((c.Flags & (CHeader | CUser)) != 0) { outl.Add(null); continue; }
            int n = seen.GetValueOrDefault(c.Name);
            seen[c.Name] = n + 1;
            outl.Add((c.Name, n));
        }
        return outl;
    }

    static string CodesId(Cheat c) => c.Name + "\0" + string.Join(",", c.Codes.Select(x => $"{x.Addr:X8}:{x.Value:X4}:{x.Cmd:X2}"));

    /// <summary>merge_record: new's data with old's choices (each Kai cheat's on/off by identity), then old's user
    /// cheats not already there. Returns (record, fitted); over the limits, old comes back unchanged.</summary>
    public static (CheatRecord Record, bool Fitted) MergeRecord(CheatRecord old, CheatRecord nw)
    {
        var oldOn = new Dictionary<(string, int), bool>();
        foreach (var (i, c) in Ident(old.Cheats).Zip(old.Cheats)) if (i is { } id) oldOn[id] = (c.Flags & COn) != 0;
        var cheats = new List<Cheat>();
        foreach (var (i, c) in Ident(nw.Cheats).Zip(nw.Cheats))
        {
            byte f = c.Flags;
            if (i is { } id && oldOn.TryGetValue(id, out bool on)) f = on ? (byte)(f | COn) : (byte)(f & ~COn);
            cheats.Add(new Cheat(c.Name, f, [.. c.Codes]));
        }
        var have = cheats.Where(c => (c.Flags & CUser) != 0).Select(CodesId).ToHashSet();
        cheats.AddRange(old.Cheats.Where(c => (c.Flags & CUser) != 0 && !have.Contains(CodesId(c))).Select(c => new Cheat(c.Name, c.Flags, [.. c.Codes])));
        if (cheats.Count > MaxCheats || cheats.Sum(c => c.Codes.Count) > MaxCodes) return (old, false);
        return (nw with { Cheats = cheats }, true);
    }

    static bool SameRecords(Dictionary<string, CheatRecord> a, Dictionary<string, CheatRecord> b) =>
        a.Count == b.Count && a.All(kv => b.TryGetValue(kv.Key, out var o) && EncodeRecord(o).AsSpan().SequenceEqual(EncodeRecord(kv.Value)));

    /// <summary>SPEC §5.15's update rule. old = the card's copy (null when none), nw = the shipped file. A key in both:
    /// MergeRecord. A key only on the card: kept as it is (its own master fields too). A corrupt old is reported and
    /// not used. Merging again with the same shipped file changes nothing.</summary>
    public static (byte[] Data, CheatMergeReport Report) Merge(byte[]? old, byte[] nw, uint date = PakDate)
    {
        var newRecs = Read(nw);
        if (old == null) return (nw, new CheatMergeReport(0, 0, [], null));
        Dictionary<string, CheatRecord> oldRecs;
        try { oldRecs = Read(old); }
        catch (Exception e) when (e is FormatException or ArgumentException) { return (nw, new CheatMergeReport(0, 0, [], e.Message)); }
        var outd = new Dictionary<string, CheatRecord>(newRecs);
        int merged = 0, cardOnly = 0;
        var unfitted = new List<string>();
        foreach (var (k, o) in oldRecs)
        {
            if (outd.TryGetValue(k, out var n))
            {
                var (rec, fitted) = MergeRecord(o, n);
                outd[k] = rec;
                merged++;
                if (!fitted) unfitted.Add(k);
            }
            else { outd[k] = o; cardOnly++; }
        }
        var rep = new CheatMergeReport(merged, cardOnly, unfitted, null);
        return SameRecords(outd, newRecs) ? (nw, rep) : (Write(outd, date), rep);
    }

    /// <summary>sdwrite.h's recovery rule: CHEATS.PAK wins whenever it exists; absent, CHEATS.PAK.NEW is the latest.</summary>
    public static byte[]? ReadCardCopy(string vibemenuDir)
    {
        foreach (var n in new[] { Name, NewName })
        {
            string p = Path.Combine(vibemenuDir, n);
            if (File.Exists(p)) return File.ReadAllBytes(p);
        }
        return null;
    }
}
