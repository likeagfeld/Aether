// Aether Card Manager, GPL-3.0-or-later.
// Rebuilding an existing Aether card's own GAMES.DB, LIBRARY.IDX and ART.PAK after the Card Manager's staged edits
// are applied to the filesystem (renames, moves, new folders, added or removed games, BOX.TGA changes): there is no
// release folder here, only the card's own files, so this is not CardBuilder.Build's job (that needs
// jp_titles.json and the compat JSONs). Instead:
// - GAMES.DB: the card's own copy, with FillDates for any newly-seen System ID dates; compat fields are left as they
//   were (no loader_compat.json on a card).
// - LIBRARY.IDX: CardBuilder's own scan (ground truth for keys, areas, discs and BOX.TGA presence after the edits),
//   with each path's title override taken from the caller (the staged renames, plus whatever survived from before).
// - ART.PAK: CardBuilder's own pass (BOX.TGA and folder art only -- no covers zip, no network, no font) gives the
//   fresh entries; any entry missing a hero or a JP-title bitmap is filled in from the card's own previous ART.PAK
//   (same key and disc), so a cover or JP title baked in by an earlier build or a Cover Project pass is not lost
//   just because this save did not touch that game.
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public sealed record RebuildResult(byte[] GamesDb, byte[] LibraryIdx, byte[] ArtPak, ScanResult Scan, Formats.GamesDbData Db);

public static class CardRebuilder
{
    /// <summary>Scans the card as it stands, applies `titleOverrides` (SD path -> override text, "" = none) to the
    /// scan's entries, merges the freshly built ART.PAK forward from `oldArtPak` (null: no previous pack, or it did
    /// not parse), and -- coordinator feature round, metadata editing -- re-applies `userEdits` (USEREDIT.INI,
    /// title/genre/year/region/players) onto the GAMES.DB record fields, so an edit survives both a rescan and a
    /// GAMES.DB replaced wholesale by a newer release, and the menu (which only reads GAMES.DB) shows it too.
    /// `oldArtPak` and the card's current GAMES.DB are read by the caller so this stays testable on an in-memory
    /// tree as well as a real folder.</summary>
    public static RebuildResult Run(string card, uint date, byte[] currentGamesDb, byte[]? oldArtPak,
                                    IReadOnlyDictionary<string, string> titleOverrides,
                                    IReadOnlyDictionary<string, UserEditRecord>? userEdits = null,
                                    IReadOnlyDictionary<string, string>? matchOverrides = null, Art.CoverPackData? coverPack = null)
    {
        // a game newly matched by this save (Match to game, a new folder) gets the bundled pack's cover too, as Prepare
        // gives one; covers baked in earlier are merged forward below either way
        var build = CardBuilder.Build(card, date, new BuildOptions(currentGamesDb, new Dictionary<string, string>(),
            CoversZip: null, FontPath: null, Remote: null, FolderArt: null, ArtPrefer: null, Compat: false, ForcedKeys: matchOverrides,
            CoverPack: coverPack));

        byte[] gamesDb = build.GamesDb;
        if (userEdits is { Count: > 0 })
        {
            var gdb = GamesDb.Read(gamesDb);
            var genres = Formats.UserEdits.ApplyAll(gdb.Records, gdb.Genres, userEdits);
            gamesDb = GamesDb.Write(gdb.Records, genres, gdb.Aliases, gdb.BuildDate);
        }

        var lib = LibraryIndex.Read(build.LibraryIdx);
        var entries = lib.Entries.Select(e =>
        {
            string t = titleOverrides.TryGetValue(e.Path, out var v) ? v : e.TitleOverride;
            return t == e.TitleOverride ? e : e with { TitleOverride = t };
        }).ToList();
        byte[] newLib = LibraryIndex.Write(entries, lib.Folders, lib.Fingerprint, date);

        byte[] newArt = oldArtPak == null ? build.ArtPak : MergeArtForward(oldArtPak, build.ArtPak, date);

        var oldDb = GamesDb.Read(currentGamesDb);
        var scan = CardScanner.Scan(new FsSource(card), oldDb.Records.Select(r => r.Key), oldDb.Aliases, date, forcedKeys: matchOverrides);
        return new RebuildResult(gamesDb, newLib, newArt, scan, GamesDb.Read(gamesDb));
    }

    /// <summary>Entries from `newPak` that have no hero cover or no JP-title bitmap get the matching (key, disc)
    /// entry's blob from `oldPak`, when it has one; everything else in `newPak` is kept as built.</summary>
    public static byte[] MergeArtForward(byte[] oldPak, byte[] newPak, uint date)
    {
        ArtPakData nd, od;
        try { nd = ArtPak.Read(newPak); } catch (Formats.FormatException) { return newPak; }
        try { od = ArtPak.Read(oldPak); } catch (Formats.FormatException) { return newPak; }
        var oldByKey = od.Entries.ToDictionary(e => (e.Key, e.Disc));
        var merged = new List<ArtEntry>();
        foreach (var e in nd.Entries)
        {
            bool square = (e.Flags & ArtPak.FSquare) != 0;
            byte[]? hero = nd.Blob(e, "hero");
            byte[]? thumb = nd.Blob(e, "thumb");
            ArtJpTitle? jp = (e.Flags & ArtPak.FJp) != 0 ? new ArtJpTitle(e.JpW, e.JpH, nd.Blob(e, "jp")!) : null;
            if ((hero == null || jp == null) && oldByKey.TryGetValue((e.Key, e.Disc), out var oe))
            {
                if (hero == null && od.Blob(oe, "hero") is byte[] oh) { hero = oh; square = (oe.Flags & ArtPak.FSquare) != 0; }
                if (thumb == null) thumb = od.Blob(oe, "thumb");
                if (jp == null && (oe.Flags & ArtPak.FJp) != 0 && od.Blob(oe, "jp") is byte[] oj) jp = new ArtJpTitle(oe.JpW, oe.JpH, oj);
            }
            merged.Add(new ArtEntry(e.Key, e.Disc, square, thumb, hero, jp, e.ManifestRow));
        }
        return ArtPak.Write(merged, date);
    }
}
