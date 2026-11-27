// Aether Card Manager, GPL-3.0-or-later.
// Build a card's Aether data (an empty SD card becomes a working card): scan the card (SPEC v1
// clarification 7), match every disc's System ID (§6), write LIBRARY.IDX (§5.4), the card's GAMES.DB (§5.3: the IP.BIN
// dates and the compat rows filled in) and ART.PAK (§5.2: hero covers, folder art, JP-title bitmaps), plus
// manifest.csv (one provenance row per blob, design spec §10) and report.json. A port of tools/omsat/cardbuild.py
// build(), checked against it on a fixture card (manager/golden/expect/build.json): byte-identical except the JP-title
// bitmaps (TitleRenderer's measured bounds). Covers come, in cardbuild's order, from the Cover Project zip (by image or
// folder name, then by GAMES.DB title + region when a fallback is on), the image's BOX.TGA, then retrohead and
// libretro-thumbnails through RemoteArt's cache; --art-prefer tries one named source first. Not ported (and refused when
// asked for): cardbuild's generated title cards (--title-cards) and the cheat-file seeding (--cheats).
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public sealed record BuildOptions(
    byte[] GamesDb, IReadOnlyDictionary<string, string> JpTitles, string? CoversZip = null, string? FontPath = null,
    RemoteArt? Remote = null, IReadOnlyDictionary<string, string>? FolderArt = null, IReadOnlyDictionary<string, string>? ArtPrefer = null,
    bool Compat = true, string? LoaderCompatJson = null, string? ControllerCompatJson = null, string? SaveToolsJson = null,
    string? FolderArtBase = null,    // where a relative FolderArt path is read from (default: the current directory); the manifest keeps it as given
    IReadOnlyDictionary<string, string>? ForcedKeys = null,
    CoverPackData? CoverPack = null)  // the Card Manager's bundled COVERS.PAK: a matched game's cover when the card has none   // "Match to game..." (M-art-pack §3): CardScanner.Scan's forcedKeys
{
    /// <summary>Tests only: the JP bitmap's rows (16 on every card). An 8-row bitmap makes the corner case of SPEC §5.2
    /// real: kanji too tall even at 10 px.</summary>
    internal int JpHeight { get; init; } = TitleRenderer.Height;
}

public sealed record BuildResult(byte[] LibraryIdx, byte[] GamesDb, byte[] ArtPak, string ManifestCsv, JsonObject Report);

public static class CardBuilder
{
    public static readonly string[] FolderArtNames = ["FOLDER.PNG", "FOLDER.JPG", "FOLDER.TGA"];
    public static readonly string[] ArtPreferSources = ["zip", "boxtga", "retrohead", "libretro"];

    sealed class Entry(string key, int disc, uint row)
    {
        public string Key = key;
        public int Disc = disc;
        public uint Row = row;
        public RgbaImage? Hero;
        public ArtJpTitle? Jp;
    }

    sealed record Hit(RgbaImage Img, int Disc, string Cov, string Source, string Src);

    static RgbaImage? TryImage(string path)
    {
        try { return RgbaImage.Decode(File.ReadAllBytes(path)); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or System.FormatException) { return null; }
    }

    /// <summary>coverpack._tnorm: lower-case, every non-alphanumeric a space, words joined by one space, a leading
    /// "the" dropped.</summary>
    static string TNorm(string t)
    {
        var w = new string([.. t.ToLowerInvariant().Select(c => char.IsLetterOrDigit(c) ? c : ' ')]).Split(' ', StringSplitOptions.RemoveEmptyEntries);
        return string.Join(' ', w.Length > 0 && w[0] == "the" ? w[1..] : w);
    }

    /// <summary>The pack's best cover for a key, in the Card Manager's own order (the Cover Project, then retrohead's flat
    /// front, then libretro, then retrohead's 3D render); null when it has none or the blob does not decode.</summary>
    public static readonly string[] PackOrder = ["coverproject", "retrohead2d", "libretro", "retrohead3d"];

    static (RgbaImage Img, string Src, CoverPackEntry? Fallback)? PackCover(CoverPackData pack, string key)
    {
        var all = pack.For(key);
        foreach (var src in PackOrder)
            if (all.FirstOrDefault(x => x.Source == src) is { } pe)
            {
                try { return (RgbaImage.Decode(pack.Blob(pe)), $"COVERS.PAK:{key}:{pe.Source}", pe.FallbackRegion != "" ? pe : null); }
                catch (Exception ex) when (ex is IOException or Formats.FormatException or System.FormatException) { }
            }
        return null;
    }

    static bool HasRegion(int flags, char letter) => (flags & (1 << "JTUEB".IndexOf(letter))) != 0;

    /// <summary>build(): reads the card, writes nothing; the caller puts the result on the card (Installer) or anywhere.</summary>
    public static BuildResult Build(string card, uint date, BuildOptions o)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var db = GamesDb.Read(o.GamesDb);
        var byKey = db.Records.ToDictionary(r => r.Key);
        bool fallbacks = o.Remote != null;
        var res = DiscSets.Apply(CardScanner.Scan(new FsSource(card), db.Records.Select(r => r.Key), db.Aliases, date, forcedKeys: o.ForcedKeys),
                                 date);   // SPEC §5.4: multi-disc sets for the lid swap (tools/omsat/discsets.py)
        var (gdb, yearsFilled) = GamesDb.FillDates(o.GamesDb, res.Dates);
        int compatApplied = 0;
        var compatNotes = new List<string>();
        if (o.Compat)
        {
            var rows = Compat.Load(o.LoaderCompatJson ?? throw new ArgumentException("compat needs LoaderCompatJson"),
                                   o.ControllerCompatJson, compatNotes, o.SaveToolsJson);
            // 2026-11-25: then each disc by Kai's IP CRC, its product + area, or its product alone (Compat.Decide)
            (gdb, compatApplied) = Compat.Fill(gdb, rows, Compat.DiscsOf(res.Entries, res.SystemIds), compatNotes);
        }
        var entries = new Dictionary<(string, int), Entry>();
        var order = new List<(string, int)>();
        var manifest = new List<string[]>();
        uint AddRow(string key, int disc, string source, string src, string transform)
        {
            manifest.Add([(manifest.Count + 1).ToString(System.Globalization.CultureInfo.InvariantCulture), key,
                          disc.ToString(System.Globalization.CultureInfo.InvariantCulture), source, src, transform]);
            return (uint)manifest.Count;
        }
        Entry Put((string, int) k, Func<uint> row)
        {
            if (!entries.TryGetValue(k, out var e)) { e = entries[k] = new Entry(k.Item1, k.Item2, row()); order.Add(k); }
            return e;
        }
        var cov = new JsonObject { ["zip"] = 0, ["box_tga"] = 0, ["none"] = 0, ["box_tga_unreadable"] = 0 };
        if (fallbacks) { cov["zip_title"] = 0; cov["retrohead"] = 0; cov["libretro"] = 0; cov["region_fallback"] = 0; cov["generated"] = 0; }
        if (o.CoverPack != null) cov["cover_pack"] = 0;
        void Count(string k) => cov[k] = cov[k]!.GetValue<int>() + 1;
        var perTitle = new JsonArray();
        var rejected = new List<string>();
        void Per(string path, string key, string source, string file) =>
            perTitle.Add(new JsonObject { ["path"] = path, ["key"] = key, ["source"] = source, ["file"] = file });
        string Fit = "fit+letterbox RGB555";
        using var cz = o.CoversZip != null ? new CoverZip(o.CoversZip) : null;
        string? ZipByTitle(LibEntry e)
        {
            if (cz == null || !byKey.TryGetValue(e.Key, out var r) || !fallbacks) return null;
            foreach (var region in RemoteArt.RegionNames(r.RegionFlags))
                if (cz.Find(RemoteArt.ZipTitleStem(r.TitleEn, region)) is string m) return m;
            return null;
        }
        // d51022a (coverpack.CoverFinder.fallback): a game with no cover of its own regions takes the same game's cover
        // from US, then JP, then EU -- its own title under that region's name, then each other record of the same title
        // that carries that region (the DAT's Redump names are not read here: cardbuild without --libretro-dat).
        var sameTitle = new Dictionary<string, List<GameRecord>>();
        foreach (var r in db.Records) { var k = TNorm(r.TitleEn); if (!sameTitle.TryGetValue(k, out var l)) sameTitle[k] = l = []; l.Add(r); }
        (RgbaImage Img, string Src, char Letter, string From)? RegionFallback(GameRecord r)
        {
            foreach (char letter in "UJE")
            {
                if (HasRegion(r.RegionFlags, letter)) continue;
                var sibs = new List<GameRecord> { r };
                sibs.AddRange(sameTitle.GetValueOrDefault(TNorm(r.TitleEn), []).Where(x => x.Key != r.Key && HasRegion(x.RegionFlags, letter))
                                     .OrderBy(x => x.Key, StringComparer.Ordinal));
                string region = letter switch { 'U' => "USA", 'J' => "Japan", _ => "Europe" };
                foreach (var x in sibs)
                {
                    if (cz?.Find(RemoteArt.ZipTitleStem(x.TitleEn, region)) is string m) return (cz.Image(m), m, letter, x.Key);
                    if (o.Remote == null) continue;
                    if (x != r && o.Remote.RetroheadCover(x.Key) is ({ } ri, { } rs)) return (ri, rs, letter, x.Key);
                    if (o.Remote.LibretroCover(x.TitleEn, 1 << "JTUEB".IndexOf(letter)) is ({ } li, { } ls)) return (li, ls, letter, x.Key);
                }
            }
            return null;
        }
        static (string Name, string Parent) Split(string path)
        {
            int i = path.LastIndexOf('/');
            string parent = i < 0 ? "" : path[..i];
            return (i < 0 ? path : path[(i + 1)..], parent[(parent.LastIndexOf('/') + 1)..]);
        }
        static string ParentPath(string path) => path.LastIndexOf('/') is int i and >= 0 ? path[..i] : "";
        string Box(string path) => Path.Combine(card, (ParentPath(path) == "" ? "" : ParentPath(path) + "/").Replace('/', Path.DirectorySeparatorChar), "BOX.TGA");
        string BoxRel(string path) => (ParentPath(path) == "" ? "." : ParentPath(path)) + "/BOX.TGA";

        Hit? Named(string source, LibEntry e, GameRecord? r)
        {
            var (name, parent) = Split(e.Path);
            switch (source)
            {
                case "zip":
                    if (cz == null) return null;
                    string? member = cz.Find(name) ?? (parent != "" ? cz.Find(parent) : null);
                    bool byTitle = false;
                    if (member == null && !e.HasBox && fallbacks && r != null)
                        foreach (var region in RemoteArt.RegionNames(r.RegionFlags))
                            if (cz.Find(RemoteArt.ZipTitleStem(r.TitleEn, region)) is string m) { member = m; byTitle = true; break; }
                    if (member == null) return null;
                    return new Hit(cz.Image(member), CoverZip.DiscOf(member), byTitle ? "zip_title" : "zip", "coverproject" + (byTitle ? " (by title)" : ""), member);
                case "boxtga":
                    if (!e.HasBox) return null;
                    var img = TryImage(Box(e.Path));
                    return img == null ? null : new Hit(img, 0, "box_tga", "user", BoxRel(e.Path));
                case "retrohead":
                    if (o.Remote == null) return null;
                    var (ri, rs) = o.Remote.RetroheadCover(e.Key);
                    return ri == null ? null : new Hit(ri, 0, "retrohead", "retrohead", rs!);
                case "libretro":
                    if (o.Remote == null || r == null) return null;
                    var (li, ls) = o.Remote.LibretroCover(r.TitleEn, r.RegionFlags);
                    return li == null ? null : new Hit(li, 0, "libretro", "libretro", ls!);
            }
            return null;
        }

        var matched = res.Entries.Where(e => e.Key != "").ToList();
        static string DirOf(string p) => p.LastIndexOf('/') is int i and >= 0 ? p[..i] : "";
        bool Loose(LibEntry e) => DirOf(e.Path) == res.Folders[e.FolderId].Path;
        // 3aba482: a folder (not the root) whose loose images are ONE game (its discs count once) gives that game its
        // BOX.TGA at the top priority, subfolders or not; shared by several loose games it stays the fallback
        var looseGames = res.Entries.Where(e => e.FolderId != 0 && Loose(e)).GroupBy(e => e.FolderId)
                                    .ToDictionary(g => g.Key, g => g.Select(e => e.Key != "" ? e.Key : e.Path).Distinct().Count());
        foreach (var e in matched)
        {
            byKey.TryGetValue(e.Key, out var rec);
            // Cover order (SPEC §5.6, matching the read side; 2026-11-23, Satiator Rings' model): a single-game
            // folder's own BOX.TGA comes first (SAROO deploy); a loose image's folder BOX.TGA (shared by the
            // folder's games) is only the fallback after every other source. An --art-prefer "boxtga" is gated the
            // same way -- it is not this image's own box unless its folder is.
            bool ownBox = (!Loose(e) || looseGames.GetValueOrDefault(e.FolderId) == 1) && e.HasBox;
            string? pref = o.ArtPrefer != null && o.ArtPrefer.TryGetValue(e.Key, out var p) ? p : null;
            if (pref == null && ownBox) pref = "boxtga";
            if (pref != null && Named(pref, pref == "boxtga" ? e with { HasBox = ownBox } : e, rec) is Hit hit)
            {
                var en = Put((e.Key, hit.Disc), () => AddRow(e.Key, hit.Disc, hit.Source, hit.Src, Fit));
                en.Hero ??= hit.Img;
                Count(hit.Cov);
                Per(e.Path, e.Key, hit.Source, hit.Src);
                continue;
            }
            var eRest = e;
            if (pref == "boxtga" && ownBox) { Count("box_tga_unreadable"); eRest = e with { HasBox = false }; }   // unreadable: counted once, then the other sources
            var (name, parent) = Split(eRest.Path);
            string? member = null;
            bool byTitle = false;
            if (cz != null) member = cz.Find(name) ?? (parent != "" ? cz.Find(parent) : null);
            if (member == null && !eRest.HasBox) { member = ZipByTitle(eRest); byTitle = member != null; }
            if (member != null)
            {
                int disc = CoverZip.DiscOf(member);
                var en = Put((e.Key, disc), () => AddRow(e.Key, disc, "coverproject", member, Fit));
                en.Hero ??= cz!.Image(member);
                Count(byTitle ? "zip_title" : "zip");
                Per(e.Path, e.Key, "coverproject" + (byTitle ? " (by title)" : ""), member);
            }
            else if (eRest.HasBox)   // a loose image's shared folder BOX.TGA, as a last resort (never retrohead/libretro)
            {
                var boxImg = TryImage(Box(eRest.Path));
                if (boxImg == null) { Count("box_tga_unreadable"); Count("none"); continue; }   // silent, as cardbuild.py
                var en = Put((e.Key, 0), () => AddRow(e.Key, 0, "user", BoxRel(eRest.Path), Fit));
                en.Hero ??= boxImg;
                Count("box_tga");
                Per(e.Path, e.Key, "user", BoxRel(eRest.Path));
            }
            else
            {
                RgbaImage? img = null;
                string? src = null, source = null;
                string fbNote = "";
                if (o.CoverPack != null && PackCover(o.CoverPack, e.Key) is var (pi, ps, pfb))
                {
                    (img, src, source) = (pi, ps, "cover_pack");
                    if (pfb != null) fbNote = $" (fallback_region {pfb.FallbackRegion}, from {pfb.FallbackFrom})";
                }
                if (img == null && o.Remote != null)
                {
                    (img, src) = o.Remote.RetroheadCover(e.Key);
                    if (img == null && src != null) rejected.Add(src);
                    source = "retrohead";
                    if (img == null && rec != null) { (img, src) = o.Remote.LibretroCover(rec.TitleEn, rec.RegionFlags); source = "libretro"; }
                }
                if (img == null && rec != null && fallbacks && RegionFallback(rec) is var (fi, fs, fl, ff))
                {
                    (img, src, source, fbNote) = (fi, fs, "region_fallback", $" (fallback_region {fl}, from {ff})");
                }
                if (img == null) { Count("none"); Per(e.Path, e.Key, "NONE", ""); continue; }
                var en = Put((e.Key, 0), () => AddRow(e.Key, 0, source! + fbNote, src!, Fit));
                en.Hero ??= img;
                Count(source!);
                Per(e.Path, e.Key, source! + fbNote, src!);
            }
        }
        cov["denominator"] = $"{matched.Count} matched images";

        var jpKeys = matched.Select(e => e.Key).Where(o.JpTitles.ContainsKey).Distinct().OrderBy(k => k, StringComparer.Ordinal).ToList();
        JsonObject jp;
        TitleRenderer? tr = null;
        try { tr = new TitleRenderer(o.FontPath); } catch (FontMissingException) { }
        if (tr == null)
            jp = new JsonObject { ["status"] = "SKIPPED", ["reason"] = $"font missing at {o.FontPath ?? TitleRenderer.DefaultFontPath}",
                                  ["denominator"] = $"{jpKeys.Count} matched keys with a JP title" };
        else
            using (tr)
            {
                int rendered = 0, scaled = 0;
                var tooTall = new JsonArray();
                foreach (var key in jpKeys)
                {
                    Formats.ArtJpTitle? t;
                    bool sc;
                    try { (t, _, sc) = tr.RenderJp(o.JpTitles[key], o.JpHeight, TitleRenderer.Baseline); }
                    catch (TitleTooTallException) { tooTall.Add(key); continue; }   // 4cdfc7b: dropped and reported, never clipped
                    if (t == null) continue;
                    Put((key, 0), () => AddRow(key, 0, "jp_title", "jp_titles.json", "Noto Sans JP 4bpp")).Jp = t;
                    rendered++;
                    if (sc) scaled++;
                }
                jp = new JsonObject { ["status"] = "OK", ["font"] = "NotoSansJP-Regular.otf", ["rendered"] = rendered, ["scaled"] = scaled,
                                      ["cropped"] = 0, ["too_tall"] = tooTall, ["denominator"] = $"{jpKeys.Count} matched keys with a JP title" };
                if (tooTall.Count > 0)
                    jp["warning"] = $"{tooTall.Count} JP title(s) taller than the 16-row bitmap even at 10 px: no JP bitmap, the menu shows the title as text";
            }

        int found = 0, injected = 0;
        foreach (var f in res.Folders.Skip(1))
        {
            string dir = Path.Combine(card, f.Path.Replace('/', Path.DirectorySeparatorChar));
            RgbaImage? img = null;
            string src = "";
            try
            {
                var names = new DirectoryInfo(dir).EnumerateFileSystemInfos().Select(x => x.Name).ToList();
                names.Sort(StringComparer.Ordinal);
                foreach (var n in names)
                    if (FolderArtNames.Contains(n.ToUpperInvariant())) { img = TryImage(Path.Combine(dir, n)); src = $"{f.Path}/{n}"; break; }
                // SPEC §5.2 Folder art source order (2026-11-23, Satiator Rings' model): FOLDER.PNG/.JPG/.TGA, else
                // the folder's own BOX.TGA (exact upper-case name) -- unconditionally now, even when the folder
                // holds its own loose images (a folder row can hold both: cardbuild.py _folder_image).
                if (img == null && names.FirstOrDefault(n => n == "BOX.TGA") is string box)
                {
                    img = TryImage(Path.Combine(dir, box));
                    src = $"{f.Path}/{box}";
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
            if (img == null && o.FolderArt != null && o.FolderArt.TryGetValue(f.Path, out var fa))
            {
                img = RgbaImage.Decode(File.ReadAllBytes(Path.IsPathRooted(fa) || o.FolderArtBase == null ? fa : Path.Combine(o.FolderArtBase, fa)));
                src = fa;
                injected++;
            }
            if (img != null)
            {
                string key = ArtPak.FolderArtKey(f.Path);
                var en = Put((key, 0), () => AddRow(key, 0, "user", src, Fit));
                en.Hero = img;
                found++;
            }
        }

        var art = order.Select(k => entries[k]).Select(e =>
        {
            if (e.Hero == null) return new ArtEntry(e.Key, e.Disc, true, null, null, e.Jp, e.Row);
            var (sq, hero, _) = CoverEncoder.Cover(e.Hero);
            return new ArtEntry(e.Key, e.Disc, sq, null, hero, e.Jp, e.Row);
        }).ToList();
        byte[] pak = ArtPak.Write(art, date);

        var errors = new JsonObject();
        void CheckFile(string n, Action a) { try { a(); } catch (Exception ex) { errors[n] = new JsonArray(ex.Message); } }
        CheckFile("LIBRARY.IDX", () => LibraryIndex.Read(res.Data));
        CheckFile("GAMES.DB", () => GamesDb.Read(gdb));
        CheckFile("ART.PAK", () => { var d = ArtPak.Read(pak); if (d.VerifyBlobs() is { Count: > 0 } b) throw new Formats.FormatException(string.Join("; ", b)); });

        var csv = new StringBuilder("id,key,disc,source,source_file,transform\r\n");
        foreach (var m in manifest) csv.Append(string.Join(',', m.Select(CsvField))).Append("\r\n");

        var report = new JsonObject
        {
            ["card"] = card,
            ["scan"] = new JsonObject
            {
                ["entries"] = res.Entries.Count, ["matched"] = res.Matched, ["unmatched"] = res.Entries.Count - res.Matched,
                ["unreadable_sysid"] = res.Unreadable, ["skipped_long_paths"] = res.SkippedLongPaths, ["skipped_folders"] = res.SkippedFolders,
                ["folders"] = res.Folders.Count, ["denominator"] = res.Denominator,
            },
            ["years_from_ipbin"] = yearsFilled, ["compat_applied"] = compatApplied, ["covers"] = cov, ["jp"] = jp,
            ["folder_art"] = new JsonObject { ["found"] = found, ["denominator"] = $"{res.Folders.Count - 1} folders below root" },
            ["art_entries"] = entries.Count, ["check_errors"] = errors, ["seconds"] = Math.Round(sw.Elapsed.TotalSeconds, 1),
        };
        if (compatNotes.Count > 0) report["compat_notes"] = new JsonArray(compatNotes.Select(n => (JsonNode)n!).ToArray());
        if (cz != null) report["cover_zip"] = new JsonObject { ["indexed_masters"] = cz.Indexed, ["ambiguous_lookups"] = cz.Ambiguous, ["resolved_lookups"] = cz.Resolved };
        if (o.FolderArt is { Count: > 0 }) report["folder_art"]!["injected"] = injected;
        if (fallbacks) report["per_title"] = perTitle;
        if (o.Remote != null)
            report["remote"] = new JsonObject
            {
                ["rejected"] = new JsonArray(rejected.Select(x => (JsonNode)x!).ToArray()),
                ["downloads"] = new JsonArray(o.Remote.Log.Select(x => (JsonNode)x!).ToArray()),
            };
        return new BuildResult(res.Data, gdb, pak, csv.ToString(), report);
    }

    /// <summary>Python csv's minimal quoting: a field with a comma, a quote, CR or LF is quoted, quotes doubled.</summary>
    static string CsvField(string s) => s.IndexOfAny([',', '"', '\r', '\n']) >= 0 ? "\"" + s.Replace("\"", "\"\"") + "\"" : s;

    /// <summary>The five files cardbuild writes into its --out folder (durably, write-then-replace).</summary>
    /// <summary>Final review I-3: the line to show when the build rendered no JP titles because the font is missing
    /// (report jp.status SKIPPED), else null. Every caller shows it: the titles are never skipped silently.</summary>
    public static string? JpWarning(BuildResult r) =>
        r.Report["jp"]?["status"]?.GetValue<string>() == "SKIPPED"
            ? $"JP titles were NOT rendered: {r.Report["jp"]!["reason"]} ({r.Report["jp"]!["denominator"]}). " +
              "Reinstall Aether Card Manager (its fonts/NotoSansJP-Regular.otf is part of it), or pass the font with --font."
            : r.Report["jp"]?["warning"]?.GetValue<string>() is string w      // 4cdfc7b: titles too tall even at 10 px
                ? $"{w} ({string.Join(", ", r.Report["jp"]!["too_tall"]!.AsArray().Select(k => k!.GetValue<string>()))})"
                : null;

    public static void WriteTo(string outDir, BuildResult r)
    {
        Directory.CreateDirectory(outDir);
        var fs = new Saves.CardFs(outDir);
        fs.WriteReplace("/LIBRARY.IDX", r.LibraryIdx);
        fs.WriteReplace("/GAMES.DB", r.GamesDb);
        fs.WriteReplace("/ART.PAK", r.ArtPak);
        fs.WriteReplace("/manifest.csv", Encoding.UTF8.GetBytes(r.ManifestCsv));
        fs.WriteReplace("/report.json", Encoding.UTF8.GetBytes(r.Report.ToJsonString(new JsonSerializerOptions { WriteIndented = true,
            Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping })));
    }
}
