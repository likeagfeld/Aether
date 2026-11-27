// Aether Card Manager, GPL-3.0-or-later.
// GAMES.DB (contract/SPEC.md §5.3, v1 clarifications 1 and 3; §5.9's 245,756-byte console limit). A port of
// tools/omsat/gamesdb.py: write_gamesdb, read_gamesdb, fill_dates, region_flags_from, periph_flags_from, ascii_fold.
using System.Globalization;
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed class GameRecord
{
    public required string Key;
    public required string TitleEn;
    public string TitleRom = "", Maker = "";
    public int DiscCount = 1, Genre = 0xFF, RegionFlags, Players, Peripherals, Year;
    public uint Date;
    public int LoaderHint, CompatFlags, CompatRegions;      // SPEC §5.3 0x2E, 0x2F, 0x34
    public string CompatNote = "";                         // 0x30
    /// <summary>0x35 bit0: TitleEn is the user's own title (USEREDIT.INI/UserEdits.Apply), not the database's. The
    /// display-name rule (manager naming fix round) falls back to this before the file name.</summary>
    public bool IsUserTitle;                                // 0x35 bit0
}

public sealed record GamesDbData(List<GameRecord> Records, List<string> Genres, Dictionary<string, string> Aliases, uint BuildDate);

public static class GamesDb
{
    public const int Rec = 64;
    /// <summary>SPEC §5.9: the menu refuses a GAMES.DB above 245,756 B (OM_LW_GAMESDB_MAX 0x3C000 - 4).</summary>
    public const int ConsoleMax = 245_756;

    public static int RegionFlagsFrom(string area) =>
        area.ToUpperInvariant().Distinct().Sum(c => "JTUEB".IndexOf(c) is int i and >= 0 ? 1 << i : 0);

    /// <summary>The inverse of RegionFlagsFrom: the record's region bits as letters (e.g. 0x5 -> "JU").</summary>
    public static string RegionLettersFrom(int flags) =>
        new(Enumerable.Range(0, 5).Where(i => (flags & (1 << i)) != 0).Select(i => "JTUEB"[i]).ToArray());

    public static int PeriphFlagsFrom(string s) =>
        s.ToUpperInvariant().Distinct().Sum(c => "JAMKST".IndexOf(c) is int i and >= 0 ? 1 << i : 0);

    /// <summary>NFKD, combining marks dropped, anything else outside ASCII becomes '?' (the menu font is ASCII only).
    /// Known divergence from omsat's ascii_fold (review-1 m-9; unreachable today, since every title read from a
    /// GAMES.DB is already ASCII): Python drops by canonical combining class, this by Unicode mark category, and a
    /// character outside the BMP gives "??" here (two UTF-16 units) and "?" there. Port Python's rule exactly when the
    /// DbBuilder (titles from the Cover Project sheet) lands.</summary>
    public static string AsciiFold(string s)
    {
        var o = new StringBuilder();
        foreach (var ch in s.Normalize(NormalizationForm.FormKD))
        {
            var cat = CharUnicodeInfo.GetUnicodeCategory(ch);
            if (cat is UnicodeCategory.NonSpacingMark or UnicodeCategory.SpacingCombiningMark or UnicodeCategory.EnclosingMark) continue;
            o.Append(ch < 128 ? ch : '?');
        }
        return o.ToString();
    }

    sealed class Pool
    {
        public readonly List<byte> Buf = [0];
        readonly Dictionary<string, uint> _index = new() { [""] = 0 };

        public uint Add(string s)
        {
            s = AsciiFold(s);
            if (!_index.TryGetValue(s, out var off))
            {
                _index[s] = off = (uint)Buf.Count;
                Buf.AddRange(Encoding.ASCII.GetBytes(s));
                Buf.Add(0);
            }
            return off;
        }
    }

    static int KeyCmp(string a, string b) => CommonHeader.KeyBytes(a).AsSpan().SequenceCompareTo(CommonHeader.KeyBytes(b));

    public static byte[] Write(IEnumerable<GameRecord> records, IReadOnlyList<string> genres,
                               IReadOnlyDictionary<string, string> aliases, uint buildDate)
    {
        var recs = records.ToList();
        recs.Sort((a, b) => KeyCmp(a.Key, b.Key));
        if (recs.Select(r => r.Key).Distinct().Count() != recs.Count) throw new ArgumentException("duplicate keys in GAMES.DB input");
        var pool = new Pool();
        var table = new byte[Rec * recs.Count];
        for (int i = 0; i < recs.Count; i++)
        {
            var r = recs[i];
            var t = table.AsSpan(Rec * i, Rec);
            CommonHeader.KeyBytes(r.Key).CopyTo(t);
            t[0x10] = (byte)r.DiscCount; t[0x11] = (byte)r.Genre; t[0x12] = (byte)r.RegionFlags; t[0x13] = (byte)r.Players;
            Be.Put16(t, 0x14, (ushort)r.Peripherals);
            Be.Put16(t, 0x16, (ushort)r.Year);
            Be.Put32(t, 0x18, r.Date);
            Be.Put32(t, 0x1C, pool.Add(r.TitleEn));
            Be.Put32(t, 0x20, pool.Add(r.TitleRom != "" ? r.TitleRom : r.TitleEn));
            Be.Put32(t, 0x24, pool.Add(r.Maker));
            // 0x28 first alias index, 0x2C alias count: 0 in v1 (clarification 1)
            t[0x2E] = (byte)r.LoaderHint; t[0x2F] = (byte)r.CompatFlags;
            Be.Put32(t, 0x30, pool.Add(r.CompatNote));
            t[0x34] = (byte)r.CompatRegions;
            t[0x35] = (byte)(r.IsUserTitle ? 1 : 0);        // 0x36..0x3F reserved, 0
        }
        var genre = new List<byte>(new byte[2]);
        Be.Put16(System.Runtime.InteropServices.CollectionsMarshal.AsSpan(genre), 0, (ushort)genres.Count);
        foreach (var g in genres) { genre.AddRange(Encoding.ASCII.GetBytes(AsciiFold(g))); genre.Add(0); }
        var alias = new List<byte>();
        foreach (var (a, c) in aliases.OrderBy(kv => kv.Key, Comparer<string>.Create(KeyCmp)))
        {
            alias.AddRange(CommonHeader.KeyBytes(a));
            alias.AddRange(CommonHeader.KeyBytes(c));
        }
        uint tOff = CommonHeader.Size, gOff = tOff + (uint)table.Length, aOff = gOff + (uint)genre.Count, pOff = aOff + (uint)alias.Count;
        var h = new CommonHeader(CommonHeader.Gdb, (uint)recs.Count, tOff, (uint)table.Length, pOff, (uint)pool.Buf.Count,
            buildDate, CommonHeader.Crc32(table), [(gOff, (uint)genre.Count), (aOff, (uint)alias.Count), (pOff, (uint)pool.Buf.Count)]);
        byte[] o = [.. h.Pack(), .. table, .. genre, .. alias, .. pool.Buf];
        if (o.Length > ConsoleMax)          // SPEC §5.9: writers must not produce a file the menu refuses
            throw new FormatException($"GAMES.DB would be {o.Length} B, over the menu's {ConsoleMax} B (SPEC 5.9)");
        return o;
    }

    public static GamesDbData Read(byte[] data) => CommonHeader.Guard(() => ReadCore(data));

    static GamesDbData ReadCore(byte[] dataArr)
    {
        ReadOnlySpan<byte> data = dataArr;
        if (data.Length > ConsoleMax) throw new FormatException($"size: {data.Length} B is over the menu's {ConsoleMax} B (SPEC 5.9)");
        var h = CommonHeader.Validate(data, CommonHeader.Gdb);
        if (h.TableSize != Rec * h.Count) throw new FormatException("table size != 64 * count");
        var pool = data.Slice((int)h.DataOff, (int)h.DataSize);
        var recs = new List<GameRecord>();
        for (int i = 0; i < h.Count; i++)
        {
            var t = data.Slice((int)h.TableOff + Rec * i, Rec);
            recs.Add(new GameRecord
            {
                Key = CommonHeader.KeyStr(t[..16]), DiscCount = t[0x10], Genre = t[0x11], RegionFlags = t[0x12], Players = t[0x13],
                Peripherals = Be.Get16(t, 0x14), Year = Be.Get16(t, 0x16), Date = Be.Get32(t, 0x18),
                TitleEn = CommonHeader.CStr(pool, Be.Get32(t, 0x1C), Encoding.ASCII),
                TitleRom = CommonHeader.CStr(pool, Be.Get32(t, 0x20), Encoding.ASCII),
                Maker = CommonHeader.CStr(pool, Be.Get32(t, 0x24), Encoding.ASCII),
                LoaderHint = t[0x2E], CompatFlags = t[0x2F], CompatNote = CommonHeader.CStr(pool, Be.Get32(t, 0x30), Encoding.ASCII),
                CompatRegions = t[0x34], IsUserTitle = (t[0x35] & 1) != 0,
            });
        }
        var (gOff, gSize) = h.Sections[0];
        var (aOff, aSize) = h.Sections[1];
        for (int i = 1; i < recs.Count; i++)          // the menu binary-searches the table (SPEC §5.3: sorted by key)
            if (KeyCmp(recs[i - 1].Key, recs[i].Key) >= 0)
                throw new FormatException($"order: record {i} ({recs[i].Key}) is not after {recs[i - 1].Key}");
        if (gSize < 2) throw new FormatException("genre table shorter than its count");
        var gsec = data.Slice((int)gOff, (int)gSize);
        int n = Be.Get16(gsec, 0);
        var genres = Encoding.ASCII.GetString(gsec[2..]).Split('\0').Take(n).ToList();
        var aliases = new Dictionary<string, string>();
        string? prev = null;
        for (int i = 0; i < aSize / 32; i++)
        {
            var row = data.Slice((int)aOff + 32 * i, 32);
            string ak = CommonHeader.KeyStr(row[..16]);
            if (prev != null && KeyCmp(prev, ak) >= 0) throw new FormatException($"order: alias {i} ({ak}) is not after {prev}");
            prev = ak;
            aliases[CommonHeader.KeyStr(row[..16])] = CommonHeader.KeyStr(row[16..]);
        }
        return new GamesDbData(recs, genres, aliases, h.BuildDate);
    }

    /// <summary>fill_dates (SPEC §5.3, the card's copy): each card game's IP.BIN date (key -> YYYYMMDD) becomes the
    /// date and year of a record that has neither; keys are resolved through the aliases. Returns the bytes unchanged
    /// when nothing is filled.</summary>
    public static (byte[] Data, int Filled) FillDates(byte[] data, IReadOnlyDictionary<string, uint> dates)
    {
        var db = Read(data);
        var byKey = db.Records.ToDictionary(r => r.Key);
        int n = 0;
        foreach (var (key, d) in dates.OrderBy(kv => kv.Key, StringComparer.Ordinal))
            if (byKey.TryGetValue(db.Aliases.GetValueOrDefault(key, key), out var r) && r.Year == 0 && r.Date == 0)
            {
                r.Year = (int)(d / 10000); r.Date = d;
                n++;
            }
        return n == 0 ? (data, 0) : (Write(db.Records, db.Genres, db.Aliases, db.BuildDate), n);
    }
}
