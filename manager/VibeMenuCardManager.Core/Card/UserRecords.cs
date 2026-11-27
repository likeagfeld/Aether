// Aether Card Manager, GPL-3.0-or-later.
// "Add to database…": a GAMES.DB record of the user's own for a disc the database does not know. It lives in
// USEREDIT.INI (a [<key>] section with new=1, plus the disc's path=key line in [MATCH]), so it survives a rescan and a
// GAMES.DB replaced by a newer release; every Save changes / Prepare writes it into GAMES.DB with the user-title flag
// (byte 0x35 bit0) and gives the disc that key in LIBRARY.IDX, which is the key the menu looks the game up by
// (menu/src/gamesdb.h om_gamesdb_find) before showing its title (menu/src/title.c, OM_GU_TITLE).
using System.Text;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Match;

namespace VibeMenuCardManager.Core.Card;

public static class UserRecords
{
    /// <summary>A product code that names no one game: blank, 4 characters or fewer ("T-81"), or letters then only zeros
    /// and at most one last digit ("SG0000000", "CD0000001") -- homebrew leaves these, and two discs share them.</summary>
    public static bool IsPlaceholder(string normalised) =>
        normalised.Length <= 4 || System.Text.RegularExpressions.Regex.IsMatch(normalised, "^[A-Z-]*0{4,}[0-9]?$");

    static string Norm(GameRow r) => r.Sid != null ? Keys.Normalise(Matcher.Cleanup(r.Sid.Product)) : "";

    /// <summary>The key a new user record for `row` takes: its own product code when that is a real one no other game
    /// on the card shares and GAMES.DB does not already hold; else a synthetic "U" + 8 hex digits from the disc's path
    /// (two homebrew discs sharing a code never collide), never one already taken.</summary>
    public static string KeyFor(CardLoad load, GameRow row)
    {
        string p = Norm(row);
        var taken = load.Db.Records.Select(r => r.Key).Concat(load.UserEdits.Keys)
                        .Concat(load.Rows.Where(r => r != row).Select(r => r.Key)).Where(k => k != "").ToHashSet();
        string title = row.Sid?.Title.Trim() ?? "";
        bool shared = load.Rows.Any(r => r != row && !r.Removed && Norm(r) == p && (r.Sid?.Title.Trim() ?? "") != title);
        if (p != "" && !IsPlaceholder(p) && !shared && !taken.Contains(p)) return p;
        uint h = CommonHeader.Crc32(Encoding.UTF8.GetBytes(row.Path));
        for (int n = 0; ; n++)
        {
            string k = "U" + (h + (uint)n).ToString("X8", System.Globalization.CultureInfo.InvariantCulture);
            if (!taken.Contains(k)) return k;
        }
    }

    /// <summary>Stages a new user record for `row` (title required; genre, year, region, players optional): the
    /// record under KeyFor's key, and the disc forced to that key. Returns the key. The caller stages for undo first.</summary>
    public static string Add(CardLoad load, GameRow row, UserEditRecord fields)
    {
        if (fields.Title.Trim() == "") throw new ArgumentException("a database entry needs a title");
        string key = KeyFor(load, row);
        var e = fields.Clone();
        e.Title = e.Title.Trim();
        e.New = true;
        load.UserEdits[key] = e;
        row.MatchOverride = key;
        return key;
    }
}
