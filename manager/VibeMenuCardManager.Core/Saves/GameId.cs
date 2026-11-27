// Aether Card Manager, GPL-3.0-or-later.
// The game id (contract/SPEC.md §5.14): IP.BIN product number (10 bytes at 0x20) + area symbols (10 bytes at 0x40; per
// ST-040-R4 §4.2, via SPEC §5.14) -> "MK-81214@UT" (PERGAME.INI section) and "MK-81214[UT]" (the SAVES folder). The rule
// is Pseudo Saturn Kai's save-folder rule (cafe-alpha, pseudo_bram.c, GPL-2.0), so Kai's per-game folders carry over.
// A port of menu/src/gameid.c; tools/omsat/pergame.py game_id/dir_name implement the same rule.
namespace VibeMenuCardManager.Core.Saves;

public static class GameId
{
    public const int Product = 10, Areas = 8;

    static bool IdChar(byte c) => c is >= (byte)'A' and <= (byte)'Z' or >= (byte)'0' and <= (byte)'9' or (byte)'-' or (byte)'_';

    /// <summary>om_gameid_ini: null for an empty product (Kai would use "_UNKNOWN").</summary>
    public static string? Ini(ReadOnlySpan<byte> product10, ReadOnlySpan<byte> areas10)
    {
        var o = new System.Text.StringBuilder();
        for (int n = 0; n < Product && n < product10.Length && product10[n] > (byte)' '; n++)
        {
            byte c = product10[n];
            if (c is >= (byte)'a' and <= (byte)'z') c -= 32;
            o.Append(IdChar(c) ? (char)c : '_');
        }
        if (o.Length == 0) return null;
        o.Append('@');
        for (int i = 0, k = 0; i < 10 && i < areas10.Length && areas10[i] != 0 && k < Areas; i++)
            if (areas10[i] is >= (byte)'A' and <= (byte)'Z') { o.Append((char)areas10[i]); k++; }
        return o.ToString();
    }

    /// <summary>om_gameid_check: "&lt;[A-Z0-9_-]{1,10}&gt;@&lt;[A-Z]{0,8}&gt;".</summary>
    public static bool Check(string id)
    {
        int p = 0, k = 0;
        while (p < id.Length && IdChar((byte)id[p]) && id[p] < 128) p++;
        if (p == 0 || p > Product || p >= id.Length || id[p] != '@') return false;
        while (p + 1 + k < id.Length && id[p + 1 + k] is >= 'A' and <= 'Z') k++;
        return k <= Areas && p + 1 + k == id.Length;
    }

    /// <summary>om_gameid_dir: "MK-81214@UT" -> "MK-81214[UT]", "MK-81214@" -> "MK-81214"; null when not an id.</summary>
    public static string? Dir(string id)
    {
        if (!Check(id)) return null;
        int at = id.IndexOf('@');
        return at == id.Length - 1 ? id[..at] : id[..at] + "[" + id[(at + 1)..] + "]";
    }
}
