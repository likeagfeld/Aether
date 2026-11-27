// Aether Card Manager, GPL-3.0-or-later.
// The product-code matcher, contract/SPEC.md §6 (v3): cleanup, normalise, split, skeleton,
// skeleton_forms, suffix_base, choose, match. A port of tools/omsat/match.py; contract/fixtures/match_cases.json is its executable form.
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Match;

/// <param name="Candidates">matched: every candidate considered, sorted; unmatched: the tied survivors.</param>
public sealed record MatchResult(string? Key, string? How, IReadOnlyList<string> Candidates);

public sealed partial class Matcher
{
    // A version spilled from IP.BIN 0x2A into the product field: "T-29002G V", "GS-9055 V1", "MK-81029V1".
    [GeneratedRegex("(\\s+V\\d?|(?<=\\d)V\\d)$")] private static partial Regex VersionSpill();
    [GeneratedRegex("-(50|18)$")] private static partial Regex Suffix();
    // A -50/-18 suffix, or the same digits with the dash lost ("MK-8109150", "T-12003H50").
    [GeneratedRegex("-?(50|18)$")] private static partial Regex AnySuffix();
    [GeneratedRegex("^([A-Z]+)0+(?=[1-9])")] private static partial Regex LeadingZeros();

    readonly List<string> _keys;
    readonly HashSet<string> _keyset;
    readonly Dictionary<string, string> _aliases = [];
    readonly Dictionary<string, HashSet<string>> _suffixedByBase = [], _skel = [];
    readonly bool _noAreaFilter;

    public Matcher(IEnumerable<string> keys, IReadOnlyDictionary<string, string> aliases) : this(keys, aliases, false) { }

    /// <summary>noAreaFilter: the mutation demo's broken matcher (choose() step 1 dropped).</summary>
    internal Matcher(IEnumerable<string> keys, IReadOnlyDictionary<string, string> aliases, bool noAreaFilter)
    {
        _noAreaFilter = noAreaFilter;
        _keys = keys.Distinct().OrderBy(k => k, StringComparer.Ordinal).ToList();
        _keyset = [.. _keys];
        foreach (var (a, c) in aliases) _aliases[Keys.Normalise(a)] = c;
        foreach (var x in _keys)
        {
            var (b, suf) = Split(x);
            if (suf != "") Add(_suffixedByBase, b, x);
            foreach (var f in SkeletonForms(b)) Add(_skel, f, x);
        }
    }

    static void Add(Dictionary<string, HashSet<string>> d, string k, string v)
    {
        if (!d.TryGetValue(k, out var s)) d[k] = s = [];
        s.Add(v);
    }

    public static string Cleanup(string product) => VersionSpill().Replace(product.TrimEnd(), "");

    /// <summary>k less a trailing -50/-18 or dashless 50/18, or null when it ends with neither or would leave fewer
    /// than 4 characters.</summary>
    public static string? SuffixBase(string k)
    {
        var m = AnySuffix().Match(k);
        return m.Success && m.Index >= 4 ? k[..m.Index] : null;
    }

    public static (string Base, string Suffix) Split(string k)
    {
        var m = Suffix().Match(k);
        return m.Success ? (k[..m.Index], k[m.Index..]) : (k, "");
    }

    public static string Skeleton(string b) =>
        LeadingZeros().Replace(b.ToUpperInvariant().Replace("-", "").Replace(" ", "").Replace("_", ""), "$1");

    public static HashSet<string> SkeletonForms(string b)
    {
        string sk = Skeleton(b);
        if (sk.StartsWith("MK", StringComparison.Ordinal)) return [sk, sk[2..]];
        // a third-party code written without its "T-": "12512H" ~ "T-12512H"
        if (sk.Length > 1 && sk[0] == 'T' && char.IsAsciiDigit(sk[1])) return [sk, sk[1..]];
        return [sk];
    }

    MatchResult Choose(IEnumerable<string> c, string area, string how)
    {
        var cands = c.Distinct().OrderBy(x => x, StringComparer.Ordinal).ToList();
        var a = area.ToUpperInvariant();
        bool hasE = a.Contains('E'), hasOther = a.IndexOfAny(['J', 'T', 'U']) >= 0;
        List<string> preferred = cands;
        if (!_noAreaFilter)
        {
            if (hasE && !hasOther) preferred = cands.Where(Keys.IsPalSuffixed).ToList();
            else if (!hasE) preferred = cands.Where(x => !Keys.IsPalSuffixed(x)).ToList();
            if (preferred.Count == 0) preferred = cands;
        }
        int shortest = preferred.Min(x => x.Length);
        var best = preferred.Where(x => x.Length == shortest).ToList();
        if (best.Count == 1) return new MatchResult(best[0], how, cands);
        if (best.All(Keys.IsPalSuffixed))
        {
            var fifties = best.Where(x => x.EndsWith("-50", StringComparison.Ordinal)).ToList();
            if (fifties.Count == 1) return new MatchResult(fifties[0], how, cands);
        }
        return new MatchResult(null, null, best);
    }

    HashSet<string> Family(string b)
    {
        var fam = new HashSet<string>();
        foreach (var f in SkeletonForms(b)) if (_skel.TryGetValue(f, out var v)) fam.UnionWith(v);
        return fam;
    }

    /// <summary>SPEC §6 match(): product = IP.BIN 0x20's 10 characters, area = 0x40's area symbols.</summary>
    public MatchResult Match(string product, string area)
    {
        string k = Keys.Normalise(Cleanup(product));
        if (k == "") return new MatchResult(null, null, []);
        if (_keyset.Contains(k))
            return Choose(_suffixedByBase.TryGetValue(k, out var s) ? s.Append(k) : [k], area, "exact");
        if (_aliases.TryGetValue(k, out var c) && _keyset.Contains(c))
            return Choose(_suffixedByBase.TryGetValue(c, out var s2) ? s2.Append(c) : [c], area, "alias");
        var skel = Family(k);
        if (skel.Count > 0) return Choose(skel, area, "skeleton");
        if (SuffixBase(k) is string b && Family(b) is { Count: > 0 } fam) return Choose(fam, area, "suffix");
        var prefix = _keys.Where(x => x.StartsWith(k, StringComparison.Ordinal)).ToList();
        if (prefix.Count > 0) return Choose(prefix, area, "prefix");
        return new MatchResult(null, null, []);
    }
}
