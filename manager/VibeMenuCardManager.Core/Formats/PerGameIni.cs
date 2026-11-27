// Aether Card Manager, GPL-3.0-or-later.
// PERGAME.INI (contract/SPEC.md §5.14): the remembered per-game choices, one [<game id>] section per game. A port of
// tools/omsat/pergame.py (parse/get/put/delete/dump), which mirrors the menu's own writer (menu/src/pergame.c): every
// line a change does not touch is kept byte-for-byte, a key at its default is never written, and a section with no
// managed keys is still kept (the game is "remembered").
using System.Text;
using System.Text.RegularExpressions;

namespace VibeMenuCardManager.Core.Formats;

public sealed class PerGameEntry
{
    public required string Id;
    public string Title = "";
    public string Loader = "auto";    // auto | cwx | jhl
    public string Saves = "default";  // default | on | off
    public string Slot = "0";         // 0-9 | ask
    public string Cheats = "default";
    public string Igr = "default";

    internal (string, string, string, string, string, string) Values() => (Title, Loader, Saves, Slot, Cheats, Igr);
}

public static partial class PerGameIni
{
    public const int MaxBytes = 16_384, MaxSections = 512, MaxLine = 255, TitleMax = 40;
    static readonly string[] Keys = ["title", "loader", "saves", "slot", "cheats", "igr"];
    static readonly Dictionary<string, string[]> Values = new()
    {
        ["loader"] = ["auto", "cwx", "jhl"], ["saves"] = ["default", "on", "off"], ["cheats"] = ["default", "on", "off"],
        ["igr"] = ["default", "on", "off"], ["slot"] = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "ask"],
    };

    [GeneratedRegex("^[A-Z0-9_-]{1,10}@[A-Z]{0,8}$")] private static partial Regex IdRe();

    public static bool IsId(string id) => IdRe().IsMatch(id);

    static List<string> Lines(string text) => Regex.Matches(text, "[^\n]*\n|[^\n]+$").Select(m => m.Value).ToList();

    static string Content(string raw)
    {
        string s = raw.EndsWith('\n') ? raw[..^1] : raw;
        return s.EndsWith('\r') ? s[..^1] : s;
    }

    static string Term(string raw) => raw.EndsWith("\r\n") ? "\r\n" : raw.EndsWith('\n') ? "\n" : "";

    enum Kind { Blank, Head, Key, Other }

    static (Kind, string?, string?) Classify(string raw)
    {
        string s = Content(raw).Trim(' ', '\t');
        if (s.Length == 0) return (Kind.Blank, null, null);
        if (s[0] == '[')
        {
            string inner = s[1..];
            int b = inner.IndexOf(']');
            return (Kind.Head, (b >= 0 ? inner[..b] : inner).Trim(' ', '\t'), null);
        }
        if (s[0] != '#' && s.Contains('='))
        {
            int eq = s.IndexOf('=');
            return (Kind.Key, s[..eq].Trim(' ', '\t'), s[(eq + 1)..].Trim(' ', '\t'));
        }
        return (Kind.Other, null, null);
    }

    static void Set(PerGameEntry g, string key, string val)
    {
        if (key == "title") { g.Title = val.Length > TitleMax ? val[..TitleMax] : val; return; }
        string v = val.ToLowerInvariant();
        if (Values[key].Contains(v)) switch (key)
        {
            case "loader": g.Loader = v; break;
            case "saves": g.Saves = v; break;
            case "slot": g.Slot = v; break;
            case "cheats": g.Cheats = v; break;
            case "igr": g.Igr = v; break;
        }
    }

    public static List<PerGameEntry> Parse(string text)
    {
        var games = new List<PerGameEntry>();
        PerGameEntry? cur = null;
        var seen = new HashSet<string>();
        foreach (var raw in Lines(text))
        {
            var (kind, a, b) = Classify(raw);
            if (kind == Kind.Head) { cur = new PerGameEntry { Id = a! }; seen = []; games.Add(cur); }
            if (cur != null && kind == Kind.Key && a != null && Keys.Contains(a) && seen.Add(a)) Set(cur, a, b!);
        }
        return games;
    }

    public static PerGameEntry? Get(string text, string id) => Parse(text).FirstOrDefault(g => g.Id == id);

    static string? Value(PerGameEntry g, string key)
    {
        string v = key switch { "title" => g.Title, "loader" => g.Loader, "saves" => g.Saves, "slot" => g.Slot, "cheats" => g.Cheats, "igr" => g.Igr, _ => "" };
        string def = key switch { "title" => "", "loader" => "auto", "saves" => "default", "slot" => "0", "cheats" => "default", "igr" => "default", _ => "" };
        if (key == "title") return v.Length == 0 ? null : (v.Length > TitleMax ? v[..TitleMax] : v);
        return v == def ? null : v;
    }

    /// <summary>om_pergame_put: change g's first [id] section in place (or append it). The whole file is rebuilt in a
    /// new string; callers enforce MaxBytes/MaxSections before writing.</summary>
    public static string Put(string text, PerGameEntry g)
    {
        if (!IsId(g.Id)) throw new ArgumentException($"not a game id: {g.Id}");
        var outp = new StringBuilder();
        bool found = false, inside = false;
        string eol = "\n";
        var written = new HashSet<string>();
        var blanks = new List<string>();
        int sections = 0;

        void Leave()
        {
            foreach (var k in Keys)
            {
                if (written.Contains(k)) continue;
                var v = Value(g, k);
                if (v == null) continue;
                if (outp.Length > 0 && outp[^1] != '\n') outp.Append(eol);
                outp.Append(k).Append('=').Append(v).Append(eol);
            }
            foreach (var bl in blanks) outp.Append(bl);
            blanks.Clear();
        }

        foreach (var raw in Lines(text))
        {
            var (kind, a, b) = Classify(raw);
            if (inside && kind == Kind.Head) { Leave(); inside = false; }
            if (kind == Kind.Head)
            {
                sections++;
                if (!found && a == g.Id) { found = inside = true; eol = raw.EndsWith("\r\n") ? "\r\n" : "\n"; }
            }
            else if (inside)
            {
                if (kind == Kind.Blank) { blanks.Add(raw); continue; }
                outp.Append(string.Concat(blanks)); blanks.Clear();
                if (kind == Kind.Key && a != null && Keys.Contains(a))
                {
                    var v = Value(g, a);
                    if (written.Add(a) && v != null) outp.Append(a).Append('=').Append(v).Append(Term(raw));
                    continue;
                }
            }
            outp.Append(raw);
        }
        if (inside) Leave();
        if (!found)
        {
            if (sections >= MaxSections) throw new ArgumentException("more than 512 sections");
            if (outp.Length > 0 && outp[^1] != '\n') outp.Append('\n');
            outp.Append('[').Append(g.Id).Append("]\n");
            foreach (var k in Keys)
                if (Value(g, k) is string v) outp.Append(k).Append('=').Append(v).Append('\n');
        }
        return outp.ToString();
    }

    /// <summary>om_pergame_del: drop every [id] section (its header through the line before the next '[').</summary>
    public static string Delete(string text, string id)
    {
        var outp = new StringBuilder();
        bool inside = false;
        foreach (var raw in Lines(text))
        {
            var (kind, a, _) = Classify(raw);
            if (kind == Kind.Head) inside = a == id;
            if (!inside) outp.Append(raw);
        }
        return outp.ToString();
    }

    /// <summary>original with `games`' values applied: a game not in the list is deleted, a changed one rewritten in
    /// place, a new one appended, in list order; a game whose values are unchanged keeps its original bytes.</summary>
    public static string Dump(IReadOnlyList<PerGameEntry> games, string original)
    {
        var old = new Dictionary<string, PerGameEntry>();
        foreach (var g in Parse(original)) old[g.Id] = g;             // the first copy wins, as the menu does
        var keep = games.Select(g => g.Id).ToHashSet();
        string text = original;
        foreach (var id in old.Keys.ToList())
            if (!keep.Contains(id)) text = Delete(text, id);
        foreach (var g in games)
            if (!old.TryGetValue(g.Id, out var o) || !o.Values().Equals(g.Values()))
                text = Put(text, g);
        return text;
    }
}
