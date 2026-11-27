// Aether Card Manager, GPL-3.0-or-later.
// Save-folder naming rules of contract/SPEC.md §5.17 ("Folders", R4 reserved records). A port of the parts of
// menu/src/pgsplan.c the PC needs: om_pgs_reserved, om_pgs_file_name, om_pgs_slot_dir_po, om_pgs_product_dir.
namespace VibeMenuCardManager.Core.Saves;

public static class SaveNames
{
    /// <summary>Aether's BRAM marker record (SPEC §5.17 "The BRAM marker").</summary>
    public const string Marker = "VIBEMENU_PG";

    /// <summary>R4 (om_pgs_reserved): the marker, PSEUDO_* (Pseudo Saturn Kai), _CARTDMP* and SAVRING* (Satiator Rings).
    /// The names of Kai's pbram_is_reserved_save (cafe-alpha pseudo_bram.c:127-161; unverified, as SPEC §5.17 R4 reads
    /// it).</summary>
    public static bool Reserved(ReadOnlySpan<byte> name12)
    {
        Span<byte> a = stackalloc byte[12];
        if (!BupFile.NameNorm(name12, a)) return false;
        return a.SequenceEqual("VIBEMENU_PG\0"u8) || a.StartsWith("PSEUDO_"u8) || a.StartsWith("_CARTDMP"u8)
            || a.StartsWith("SAVRING"u8);
    }

    /// <summary>om_pgs_file_name: the record name with every byte outside A-Z 0-9 _ - replaced by '_', then ".BUP"; a
    /// collision with taken (ASCII case-insensitive) gets ~1..~9 before the extension. null: a bad name, or all ten taken.</summary>
    public static string? FileName(ReadOnlySpan<byte> name12, IReadOnlyCollection<string> taken)
    {
        Span<byte> nm = stackalloc byte[12];
        if (!BupFile.NameNorm(name12, nm)) return null;
        var b = new System.Text.StringBuilder();
        for (int n = 0; n < 11 && nm[n] != 0; n++)
        {
            byte c = nm[n];
            b.Append(c is >= (byte)'A' and <= (byte)'Z' or >= (byte)'0' and <= (byte)'9' or (byte)'_' or (byte)'-' ? (char)c : '_');
        }
        string stem = b.ToString();
        for (int i = 0; i <= 9; i++)
        {
            string f = stem + (i > 0 ? "~" + i.ToString(System.Globalization.CultureInfo.InvariantCulture) : "") + ".BUP";
            if (!taken.Any(t => Be.AsciiIEq(f, t))) return f;
        }
        return null;
    }

    /// <summary>om_pgs_slot_dir_po: "SAVES/&lt;folder&gt;" for slot 0, "SAVES/&lt;folder&gt;/0n" for 1..9; po = the
    /// product-only folder (the areas dropped). null when gid is not an id or slot &gt; 9.</summary>
    public static string? SlotDir(string gid, int slot, bool productOnly = false)
    {
        if (slot is < 0 or > 9) return null;
        string? f = GameId.Dir(gid);
        if (f == null) return null;
        if (productOnly && f.IndexOf('[') is int b and >= 0) f = f[..b];
        return "SAVES/" + f + (slot > 0 ? "/0" + slot.ToString(System.Globalization.CultureInfo.InvariantCulture) : "");
    }

    /// <summary>om_pgs_product_dir: "SAVES/&lt;product&gt;" for an id with areas; null otherwise.</summary>
    public static string? ProductDir(string gid)
    {
        string? f = GameId.Dir(gid);
        return f == null || !f.Contains('[') ? null : SlotDir(gid, 0, productOnly: true);
    }
}
