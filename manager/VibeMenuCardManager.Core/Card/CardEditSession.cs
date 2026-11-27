// Aether Card Manager, GPL-3.0-or-later.
// The Card Manager's staged-edit model for an already-built Aether card: every edit (rename, favorite, loader,
// cover, match override, metadata) changes only this in-memory session until Apply() runs as one transaction. Game
// and folder file management (adding, removing, moving, creating folders) stays in Explorer -- same as Satiator
// Rings -- so this never creates, moves or deletes a game file or folder; OpenCard()/Refresh rescans the card so
// anything the user added or removed in Explorer shows up here. Apply() writes BOX.TGA/FOLDER.PNG/FAVS.TXT/
// PERGAME.INI/USEREDIT.INI, then rebuilds GAMES.DB/LIBRARY.IDX/ART.PAK (CardRebuilder) from the card as it now
// stands, so the menu picks up both the staged edits and anything added in Explorer.
using System.Text;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Core.Card;

/// <summary>One LIBRARY.IDX entry (one disc of one game), with its staged edits. `Path` is the SD path as scanned;
/// games are never moved or renamed on disk by this app, so unlike an earlier design this is not updated in-session.</summary>
public sealed class GameRow
{
    public required string Path;
    public required string OriginalPath;
    /// <summary>The key CardScanner's own matcher found (possibly "" = unmatched); never written back. `Key`
    /// (below) is what callers read -- MatchOverride, when staged, wins over this.</summary>
    public string ScanKey = "";
    public int Disc = 1, DiscTotal = 1, AreaFlags = 0, FolderId = 0;
    public bool HasBox;
    /// <summary>A cover renders for this game from some source already on the card -- ART.PAK or BOX.TGA (SPEC §5.6
    /// cover lookup order, steps 1-3); set once at Load(). Build-time-only fallbacks (a covers zip, retrohead,
    /// libretro, the bundled cover pack) are a UI preview convenience and do not count: they are not what the menu
    /// will show without a further staged change.</summary>
    public bool HasArt;
    /// <summary>True when this image is a single-game folder's one image (CardScanner's IsGameDir: no subfolder,
    /// exactly one image after the cue dedupe) -- its own directory is not its LIBRARY.IDX folder row. Used by
    /// DisplayTitle (2026-11-23, Satiator Rings' model, mirrors menu/src/title.c om_entry_name).</summary>
    public bool OwnFolder;
    public SystemId? Sid;
    public string GameIdIni = "";
    public string TitleOverride = "";
    public string OriginalTitleOverride = "";
    public bool Favorite;
    public bool OriginalFavorite;
    public string Loader = "auto";
    public string OriginalLoader = "auto";
    public RgbaImage? PendingCover;
    public bool RestoreCoverStaged;
    public bool Removed;
    public bool IsNew;
    /// <summary>"Match to game..." (M-art-pack): a GAMES.DB key the user chose for this disc, overriding whatever
    /// CardScanner's own matcher found (or found nothing, for an unrecognized game, a translation or homebrew). ""
    /// = no override. Persisted in USEREDIT.INI's [MATCH] section (Formats.MatchOverrides), keyed by SD path since
    /// an unmatched game has no GAMES.DB key to key it by otherwise.</summary>
    public string MatchOverride = "";
    public string OriginalMatchOverride = "";

    public string Folder => Path.LastIndexOf('/') is int i and >= 0 ? Path[..i] : "";
    public string FileName => Path.LastIndexOf('/') is int i and >= 0 ? Path[(i + 1)..] : Path;

    public bool Dirty => !IsNew && !Removed && (TitleOverride != OriginalTitleOverride || Favorite != OriginalFavorite
                         || Loader != OriginalLoader || PendingCover != null || RestoreCoverStaged || MatchOverride != OriginalMatchOverride);

    /// <summary>The key used for GAMES.DB/LIBRARY.IDX/ART.PAK lookups and rebuilds: the staged match override when
    /// one is set, else whatever CardScanner's matcher found.</summary>
    public string Key => MatchOverride != "" ? MatchOverride : ScanKey;

    /// <summary>Display name rule (2026-11-23, Satiator Rings' model, mirrors menu/src/title.c om_entry_name): an
    /// explicit override wins -- an inline/batch rename or "Rename to DB title" (TitleOverride, LIBRARY.IDX, per
    /// entry); else GAMES.DB's own title_en, but only when its 0x35 bit0 says it is the user's own title (set by
    /// UserEdits.Apply, re-applied onto GAMES.DB at every Save changes); else, for a single-game folder's one image
    /// (OwnFolder), that folder's own name; else the image's exact file name, extension included. Never automatic
    /// from a non-user GAMES.DB title or the disc's own System ID title. `userEdits` -- the live, not-yet-saved
    /// session edits (USEREDIT.INI) -- is checked before `db`'s own bit0, so an edit shows immediately, before a
    /// save bakes it into GAMES.DB; optional (null: as if none were set) for callers that have no CardLoad at hand.</summary>
    public string DisplayTitle(IReadOnlyDictionary<string, GameRecord> db, IReadOnlyDictionary<string, Formats.UserEditRecord>? userEdits = null)
    {
        if (TitleOverride != "") return TitleOverride;
        if (userEdits != null && Key != "" && userEdits.TryGetValue(Key, out var ue) && ue.Title != "") return ue.Title;
        if (Key != "" && db.TryGetValue(Key, out var rec) && rec.IsUserTitle && rec.TitleEn != "") return rec.TitleEn;
        if (OwnFolder)
        {
            string dir = Folder;
            int i = dir.LastIndexOf('/');
            return i >= 0 ? dir[(i + 1)..] : dir;
        }
        return FileName;
    }

    public string Code => Sid != null && Sid.Product.Trim() != "" ? Sid.Product.Trim() : Key;

    public string RegionText => Sid != null ? Sid.Area.Trim() : AreaFlags != 0 ? RegionFromFlags(AreaFlags) : "";
    static string RegionFromFlags(int flags)
    {
        var s = new StringBuilder();
        foreach (var (c, bit) in new[] { ('J', 1), ('T', 2), ('U', 4), ('E', 8), ('B', 16) }) if ((flags & bit) != 0) s.Append(c);
        return s.ToString();
    }

    public string DiscText => DiscTotal > 1 ? $"{Disc}/{DiscTotal}" : "1/1";

    /// <summary>"no cover" (nothing in ART.PAK or BOX.TGA, no staged cover) or "header problem" (the image's System ID
    /// did not parse).</summary>
    public string? Problem => !HasArt && PendingCover == null ? "no cover" : Sid == null ? "header problem" : null;
}

/// <summary>A LIBRARY.IDX folder, with its staged folder-art edit (coordinator fix round, "custom artwork for
/// folders"): a game folder's FOLDER.PNG/.JPG/.TGA (SPEC §5.2 Folder art; CardBuilder.FolderArtNames), keyed in
/// ART.PAK by ArtPak.FolderArtKey(Path), disc 0, hero only.</summary>
public sealed class FolderRow
{
    public required string Path;
    public int Parent;
    public bool IsNew;
    /// <summary>A staged "write this as FOLDER.PNG" (replacing any existing FOLDER.*).</summary>
    public Art.RgbaImage? PendingCover;
    /// <summary>A staged "delete the folder's FOLDER.* file" (distinct from a game's Restore cover, which only
    /// discards the staged change: the user asked for Remove folder art to really remove it).</summary>
    public bool RemoveArtStaged;

    public string Name => Path.LastIndexOf('/') is int i and >= 0 ? Path[(i + 1)..] : Path;
    public bool Dirty => !IsNew && (PendingCover != null || RemoveArtStaged);
}

public sealed class CardLoad
{
    public required string Root;
    public byte[] GamesDbBytes = [];
    public GamesDbData Db = new([], [], new Dictionary<string, string>(), 0);
    public byte[]? ArtPak;
    /// <summary>ArtPak parsed once at Load() (null: absent or corrupt), so every row's cover lookup and thumbnail is a
    /// table lookup, not a re-parse.</summary>
    public Formats.ArtPakData? ArtData;
    public List<GameRow> Rows = [];
    public List<FolderRow> Folders = [];
    public string FavsText = "";
    /// <summary>/aether/RECENT.TXT (Formats.PathList, same shape as FAVS.TXT): read-only here -- the menu is the
    /// only writer -- most-recently-played first, for the Card Manager's own "Recently played" view.</summary>
    public List<string> RecentPaths = [];
    public string PerGameText = "";
    /// <summary>Metadata edits (USEREDIT.INI), keyed by GAMES.DB key; UserEdits is live (the details panel edits
    /// here), OriginalUserEdits is the Load()-time snapshot, for the dirty check and the confirmation.</summary>
    public Dictionary<string, Formats.UserEditRecord> UserEdits = new();
    public Dictionary<string, Formats.UserEditRecord> OriginalUserEdits = new();
    public uint Fingerprint;
    /// <summary>Problems view: a .cue's FILE line names a file not present anywhere in its own folder, keyed by the
    /// cue's SD path (CardScanner.Scan's CueMissing).</summary>
    public Dictionary<string, List<string>> CueMissing = new();
    /// <summary>Set when the card has no /aether/GAMES.DB: the window stays read-only (no games to show) until the
    /// card is prepared (Prepare SD Card for Aether).</summary>
    public string? Problem;
    /// <summary>PC backup/restore: the snapshot name a restore was staged from (Backup.CardRestore.Stage), for
    /// Describe()/HasChanges() -- most of a restore merges into the fields above, which already have their own
    /// dirty checks, but this covers a PERGAME.INI-text-only restore that touches no single row's Loader.</summary>
    public string? PendingRestoreFrom;
    /// <summary>PC backup/restore: raw tracked writes a restore staged for files with no staged-edit field of their
    /// own (CHEATS.PAK, the /aether/SAVES tree) -- Apply() writes each one verbatim.</summary>
    public List<(string CardPath, byte[] Data)> PendingRestoreRawFiles = [];
    /// <summary>/aether/CHEATS.PAK (or its .NEW, SPEC §5.15's recovery rule), read-only here: each GAMES.DB key's
    /// count of cheats switched on, for the "N cheats on" pill. Empty when the card has none or it does not read.</summary>
    public Dictionary<string, int> CheatsOn = [];
    /// <summary>Every image path the card's own /aether/LIBRARY.IDX lists, with its disc count and GAMES.DB key, as
    /// read at Load() (null: the file is missing or does not read). CardEditSession.LibraryDrift compares it with the
    /// fresh scan in Rows.</summary>
    public Dictionary<string, (int DiscTotal, string Key)>? LibraryPaths;
}

public sealed class ApplyReport
{
    public readonly List<string> Steps = [];
    public readonly List<string> Stranded = [];
}

/// <summary>A card operation was stopped (cancelled, or the confirmed volume changed under it) after some staged
/// edits had already been applied and then rolled back.</summary>
public sealed class ApplyStoppedException(string message, Exception inner) : IOException(message, inner);

public static class CardEditSession
{
    /// <summary>Ensures `folders` has a FolderRow for every segment of `path` (root "" always exists at index 0),
    /// creating any missing ancestor first. CardScanner gives a tree node to every real directory EXCEPT one that
    /// holds a single game (or a matching multi-disc set) directly -- it folds that into its parent for LIBRARY.IDX
    /// purposes (SPEC v1 clarification 7; unchanged, not a contract this touches). The Card Manager's own tree
    /// ("mirror the card's real folders") still needs a node for that folder too, so this synthesizes the
    /// ones CardScanner omitted -- UI-only, never written back to LIBRARY.IDX.</summary>
    static int EnsureFolder(List<FolderRow> folders, Dictionary<string, int> indexByPath, string path)
    {
        if (indexByPath.TryGetValue(path, out var idx)) return idx;
        string parent = path.LastIndexOf('/') is int i and >= 0 ? path[..i] : "";
        int parentIdx = EnsureFolder(folders, indexByPath, parent);
        folders.Add(new FolderRow { Path = path, Parent = parentIdx });
        return indexByPath[path] = folders.Count - 1;
    }

    public static CardLoad Load(string root, uint date)
    {
        string gdbPath = Path.Combine(root, "aether", "GAMES.DB");
        switch (CardFolderMigration.Detect(root))
        {   // the Aether rename: a card set up before it -- nothing is edited until Update card has moved its folder
            case CardFolderState.Old:
                return new CardLoad { Root = root, Problem = $"{root} was set up before the Aether rename: its menu folder is /vibemenu. " +
                                                             "Click Update card: it backs the folder up to this PC, checks the copy, and renames it to /aether." };
            case CardFolderState.Both:
                return new CardLoad { Root = root, Problem = CardFolderMigration.BothMessage(root) };
        }
        if (!File.Exists(gdbPath))
            return new CardLoad { Root = root, Problem = $"{root}: no /aether/GAMES.DB. Click Prepare SD Card for Aether to set it up, then open it here." };

        var gdb = File.ReadAllBytes(gdbPath);
        GamesDbData db;
        try { db = GamesDb.Read(gdb); }
        catch (Formats.FormatException e) { return new CardLoad { Root = root, Problem = $"{root}: /aether/GAMES.DB is corrupt ({e.Message})." }; }

        var res = CardScanner.Scan(new FsSource(root), db.Records.Select(r => r.Key), db.Aliases, date);

        LibraryIndexData? lib = null;
        string libPath = Path.Combine(root, "aether", "LIBRARY.IDX");
        if (File.Exists(libPath)) { try { lib = LibraryIndex.Read(File.ReadAllBytes(libPath)); } catch (Formats.FormatException) { } }
        var overrideByPath = lib?.Entries.Where(e => e.TitleOverride != "").ToDictionary(e => e.Path, e => e.TitleOverride)
                              ?? new Dictionary<string, string>();

        string favsPath = Path.Combine(root, "aether", "FAVS.TXT");
        string favsText = File.Exists(favsPath) ? File.ReadAllText(favsPath, Encoding.ASCII) : "";
        var known = res.Entries.Select(e => e.Path).ToHashSet();
        var favs = PathList.Parse(favsText, PathList.FavsMax, known).Paths.ToHashSet();

        string recentPath = Path.Combine(root, "aether", "RECENT.TXT");
        string recentText = File.Exists(recentPath) ? File.ReadAllText(recentPath, Encoding.ASCII) : "";
        var recentPaths = PathList.Parse(recentText, PathList.RecentMax, known).Paths;

        string perGamePath = Path.Combine(root, "aether", "PERGAME.INI");
        string perGameText = File.Exists(perGamePath) ? File.ReadAllText(perGamePath, Encoding.ASCII) : "";
        var perGame = PerGameIni.Parse(perGameText).ToDictionary(g => g.Id);

        string artPath = Path.Combine(root, "aether", "ART.PAK");
        byte[]? artPak = File.Exists(artPath) ? File.ReadAllBytes(artPath) : null;
        Formats.ArtPakData? artData = null;
        if (artPak != null) { try { artData = Formats.ArtPak.Read(artPak); } catch (Formats.FormatException) { } }

        string userEditPath = Path.Combine(root, "aether", "USEREDIT.INI");
        string userEditText = File.Exists(userEditPath) ? File.ReadAllText(userEditPath, Encoding.ASCII) : "";
        var userEdits = Formats.UserEdits.Parse(userEditText);
        var userEditsLive = userEdits.ToDictionary(kv => kv.Key, kv => kv.Value.Clone());
        var userEditsOriginal = userEdits.ToDictionary(kv => kv.Key, kv => kv.Value.Clone());
        var matchByPath = Formats.MatchOverrides.Parse(userEditText);

        var rows = res.Entries.Select(e =>
        {
            var sid = res.SystemIds.GetValueOrDefault(e.Path);
            string gid = sid != null ? GameId.Ini(sid.ProductField, sid.AreaField) ?? "" : "";
            string loader = gid != "" && perGame.TryGetValue(gid, out var pg) ? pg.Loader : "auto";
            string ov = overrideByPath.GetValueOrDefault(e.Path, "");
            bool fav = favs.Contains(e.Path);
            bool hasArt = e.HasBox || Art.CoverLookup.HasCover(artData, e.Key, e.Disc);
            string match = matchByPath.GetValueOrDefault(e.Path, "");
            return new GameRow
            {
                Path = e.Path, OriginalPath = e.Path, ScanKey = e.Key, Disc = e.Disc, DiscTotal = e.DiscTotal,
                AreaFlags = e.AreaFlags, FolderId = e.FolderId, HasBox = e.HasBox, HasArt = hasArt,
                OwnFolder = res.OwnFolder.GetValueOrDefault(e.Path), Sid = sid, GameIdIni = gid,
                TitleOverride = ov, OriginalTitleOverride = ov, Favorite = fav, OriginalFavorite = fav,
                Loader = loader, OriginalLoader = loader, MatchOverride = match, OriginalMatchOverride = match,
            };
        }).ToList();

        var folders = res.Folders.Select(f => new FolderRow { Path = f.Path, Parent = f.Parent }).ToList();
        var indexByPath = new Dictionary<string, int>();
        for (int i = 0; i < folders.Count; i++) indexByPath[folders[i].Path] = i;
        foreach (var row in rows) EnsureFolder(folders, indexByPath, row.Folder);

        return new CardLoad
        {
            Root = root, GamesDbBytes = gdb, Db = db, ArtPak = artPak, ArtData = artData, Rows = rows, Folders = folders,
            UserEdits = userEditsLive, OriginalUserEdits = userEditsOriginal,
            FavsText = favsText, RecentPaths = recentPaths, PerGameText = perGameText, Fingerprint = res.Fingerprint, CueMissing = res.CueMissing,
            CheatsOn = Formats.CheatPak.OnCounts(Formats.CheatPak.ReadCardCopy(Path.Combine(root, "aether"))),
            LibraryPaths = lib?.Entries.GroupBy(e => e.Path, StringComparer.Ordinal).ToDictionary(g => g.Key, g => (g.First().DiscTotal, g.First().Key), StringComparer.Ordinal),
        };
    }

    /// <summary>Games copied onto (or deleted from, or moved on) the card in Explorer after Aether's game list was
    /// built: the fresh scan's image paths against LIBRARY.IDX's. The console only rescans when the card root's
    /// names change, so a game added inside an existing folder would never show there until the list is rebuilt.
    /// Paths compare exactly (ordinal): CardBuilder writes the scanner's own paths, so a rebuilt card matches. A
    /// multi-disc set counts as one game. Null when the list matches, or when the card has no games and no list.</summary>
    public static string? LibraryDrift(CardLoad load)
    {
        var onCard = load.Rows.Select(r => r.Path).ToHashSet(StringComparer.Ordinal);
        if (load.LibraryPaths == null)
        {
            if (onCard.Count == 0) return null;
            int all = Games(load.Rows.Select(r => (r.Path, r.DiscTotal, r.Key)));
            return $"Aether's game list on this card (LIBRARY.IDX) is missing or unreadable: Update card rebuilds it with covers for the {Plural(all, "game")} on the card";
        }
        var lib = load.LibraryPaths;
        int added = Games(load.Rows.Where(r => !lib.ContainsKey(r.Path)).Select(r => (r.Path, r.DiscTotal, r.Key)));
        int removed = Games(lib.Where(kv => !onCard.Contains(kv.Key)).Select(kv => (kv.Key, kv.Value.DiscTotal, kv.Value.Key)));
        if (added == 0 && removed == 0) return null;
        if (removed == 0)
            return $"{Plural(added, "new game")} on the card: Update card adds {(added == 1 ? "it" : "them")} to Aether's list with {(added == 1 ? "its cover" : "their covers")}";
        if (added == 0)
            return $"{Plural(removed, "game")} removed from the card: Update card takes {(removed == 1 ? "it" : "them")} off Aether's list";
        return $"{Plural(added, "new game")} and {Plural(removed, "removed game")} on the card: Update card brings Aether's list up to date, with covers for the new {(added == 1 ? "one" : "ones")}";

        static string Dir(string p) => p.LastIndexOf('/') is int i and >= 0 ? p[..i] : "";
        // one per single-disc image; a multi-disc set's images (same folder, key and DiscTotal > 1) count once
        static int Games(IEnumerable<(string Path, int DiscTotal, string Key)> images) =>
            images.Select(x => x.DiscTotal > 1 ? "*" + Dir(x.Path) + "*" + x.Key + "*" + x.DiscTotal : x.Path).Distinct(StringComparer.Ordinal).Count();
        static string Plural(int n, string what) => $"{n} {what}{(n == 1 ? "" : "s")}";
    }

    /// <summary>Every GAMES.DB key whose UserEdits record differs from the Load()-time snapshot (added, changed or
    /// cleared back to empty).</summary>
    static List<string> ChangedUserEditKeys(CardLoad load)
    {
        var keys = new HashSet<string>(load.UserEdits.Keys);
        keys.UnionWith(load.OriginalUserEdits.Keys);
        var o = new List<string>();
        foreach (var k in keys)
        {
            var now = load.UserEdits.GetValueOrDefault(k) ?? new Formats.UserEditRecord();
            var was = load.OriginalUserEdits.GetValueOrDefault(k) ?? new Formats.UserEditRecord();
            if (!now.SameAs(was)) o.Add(k);
        }
        return o;
    }

    /// <summary>Every live row's path -> staged match-override key (CardScanner's ForcedKeys), for the rebuild and
    /// for writing USEREDIT.INI's [MATCH] section.</summary>
    public static Dictionary<string, string> MatchOverrides(CardLoad load) =>
        load.Rows.Where(r => !r.Removed && r.MatchOverride != "").ToDictionary(r => r.Path, r => r.MatchOverride);

    /// <summary>Every change Save changes will make, for the confirmation (CardWriteGuard adds the drive details).</summary>
    public static List<string> Describe(CardLoad load)
    {
        var o = new List<string>();
        int renamed = load.Rows.Count(r => !r.Removed && r.TitleOverride != r.OriginalTitleOverride);
        if (renamed > 0) o.Add($"{renamed} title override(s)");
        int favd = load.Rows.Count(r => !r.Removed && r.Favorite != r.OriginalFavorite);
        if (favd > 0) o.Add($"{favd} favorite change(s) in /aether/FAVS.TXT");
        int loaderd = load.Rows.Count(r => !r.Removed && r.Loader != r.OriginalLoader);
        if (loaderd > 0) o.Add($"{loaderd} loader override(s) in /aether/PERGAME.INI");
        int covers = load.Rows.Count(r => !r.Removed && (r.PendingCover != null || r.RestoreCoverStaged));
        if (covers > 0) o.Add($"{covers} cover change(s) in BOX.TGA");
        int folderArt = load.Folders.Count(f => f.Dirty);
        if (folderArt > 0) o.Add($"{folderArt} folder art change(s) (FOLDER.PNG written or removed)");
        int matched = load.Rows.Count(r => !r.Removed && r.MatchOverride != r.OriginalMatchOverride);
        if (matched > 0) o.Add($"{matched} match override(s) in /aether/USEREDIT.INI, used for this save's GAMES.DB/LIBRARY.IDX/ART.PAK keys");
        int meta = ChangedUserEditKeys(load).Count;
        if (meta > 0) o.Add($"{meta} metadata edit(s) in /aether/USEREDIT.INI, re-applied onto /aether/GAMES.DB (title/genre/year/region/players)");
        if (load.PendingRestoreFrom is string snap) o.Add($"restoring the PC backup from {snap}" +
            (load.PendingRestoreRawFiles.Count > 0 ? $" ({load.PendingRestoreRawFiles.Count} file(s) written as-is: CHEATS.PAK/saves)" : ""));
        o.Add("then rescan the card and rebuild /aether/GAMES.DB, LIBRARY.IDX and ART.PAK so the menu sees the changes");
        return o;
    }

    public static bool HasChanges(CardLoad load) =>
        load.Rows.Any(r => r.Dirty) || load.Folders.Any(f => f.Dirty) || ChangedUserEditKeys(load).Count > 0 ||
        load.PendingRestoreFrom != null;

    public static ApplyReport Apply(Saves.CardFs fs, CardLoad load, uint date, Action<string>? progress, CancellationToken token,
                                    Art.CoverPackData? coverPack = null)
    {
        var report = new ApplyReport();
        var undo = new List<Action>();
        try
        {
            // Final review I1: never for a loose game (shares its folder with other things, or sits at the card
            // root, so BOX.TGA there belongs to that folder, not to this one game). The UI already gates this at
            // the button (MainWindow.ChooseCover/MissingCoverCandidates); this is the last line of defense against
            // ever overwriting a shared or root cover from here.
            foreach (var row in load.Rows.Where(r => !r.Removed && r.OwnFolder && (r.PendingCover != null || r.RestoreCoverStaged)))
            {
                token.ThrowIfCancellationRequested();
                fs.Verify();
                string box = (row.Folder == "" ? "" : row.Folder + "/") + "BOX.TGA";
                string dest = fs.Host("/" + box);
                bool existed = File.Exists(dest);
                byte[]? prev = existed ? File.ReadAllBytes(dest) : null;
                if (row.PendingCover != null)
                {
                    File.WriteAllBytes(dest, TgaEncoder.Encode(row.PendingCover));
                    undo.Add(() => { try { if (prev != null) File.WriteAllBytes(dest, prev); else File.Delete(dest); } catch (IOException) { } });
                    row.HasBox = true;
                }
                // RestoreCoverStaged with no PendingCover: nothing on disk to undo (the on-disk BOX.TGA, if any, was
                // never touched -- "Restore cover" only discards this session's pending change).
                report.Steps.Add($"COVER: {row.Path}");
                progress?.Invoke($"COVER: {row.Path}");
            }
            foreach (var folder in load.Folders.Where(f => f.PendingCover != null || f.RemoveArtStaged))
            {
                token.ThrowIfCancellationRequested();
                fs.Verify();
                string dir = fs.Host("/" + folder.Path);
                var existing = Directory.Exists(dir)
                    ? Directory.EnumerateFiles(dir).Where(p => CardBuilder.FolderArtNames.Contains(Path.GetFileName(p).ToUpperInvariant())).ToList()
                    : [];
                var backups = existing.Select(p => (Path: p, Bytes: File.ReadAllBytes(p))).ToList();
                foreach (var p in existing) File.Delete(p);
                string? written = null;
                if (folder.PendingCover != null)
                {
                    Directory.CreateDirectory(dir);
                    written = Path.Combine(dir, "FOLDER.PNG");
                    File.WriteAllBytes(written, CoverEncoder.EncodePng(folder.PendingCover));
                }
                string? capturedWritten = written;
                undo.Add(() =>
                {
                    try
                    {
                        if (capturedWritten != null && File.Exists(capturedWritten)) File.Delete(capturedWritten);
                        foreach (var (p, b) in backups) File.WriteAllBytes(p, b);
                    }
                    catch (IOException) { }
                });
                report.Steps.Add($"FOLDER ART: {folder.Path}");
                progress?.Invoke($"FOLDER ART: {folder.Path}");
            }

            void WriteTracked(string sdPath, byte[] data)
            {
                string host = fs.Host(sdPath);
                byte[]? prev = File.Exists(host) ? File.ReadAllBytes(host) : null;
                fs.WriteReplace(sdPath, data);
                undo.Add(() => { try { if (prev != null) File.WriteAllBytes(host, prev); else File.Delete(host); } catch (IOException) { } });
            }

            // PC backup restore: raw tracked writes for files with no staged-edit field of their
            // own (CHEATS.PAK, /aether/SAVES/*) -- "/menu.bin" is refused even if it somehow got in here, belt and
            // braces alongside Backup.CardRestore's own refusal to ever queue it.
            foreach (var (cardPath, data) in load.PendingRestoreRawFiles)
            {
                if (cardPath.Equals("/menu.bin", StringComparison.OrdinalIgnoreCase)) continue;
                token.ThrowIfCancellationRequested();
                fs.Verify();
                string? dir = cardPath.LastIndexOf('/') is int i and > 0 ? cardPath[..i] : null;
                if (dir != null) fs.MkDir(dir);        // a restored save's folder may not exist yet (a fresh card)
                WriteTracked(cardPath, data);
                report.Steps.Add($"RESTORED: {cardPath}");
                progress?.Invoke($"RESTORED: {cardPath}");
            }

            token.ThrowIfCancellationRequested();
            fs.Verify();
            var favPaths = load.Rows.Where(r => !r.Removed && r.Favorite).Select(r => r.Path).Distinct();
            WriteTracked("/aether/FAVS.TXT", PathList.Write(favPaths, PathList.FavsMax));
            report.Steps.Add("FAVS.TXT");

            string perGame = load.PerGameText;
            foreach (var row in load.Rows.Where(r => !r.Removed && r.GameIdIni != "" && r.Loader != r.OriginalLoader))
                perGame = PerGameIni.Put(perGame, new PerGameEntry { Id = row.GameIdIni, Loader = row.Loader });
            if (Encoding.ASCII.GetByteCount(perGame) > PerGameIni.MaxBytes)
                throw new IOException($"PERGAME.INI would be over {PerGameIni.MaxBytes} B: nothing was written for the loader changes");
            WriteTracked("/aether/PERGAME.INI", Encoding.ASCII.GetBytes(perGame));
            report.Steps.Add("PERGAME.INI");

            token.ThrowIfCancellationRequested();
            fs.Verify();
            var matchOverrides = MatchOverrides(load);
            string userEditText = Formats.UserEdits.Write(load.UserEdits) + Formats.MatchOverrides.Write(matchOverrides);
            if (Encoding.ASCII.GetByteCount(userEditText) > Formats.UserEdits.CombinedMaxBytes)
                throw new IOException($"USEREDIT.INI would be over {Formats.UserEdits.CombinedMaxBytes} B: nothing was written for the metadata/match changes");
            WriteTracked("/aether/USEREDIT.INI", Encoding.ASCII.GetBytes(userEditText));
            report.Steps.Add("USEREDIT.INI");

            token.ThrowIfCancellationRequested();
            fs.Verify();
            var overrides = load.Rows.Where(r => !r.Removed).ToDictionary(r => r.Path, r => r.TitleOverride);
            var rb = CardRebuilder.Run(load.Root, date, load.GamesDbBytes, load.ArtPak, overrides, load.UserEdits, matchOverrides, coverPack);
            WriteTracked("/aether/GAMES.DB", rb.GamesDb);
            token.ThrowIfCancellationRequested();
            WriteTracked("/aether/LIBRARY.IDX", rb.LibraryIdx);
            token.ThrowIfCancellationRequested();
            WriteTracked("/aether/ART.PAK", rb.ArtPak);
            report.Steps.Add("GAMES.DB, LIBRARY.IDX, ART.PAK rebuilt");

            return report;
        }
        catch (OperationCanceledException)
        {
            Rollback(undo, report);
            throw;
        }
        catch (Exception e)
        {
            Rollback(undo, report);
            throw new ApplyStoppedException($"Save changes stopped ({e.Message}); every staged edit before the rebuild was undone.", e);
        }
    }

    static void Rollback(List<Action> undo, ApplyReport report)
    {
        for (int i = undo.Count - 1; i >= 0; i--)
            try { undo[i](); } catch (Exception e) { report.Stranded.Add(e.Message); }
    }
}
