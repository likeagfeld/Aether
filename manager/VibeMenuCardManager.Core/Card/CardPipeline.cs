// Aether Card Manager, GPL-3.0-or-later.
// "Build a card" end to end (design spec §11 M4: an empty SD card becomes a working card): CardBuilder reads
// the card and builds LIBRARY.IDX, GAMES.DB and ART.PAK into a staging folder (never on the card), then the Installer
// puts /autoboot.iso and /aether/ on the card through its stage/commit/undo transaction, never touching /menu.bin
// and backing up the old /autoboot.iso first -- both pinned to the volume the user confirmed (CardWriteGuard).
//
// The release folder (what an Aether release ships, laid out as tools/m3_release.sh's build/release plus the DB):
//   autoboot.iso  LAUNCHER.BIN  VRTILES.BIN  GAMES.DB  jp_titles.json      required
//   LOADBG.BIN  BGM.MUS  THEMES/<name>/THEME.INI (+ BG.BIN)               optional
//   compat/loader_compat.json, compat/controller_compat.json, compat/save_tools.json   the GAMES.DB compat rows
using System.Text.Json;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Install;

namespace VibeMenuCardManager.Core.Card;

public sealed record PipelineOptions(string Release, string? CoversZip = null, string? ArtCache = null, bool Download = false,
                                     IReadOnlyDictionary<string, string>? FolderArt = null, IReadOnlyDictionary<string, string>? ArtPrefer = null,
                                     bool Compat = true, string? FontPath = null, string? CoverPack = null);

/// <param name="Migrations">what each CardMigrations step of an in-place upgrade did (empty on a fresh card).</param>
/// <param name="Folder">the Aether rename's move of a pre-rename /vibemenu to /aether (Moved false: nothing to move).</param>
public sealed record PipelineReport(BuildResult Build, InstallReport Install, IReadOnlyList<string>? Migrations = null,
                                    FolderMigrationReport? Folder = null);

public static class CardPipeline
{
    public static readonly string[] ReleaseRequired = ["autoboot.iso", "LAUNCHER.BIN", "VRTILES.BIN", "GAMES.DB", "jp_titles.json"];

    /// <summary>The release folder's missing required files (empty = complete).</summary>
    public static List<string> CheckRelease(string release, bool compat)
    {
        var o = ReleaseRequired.Where(f => !File.Exists(Path.Combine(release, f))).ToList();
        if (compat && !File.Exists(Path.Combine(release, "compat", "loader_compat.json"))) o.Add("compat/loader_compat.json (or build without compat)");
        return o;
    }

    public static BuildOptions BuildOptionsFor(PipelineOptions p)
    {
        string R(string f) => Path.Combine(p.Release, f);
        var jp = JsonDocument.Parse(File.ReadAllText(R("jp_titles.json"))).RootElement.EnumerateObject()
                             .ToDictionary(x => x.Name, x => x.Value.GetString() ?? "");
        string? Opt(string f) => File.Exists(R(f)) ? R(f) : null;
        return new BuildOptions(File.ReadAllBytes(R("GAMES.DB")), jp, p.CoversZip, p.FontPath,
            p.ArtCache != null ? new RemoteArt(p.ArtCache, p.Download) : null, p.FolderArt, p.ArtPrefer, p.Compat,
            Opt("compat/loader_compat.json"), Opt("compat/controller_compat.json"), Opt("compat/save_tools.json"));
    }

    public enum MenuPairState { NotInstalled, Current, Mismatched }

    /// <summary>Whether the card's menu and LAUNCHER.BIN are the release's own pair (hand-off block v3+: a menu and a
    /// launcher from different builds refuse each other's block): NotInstalled when the card has no Aether
    /// autoboot.iso; Current when both files are byte-identical to the release's; else Mismatched, with the reason --
    /// Prepare then offers the update, which always installs both together.</summary>
    public static (MenuPairState State, string? Reason) MenuPair(string card, string release)
    {
        string iso = Path.Combine(card, "autoboot.iso"), lnc = Path.Combine(card, "aether", "LAUNCHER.BIN");
        if (!File.Exists(iso) || Disc.SystemIdReader.ReadImage(iso) is not { } sid || !CardPaths.IsMenuTitle(sid.Title))
            return (MenuPairState.NotInstalled, null);
        static string Sha(string p) => Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(p)));
        if (!File.Exists(lnc)) return (MenuPairState.Mismatched, "the card has the menu but no /aether/LAUNCHER.BIN");
        bool isoSame = Sha(iso) == Sha(Path.Combine(release, "autoboot.iso"));
        bool lncSame = Sha(lnc) == Sha(Path.Combine(release, "LAUNCHER.BIN"));
        if (isoSame && lncSame) return DataAge(card, release) is string old ? (MenuPairState.Mismatched, old) : (MenuPairState.Current, null);
        return (MenuPairState.Mismatched, isoSame ? "the card's LAUNCHER.BIN is not the one built with its menu"
                                          : lncSame ? "the card's menu is not the one built with its LAUNCHER.BIN"
                                          : "the card's menu and LAUNCHER.BIN differ from this release");
    }

    /// <summary>Why the card's /aether/GAMES.DB is older than the release's (fewer records, or an older build date) --
    /// the user's card after a matcher or data update kept "Not in database" for games the release knows -- else null.</summary>
    public static string? DataAge(string card, string release)
    {
        try
        {
            var have = GamesDb.Read(File.ReadAllBytes(Path.Combine(card, "aether", "GAMES.DB")));
            var ship = GamesDb.Read(File.ReadAllBytes(Path.Combine(release, "GAMES.DB")));
            if (have.Records.Count < ship.Records.Count || have.BuildDate < ship.BuildDate)
                return $"the card's GAMES.DB ({have.Records.Count} games, built {have.BuildDate}) is older than this release's ({ship.Records.Count} games, built {ship.BuildDate})";
            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or Formats.FormatException)
        {
            return "the card's GAMES.DB is missing or does not read";
        }
    }

    /// <summary>Build the card's data, then install it with the menu. confirmedIdentity: the volume the user confirmed
    /// (null only for tests that pass no guard). date: the build date written into the files.
    /// The Aether rename: on a card from before it (/vibemenu alone) everything is built and staged first, then the folder
    /// is backed up and moved to /aether (CardFolderMigration), then the install runs; if anything fails after the move
    /// while the card's menu is still the VibeMenu one, the folder goes back to /vibemenu (on the confirmed card only).
    /// Both folders: refused before anything is written.</summary>
    public static PipelineReport Run(string card, PipelineOptions p, uint date, string? confirmedIdentity, string? stamp = null,
                                     IReadOnlyList<CardMigration>? migrations = null)
    {
        // an in-place upgrade: the version the card had before, for the migration steps run after the install
        string iso = Path.Combine(card, "autoboot.iso");
        string? installed = File.Exists(iso) && ReleaseAssembler.BuildOf(iso).Build is string b ? Update.AppVersion.Parse(b) : null;
        var missing = CheckRelease(p.Release, p.Compat);
        if (missing.Count > 0) throw new InstallException($"the release folder {p.Release} is missing: {string.Join(", ", missing)}");
        // never an old VibeMenu release on a renamed card: its menu reads /vibemenu, which the move takes away
        if (Disc.SystemIdReader.ReadImage(Path.Combine(p.Release, "autoboot.iso")) is not { } rel || !rel.Title.StartsWith("AETHER", StringComparison.Ordinal))
            throw new InstallException($"{Path.Combine(p.Release, "autoboot.iso")} is not an Aether menu image; nothing was written");
        var state = CardFolderMigration.Detect(card);
        if (state == CardFolderState.Both) throw new InstallException(CardFolderMigration.BothMessage(card));
        string folderNow = state == CardFolderState.Old ? CardPaths.OldFolder : CardPaths.Folder;

        // the bundled COVERS.PAK (p.CoverPack, else covers/COVERS.PAK next to the exe when it is there): a matched game
        // with no BOX.TGA and no zip cover gets the pack's, so the menu shows the covers the Card Manager shows
        string? packPath = p.CoverPack ?? (File.Exists(Art.CoverPack.DefaultPath()) ? Art.CoverPack.DefaultPath() : null);
        using var pack = packPath != null ? Art.CoverPack.Open(packPath) : null;
        // the card's own USEREDIT.INI (as omsat.cardbuild applies it): its [MATCH] keys, its metadata edits and its user
        // records survive a Prepare/update the same as a Save changes, never dropped by the release's newer GAMES.DB
        string ue = Path.Combine(card, folderNow, "USEREDIT.INI");
        string ueText = File.Exists(ue) ? File.ReadAllText(ue, System.Text.Encoding.ASCII) : "";
        var edits = UserEdits.Parse(ueText);
        var forced = Formats.MatchOverrides.Parse(ueText);
        var build = CardBuilder.Build(card, date, BuildOptionsFor(p) with { CoverPack = pack, ForcedKeys = forced.Count > 0 ? forced : null });
        if (edits.Count > 0)
        {
            var gdb = GamesDb.Read(build.GamesDb);
            var genres = UserEdits.ApplyAll(gdb.Records, gdb.Genres, edits);
            build = build with { GamesDb = GamesDb.Write(gdb.Records, genres, gdb.Aliases, gdb.BuildDate) };
        }
        if (build.Report["check_errors"] is System.Text.Json.Nodes.JsonObject { Count: > 0 } errs)
            throw new InstallException($"the built files failed their checks, nothing was installed: {errs.ToJsonString()}");
        string stage = Path.Combine(Path.GetTempPath(), "vmcm-build-" + Guid.NewGuid().ToString("N"));
        FolderMigrationReport? folder = null;
        try
        {
            CardBuilder.WriteTo(stage, build);
            string R(string f) => Path.Combine(p.Release, f);
            var themes = Directory.Exists(R("THEMES"))
                ? Directory.GetDirectories(R("THEMES")).Where(d => File.Exists(Path.Combine(d, "THEME.INI"))).ToDictionary(d => Path.GetFileName(d)!, d => d)
                : new Dictionary<string, string>();
            var src = new InstallSources(R("autoboot.iso"), R("LAUNCHER.BIN"), stage, R("VRTILES.BIN"),
                File.Exists(R("LOADBG.BIN")) ? R("LOADBG.BIN") : null, File.Exists(R("BGM.MUS")) ? R("BGM.MUS") : null, themes,
                File.Exists(R("CHEATS.PAK")) ? R("CHEATS.PAK") : null);
            string cardName = state == CardFolderState.Old ? Backup.CardBackup.CardFolderName(CardTarget.Inspect(card)) : "";
            folder = CardFolderMigration.Migrate(card, confirmedIdentity, cardName, DateTime.UtcNow);
            InstallReport inst;
            try { inst = Installer.Install(card, src, stamp, confirmedIdentity); }
            catch (Exception e) when (folder.Moved)
            {
                string back = e.Message.Contains("rollback incomplete", StringComparison.Ordinal)
                    ? $"/{CardPaths.Folder} left in place (the stranded files above are named there)" : MoveBack(card, iso, confirmedIdentity);
                throw new InstallException($"{e.Message}; {back}; the PC backup of /{CardPaths.OldFolder} is " +
                                           $"{folder.BackupDir}. Run Update card again", e);
            }
            string target = ReleaseAssembler.BuildOf(R("autoboot.iso")).Build is string nb ? Update.AppVersion.Parse(nb) ?? About.Credits.Version : About.Credits.Version;
            return new PipelineReport(build, inst, CardMigrations.Run(card, installed, target, migrations), folder);
        }
        finally
        {
            try { Directory.Delete(stage, recursive: true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
    }

    /// <summary>After a move and a failed install: /aether goes back to /vibemenu only while the card still boots the
    /// VibeMenu menu (the install changed no menu file: Installer's own undo, or it never started) and only on the volume
    /// the user confirmed. Otherwise it stays, and the message says so.</summary>
    static string MoveBack(string card, string iso, string? confirmedIdentity)
    {
        try { CardFolderMigration.VerifyVolume(card, confirmedIdentity); }
        catch (CardChangedException e) { return $"/{CardPaths.Folder} NOT moved back: {e.Message}"; }
        if (!(File.Exists(iso) && Disc.SystemIdReader.ReadImage(iso) is { } t && t.Title.StartsWith("VIBEMENU", StringComparison.Ordinal)))
            return $"/{CardPaths.Folder} left in place (the card's /autoboot.iso is no longer the VibeMenu menu)";
        if (CardFolderMigration.Detect(card) != CardFolderState.New) return $"/{CardPaths.Folder} not moved back (state now {CardFolderMigration.Detect(card)})";
        try
        {
            Directory.Move(Path.Combine(card, CardPaths.Folder), Path.Combine(card, CardPaths.OldFolder));
            return $"/{CardPaths.Folder} moved back to /{CardPaths.OldFolder}";
        }
        catch (Exception m) when (m is IOException or UnauthorizedAccessException)
        {
            return $"/{CardPaths.Folder} could NOT be moved back to /{CardPaths.OldFolder} ({m.Message})";
        }
    }
}
