// Aether Card Manager, GPL-3.0-or-later.
// USEREDIT.INI (metadata editing): the Card Manager's own user overrides for a
// GAMES.DB record's title, genre, year, region and players, keyed by the GAMES.DB key. Not part of contract/SPEC.md
// (proposed here, smallest file that does the job, ateam-style "user edits" overlay, since no such contract exists
// yet): the menu never reads this file. It survives a rescan (the Card Manager reads it back) and a GAMES.DB
// replacement from a newer release (Build card/Installer only write the release's named files, never this one), and
// is re-applied onto GAMES.DB's own record fields at every Save changes / Build card (CardRebuilder), so the menu --
// which only ever reads GAMES.DB -- shows the edited values too. Plain text, written in full each time (no other
// writer to stay byte-compatible with): one `[<key>]` section per edited game, only the fields actually overridden.
using System.Text;

namespace VibeMenuCardManager.Core.Formats;

public sealed class UserEditRecord
{
    public string Title = "";
    public string Genre = "";
    public int Year;
    public string Region = "";      // a subset of "JTUEB"; "" = no override
    public int Players;
    /// <summary>"Add to database": a record of the user's own, created in GAMES.DB when the key has none (a disc the
    /// database does not know, or homebrew under a synthetic key, UserRecords.KeyFor). Written "new=1".</summary>
    public bool New;

    public bool IsEmpty => Title == "" && Genre == "" && Year == 0 && Region == "" && Players == 0 && !New;

    public UserEditRecord Clone() => new() { Title = Title, Genre = Genre, Year = Year, Region = Region, Players = Players, New = New };

    public bool SameAs(UserEditRecord other) =>
        Title == other.Title && Genre == other.Genre && Year == other.Year && Region == other.Region && Players == other.Players && New == other.New;
}

public static class UserEdits
{
    public const int MaxBytes = 16_384;
    /// <summary>The whole file (this section plus MatchOverrides' [MATCH] section): menu never reads it, so this is
    /// generous -- about 2,700 "path=key" match-override lines' worth.</summary>
    public const int CombinedMaxBytes = 65_536;

    public static Dictionary<string, UserEditRecord> Parse(string text)
    {
        var o = new Dictionary<string, UserEditRecord>();
        UserEditRecord? cur = null;
        foreach (var raw in text.Replace("\r\n", "\n").Split('\n'))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#') continue;
            if (line[0] == '[' && line.EndsWith(']'))
            {
                string key = line[1..^1].Trim();
                if (key == "" || key == MatchOverrides.SectionName) { cur = null; continue; }   // [MATCH] is reserved
                cur = o.TryGetValue(key, out var existing) ? existing : o[key] = new UserEditRecord();
                continue;
            }
            if (cur == null) continue;
            int eq = line.IndexOf('=');
            if (eq < 0) continue;
            string k = line[..eq].Trim().ToLowerInvariant(), v = line[(eq + 1)..].Trim();
            switch (k)
            {
                case "title": cur.Title = v; break;
                case "genre": cur.Genre = v; break;
                case "year": if (int.TryParse(v, out int y)) cur.Year = y; break;
                case "region": cur.Region = v; break;
                case "players": if (int.TryParse(v, out int p)) cur.Players = p; break;
                case "new": cur.New = v == "1"; break;
            }
        }
        return o;
    }

    public static string Write(IReadOnlyDictionary<string, UserEditRecord> edits)
    {
        var sb = new StringBuilder("# /aether/USEREDIT.INI -- Aether Card Manager's user metadata overrides.\n" +
                                   "# Not part of contract/SPEC.md: the menu never reads this file. Applied onto GAMES.DB\n" +
                                   "# (title, genre, year, region, players) at every Save changes / Build card.\n");
        foreach (var (key, e) in edits.OrderBy(kv => kv.Key, StringComparer.Ordinal))
        {
            if (e.IsEmpty) continue;
            sb.Append('[').Append(key).Append("]\n");
            if (e.Title != "") sb.Append("title=").Append(e.Title).Append('\n');
            if (e.Genre != "") sb.Append("genre=").Append(e.Genre).Append('\n');
            if (e.Year != 0) sb.Append("year=").Append(e.Year.ToString(System.Globalization.CultureInfo.InvariantCulture)).Append('\n');
            if (e.Region != "") sb.Append("region=").Append(e.Region).Append('\n');
            if (e.Players != 0) sb.Append("players=").Append(e.Players.ToString(System.Globalization.CultureInfo.InvariantCulture)).Append('\n');
            if (e.New) sb.Append("new=1\n");
        }
        return sb.ToString();
    }

    /// <summary>Apply, after adding a GAMES.DB record for every user record ("new=1", with a title) whose key the
    /// database lacks -- so a user record survives a GAMES.DB replaced by a newer release, and once a release does know
    /// the key, the user's fields simply override its record.</summary>
    public static List<string> ApplyAll(List<GameRecord> records, IReadOnlyList<string> genres, IReadOnlyDictionary<string, UserEditRecord> edits)
    {
        var have = records.Select(r => r.Key).ToHashSet();
        foreach (var (key, e) in edits.OrderBy(kv => kv.Key, StringComparer.Ordinal))
            if (e.New && e.Title != "" && !have.Contains(key)) records.Add(new GameRecord { Key = key, TitleEn = e.Title });
        return Apply(records, genres, edits);
    }

    /// <summary>Overrides each edited record's non-empty fields in place; a genre name not already in `genres` is
    /// appended. Returns the (possibly grown) genre list -- callers pass it to GamesDb.Write.</summary>
    public static List<string> Apply(IEnumerable<GameRecord> records, IReadOnlyList<string> genres, IReadOnlyDictionary<string, UserEditRecord> edits)
    {
        var genreList = new List<string>(genres);
        int GenreIndex(string name)
        {
            int i = genreList.FindIndex(g => string.Equals(g, name, StringComparison.OrdinalIgnoreCase));
            if (i >= 0) return i;
            genreList.Add(name);
            return genreList.Count - 1;
        }
        foreach (var r in records)
        {
            if (!edits.TryGetValue(r.Key, out var e) || e.IsEmpty) { r.IsUserTitle = false; continue; }
            // GAMES.DB byte 0x35 bit0: set when this record's TitleEn is the user's own
            // title, cleared otherwise -- the display-name rule falls back to it before the file name.
            if (e.Title != "") { r.TitleEn = e.Title; r.TitleRom = e.Title; r.IsUserTitle = true; }
            else r.IsUserTitle = false;
            if (e.Genre != "") r.Genre = GenreIndex(e.Genre);
            if (e.Year != 0) r.Year = e.Year;
            if (e.Region != "") r.RegionFlags = GamesDb.RegionFlagsFrom(e.Region);
            if (e.Players != 0) r.Players = e.Players;
        }
        return genreList;
    }
}
