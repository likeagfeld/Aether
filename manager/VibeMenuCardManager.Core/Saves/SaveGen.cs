// Aether Card Manager, GPL-3.0-or-later.
// The reader rule of the add-only save folders (contract/SPEC.md §5.17): a port of menu/src/savegen.c
// (BSD-3-Clause). A slot folder D keeps each archived set in a generation folder D/G<sss>/G<nnnnn> (n = 1..99999, sss =
// n / 100); a generation is complete once it holds DONE.TXT. D's CURRENT set is its highest-numbered complete
// generation; with none, D itself. Sub-folders 01..09 and CART are never part of a set.
namespace VibeMenuCardManager.Core.Saves;

public static class SaveGen
{
    public const string Done = "DONE.TXT";

    static string D(uint v, int digits) => v.ToString(new string('0', digits), System.Globalization.CultureInfo.InvariantCulture);

    /// <summary>om_gen_shard: d + "/G&lt;g / 100&gt;".</summary>
    public static string Shard(string d, uint g) => d + "/G" + D(g / 100, 3);

    /// <summary>om_gen_dir: d + "/G&lt;sss&gt;/G&lt;nnnnn&gt;".</summary>
    public static string Dir(string d, uint g) => Shard(d, g) + "/G" + D(g, 5);

    /// <summary>om_gen_num: "G" (or "g") + exactly `digits` digits -> the number, else -1.</summary>
    public static int Num(string name, int digits)
    {
        if (name.Length == 0 || (name[0] != 'G' && name[0] != 'g')) return -1;
        int v = 0;
        for (int k = 1; k <= digits; k++)
        {
            if (k >= name.Length || name[k] < '0' || name[k] > '9') return -1;
            v = v * 10 + (name[k] - '0');
        }
        return name.Length == digits + 1 ? v : -1;
    }

    /// <summary>scan: d's folders "G" + digits; the highest below lim (-1 for none); digits 5: the generations of shard
    /// sh (n / 100 == sh, n != 0) into bits.</summary>
    static int Scan(CardFs fs, string d, int digits, int lim, uint sh, bool[]? bits)
    {
        int max = -1;
        foreach (var e in fs.List(d))
        {
            int v;
            if (!e.IsDir || (v = Num(e.Name, digits)) < 0) continue;
            if (digits == 5)
            {
                if ((uint)v / 100 != sh || v == 0) continue;
                bits![v % 100] = true;
            }
            if (v < lim && v > max) max = v;
        }
        return max;
    }

    /// <summary>om_gen_cur: d's current set -- its folder and generation number (0: d itself, no complete
    /// generation). An absent d is current at d.</summary>
    public static (string Path, uint Gen) Cur(CardFs fs, string d)
    {
        int lim = 1000;
        for (; ; )
        {
            int sh = Scan(fs, d, 3, lim, 0, null);
            if (sh < 0) break;
            var bits = new bool[100];
            Scan(fs, Shard(d, (uint)sh * 100), 5, 100000, (uint)sh, bits);
            for (int k = 99; k >= 0; k--)
            {
                if (!bits[k]) continue;
                uint n = (uint)sh * 100 + (uint)k;
                string g = Dir(d, n);
                if (!fs.Absent(g + "/" + Done)) return (g, n);
            }
            lim = sh;                                     // no complete generation in this shard: the one below
        }
        return (d, 0);
    }
}
