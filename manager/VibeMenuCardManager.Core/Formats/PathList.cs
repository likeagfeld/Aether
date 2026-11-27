// Aether Card Manager, GPL-3.0-or-later.
// FAVS.TXT / RECENT.TXT (contract/SPEC.md §5.5): one LIBRARY.IDX path per line, no leading '/'. A port of
// menu/src/plist.c's om_plist_parse / om_plist_emit: blank lines and lines starting with '#' are ignored, a path not
// on the card (not in `known`) is dropped and counted, duplicates collapse to the first occurrence, and the kept list
// is capped. The menu writes LF only, one path then '\n', nothing else.
namespace VibeMenuCardManager.Core.Formats;

public sealed record PathListResult(List<string> Paths, int Dropped);

public static class PathList
{
    public const int FavsMax = 256, RecentMax = 20;

    /// <summary>om_plist_parse: lines not found in `known` (when given) are dropped and counted; a duplicate or a list
    /// already at `cap` is also dropped. `known` null skips the "on the card" check (callers that have not scanned yet).</summary>
    public static PathListResult Parse(string text, int cap, IReadOnlySet<string>? known = null)
    {
        var kept = new List<string>();
        var seen = new HashSet<string>();
        int dropped = 0;
        foreach (var raw in text.Replace("\r\n", "\n").Split('\n'))
        {
            string line = raw.TrimEnd('\r');
            if (line.Length == 0 || line[0] == '#') continue;
            if ((known != null && !known.Contains(line)) || seen.Contains(line) || kept.Count >= cap) { dropped++; continue; }
            seen.Add(line);
            kept.Add(line);
        }
        return new PathListResult(kept, dropped);
    }

    /// <summary>om_plist_emit: each path then '\n', nothing else; truncated to `cap` entries.</summary>
    public static byte[] Write(IEnumerable<string> paths, int cap) =>
        System.Text.Encoding.UTF8.GetBytes(string.Concat(paths.Take(cap).Select(p => p + "\n")));
}
