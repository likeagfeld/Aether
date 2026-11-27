// Aether Card Manager, GPL-3.0-or-later.
// Help > Send feedback: a bug, a feature request or a user-experience issue, as a short form that opens a prefilled
// GitHub "new issue" page for the Aether repo -- the matching issue form (.github/ISSUE_TEMPLATE/bug.yml, feature.yml,
// ux.yml), its fields filled by their ids, the whole report also in body= for a blank issue. A "Your setup" section
// (open for a bug or a UX issue) asks for the console, its health and mods, the cartridge, both controller ports, link,
// video and the Satiator, fills in the open card, and attaches a picked game with its Launch Options. No token, nothing
// is sent from here: the browser shows the issue and the user submits it.
using System.Runtime.InteropServices;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Layout;
using Avalonia.Media;
using VibeMenuCardManager.Core.About;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.UI;

public enum FeedbackKind { Bug, Feature, Ux }

/// <summary>A field shown only while `Id`'s value is (or, for checkboxes, includes) one of `Values`.</summary>
public sealed record FeedbackWhen(string Id, string[] Values);

/// <param name="Id">the issue form's field id</param>
/// <param name="Choices">a dropdown's (or the checkboxes') options, exactly the form's; null for text</param>
/// <param name="Help">a line of help under the label (the form's description)</param>
/// <param name="Group">its heading in "Your setup" ("" = one of the type's own fields)</param>
public sealed record FeedbackField(string Id, string Label, bool Required, bool Multi, string Placeholder, string[]? Choices = null,
                                   string Help = "", FeedbackWhen? When = null, string Group = "", bool Checks = false);

public sealed record FeedbackForm(FeedbackKind Kind, string Name, string Template, string Label, string TitlePrefix, string Intro,
                                  FeedbackField[] Fields, FeedbackField[] Setup)
{
    public IEnumerable<FeedbackField> All => Fields.Concat(Setup);
}

/// <summary>What the Card Manager knows and fills in: the open card, and its games with their details.</summary>
/// <param name="Detected">the console's own DIAG.TXT on the open card (what Aether detected at start-up), else null</param>
public sealed record FeedbackContext(string CardText, IReadOnlyList<(string Name, string Details)> Games, ConsoleSetup? Detected = null)
{
    public static readonly FeedbackContext None = new("", []);
}

/// <summary>DIAG.TXT's answers as "Your setup" values (only what it knows; the user checks and corrects them).</summary>
public static class DetectedSetup
{
    public const string PadHint = "(a 3D Control Pad in digital (+) mode also shows as this)";
    const string StandardPad = "Standard pad (Model 2 / Japanese style)";

    public static Dictionary<string, string> Values(ConsoleSetup d)
    {
        var v = new Dictionary<string, string>();
        if (d.Area is { } a)
            v["console-region"] = a switch
            {
                ConsoleArea.Japan => "Japan", ConsoleArea.NorthAmerica => "USA", ConsoleArea.Europe => "Europe", ConsoleArea.Korea => "Korea",
                ConsoleArea.AsiaNtsc or ConsoleArea.AsiaPal => "Asia", _ => "Brazil",
            };
        if (d.SatiatorFirmware is int fw) v["satiator-firmware"] = $"FW{fw}";
        if (d.HasCartRom == true)
        {
            v["cart"] = "Action Replay / Pro Action Replay";
            if (d.CartRomIsPseudoSaturnKai) v["cart-ar"] = "Flashed with Pseudo Saturn Kai";
        }
        else if (d.HasCartBram == true)
        {
            v["cart"] = "Backup memory cart";
            if (d.CartBram is { } b) v["cart-backup-size"] = $"{b.SizeKb} KB";
        }
        else if (d.Cart is { } c)
            v["cart"] = c switch { CartKind.None => "None", CartKind.Ram1MB => "1MB RAM", CartKind.Ram4MB => "4MB RAM", _ => "Other (describe)" };
        Port(v, "port1", d.Port1);
        Port(v, "port2", d.Port2);
        if (d.Clock == ClockState.Unset) v["clock-prompt"] = "Yes";
        else if (d.Clock == ClockState.Ok) v["clock-prompt"] = "No";
        return v;
    }

    static void Port(Dictionary<string, string> v, string id, ControllerPort? p)
    {
        if (p == null) return;
        if (p.IsMultitap)
        {
            v[id] = "Multitap (6 Player)";
            v[id + "-tap-count"] = Math.Clamp(p.Devices.Count, 1, 6).ToString(System.Globalization.CultureInfo.InvariantCulture);
            v[id + "-tap-types"] = string.Join(", ", p.Devices.Select(ConsoleSetup.DescribeDevice));
            return;
        }
        string token = p.Devices.Count == 0 ? "NONE" : p.Devices[0].ToUpperInvariant();
        switch (token)
        {
            case "NONE": v[id] = "None"; break;
            case "PAD": v[id] = StandardPad; break;
            case "3DPAD_ANALOG": v[id] = "3D Control Pad"; v[id + "-3d-mode"] = "Analog (○)"; break;
            case "ANALOG": v[id] = "Mission Stick"; break;
            case "RACER": v[id] = "Arcade Racer"; break;
            case "MOUSE": v[id] = "Shuttle Mouse"; break;
            case "GUN": v[id] = "Virtua Gun / Stunner"; break;
            case "KEYBOARD": v[id] = "Keyboard (NetLink)"; break;
            default: v[id] = "USB adapter / other"; v[id + "-name"] = ConsoleSetup.DescribeDevice(token); break;
        }
    }

    /// <summary>DIAG.TXT as it was read, one KEY=VALUE per line, capped for the link.</summary>
    public static string RawText(ConsoleSetup d, int max = 1500)
    {
        string t = string.Join("\n", d.Raw.Select(kv => $"{kv.Key}={kv.Value}"));
        return t.Length <= max ? t : t[..max] + "\n...";
    }
}

public static class FeedbackForms
{
    public static readonly string[] Places = ["The menu on the console", "The Card Manager", "Both"];
    public const string OtherModel = "Other (describe)";
    public const string ModelHelp = "Look at the console's shape (oval or round Power/Reset buttons) and the model number on the label underneath (e.g. MK-80000, HST-3200, HST-3220).";
    const string PhotoHint = "Optional: the photo's file name; drag it into the issue page";
    static readonly FeedbackField Photo = new("photo", "Photo or screenshot", false, false, PhotoHint);

    // ---- generated from one spec with the three issue forms (FeedbackTests checks they match) ----
    static FeedbackField[] SetupFields(FeedbackKind k) =>
    [
        new("saturn-model", "Saturn model", k == FeedbackKind.Bug || k == FeedbackKind.Ux, false, "Choose your console", ["Model 1 (oval buttons)", "Model 2 (round buttons)", "Japanese white", "Japanese grey", "Japanese skeleton", "Hi-Saturn (Hitachi)", "V-Saturn (Victor)", "Samsung Saturn", "Sega Saturn with a modded BIOS or region switch", "I don't know", "Other (describe)"], "Look at the console's shape (oval or round Power/Reset buttons) and the model number on the label underneath (e.g. MK-80000, HST-3200, HST-3220).", null, "Console"),
        new("saturn-model-other", "Other model (describe)", false, false, "What it is", null, "", new("saturn-model", ["Other (describe)"]), "Console"),
        new("model-number", "Model number from the label", false, false, "e.g. HST-3220", null, "", null, "Console"),
        new("console-region", "Console region", k == FeedbackKind.Bug, false, "Choose your console's region", ["Japan", "USA", "Europe", "Asia", "Brazil", "Korea", "I don't know"], "", null, "Console"),
        new("region-mod", "Region mod or switch", false, false, "", ["None", "Region switch", "Modchip", "Modded BIOS", "I don't know"], "", null, "Console"),
        new("hz-switch", "50/60 Hz switch", false, false, "", ["None", "50 Hz", "60 Hz", "I don't know"], "", null, "Console"),
        new("battery", "Internal battery last changed", false, false, "", ["Never / original", "Within a year", "1-3 years", "Over 3 years", "I don't know"], "", null, "Console health"),
        new("clock-prompt", "Does the console ask you to set the clock at power on?", false, false, "", ["Yes", "No"], "", null, "Console health"),
        new("psu", "Power supply", false, false, "", ["Original", "Original, recapped", "Aftermarket replacement PSU", "Step-down transformer only (Japanese console abroad)", "I don't know"], "", null, "Console health"),
        new("psu-name", "Replacement PSU (name or brand)", false, false, "e.g. a Saturn PSU replacement board", null, "", new("psu", ["Aftermarket replacement PSU"]), "Console health"),
        new("mods", "Console mods", false, false, "", ["None", "Recapped mainboard", "Region switch or modchip", "Modded BIOS / BIOS switch", "50/60 Hz switch", "RGB/video mod", "Quiet or replacement fan", "Clock battery holder mod", "ODE other than Satiator also installed", "Controller port or expansion mods", "Other"], "", null, "Console mods", Checks: true),
        new("mods-video", "RGB/video mod (name it)", false, false, "e.g. a built-in RGB or HDMI kit", null, "", new("mods", ["RGB/video mod"]), "Console mods"),
        new("mods-ode", "Other ODE installed", false, false, "", ["Fenrir", "Rhea/Phoebe", "MODE", "Other"], "", new("mods", ["ODE other than Satiator also installed"]), "Console mods"),
        new("mods-other", "Other mods (describe)", false, false, "", null, "", new("mods", ["Other"]), "Console mods"),
        new("cart", "Cartridge slot", false, false, "", ["None", "1MB RAM", "4MB RAM", "Backup memory cart", "Action Replay / Pro Action Replay", "NetLink modem", "Other (describe)"], "", null, "Cartridge slot"),
        new("cart-backup-size", "Backup memory cart size", false, false, "e.g. 4 Mbit", null, "", new("cart", ["Backup memory cart"]), "Cartridge slot"),
        new("cart-ar", "Action Replay", false, false, "", ["Stock", "Flashed with Pseudo Saturn Kai", "4-in-1", "Other"], "", new("cart", ["Action Replay / Pro Action Replay"]), "Cartridge slot"),
        new("cart-other", "Other cartridge (describe)", false, false, "", null, "", new("cart", ["Other (describe)"]), "Cartridge slot"),
        new("port1", "Port 1", false, false, "Choose the device", ["None", "Standard pad (Model 1 style)", "Standard pad (Model 2 / Japanese style)", "3D Control Pad", "Arcade Racer", "Mission Stick", "Virtua Gun / Stunner", "Shuttle Mouse", "Keyboard (NetLink)", "Multitap (6 Player)", "Wireless pad", "Third-party pad", "USB adapter / other"], "", null, "Controller ports"),
        new("port1-3d-mode", "Port 1: 3D Control Pad mode switch", false, false, "", ["Analog (○)", "Digital (+)", "I don't know"], "", new("port1", ["3D Control Pad"]), "Controller ports"),
        new("port1-stick", "Port 1: Mission Stick", false, false, "", ["Single", "Dual"], "", new("port1", ["Mission Stick"]), "Controller ports"),
        new("port1-tap-count", "Port 1: controllers on the multitap", false, false, "", ["1", "2", "3", "4", "5", "6"], "", new("port1", ["Multitap (6 Player)"]), "Controller ports"),
        new("port1-tap-types", "Port 1: the multitap's controllers", false, false, "e.g. 4 standard pads, 1 3D Control Pad", null, "", new("port1", ["Multitap (6 Player)"]), "Controller ports"),
        new("port1-name", "Port 1: which one", false, false, "e.g. 8BitDo M30 2.4G, Retro-Bit", null, "", new("port1", ["Third-party pad", "USB adapter / other"]), "Controller ports"),
        new("port2", "Port 2", false, false, "Choose the device", ["None", "Standard pad (Model 1 style)", "Standard pad (Model 2 / Japanese style)", "3D Control Pad", "Arcade Racer", "Mission Stick", "Virtua Gun / Stunner", "Shuttle Mouse", "Keyboard (NetLink)", "Multitap (6 Player)", "Wireless pad", "Third-party pad", "USB adapter / other"], "", null, "Controller ports"),
        new("port2-3d-mode", "Port 2: 3D Control Pad mode switch", false, false, "", ["Analog (○)", "Digital (+)", "I don't know"], "", new("port2", ["3D Control Pad"]), "Controller ports"),
        new("port2-stick", "Port 2: Mission Stick", false, false, "", ["Single", "Dual"], "", new("port2", ["Mission Stick"]), "Controller ports"),
        new("port2-tap-count", "Port 2: controllers on the multitap", false, false, "", ["1", "2", "3", "4", "5", "6"], "", new("port2", ["Multitap (6 Player)"]), "Controller ports"),
        new("port2-tap-types", "Port 2: the multitap's controllers", false, false, "e.g. 4 standard pads, 1 3D Control Pad", null, "", new("port2", ["Multitap (6 Player)"]), "Controller ports"),
        new("port2-name", "Port 2: which one", false, false, "e.g. 8BitDo M30 2.4G, Retro-Bit", null, "", new("port2", ["Third-party pad", "USB adapter / other"]), "Controller ports"),
        new("link-cable", "Link cable (Taisen / Communication cable) connected", false, false, "", ["Yes", "No"], "", null, "Link and online"),
        new("netlink", "Modem", false, false, "", ["None", "JP XBAND Modem", "US NetLink Modem"], "", null, "Link and online"),
        new("video-cable", "Video cable", false, false, "", ["Composite", "S-Video", "RGB SCART", "Component (via transcoder)", "Other"], "", null, "Video"),
        new("scaler", "Through an upscaler or scaler?", false, false, "", ["None", "RetroTINK", "OSSC", "Framemeister", "Morph", "Other"], "", null, "Video"),
        new("display", "Display", false, false, "", ["CRT", "Flat TV", "Monitor"], "", null, "Video"),
        new("satiator-firmware", "Satiator firmware", k == FeedbackKind.Bug, false, "e.g. FW163", null, "The version Aether's diagnostics page shows, e.g. FW163.", null, "Satiator and card"),
        new("card", "SD card", false, true, "Filled in from the open card", null, "", null, "Satiator and card"),
        .. (k is FeedbackKind.Bug or FeedbackKind.Ux ? new FeedbackField[] { new("game", "Which game?", false, false, "Pick a game from the open card", [], "", null, "Which game?") } : []),
        .. (k is FeedbackKind.Bug or FeedbackKind.Ux ? new FeedbackField[] { new("game-details", "The game and its Launch Options", false, true, "Filled in when you pick a game", null, "", null, "Which game?") } : []),
    ];
    // ---- end of generated ----

    public static readonly FeedbackForm Bug = new(FeedbackKind.Bug, "Bug", "bug.yml", "bug", "[Bug] ", "Something doesn't work as it should.",
    [
        new("what-happened", "What happened?", true, true, "The menu froze after I picked a game..."),
        new("expected", "What did you expect?", true, true, "The game should start."),
        new("steps", "Steps to reproduce", true, true, "1. Open the Favorites view\n2. Pick NiGHTS\n3. ..."),
        Photo,
    ], SetupFields(FeedbackKind.Bug));

    public static readonly FeedbackForm Feature = new(FeedbackKind.Feature, "Feature request", "feature.yml", "enhancement", "[Feature] ",
        "Something you'd like Aether or the Card Manager to do.",
    [
        new("wish", "What would you like?", true, true, "Let me sort the list by release year..."),
        new("why", "Why? What problem does it solve?", true, true, "With 700 games I can't find the ones from one year."),
        new("where", "Where does it fit?", true, false, "Choose where", Places),
        new("example", "An example or reference", false, true, "Optional: another menu or app that does this, a sketch, a link"),
    ], SetupFields(FeedbackKind.Feature));

    public static readonly FeedbackForm Ux = new(FeedbackKind.Ux, "User experience issue", "ux.yml", "ux", "[UX] ",
        "Something works, but felt wrong, slow or confusing.",
    [
        new("where", "Where? (screen or feature)", true, false, "e.g. Launch Options, the Card Manager's Covers view"),
        new("what-felt-wrong", "What felt wrong or confusing?", true, true, "I couldn't tell which save slot was in use."),
        new("prefer", "What did you expect, or would you prefer?", true, true, "The slot in use highlighted."),
        Photo,
    ], SetupFields(FeedbackKind.Ux));

    public static readonly FeedbackForm[] All = [Bug, Feature, Ux];
    public static FeedbackForm For(FeedbackKind k) => All.Single(f => f.Kind == k);

    /// <summary>Whether `f` applies given the other values (a field with no condition always does).</summary>
    public static bool Shown(FeedbackField f, Func<string, string> value) =>
        f.When is not { } w || value(w.Id).Split("; ", StringSplitOptions.RemoveEmptyEntries).Intersect(w.Values).Any();
}

/// <param name="Values">field id -> what the user entered (checkboxes: the checked options joined by "; ")</param>
/// <param name="DetectedRaw">the console's DIAG.TXT lines, for a collapsed "Detected setup" block ("" = none)</param>
public sealed record FeedbackReport(FeedbackKind Kind, IReadOnlyDictionary<string, string> Values, string AetherVersion, string CardManagerVersion, string Os,
                                    string DetectedRaw = "")
{
    public FeedbackForm Form => FeedbackForms.For(Kind);

    /// <summary>A field's value; "" for one whose condition is not met (a hidden field never reaches the issue).</summary>
    public string V(string id)
    {
        var f = Form.All.FirstOrDefault(x => x.Id == id);
        string raw = (Values.GetValueOrDefault(id) ?? "").Trim();
        return f == null || FeedbackForms.Shown(f, i => (Values.GetValueOrDefault(i) ?? "").Trim()) ? raw : "";
    }

    /// <summary>The required fields still empty, by label (empty list: ready to send).</summary>
    public List<string> Missing()
    {
        var m = Form.All.Where(f => f.Required && V(f.Id) == "").Select(f => f.Label.TrimEnd('?')).ToList();
        if (V("saturn-model") == FeedbackForms.OtherModel && V("saturn-model-other") == "") m.Add("Other model (describe)");
        return m;
    }

    public string Title()
    {
        string first = V(Form.Fields[0].Id).Split('\n')[0].Trim();
        if (first.Length > 80) first = first[..77].TrimEnd() + "...";
        return Form.TitlePrefix + first;
    }

    /// <summary>Every field and the automatic ones, each id with its value (the form's own prefill).</summary>
    public List<(string Id, string Value)> Fields() =>
        [.. Form.All.Select(f => (f.Id, V(f.Id))), ("aether-version", AetherVersion), ("card-manager-version", CardManagerVersion), ("os", Os)];

    /// <summary>The type's own sections, then one compact "Setup" block (a line per answered setup question), the card
    /// and the game as their own sections, then the versions.</summary>
    public string Body()
    {
        var b = new StringBuilder();
        void Section(string h, string v) { b.Append("### ").Append(h).Append("\n\n").Append(v == "" ? "_No response_" : v).Append("\n\n"); }
        foreach (var f in Form.Fields)
            Section(f.Label, f.Id == "photo" && V(f.Id) != "" ? V(f.Id) + "\n\n(Drag the file into this issue to attach it.)" : V(f.Id));
        var setup = Form.Setup.Where(f => f.Id is not ("card" or "game" or "game-details") && V(f.Id) != "").Select(f => $"- {f.Label}: {V(f.Id)}").ToList();
        if (setup.Count > 0) Section("Setup", string.Join("\n", setup));
        if (V("card") != "") Section("SD card", V("card"));
        if (V("game-details") != "") Section("Game", V("game-details"));
        if (DetectedRaw != "")
            b.Append("<details><summary>Detected setup (Aether's DIAG.TXT)</summary>\n\n```\n").Append(DetectedRaw).Append("\n```\n</details>\n\n");
        Section("Aether version", AetherVersion);
        Section("Card Manager version", CardManagerVersion);
        Section("Operating system", Os);
        return b.ToString().TrimEnd() + "\n";
    }
}

public static class FeedbackUrl
{
    public const string NewIssue = "https://github.com/likeagfeld/Aether/issues/new";
    /// <summary>GitHub refuses very long URLs (about 8 KB); stay well under it.</summary>
    public const int MaxLength = 7500;
    public const string TruncatedNote = " [cut to fit the link: please paste the rest here]";

    /// <summary>The prefilled new-issue URL for the report's own form, and whether a field had to be cut to fit
    /// MaxLength: every field and the whole body first; too long, the body goes (the form still has every field);
    /// still too long, the free-text fields are cut to one common length, each cut one saying so.</summary>
    public static (string Url, bool Truncated) Build(FeedbackReport r)
    {
        string title = r.Title(), body = r.Body();
        var fields = r.Fields();
        var freeText = r.Form.All.Where(f => f.Multi || f.Id == "photo").Select(f => f.Id).ToHashSet();
        string Make(IEnumerable<(string Id, string Value)> fs, string? bd) =>
            $"{NewIssue}?template={r.Form.Template}&labels={r.Form.Label}&title={Uri.EscapeDataString(title)}" +
            string.Concat(fs.Where(f => f.Value != "").Select(f => $"&{f.Id}={Uri.EscapeDataString(f.Value)}")) +
            (bd != null ? $"&body={Uri.EscapeDataString(bd)}" : "");
        string full = Make(fields, body);
        if (full.Length <= MaxLength) return (full, false);
        string noBody = Make(fields, null);
        if (noBody.Length <= MaxLength) return (noBody, false);
        List<(string, string)> Capped(int n) => [.. fields.Select(f => freeText.Contains(f.Id) && f.Value.Length > n ? (f.Id, Cut(f.Value, n) + TruncatedNote) : f)];
        int lo = 0, hi = fields.Max(f => f.Value.Length);
        while (lo < hi)
        {
            int mid = (lo + hi + 1) / 2;
            if (Make(Capped(mid), null).Length <= MaxLength) lo = mid; else hi = mid - 1;
        }
        return (Make(Capped(lo), null), true);
    }

    static string Cut(string s, int n)
    {
        if (n > 0 && n < s.Length && char.IsHighSurrogate(s[n - 1])) n--;
        return s[..n];
    }
}

public sealed class FeedbackWindow : Window
{
    public readonly ComboBox KindBox = new() { ItemsSource = FeedbackForms.All.Select(f => f.Name).ToList(), SelectedIndex = 0, HorizontalAlignment = HorizontalAlignment.Stretch };
    public readonly TextBlock Problem = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Warn"), TextWrapping = TextWrapping.Wrap };
    public readonly Button OpenButton = new() { Content = "Open on GitHub", IsDefault = true, FontWeight = FontWeight.Bold, Classes = { "primary" } };
    public readonly Expander SetupSection = new() { Header = "Your setup", HorizontalAlignment = HorizontalAlignment.Stretch, Margin = new Thickness(0, 10, 0, 0) };
    public readonly string AetherVersion, CardManagerVersion, Os;
    readonly FeedbackContext _ctx;
    readonly StackPanel _fields = new() { Spacing = 6 };
    readonly StackPanel _setup = new() { Spacing = 6 };
    readonly TextBlock _intro = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), TextWrapping = TextWrapping.Wrap };
    readonly Dictionary<string, Control> _inputs = [];
    readonly Dictionary<string, Control> _rows = [];
    readonly Dictionary<string, TextBlock> _hints = [];
    /// <summary>"Detected by Aether on ..." at the top of "Your setup" (hidden with no DIAG.TXT).</summary>
    public readonly TextBlock DetectedNote = new() { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Acc2"), TextWrapping = TextWrapping.Wrap, IsVisible = false, Margin = new Thickness(0, 4, 0, 0) };
    bool _building, _prefilled;

    /// <summary>The detection hint shown under a field ("" = none), e.g. Port 1's PAD caveat.</summary>
    public string Hint(string id) => _hints.TryGetValue(id, out var t) && t.IsVisible ? t.Text ?? "" : "";

    /// <summary>The last URL opened (tests); OpenUrlForTests opens it instead of the browser, per window.</summary>
    public string? LastUrl { get; private set; }
    public Action<string>? OpenUrlForTests;

    public FeedbackKind Kind
    {
        get => FeedbackForms.All[Math.Max(0, KindBox.SelectedIndex)].Kind;
        set => KindBox.SelectedIndex = Array.FindIndex(FeedbackForms.All, f => f.Kind == value);
    }

    /// <summary>The current form's input for a field id (a TextBox, a ComboBox, or the checkboxes' panel).</summary>
    public Control Input(string id) => _inputs[id];

    /// <summary>Whether a field is on screen now (a conditional one only while its condition holds).</summary>
    public bool IsShown(string id) => _rows.TryGetValue(id, out var r) && r.IsVisible;

    /// <summary>Sets a field: a TextBox's text, a dropdown's option, or the checkboxes named in "A; B".</summary>
    public void Set(string id, string value)
    {
        switch (_inputs[id])
        {
            case ComboBox c: c.SelectedItem = value == "" ? null : value; break;
            case TextBox t: t.Text = value; break;
            case Panel p:
                var on = value.Split("; ", StringSplitOptions.RemoveEmptyEntries).ToHashSet();
                foreach (var cb in p.Children.OfType<CheckBox>()) cb.IsChecked = on.Contains(cb.Content as string ?? "");
                break;
        }
        UpdateVisibility();
    }

    string Value(string id) => _inputs.GetValueOrDefault(id) switch
    {
        TextBox t => t.Text ?? "",
        ComboBox c => c.SelectedItem as string ?? "",
        Panel p => string.Join("; ", p.Children.OfType<CheckBox>().Where(cb => cb.IsChecked == true).Select(cb => cb.Content as string)),
        _ => "",
    };

    /// <param name="aetherVersion">the version installed on the open card, else null (the bundled one is reported).</param>
    /// <param name="context">the open card and its games, filled in and offered by the form (null: no card open).</param>
    public FeedbackWindow(string? aetherVersion = null, FeedbackKind kind = FeedbackKind.Bug, FeedbackContext? context = null)
    {
        _ctx = context ?? FeedbackContext.None;
        AetherVersion = aetherVersion ?? $"{Credits.Version} (bundled; no card open)";
        CardManagerVersion = Credits.Version;
        Os = $"{RuntimeInformation.OSDescription} ({RuntimeInformation.OSArchitecture})";
        Title = "Send feedback";
        Width = 600; Height = 780; MinWidth = 480; MinHeight = 520;
        AppTheme.ApplyToWindow(this);

        var form = new StackPanel { Spacing = 6, Margin = new Thickness(18, 16, 18, 8) };
        form.Children.Add(new TextBlock { Text = "Send feedback", FontSize = 18, FontWeight = FontWeight.Bold });
        form.Children.Add(new TextBlock { Text = "This opens a new issue on GitHub with your answers filled in. Nothing is sent until you submit it there.",
                                          [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 6) });
        form.Children.Add(new TextBlock { Text = "What is it? *", [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 12 });
        form.Children.Add(KindBox);
        form.Children.Add(_intro);
        form.Children.Add(_fields);
        SetupSection.Content = new StackPanel { Children = { DetectedNote, _setup } };
        if (_ctx.Detected is { } det)
        {
            DetectedNote.Text = (det.WrittenOn is DateTime on ? $"Detected by Aether on {on:yyyy-MM-dd HH:mm}" : "Detected by Aether")
                                + " — check and correct anything below."
                                + (det.IsNewerFormat ? " (Made by a newer Aether: some of what it found may not be shown here.)" : "");
            DetectedNote.IsVisible = true;
        }
        form.Children.Add(SetupSection);
        var auto = new Grid { ColumnDefinitions = new ColumnDefinitions("150,*"), RowDefinitions = new RowDefinitions("Auto,Auto,Auto"), RowSpacing = 2, Margin = new Thickness(0, 10, 0, 0) };
        int row = 0;
        foreach (var (k, v) in new[] { ("Aether", AetherVersion), ("Card Manager", CardManagerVersion), ("Operating system", Os) })
        {
            var l = new TextBlock { Text = k, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 12 };
            var t = new TextBlock { Text = v, FontSize = 12, TextWrapping = TextWrapping.Wrap };
            Grid.SetRow(l, row); Grid.SetRow(t, row); Grid.SetColumn(t, 1);
            auto.Children.Add(l); auto.Children.Add(t);
            row++;
        }
        form.Children.Add(auto);
        form.Children.Add(Problem);

        var cancel = new Button { Content = "Cancel", IsCancel = true };
        cancel.Click += (_, _) => Close();
        OpenButton.Click += (_, _) => Submit();
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right,
                                       Margin = new Thickness(18, 8, 18, 16), Children = { cancel, OpenButton } };
        DockPanel.SetDock(buttons, Dock.Bottom);
        Content = new DockPanel { Children = { buttons, new ScrollViewer { Content = form } } };

        KindBox.SelectionChanged += (_, _) => BuildFields();
        Kind = kind;
        BuildFields();
    }

    Control MakeInput(FeedbackField fld)
    {
        if (fld.Checks)
        {
            var panel = new WrapPanel();
            foreach (var c in fld.Choices!)
            {
                var cb = new CheckBox { Content = c, Margin = new Thickness(0, 0, 12, 2) };
                cb.IsCheckedChanged += (_, _) => UpdateVisibility();
                panel.Children.Add(cb);
            }
            return panel;
        }
        if (fld.Id == "game")
        {
            var g = new ComboBox { ItemsSource = _ctx.Games.Select(x => x.Name).ToList(), PlaceholderText = _ctx.Games.Count > 0 ? fld.Placeholder : "Open a card to pick one of its games",
                                   HorizontalAlignment = HorizontalAlignment.Stretch, IsEnabled = _ctx.Games.Count > 0 };
            g.SelectionChanged += (_, _) =>
            {
                if (_building || _inputs.GetValueOrDefault("game-details") is not TextBox d) return;
                d.Text = g.SelectedIndex >= 0 ? _ctx.Games[g.SelectedIndex].Details : "";
            };
            return g;
        }
        if (fld.Choices != null)
        {
            var c = new ComboBox { ItemsSource = fld.Choices, PlaceholderText = fld.Placeholder == "" ? "Choose" : fld.Placeholder, HorizontalAlignment = HorizontalAlignment.Stretch };
            c.SelectionChanged += (_, _) => UpdateVisibility();
            return c;
        }
        return new TextBox { PlaceholderText = fld.Placeholder, AcceptsReturn = fld.Multi, TextWrapping = TextWrapping.Wrap,
                             MinHeight = fld.Multi ? 64 : 0, MaxHeight = fld.Multi ? 160 : double.PositiveInfinity };
    }

    /// <summary>The chosen type's fields; anything already entered into a field of the same id carries over.</summary>
    void BuildFields()
    {
        var kept = _inputs.Keys.ToDictionary(k => k, Value);
        var f = FeedbackForms.For(Kind);
        _building = true;
        _intro.Text = f.Intro;
        _fields.Children.Clear(); _setup.Children.Clear();
        _inputs.Clear(); _rows.Clear();
        Problem.Text = "";
        string group = "";
        foreach (var fld in f.All)
        {
            var target = fld.Group == "" ? _fields : _setup;
            if (fld.Group != "" && fld.Group != group)
            {
                group = fld.Group;
                target.Children.Add(new TextBlock { Text = group.ToUpperInvariant(), FontSize = 10.5, FontWeight = FontWeight.Bold, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"),
                                                    LetterSpacing = 1.2, Margin = new Thickness(0, 12, 0, 0) });
            }
            var input = MakeInput(fld);
            var rowPanel = new StackPanel { Spacing = 2 };
            rowPanel.Children.Add(new TextBlock { Text = fld.Label + (fld.Required ? " *" : ""), [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 12, Margin = new Thickness(0, 6, 0, 0) });
            if (fld.Help != "") rowPanel.Children.Add(new TextBlock { Text = fld.Help, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11, Opacity = 0.85, TextWrapping = TextWrapping.Wrap });
            rowPanel.Children.Add(input);
            var hint = new TextBlock { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), FontSize = 11, TextWrapping = TextWrapping.Wrap, IsVisible = false };
            rowPanel.Children.Add(hint);
            _hints[fld.Id] = hint;
            target.Children.Add(rowPanel);
            _inputs[fld.Id] = input;
            _rows[fld.Id] = rowPanel;
        }
        foreach (var (id, v) in kept) if (v != "" && _inputs.ContainsKey(id)) Set(id, v);
        if (_inputs.GetValueOrDefault("card") is TextBox card && (card.Text ?? "") == "") card.Text = _ctx.CardText;
        // DIAG.TXT's answers, once, into fields still empty: everything stays editable and a later type change keeps
        // whatever is there now (the user's own edits included)
        if (!_prefilled && _ctx.Detected is { } d)
        {
            _prefilled = true;
            foreach (var (id, v) in DetectedSetup.Values(d))
                if (_inputs.ContainsKey(id) && Value(id) == "") Set(id, v);
        }
        foreach (var p in new[] { "port1", "port2" })
        {
            var port = p == "port1" ? _ctx.Detected?.Port1 : _ctx.Detected?.Port2;
            if (_hints.TryGetValue(p, out var h) && port is { IsMultitap: false, Devices: ["PAD"] or ["pad"] })
            { h.Text = "Detected: Control Pad " + DetectedSetup.PadHint; h.IsVisible = true; }
        }
        // open for a bug or a UX issue (some of it is required there); a feature request may leave it closed
        SetupSection.IsExpanded = Kind != FeedbackKind.Feature;
        _building = false;
        UpdateVisibility();
    }

    void UpdateVisibility()
    {
        if (_building) return;
        foreach (var fld in FeedbackForms.For(Kind).All)
            if (_rows.TryGetValue(fld.Id, out var r)) r.IsVisible = FeedbackForms.Shown(fld, Value);
    }

    public FeedbackReport Report() => new(Kind, _inputs.Keys.ToDictionary(k => k, Value), AetherVersion, CardManagerVersion, Os,
                                          _ctx.Detected is { } d ? DetectedSetup.RawText(d) : "");

    /// <summary>Checks the required fields, then opens the prefilled issue and closes; else names what is missing.</summary>
    public bool Submit()
    {
        var r = Report();
        var missing = r.Missing();
        if (missing.Count > 0) { Problem.Text = "Please fill in: " + string.Join(", ", missing) + "."; SetupSection.IsExpanded = true; return false; }
        var (url, truncated) = FeedbackUrl.Build(r);
        LastUrl = url;
        (OpenUrlForTests ?? AboutWindow.OpenUrl)(url);
        if (truncated)
        {
            // The full text goes to the clipboard so "paste the rest" is one Ctrl+V on the issue page.
            _ = Clipboard?.SetTextAsync(r.Body());
            Problem.Text = "The report was long, so the issue page has the first part. The full report is on the clipboard: paste it there.";
            return true;
        }
        Close();
        return true;
    }
}
