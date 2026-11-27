// Aether Card Manager, GPL-3.0-or-later.
// Kai's and Rings' save-folder names (contract/SPEC.md §5.17 Import). Kai: /SAVES/BRAM/<product>[<areas>]/ (cafe-alpha,
// Pseudo Saturn Kai pseudo_bram.c, GPL-2.0). Rings: /satiator-saves/<id>/ and /satiator-saves/[SLOTnn]/<id>/ (retrohead,
// Satiator Rings save_functions.c getPathForGameId). A port of menu/launcher/pgsbak.c om_pgs_imp_folder and
// om_pgs_rings_slot; the name is handled as its bytes, as on the console.
using System.Text;

namespace VibeMenuCardManager.Core.Saves;

public static class ImportFolders
{
    static bool PChar(byte c) => c is >= (byte)'A' and <= (byte)'Z' or >= (byte)'0' and <= (byte)'9' or (byte)'_' or (byte)'-';

    /// <summary>A Kai or Rings folder name -> Aether's: Kai's sanitizing (pseudo_bram.c:199-252: a-z upper-cased, any
    /// byte outside 0-9 A-Z _ - becomes '_', trailing spaces trimmed), "&lt;product&gt;" or
    /// "&lt;product&gt;[&lt;A-Z&gt;{1,8}]", product 1..10 bytes. null for anything else, and for a name starting '_' or '['.</summary>
    public static string? VibeFolder(string name)
    {
        byte[] s = Encoding.UTF8.GetBytes(name);
        if (s.Length == 0 || s[0] == (byte)'_' || s[0] == (byte)'[') return null;
        var o = new List<byte>();
        int i = 0;
        for (; i < s.Length && s[i] != (byte)'['; i++)
        {
            byte c = s[i] is >= (byte)'a' and <= (byte)'z' ? (byte)(s[i] - 32) : s[i];
            if (o.Count >= GameId.Product) return null;
            o.Add(PChar(c) ? c : (byte)'_');
        }
        int n = o.Count;
        while (n > 0 && o[n - 1] == (byte)'_' && s[n - 1] == (byte)' ') n--;    // Kai trims trailing spaces first
        if (n == 0) return null;
        o.RemoveRange(n, o.Count - n);
        if (i < s.Length)                                                          // s[i] == '['
        {
            o.Add((byte)'[');
            int a = 0;
            for (i++; i < s.Length && s[i] is >= (byte)'A' and <= (byte)'Z' && a < GameId.Areas; i++, a++) o.Add(s[i]);
            if (a == 0 || i >= s.Length || s[i] != (byte)']' || i + 1 != s.Length) return null;
            o.Add((byte)']');
        }
        return Encoding.ASCII.GetString(o.ToArray());
    }

    /// <summary>om_pgs_rings_slot: "[SLOTnn]" -> nn; 0 when not one.</summary>
    public static int RingsSlot(string n) =>
        n.Length == 8 && n.StartsWith("[SLOT", StringComparison.Ordinal) && char.IsAsciiDigit(n[5]) && char.IsAsciiDigit(n[6])
        && n[7] == ']' ? (n[5] - '0') * 10 + (n[6] - '0') : 0;
}
