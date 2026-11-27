// Aether Card Manager, GPL-3.0-or-later.
// "Match to game..." overrides, kept in USEREDIT.INI's reserved [MATCH] section (UserEdits.Parse skips it): one
// "<sd path>=<GAMES.DB key>" line per overridden disc. Keyed by SD path, not by GAMES.DB key -- the whole point is a
// game the normal matcher could not key at all (unrecognized, a translation, homebrew), so there may be no key to
// hang the override on until the user picks one. The menu never reads USEREDIT.INI; this only feeds CardScanner's
// ForcedKeys at the Card Manager's own rebuild (CardBuilder/CardRebuilder), which is what actually puts the chosen
// key into GAMES.DB/LIBRARY.IDX/ART.PAK.
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public static class MatchOverrides
{
    public const string SectionName = "MATCH";

    /// <summary>The [MATCH] section's path=key lines, anywhere in `text` (UserEdits.Write/MatchOverrides.Write
    /// always put it after the per-key sections, but this does not require that order).</summary>
    public static Dictionary<string, string> Parse(string text)
    {
        var o = new Dictionary<string, string>();
        bool inSection = false;
        foreach (var raw in text.Replace("\r\n", "\n").Split('\n'))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#') continue;
            if (line[0] == '[' && line.EndsWith(']')) { inSection = line[1..^1].Trim() == SectionName; continue; }
            if (!inSection) continue;
            int eq = line.LastIndexOf('=');       // last, not first: an SD path could in principle contain '='
            if (eq < 0) continue;
            string path = line[..eq].Trim(), key = line[(eq + 1)..].Trim();
            if (path != "" && key != "") o[path] = key;
        }
        return o;
    }

    public static string Write(IReadOnlyDictionary<string, string> matches)
    {
        if (matches.Count == 0) return "";
        var sb = new StringBuilder("[" + SectionName + "]\n");
        foreach (var (path, key) in matches.OrderBy(kv => kv.Key, StringComparer.Ordinal))
            sb.Append(path).Append('=').Append(key).Append('\n');
        return sb.ToString();
    }
}
