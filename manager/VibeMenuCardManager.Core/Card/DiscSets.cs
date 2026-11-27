// Aether Card Manager, GPL-3.0-or-later.
// Multi-disc sets (contract/SPEC.md §5.4 LIBRARY.IDX flags bits 2 and 4-15): a port of tools/omsat/discsets.py, which
// states the rule step by step. contract/fixtures/discsets.json gates both. The menu reads the bits at launch and
// LAUNCHER.BIN writes out1..outN.desc, so opening and closing the console's lid swaps discs (Satiator firmware 161+).
using System.Text;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public static class DiscSets
{
    public const int SetMax = 4095, FlagAlt = 0x4;
    static readonly string[] Words = ["volume", "vol.", "vol", "disc", "disk", "cd"];
    const string Roman = "ivxIVX";

    static bool AlNum(char c) => char.IsAsciiLetterOrDigit(c);

    static (int, int)? Token(string s, int i)
    {
        if (i > 0 && AlNum(s[i - 1])) return null;
        var low = new StringBuilder();
        for (int k = i; k < s.Length && k < i + 6; k++) low.Append(s[k] is >= 'A' and <= 'Z' ? (char)(s[k] + 32) : s[k]);
        string l = low.ToString();
        foreach (var w in Words)
        {
            if (!l.StartsWith(w, StringComparison.Ordinal)) continue;
            int j = i + w.Length;
            while (j < s.Length && s[j] == ' ') j++;
            int e = j;
            while (e < s.Length && char.IsAsciiDigit(s[e])) e++;
            if (e - j is < 1 or > 2)
            {
                e = j;
                while (e < s.Length && Roman.Contains(s[e])) e++;
                if (e - j is < 1 or > 4) return null;
            }
            if (e < s.Length && AlNum(s[e])) return null;
            return (j, e);
        }
        return null;
    }

    static (int, int)? Underscore(string s, int i)
    {
        if (s[i] != '_') return null;
        int j = i + 1, k = j;
        while (k < s.Length && char.IsAsciiDigit(s[k])) k++;
        if (k - j is < 1 or > 2 || k >= s.Length || s[k] != '.') return null;
        int extLen = s.Length - (k + 1);
        if (extLen is < 1 or > 4) return null;
        for (int x = k + 1; x < s.Length; x++) if (!AlNum(s[x])) return null;
        return (j, k);
    }

    static (string Masked, int First) Scan(string s, bool firstOnly)
    {
        var o = new StringBuilder();
        int i = 0, first = -1;
        while (i < s.Length)
        {
            var t = Token(s, i) ?? Underscore(s, i);
            if (t is (int a, int b))
            {
                o.Append(s, i, a - i).Append('#');
                if (first < 0)
                {
                    first = o.Length;
                    if (firstOnly) return (o.ToString(), first);
                }
                i = b;
            }
            else o.Append(s[i++]);
        }
        return (o.ToString(), first);
    }

    public static string Mask(string path) => Scan(path, false).Masked;

    public static string Stem(string path)
    {
        var (m, first) = Scan(path, true);
        return first < 0 ? m : m[..first];
    }

    public static string NormProduct(string raw)
    {
        var o = new StringBuilder();
        foreach (var ch in raw)
        {
            if (ch <= ' ') break;
            char c = ch is >= 'a' and <= 'z' ? (char)(ch - 32) : ch;
            o.Append(c is >= '0' and <= '9' or >= 'A' and <= 'Z' or '_' or '-' ? c : '_');
        }
        return o.Length > 10 ? o.ToString(0, 10) : o.ToString();
    }

    static int Lcp(byte[] a, byte[] b)
    {
        int n = 0;
        while (n < Math.Min(a.Length, b.Length) && a[n] == b[n]) n++;
        return n;
    }

    sealed class Set(int first, string stem, byte[] sig)
    {
        public int First = first;
        public readonly string Stem = stem;
        public readonly byte[] Sig = sig;
        public readonly Dictionary<int, List<int>> Discs = [];
    }

    /// <summary>Each image's flags bits (set id &lt;&lt; 4 | FlagAlt), 0 for none; items in LIBRARY.IDX order.</summary>
    public static int[] Assign(IReadOnlyList<(string Path, string Product, int Disc, int Total)> items)
    {
        var buckets = new List<((string, int) Key, List<int> Idx)>();
        var bi = new Dictionary<(string, int), int>();
        for (int i = 0; i < items.Count; i++)
        {
            var (path, product, disc, total) = items[i];
            string p = NormProduct(product ?? "");
            if (p == "" || total is < 2 or > 9 || disc < 1 || disc > total) continue;
            if (!bi.TryGetValue((p, total), out int b)) { bi[(p, total)] = b = buckets.Count; buckets.Add(((p, total), [])); }
            buckets[b].Idx.Add(i);
        }
        var sets = new List<Set>();
        foreach (var ((_, total), idx) in buckets)
        {
            var clusters = new List<(string Sig, byte[] SigB, List<int> M)>();
            var ci = new Dictionary<string, int>(StringComparer.Ordinal);
            foreach (int i in idx)
            {
                string sig = Mask(items[i].Path);
                if (!ci.TryGetValue(sig, out int c)) { ci[sig] = c = clusters.Count; clusters.Add((sig, Encoding.UTF8.GetBytes(sig), [])); }
                clusters[c].M.Add(i);
            }
            var ordered = clusters.OrderBy(c => -c.M.Count).ThenBy(c => c.SigB, ByteArrayOrder.Instance).ToList();
            var gs = new List<Set>();
            foreach (var (_, sigB, members) in ordered)
            {
                var nums = members.Select(i => items[i].Disc).ToHashSet();
                string st = Stem(items[members[0]].Path);
                var same = gs.Where(g => g.Stem == st).ToList();
                Set? tgt = same.FirstOrDefault(g => !nums.Overlaps(g.Discs.Keys));
                if (tgt == null && nums.Count < total && same.Count > 0)
                {
                    int best = -1;
                    foreach (var g in same) { int n = Lcp(g.Sig, sigB); if (n > best) { best = n; tgt = g; } }
                }
                if (tgt == null) { tgt = new Set(members[0], st, sigB); gs.Add(tgt); }
                foreach (int i in members)
                {
                    int d = items[i].Disc;
                    if (!tgt.Discs.TryGetValue(d, out var l)) tgt.Discs[d] = l = [];
                    l.Add(i);
                    tgt.First = Math.Min(tgt.First, i);
                }
            }
            sets.AddRange(gs);
        }
        var flags = new int[items.Count];
        int sid = 0;
        foreach (var g in sets.Where(g => g.Discs.Count >= 2).OrderBy(g => g.First))
        {
            if (sid == SetMax) break;
            sid++;
            foreach (var members in g.Discs.Values)
                for (int k = 0; k < members.Count; k++)
                    flags[members[k]] = sid << 4 | (k > 0 ? FlagAlt : 0);
        }
        return flags;
    }

    /// <summary>The scan's entries with their set bits, and its LIBRARY.IDX rewritten (same size).</summary>
    public static ScanResult Apply(ScanResult res, uint buildDate)
    {
        var flags = Assign(res.Entries.Select(e => (e.Path, res.SystemIds.GetValueOrDefault(e.Path)?.Product ?? "", e.Disc, e.DiscTotal)).ToList());
        var ents = res.Entries.Select((e, i) => e with { Sets = flags[i] }).ToList();
        return res with { Entries = ents, Data = LibraryIndex.Write(ents, res.Folders, res.Fingerprint, buildDate) };
    }

    sealed class ByteArrayOrder : IComparer<byte[]>
    {
        public static readonly ByteArrayOrder Instance = new();
        public int Compare(byte[]? a, byte[]? b) => a.AsSpan().SequenceCompareTo(b);
    }
}
