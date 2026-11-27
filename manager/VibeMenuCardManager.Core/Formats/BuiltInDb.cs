// Aether Card Manager, GPL-3.0-or-later.
// The Card Manager's own bundled copy of GAMES.DB (build/db/GAMES.DB, built by tools/omsat), embedded as a resource
// (VibeMenuCardManager.Core.csproj) so "Match to game..." can search a full product-code/title database even for a
// card whose own GAMES.DB is small, stale, or missing. Embedding is conditional on the build-output file existing
// (never committed: it is a build artifact, not source) -- a dev build without build/db/GAMES.DB simply has no
// built-in DB, same as a packaged build before packaging ships it.
using System.Reflection;

namespace VibeMenuCardManager.Core.Formats;

public static class BuiltInDb
{
    public static byte[]? Bytes()
    {
        using var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("GAMES.DB");
        if (s == null) return null;
        using var m = new MemoryStream();
        s.CopyTo(m);
        return m.ToArray();
    }

    /// <summary>null when the resource was not embedded (build/db/GAMES.DB absent at build time) or does not parse.</summary>
    public static GamesDbData? Load()
    {
        if (Bytes() is not byte[] b) return null;
        try { return GamesDb.Read(b); }
        catch (FormatException) { return null; }
    }
}
