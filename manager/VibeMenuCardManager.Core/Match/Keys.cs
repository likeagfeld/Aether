// Aether Card Manager, GPL-3.0-or-later.
// Product-key normalisation, contract/SPEC.md §6 normalise(). A port of tools/omsat/keys.py.
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Match;

public static partial class Keys
{
    [GeneratedRegex("\\([^)]*\\)")] private static partial Regex Paren();
    [GeneratedRegex("\\s+")] private static partial Regex Ws();
    [GeneratedRegex("-(50|18)$")] private static partial Regex Pal();

    /// <summary>The text before the first newline, parenthesised groups deleted, whitespace removed, upper-cased,
    /// truncated to 16. Known divergence from keys.py (review-1 m-9; unreachable from an IP.BIN, which is ASCII text
    /// with spaces): Python's \s also matches \x1C-\x1F, .NET's does not.</summary>
    public static string Normalise(string s)
    {
        string first = s.Replace("\r", "\n").Split('\n', 2)[0];
        string t = Ws().Replace(Paren().Replace(first, ""), "").ToUpperInvariant();
        return t.Length > 16 ? t[..16] : t;
    }

    public static bool IsPalSuffixed(string key) => Pal().IsMatch(key);
}
