// Aether Card Manager, GPL-3.0-or-later.
// Help > Send feedback: a bug, a feature request or a user-experience issue, with "Your setup". Per type: its required
// fields are enforced, the URL picks its issue form and label and fills its fields (setup and automatic ones too), a
// long report is cut to fit with a note; conditional setup fields show and hide (and a hidden one never reaches the
// issue); the open card and a picked game's Launch Options are filled in; every field matches its .yml form.
using System.Text.Json;
using System.Web;
using Avalonia.Controls;
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class FeedbackTests
{
    static readonly Dictionary<FeedbackKind, Dictionary<string, string>> Filled = new()
    {
        [FeedbackKind.Bug] = new() { ["what-happened"] = "Menu froze on NiGHTS", ["expected"] = "The game starts", ["steps"] = "1. Pick NiGHTS\n2. Press A",
                                     ["console-region"] = "USA", ["satiator-firmware"] = "FW163", ["saturn-model"] = "Model 2 (round buttons)",
                                     ["model-number"] = "MK-80000A" },
        [FeedbackKind.Feature] = new() { ["wish"] = "Sort by release year", ["why"] = "700 games", ["where"] = "The Card Manager" },
        [FeedbackKind.Ux] = new() { ["where"] = "Launch Options", ["what-felt-wrong"] = "Slot in use unclear", ["prefer"] = "Highlight it",
                                    ["saturn-model"] = "Hi-Saturn (Hitachi)" },
    };

    static FeedbackReport Report(FeedbackKind k, Dictionary<string, string>? values = null) =>
        new(k, values ?? Filled[k], "0.1", "0.1", "Windows 11 (X64)");

    static System.Collections.Specialized.NameValueCollection Q(string url) => HttpUtility.ParseQueryString(new Uri(url).Query);

    [Theory]
    [InlineData(FeedbackKind.Bug, "What happened, What did you expect, Steps to reproduce, Saturn model, Console region, Satiator firmware")]
    [InlineData(FeedbackKind.Feature, "What would you like, Why? What problem does it solve, Where does it fit")]   // its setup is optional
    [InlineData(FeedbackKind.Ux, "Where? (screen or feature), What felt wrong or confusing, What did you expect, or would you prefer, Saturn model")]
    public void Each_type_names_its_own_empty_required_fields(FeedbackKind k, string want)
    {
        Assert.Equal(want, string.Join(", ", Report(k, []).Missing()));
        Assert.Empty(Report(k).Missing());
    }

    [Theory]
    [InlineData(FeedbackKind.Bug, "bug.yml", "bug", "[Bug] Menu froze on NiGHTS")]
    [InlineData(FeedbackKind.Feature, "feature.yml", "enhancement", "[Feature] Sort by release year")]
    [InlineData(FeedbackKind.Ux, "ux.yml", "ux", "[UX] Launch Options")]
    public void The_url_picks_the_types_form_and_label_and_fills_every_field(FeedbackKind k, string template, string label, string title)
    {
        var values = new Dictionary<string, string>(Filled[k]) { ["cart"] = "4MB RAM", ["mods"] = "Recapped mainboard; Quiet or replacement fan", ["display"] = "CRT" };
        var (url, cut) = FeedbackUrl.Build(Report(k, values));
        Assert.False(cut);
        Assert.StartsWith("https://github.com/likeagfeld/Aether/issues/new?", url);
        var q = Q(url);
        Assert.Equal((template, label, title), (q["template"], q["labels"], q["title"]));
        foreach (var (id, v) in values) Assert.Equal(v, q[id]);
        Assert.Equal(("0.1", "0.1", "Windows 11 (X64)"), (q["aether-version"], q["card-manager-version"], q["os"]));
        foreach (var f in FeedbackForms.For(k).Fields) Assert.Contains("### " + f.Label, q["body"]);
        Assert.Contains("### Setup\n\n", q["body"]);                                        // one compact block
        Assert.Contains("- Cartridge slot: 4MB RAM\n", q["body"]);
        Assert.Contains("- Console mods: Recapped mainboard; Quiet or replacement fan", q["body"]);
        Assert.DoesNotContain("token", url, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData(FeedbackKind.Bug, "steps")]
    [InlineData(FeedbackKind.Feature, "why")]
    [InlineData(FeedbackKind.Ux, "what-felt-wrong")]
    public void A_long_report_is_cut_to_fit_and_says_so(FeedbackKind k, string longField)
    {
        var values = new Dictionary<string, string>(Filled[k])
        {
            [longField] = string.Concat(Enumerable.Repeat("Words with spaces & symbols äö 🎮\n", 600)),
            ["card"] = "29.7 GB exFAT, label \"SAROO\", 12.0 GB free, 689 games", ["port1"] = "Multitap (6 Player)", ["port1-tap-count"] = "6",
            ["port1-tap-types"] = "6 standard pads", ["mods"] = "Recapped mainboard; RGB/video mod", ["mods-video"] = "a built-in RGB kit",
        };
        var (url, cut) = FeedbackUrl.Build(Report(k, values));
        Assert.True(cut);
        Assert.InRange(url.Length, FeedbackUrl.MaxLength - 200, FeedbackUrl.MaxLength);
        var q = Q(url);
        Assert.Null(q["body"]);                                    // the body goes first: the form has every field
        Assert.EndsWith(FeedbackUrl.TruncatedNote, q[longField]);
        foreach (var (id, v) in values) if (id != longField) Assert.Equal(v, q[id]);   // short fields are never cut

        var mid = new Dictionary<string, string>(Filled[k]) { [longField] = new string('s', 3000) };   // fits without the body: nothing cut
        var (url2, cut2) = FeedbackUrl.Build(Report(k, mid));
        Assert.False(cut2);
        Assert.Equal(new string('s', 3000), Q(url2)[longField]);
    }

    [Fact]
    public void Other_model_needs_its_description_and_a_hidden_field_never_reaches_the_issue()
    {
        var other = new Dictionary<string, string>(Filled[FeedbackKind.Bug]) { ["saturn-model"] = FeedbackForms.OtherModel };
        Assert.Equal(["Other model (describe)"], Report(FeedbackKind.Bug, other).Missing());
        other["saturn-model-other"] = "ST-V test board";
        Assert.Empty(Report(FeedbackKind.Bug, other).Missing());
        // a value left in a field whose condition no longer holds (Mission Stick chosen, then the port changed)
        var stale = new Dictionary<string, string>(Filled[FeedbackKind.Bug]) { ["port2"] = "3D Control Pad", ["port2-stick"] = "Dual", ["port2-3d-mode"] = "Analog (○)" };
        var q = Q(FeedbackUrl.Build(Report(FeedbackKind.Bug, stale)).Url);
        Assert.Null(q["port2-stick"]);
        Assert.Equal("Analog (○)", q["port2-3d-mode"]);
        Assert.DoesNotContain("Mission Stick", q["body"]);
        Assert.Contains("HST-3220", FeedbackForms.ModelHelp);
    }

    [Fact]
    public void A_long_title_is_shortened()
    {
        var t = Report(FeedbackKind.Bug, new() { ["what-happened"] = new string('x', 200) }).Title();
        Assert.True(t.Length <= 6 + 80);
        Assert.EndsWith("...", t);
    }

    [Fact]
    public async Task Conditional_setup_fields_show_only_when_they_apply()
    {
        var shown = await HeadlessApp.Run(() =>
        {
            var w = new FeedbackWindow("0.1");
            w.Show();
            var seen = new List<string>();
            void Snap(string step) => seen.Add(step + ":" + string.Join(",", new[] { "port1-3d-mode", "port1-stick", "port1-tap-count", "port1-name", "psu-name",
                "mods-video", "mods-ode", "mods-other", "cart-ar", "cart-backup-size", "saturn-model-other" }.Where(w.IsShown)));
            Snap("start");
            w.Set("port1", "3D Control Pad"); Snap("3d");
            w.Set("port1", "Multitap (6 Player)"); Snap("tap");
            w.Set("port1", "Third-party pad"); Snap("third");
            w.Set("psu", "Aftermarket replacement PSU"); w.Set("cart", "Action Replay / Pro Action Replay"); Snap("psu+ar");
            w.Set("mods", "RGB/video mod; ODE other than Satiator also installed; Other"); w.Set("saturn-model", FeedbackForms.OtherModel); Snap("mods");
            ((Panel)w.Input("mods")).Children.OfType<CheckBox>().Single(c => (string)c.Content! == "Other").IsChecked = false; Snap("unticked");
            bool expandedBug = w.SetupSection.IsExpanded;
            w.Kind = FeedbackKind.Feature;
            bool expandedFeature = w.SetupSection.IsExpanded;
            bool keptAcrossTypes = w.IsShown("port1-name");                      // the port carried over, so its follow-up still shows
            w.Close();
            return Task.FromResult((seen, expandedBug, expandedFeature, keptAcrossTypes));
        });
        Assert.Equal(["start:", "3d:port1-3d-mode", "tap:port1-tap-count", "third:port1-name", "psu+ar:port1-name,psu-name,cart-ar",
                      "mods:port1-name,psu-name,mods-video,mods-ode,mods-other,cart-ar,saturn-model-other",
                      "unticked:port1-name,psu-name,mods-video,mods-ode,cart-ar,saturn-model-other"], shown.seen);
        Assert.True(shown.expandedBug);
        Assert.False(shown.expandedFeature);
        Assert.True(shown.keptAcrossTypes);
    }

    [Theory]
    [InlineData(FeedbackKind.Bug)]
    [InlineData(FeedbackKind.Feature)]
    [InlineData(FeedbackKind.Ux)]
    public async Task The_window_refuses_an_incomplete_form_then_opens_the_types_issue(FeedbackKind k)
    {
        string? opened = null;
        var (first, problem, second, last, auto) = await HeadlessApp.Run(() =>
        {
            var w = new FeedbackWindow("0.1") { OpenUrlForTests = u => opened = u };
            w.Show();
            w.Kind = k;
            bool a = w.Submit();
            string p = w.Problem.Text ?? "";
            foreach (var (id, v) in Filled[k]) w.Set(id, v);
            bool b = w.Submit();
            return Task.FromResult((a, p, b, w.LastUrl, w.Os));
        });
        Assert.False(first);
        Assert.StartsWith("Please fill in: ", problem);
        Assert.True(second);
        Assert.Equal(last, opened);
        var q = Q(opened!);
        Assert.Equal(FeedbackForms.For(k).Template, q["template"]);
        foreach (var (id, v) in Filled[k]) Assert.Equal(v, q[id]);
        Assert.Equal(auto, q["os"]);
    }

    /// <summary>A card with two games, one with Launch Options in PERGAME.INI and cheats on in CHEATS.PAK.</summary>
    static void Card(string root)
    {
        Directory.CreateDirectory(Path.Combine(root, "aether"));
        File.WriteAllBytes(Path.Combine(root, "aether", "GAMES.DB"), GamesDb.Write(
            [new GameRecord { Key = "T-00001", TitleEn = "Alpha Game", LoaderHint = 1 }, new GameRecord { Key = "T-00002", TitleEn = "Beta Game" }],
            [], new Dictionary<string, string>(), 20261125));
        foreach (var (f, p) in new[] { ("Alpha", "T-00001"), ("Beta", "T-00002") })
        {
            Directory.CreateDirectory(Path.Combine(root, "Games", f));
            File.WriteAllBytes(Path.Combine(root, "Games", f, $"{f}.iso"),
                SystemIdReader.Make(new Dictionary<string, string> { ["product"] = p, ["area"] = "U", ["title"] = f, ["device"] = "CD-1/1" }));
        }
        File.WriteAllText(Path.Combine(root, "aether", "PERGAME.INI"), "[T-00002@U]\nloader=jhl\nsaves=on\nslot=3\ncheats=on\nigr=off\n");
        var shipped = CheatPak.Read(File.ReadAllBytes(Repo.P("contract/fixtures/tiny.cheats.pak")));
        File.WriteAllBytes(Path.Combine(root, "aether", "CHEATS.PAK"), CheatPak.Write(new Dictionary<string, CheatRecord> { ["T-00002"] = shipped["GS-9001"] }));
    }

    [Fact]
    public async Task The_open_card_is_filled_in_and_a_picked_game_attaches_its_launch_options()
    {
        using var t = new TempCard();
        Card(t.Dir);
        string? opened = null;
        var (ctx, cardField, details) = await HeadlessApp.Run(() =>
        {
            var m = new MainWindow(removableDriveListForTests: () => []);
            m.OpenCard(t.Dir);
            var c = m.FeedbackContextFor();
            m.Close();
            var w = new FeedbackWindow("0.1", FeedbackKind.Bug, c) { OpenUrlForTests = u => opened = u };
            w.Show();
            string card = ((TextBox)w.Input("card")).Text ?? "";
            w.Set("game", c.Games.Single(g => g.Details.Contains("T-00002", StringComparison.Ordinal)).Name);
            string d = ((TextBox)w.Input("game-details")).Text ?? "";
            foreach (var (id, v) in Filled[FeedbackKind.Bug]) w.Set(id, v);
            w.Submit();
            return Task.FromResult((c, card, d));
        });
        Assert.EndsWith("2 games", ctx.CardText);
        Assert.Contains(" free, ", ctx.CardText);                                          // size, file system, label, free space
        Assert.Equal(ctx.CardText, cardField);
        Assert.Contains("Title: Beta Game", details);
        Assert.Contains("File: /Games/Beta/Beta.iso", details);
        Assert.Contains("Loader: JHL", details);
        Assert.Contains("Saves: on, slot 3", details);
        Assert.Contains("Cheats: on, 2 on", details);                                     // CHEATS.PAK: two cheats switched on
        Assert.Contains("In Game Reset: off", details);
        Assert.Contains("Loader: Auto (JHL)", ctx.Games.Single(g => g.Details.Contains("T-00001", StringComparison.Ordinal)).Details);   // GAMES.DB's hint
        var q = Q(opened!);
        Assert.Equal(details, q["game-details"]);
        Assert.Contains("### Game\n\n" + details, q["body"]);
        Assert.Contains("### SD card\n\n" + ctx.CardText, q["body"]);
    }

    /// <summary>`key: value` (or `- value` for a dropdown option), plain or JSON-quoted as the generator writes it.</summary>
    static bool Has(string yml, string key, string value)
    {
        string sep = key.EndsWith('-') ? " " : ": ";
        string quoted = JsonSerializer.Serialize(value, new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
        return yml.Contains($"{key}{sep}{value}\n", StringComparison.Ordinal) || yml.Contains($"{key}{sep}{quoted}\n", StringComparison.Ordinal);
    }

    [Fact]
    public void Each_issue_form_has_the_same_fields_options_and_required_ones_as_its_type()
    {
        foreach (var f in FeedbackForms.All)
        {
            string yml = File.ReadAllText(Path.Combine(Repo.Root, ".github", "ISSUE_TEMPLATE", f.Template)).Replace("\r\n", "\n");
            Assert.Contains($"labels: [{f.Label}]", yml);
            Assert.Contains($"title: \"{f.TitlePrefix}\"", yml);
            foreach (var fld in f.All)
            {
                int at = yml.IndexOf($"    id: {fld.Id}\n", StringComparison.Ordinal);
                Assert.True(at > 0, $"{f.Template}: no {fld.Id}");
                int end = yml.IndexOf("  - type:", at, StringComparison.Ordinal);
                string block = yml[at..(end < 0 ? yml.Length : end)];
                Assert.True(Has(block, "      label", fld.Label), $"{f.Template} {fld.Id}: label");
                foreach (var c in fld.Choices ?? [])
                    Assert.True(fld.Checks ? Has(block, "        - label", c) : Has(block, "        -", c), $"{f.Template} {fld.Id}: {c}");
                // and no option of the form's that the app does not offer
                Assert.Equal((fld.Choices ?? []).Count(), block.Split('\n').Count(l => l.StartsWith("        - ", StringComparison.Ordinal)));
                if (fld.Help != "") Assert.Contains(fld.Help, block);
                Assert.Equal(fld.Required, block.Contains("required: true", StringComparison.Ordinal));
                if (fld.Checks) Assert.StartsWith("checkboxes", yml[(yml.LastIndexOf("type: ", at, StringComparison.Ordinal) + 6)..]);
            }
            foreach (var id in new[] { "aether-version", "card-manager-version", "os" }) Assert.Contains("id: " + id, yml);
            Assert.Equal(f.Kind != FeedbackKind.Feature, yml.Contains("id: game-details", StringComparison.Ordinal));   // the game picker: bugs and UX
        }
    }

    [Fact]
    public void Your_setup_asks_which_modem_from_exactly_none_XBAND_or_NetLink()
    {
        foreach (var f in FeedbackForms.All)
        {
            var modem = f.All.Single(x => x.Id == "netlink");
            Assert.Equal("Modem", modem.Label);
            Assert.Equal(["None", "JP XBAND Modem", "US NetLink Modem"], modem.Choices!);
        }
    }

    const string Diag =
        "# Aether console setup\nFORMAT=1\nAETHER=0.1 2bd1de1 2026-11-25\nDATE=2026-11-25 14:03:05\nSATIATOR_FW=163\n" +
        "BIOS=BTR_1.0191941228\nAREA=J\nAREA_CODE=1\nVIDEO=NTSC60\nCART=RAM4MB\nCART_ID=5C\nCART_BRAM=NONE\nCART_ROM=NONE\n" +
        "NETLINK=UNKNOWN\nPORT1=PAD\nPORT2=MULTITAP:3DPAD_ANALOG,PAD,MOUSE\nRTC=UNSET\nBRAM=509/512 32KB\nGAMES=689\nLOADER=AUTO\n";

    static (FeedbackWindow W, Dictionary<string, string> Values) Open(ConsoleSetup? diag, Action<FeedbackWindow>? before = null)
    {
        var w = new FeedbackWindow("0.1", FeedbackKind.Bug, new FeedbackContext("", [], diag));
        w.Show();
        before?.Invoke(w);
        var v = w.Report().Values.ToDictionary(kv => kv.Key, kv => kv.Value);
        return (w, v);
    }

    [Fact]
    public async Task The_consoles_diag_txt_prefills_your_setup_with_a_note_and_the_pad_hint()
    {
        var (values, note, hint1, hint2, body, newer) = await HeadlessApp.Run(() =>
        {
            var (w, v) = Open(ConsoleSetup.Parse(Diag));
            string n = w.DetectedNote.IsVisible ? w.DetectedNote.Text ?? "" : "";
            var (h1, h2) = (w.Hint("port1"), w.Hint("port2"));
            string b = w.Report().Body();
            w.Close();
            var (w2, _) = Open(ConsoleSetup.Parse(Diag.Replace("FORMAT=1", "FORMAT=2")));
            string nn = w2.DetectedNote.Text ?? "";
            w2.Close();
            return Task.FromResult((v, n, h1, h2, b, nn));
        });
        Assert.Equal("Detected by Aether on 2026-11-25 14:03 — check and correct anything below.", note);
        Assert.Equal(("Japan", "FW163", "4MB RAM", "Yes"), (values["console-region"], values["satiator-firmware"], values["cart"], values["clock-prompt"]));
        Assert.Equal("Standard pad (Model 2 / Japanese style)", values["port1"]);
        Assert.Contains(DetectedSetup.PadHint, hint1);                                           // PAD may be a 3D pad in digital mode
        Assert.Equal("", hint2);
        Assert.Equal(("Multitap (6 Player)", "3"), (values["port2"], values["port2-tap-count"]));
        Assert.Equal("3D Control Pad (analog mode), Control Pad (or 3D Control Pad in digital mode), Mouse", values["port2-tap-types"]);   // not "Shuttle Mouse"
        Assert.Contains("<details><summary>Detected setup (Aether's DIAG.TXT)</summary>", body);
        Assert.Contains("PORT2=MULTITAP:3DPAD_ANALOG,PAD,MOUSE\n", body);
        Assert.Contains("made by a newer Aether", newer, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_users_own_answer_is_never_overwritten_and_no_diag_txt_leaves_the_form_as_it_was()
    {
        var (edited, kept, plain, noNote) = await HeadlessApp.Run(() =>
        {
            // the user corrects the region, then changes the report type: the correction stays
            var (w, _) = Open(ConsoleSetup.Parse(Diag), x => { x.Set("console-region", "USA"); x.Kind = FeedbackKind.Ux; x.Kind = FeedbackKind.Bug; });
            var r = w.Report().Values;
            w.Close();
            var (w2, v2) = Open(null);
            bool hidden = !w2.DetectedNote.IsVisible && w2.Report().DetectedRaw == "";
            w2.Close();
            return Task.FromResult((r["console-region"], r["satiator-firmware"], v2, hidden));
        });
        Assert.Equal(("USA", "FW163"), (edited, kept));
        Assert.All(plain, kv => Assert.Equal("", kv.Value));                                     // nothing pre-filled without DIAG.TXT
        Assert.True(noNote);
    }

    [Fact]
    public void DIAG_TXT_maps_a_cart_rom_kai_a_backup_cart_and_an_unknown_device()
    {
        var kai = DetectedSetup.Values(ConsoleSetup.Parse("FORMAT=1\nCART=OTHER\nCART_ROM=PSKAI 6.545\nPORT1=GUN\nPORT2=ID_E3\nRTC=OK\n"));
        Assert.Equal(("Action Replay / Pro Action Replay", "Flashed with Pseudo Saturn Kai"), (kai["cart"], kai["cart-ar"]));
        Assert.Equal(("Virtua Gun / Stunner", "USB adapter / other", "Unrecognised peripheral (ID E3)", "No"), (kai["port1"], kai["port2"], kai["port2-name"], kai["clock-prompt"]));
        var bram = DetectedSetup.Values(ConsoleSetup.Parse("FORMAT=1\nCART=OTHER\nCART_BRAM=8000/8192 512KB\nAREA=E\n"));
        Assert.Equal(("Backup memory cart", "512 KB", "Europe"), (bram["cart"], bram["cart-backup-size"], bram["console-region"]));
    }
}
