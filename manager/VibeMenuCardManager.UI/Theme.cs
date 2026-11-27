// Aether Card Manager, GPL-3.0-or-later.
// The Saturn-menu look in two themes, Dark (the default: the original deep-blue look, user-approved mockup
// cm-mockup2.html) and Light. Every color the app uses lives in a Palette here, so nothing drifts and nothing is a per-control
// hack. A palette is installed as one ResourceDictionary in Application.Resources (looked up before Fluent's own
// theme resources), and every control reads it through a DynamicResource ("Aether.<Name>" for our colors, Fluent's
// own keys for the control templates' states), so View > Theme switches every open window live: AppTheme.Use swaps
// the dictionary and Fluent's RequestedThemeVariant (the native menus, scrollbars and flyouts) together. The OS
// light/dark setting is never followed; Dark is the default.
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Controls.Shapes;
using Avalonia.Controls.Primitives;
using Avalonia.Markup.Xaml.MarkupExtensions;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Styling;

namespace VibeMenuCardManager.UI;

public enum ThemeKind { Light, Dark }

/// <summary>One theme's colors. Every brush is immutable (shared safely across windows and threads); the same
/// instance is both the AppTheme.X property and the "Aether.X" resource value, so a test can compare by reference.</summary>
public sealed class Palette
{
    public required ThemeKind Kind { get; init; }
    public required Color TextColor { get; init; }
    public required IBrush WindowBackground { get; init; }
    /// <summary>The window background's lightest and darkest points (contrast tests composite against both).</summary>
    public required Color[] BackgroundStops { get; init; }
    public required IBrush Panel { get; init; }
    public required BoxShadows PaneShadow { get; init; }
    public required IBrush Line { get; init; }
    public required IBrush Text { get; init; }
    public required IBrush Muted { get; init; }
    public required IBrush Acc { get; init; }
    public required IBrush Acc2 { get; init; }
    public required IBrush Gold { get; init; }
    public required IBrush Warn { get; init; }
    public required IBrush WarnBadgeText { get; init; }
    public required IBrush Green { get; init; }
    public required IBrush Placeholder { get; init; }
    public required IBrush HatchLine { get; init; }
    public required IBrush ButtonBg { get; init; }
    public required IBrush HoverOverlay { get; init; }
    public required IBrush PressedOverlay { get; init; }
    public required IBrush ListHover { get; init; }
    public required IBrush HeaderBg { get; init; }
    public required IBrush HeaderBarBg { get; init; }
    public required IBrush PillBg { get; init; }
    public required IBrush Selection { get; init; }
    public required IBrush SelectionSolid { get; init; }
    public required IBrush SelectionSolidHover { get; init; }
    public required IBrush PopupBg { get; init; }
    public required IBrush FieldBg { get; init; }
    public required IBrush FieldBgHover { get; init; }
    public required IBrush FieldBgFocused { get; init; }
    public required IBrush FieldBgDisabled { get; init; }
    public required IBrush FieldBorder { get; init; }
    public required IBrush FieldBorderHover { get; init; }
    public required IBrush ToggleOnBg { get; init; }
    public required IBrush ToggleOnBgHover { get; init; }
    public required IBrush ToggleOnFg { get; init; }
    public required IBrush PrimaryButton { get; init; }
    public required IBrush PrimaryButtonHover { get; init; }
    public required IBrush PrimaryButtonPressed { get; init; }
    public required IBrush PrimaryBorder { get; init; }
    public required IBrush PrepareButton { get; init; }
    public required IBrush PrepareButtonHover { get; init; }
    public required IBrush PrepareButtonPressed { get; init; }
    public required IBrush PrepareBorder { get; init; }
    public required IBrush OnAccent { get; init; }
    public required IBrush CountBubble { get; init; }
    public required IBrush Disabled { get; init; }
    public required IBrush Watermark { get; init; }
    public required IBrush Link { get; init; }
    public required IBrush BrandPlate { get; init; }
    public required IEffect? PrimaryEffect { get; init; }
    public required IEffect? PrepareEffect { get; init; }
    public required IEffect? CoverEffect { get; init; }
    public required IEffect? TileEffect { get; init; }
    public required double ReflectionOpacity { get; init; }

    public IBrush PrepareDone => ButtonBg;

    /// <summary>A new dictionary of the "Aether.*" keys plus the Fluent overrides (a dictionary has one owner, and a
    /// headless test session builds a new Application per test).</summary>
    public ResourceDictionary BuildResources() => AppTheme.BuildResources(this);
}

public static class AppTheme
{
    static IBrush S(uint argb) => new ImmutableSolidColorBrush(Color.FromUInt32(argb));
    static IBrush V(uint top, uint bottom) => new LinearGradientBrush
    {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
        GradientStops = { new GradientStop(Color.FromUInt32(top), 0), new GradientStop(Color.FromUInt32(bottom), 1) },
    }.ToImmutable();
    static IBrush H(uint left, uint right) => new LinearGradientBrush
    {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
        GradientStops = { new GradientStop(Color.FromUInt32(left), 0), new GradientStop(Color.FromUInt32(right), 1) },
    }.ToImmutable();
    static IBrush Diagonal(uint a, uint b, uint c) => new LinearGradientBrush
    {
        StartPoint = new RelativePoint(0.1, 0, RelativeUnit.Relative),
        EndPoint = new RelativePoint(0.9, 1, RelativeUnit.Relative),
        GradientStops = { new GradientStop(Color.FromUInt32(a), 0), new GradientStop(Color.FromUInt32(b), 0.35), new GradientStop(Color.FromUInt32(c), 1) },
    }.ToImmutable();
    static IEffect Shadow(double y, double blur, uint rgb, double opacity) =>
        new ImmutableDropShadowEffect(0, y, blur, Color.FromUInt32(0xff000000 | rgb), opacity);

    // Shared by both themes: white text on these passes 4.5:1 at every stop (the old #4db5ff/#5fd28f tops did not).
    static readonly IBrush Primary = V(0xff1f74d6, 0xff0b58b0), PrimaryHover = V(0xff1b6dcc, 0xff0a52a4), PrimaryPressed = V(0xff0b58b0, 0xff0a4d9a);
    static readonly IBrush Prepare = V(0xff168546, 0xff11703b), PrepareHover = V(0xff137a40, 0xff0e6534), PreparePressed = V(0xff11703b, 0xff0d5f31);

    /// <summary>Clean and crisp: a near-white cool-grey window, white panes with a hairline border and a soft navy
    /// shadow, navy text, the Saturn blue darkened for contrast on white.</summary>
    public static readonly Palette Light = new()
    {
        Kind = ThemeKind.Light,
        TextColor = Color.FromUInt32(0xff0f1f3d),
        WindowBackground = Diagonal(0xfff7faff, 0xffeef3fa, 0xffe6edf7),
        BackgroundStops = [Color.FromUInt32(0xfff7faff), Color.FromUInt32(0xffe6edf7)],
        Panel = S(0xffffffff),
        PaneShadow = BoxShadows.Parse("0 1 3 0 #1A0B2A6B, 0 4 14 0 #120B2A6B"),
        Line = S(0xffd3dcea),
        Text = S(0xff0f1f3d),
        Muted = S(0xff4b5b78),
        Acc = S(0xff0a5cb5),
        Acc2 = S(0xff0b5bb5),
        Gold = S(0xff855700),
        Warn = S(0xff984600),
        WarnBadgeText = S(0xffffffff),
        Green = S(0xff14803f),
        Placeholder = S(0xffdfe6f1),
        HatchLine = S(0xffb3c1d6),
        ButtonBg = S(0xfff2f5fa),
        HoverOverlay = S(0xffe3eaf5),
        PressedOverlay = S(0xffd6e0ef),
        ListHover = S(0x140a62c0),
        HeaderBg = S(0xfff0f4fa),
        HeaderBarBg = S(0xffffffff),
        PillBg = S(0xffeef3fa),
        Selection = H(0x2e0a62c0, 0x0a0a62c0),
        SelectionSolid = S(0xffdce9fa),
        SelectionSolidHover = S(0xffcfe0f7),
        PopupBg = S(0xffffffff),
        FieldBg = S(0xffffffff),
        FieldBgHover = S(0xfff7f9fd),
        FieldBgFocused = S(0xffffffff),
        FieldBgDisabled = S(0xfff2f5fa),
        FieldBorder = S(0xff7f8da5),
        FieldBorderHover = S(0xff6b7a96),
        ToggleOnBg = S(0xff0a5cb5),
        ToggleOnBgHover = S(0xff0b4f9c),
        ToggleOnFg = S(0xffffffff),
        PrimaryButton = Primary, PrimaryButtonHover = PrimaryHover, PrimaryButtonPressed = PrimaryPressed,
        PrimaryBorder = S(0xff0a4d9a),
        PrepareButton = Prepare, PrepareButtonHover = PrepareHover, PrepareButtonPressed = PreparePressed,
        PrepareBorder = S(0xff0e6534),
        OnAccent = S(0xffffffff),
        CountBubble = S(0x2e000000),
        Disabled = S(0xff56647e),
        Watermark = S(0xff5f6d87),
        Link = S(0xff0a5cb5),
        BrandPlate = S(0xff0b2a6b),
        PrimaryEffect = Shadow(2, 8, 0x0b2a6b, 0.25),
        PrepareEffect = Shadow(2, 8, 0x0f5132, 0.25),
        CoverEffect = Shadow(6, 18, 0x0b2a6b, 0.35),
        TileEffect = Shadow(2, 6, 0x0b2a6b, 0.28),
        ReflectionOpacity = 0.18,
    };

    /// <summary>The original Saturn-menu look (deep navy gradient, translucent panes, light-blue accents), with the
    /// contrast fixes: a calmer hover and selection fill, a slightly lighter Muted, darker pill fills, a slightly
    /// deeper top-left glow (muted text sits right on it in the dialogs), and button gradients white text reads on.</summary>
    public static readonly Palette Dark = new()
    {
        Kind = ThemeKind.Dark,
        TextColor = Color.FromUInt32(0xffeaf1ff),
        WindowBackground = Diagonal(0xff18449c, 0xff0b2a6b, 0xff06173d),
        BackgroundStops = [Color.FromUInt32(0xff18449c), Color.FromUInt32(0xff06173d)],
        Panel = S(0xb8061437),
        PaneShadow = default,
        Line = S(0x388cb4ff),
        Text = S(0xffeaf1ff),
        Muted = S(0xffa9bce0),
        Acc = S(0xff3fa9ff),
        Acc2 = S(0xff7cd0ff),
        Gold = S(0xffffcc3d),
        Warn = S(0xffffb347),
        WarnBadgeText = S(0xff1a1206),
        Green = S(0xff3ddc84),
        Placeholder = S(0xff22417f),
        HatchLine = S(0x388cb4ff),
        ButtonBg = S(0x14ffffff),
        HoverOverlay = S(0x1effffff),
        PressedOverlay = S(0x2effffff),
        ListHover = S(0x1effffff),
        HeaderBg = S(0xf2061437),
        HeaderBarBg = S(0xf2061437),
        PillBg = S(0x40000814),
        Selection = H(0x473fa9ff, 0x1f3fa9ff),
        SelectionSolid = S(0xff1f4f99),
        SelectionSolidHover = S(0xff285bb0),
        PopupBg = S(0xff0c1f4a),
        FieldBg = S(0x33000814),
        FieldBgHover = S(0x40000814),
        FieldBgFocused = S(0x4d000814),
        FieldBgDisabled = S(0x26000814),
        FieldBorder = S(0x388cb4ff),
        FieldBorderHover = S(0x668cb4ff),
        ToggleOnBg = S(0xff1f5fae),
        ToggleOnBgHover = S(0xff2668bb),
        ToggleOnFg = S(0xffffffff),
        PrimaryButton = Primary, PrimaryButtonHover = PrimaryHover, PrimaryButtonPressed = PrimaryPressed,
        PrimaryBorder = S(0xff6cc4ff),
        PrepareButton = Prepare, PrepareButtonHover = PrepareHover, PrepareButtonPressed = PreparePressed,
        PrepareBorder = S(0xff7fe6a8),
        OnAccent = S(0xffffffff),
        CountBubble = S(0x2e000000),
        Disabled = S(0xffa9bce0),
        Watermark = S(0xff93a7cc),
        Link = S(0xff7cd0ff),
        BrandPlate = S(0xff0b2a6b),
        PrimaryEffect = Shadow(0, 14, 0x3fa9ff, 0.45),
        PrepareEffect = Shadow(0, 18, 0x3ddc84, 0.45),
        CoverEffect = Shadow(0, 26, 0x3fa9ff, 0.5),
        TileEffect = null,
        ReflectionOpacity = 0.35,
    };

    public static Palette Current { get; private set; } = Dark;
    public static ThemeKind Kind => Current.Kind;

    public static Palette For(ThemeKind kind) => kind == ThemeKind.Dark ? Dark : Light;

    public static ThemeKind Parse(string? s) =>
        string.Equals(s, "light", StringComparison.OrdinalIgnoreCase) ? ThemeKind.Light : ThemeKind.Dark;

    /// <summary>Makes `kind` the app's theme, live: every open window re-reads its DynamicResources.</summary>
    public static void Use(ThemeKind kind)
    {
        Current = For(kind);
        if (Application.Current is { } app)
        {
            var merged = app.Resources.MergedDictionaries;
            foreach (var old in merged.OfType<ResourceDictionary>().Where(d => d.ContainsKey("Aether.IsDark")).ToList()) merged.Remove(old);
            merged.Add(Current.BuildResources());
            app.RequestedThemeVariant = kind == ThemeKind.Dark ? ThemeVariant.Dark : ThemeVariant.Light;
        }
    }

    /// <summary>A binding to the current theme's "Aether.<name>" resource, for a property that must follow a theme
    /// switch: <c>new TextBlock { [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted") }</c>.</summary>
    public static DynamicResourceExtension Dyn(string name) => new("Aether." + name);

    // The current palette's values, for code that computes with them (and the tests).
    public static Color TextColor => Current.TextColor;
    public static IBrush WindowBackground => Current.WindowBackground;
    public static IBrush Panel => Current.Panel;
    public static IBrush Line => Current.Line;
    public static IBrush Text => Current.Text;
    public static IBrush Muted => Current.Muted;
    public static IBrush Acc => Current.Acc;
    public static IBrush Acc2 => Current.Acc2;
    public static IBrush Gold => Current.Gold;
    public static IBrush Warn => Current.Warn;
    public static IBrush Placeholder => Current.Placeholder;
    public static IBrush ButtonBg => Current.ButtonBg;
    public static IBrush HoverOverlay => Current.HoverOverlay;
    public static IBrush HeaderBg => Current.HeaderBg;
    public static IBrush WarnBadgeText => Current.WarnBadgeText;
    public static IBrush Selection => Current.Selection;
    public static IBrush PrimaryButton => Current.PrimaryButton;
    public static IBrush PrepareButton => Current.PrepareButton;
    public static IBrush PrepareDone => Current.PrepareDone;
    public static IBrush Disabled => Current.Disabled;
    public static IBrush Watermark => Current.Watermark;

    internal static ResourceDictionary BuildResources(Palette p)
    {
        var r = new ResourceDictionary();
        foreach (var prop in typeof(Palette).GetProperties())
        {
            if (prop.Name == nameof(Palette.Kind)) continue;
            r["Aether." + prop.Name] = prop.GetValue(p);
        }
        r["Aether.IsDark"] = p.Kind == ThemeKind.Dark;
        r["Aether.IsLight"] = p.Kind == ThemeKind.Light;
        r["Aether.Clear"] = Brushes.Transparent;
        OverrideFluentResources(r, p);
        return r;
    }

    /// <summary>Fluent's ControlThemes set state colors (":disabled", ":pointerover", ":focus", ":pressed",
    /// ":checked") at StyleTrigger priority on their template parts, which beats a plain Style Setter on the control
    /// every time -- so the one override that reaches every state is the DynamicResource key each state reads.
    /// Confirmed against this Avalonia version's real keys (a resource-key dump of FluentTheme; some plausible names do
    /// not exist, e.g. "ComboBoxPlaceholderForeground" is actually "ComboBoxPlaceHolderForeground").</summary>
    static void OverrideFluentResources(ResourceDictionary r, Palette p)
    {
        void Set(IBrush b, params string[] keys) { foreach (var k in keys) r[k] = b; }
        IBrush clear = Brushes.Transparent;

        Set(p.Text, "TextControlForeground", "TextControlForegroundPointerOver", "TextControlForegroundFocused");
        Set(p.Disabled, "TextControlForegroundDisabled", "TextControlPlaceholderForegroundDisabled");
        Set(p.Watermark, "TextControlPlaceholderForeground", "TextControlPlaceholderForegroundPointerOver", "TextControlPlaceholderForegroundFocused");
        Set(p.FieldBorder, "TextControlBorderBrush");
        Set(p.FieldBorderHover, "TextControlBorderBrushPointerOver");
        Set(p.Acc, "TextControlBorderBrushFocused");
        Set(p.Line, "TextControlBorderBrushDisabled");
        Set(p.FieldBg, "TextControlBackground");
        Set(p.FieldBgHover, "TextControlBackgroundPointerOver");
        Set(p.FieldBgFocused, "TextControlBackgroundFocused");
        Set(p.FieldBgDisabled, "TextControlBackgroundDisabled");
        Set(p.Muted, "TextControlButtonForeground", "TextControlButtonForegroundPointerOver", "TextControlButtonForegroundPressed");
        r["TextControlPlaceholderOpacity"] = 1.0;   // Fluent fades the watermark further; Watermark is already the dimmed color
        r["TextControlSelectionHighlightColor"] = p.Kind == ThemeKind.Dark ? S(0xff2f6fc4) : S(0xffb9d4f5);

        Set(p.Text, "ButtonForeground", "ButtonForegroundPointerOver", "ButtonForegroundPressed");
        Set(p.Disabled, "ButtonForegroundDisabled");
        Set(p.ButtonBg, "ButtonBackground");
        Set(p.FieldBgDisabled, "ButtonBackgroundDisabled");
        Set(p.HoverOverlay, "ButtonBackgroundPointerOver");
        Set(p.PressedOverlay, "ButtonBackgroundPressed");
        Set(p.Line, "ButtonBorderBrush", "ButtonBorderBrushDisabled");
        Set(p.FieldBorderHover, "ButtonBorderBrushPointerOver", "ButtonBorderBrushPressed");

        Set(p.Text, "ToggleButtonForeground", "ToggleButtonForegroundPointerOver", "ToggleButtonForegroundPressed",
            "ToggleButtonForegroundIndeterminate", "ToggleButtonForegroundIndeterminatePointerOver", "ToggleButtonForegroundIndeterminatePressed");
        Set(p.Disabled, "ToggleButtonForegroundDisabled", "ToggleButtonForegroundCheckedDisabled", "ToggleButtonForegroundIndeterminateDisabled");
        Set(p.ToggleOnFg, "ToggleButtonForegroundChecked", "ToggleButtonForegroundCheckedPointerOver", "ToggleButtonForegroundCheckedPressed");
        Set(p.FieldBgDisabled, "ToggleButtonBackgroundDisabled", "ToggleButtonBackgroundCheckedDisabled");
        Set(p.ButtonBg, "ToggleButtonBackground",
            "ToggleButtonBackgroundIndeterminate", "ToggleButtonBackgroundIndeterminateDisabled");
        Set(p.HoverOverlay, "ToggleButtonBackgroundPointerOver", "ToggleButtonBackgroundIndeterminatePointerOver");
        Set(p.PressedOverlay, "ToggleButtonBackgroundPressed", "ToggleButtonBackgroundIndeterminatePressed");
        Set(p.ToggleOnBg, "ToggleButtonBackgroundChecked", "ToggleButtonBackgroundCheckedPressed");
        Set(p.ToggleOnBgHover, "ToggleButtonBackgroundCheckedPointerOver");
        Set(p.Line, "ToggleButtonBorderBrush", "ToggleButtonBorderBrushDisabled", "ToggleButtonBorderBrushCheckedDisabled",
            "ToggleButtonBorderBrushIndeterminate", "ToggleButtonBorderBrushIndeterminateDisabled");
        Set(p.FieldBorderHover, "ToggleButtonBorderBrushPointerOver", "ToggleButtonBorderBrushPressed",
            "ToggleButtonBorderBrushIndeterminatePointerOver", "ToggleButtonBorderBrushIndeterminatePressed");
        Set(p.ToggleOnBg, "ToggleButtonBorderBrushChecked", "ToggleButtonBorderBrushCheckedPointerOver", "ToggleButtonBorderBrushCheckedPressed");

        Set(p.Text, "ComboBoxForeground", "ComboBoxForegroundFocused", "ComboBoxForegroundFocusedPressed");
        Set(p.Disabled, "ComboBoxForegroundDisabled", "ComboBoxDropDownGlyphForegroundDisabled");
        Set(p.Watermark, "ComboBoxPlaceHolderForeground", "ComboBoxPlaceHolderForegroundFocusedPressed");
        Set(p.FieldBorder, "ComboBoxBorderBrush", "ComboBoxBackgroundBorderBrushUnfocused");
        Set(p.FieldBorderHover, "ComboBoxBorderBrushPointerOver", "ComboBoxBorderBrushPressed");
        Set(p.Acc, "ComboBoxBackgroundBorderBrushFocused");
        Set(p.Line, "ComboBoxBorderBrushDisabled");
        Set(p.FieldBg, "ComboBoxBackground", "ComboBoxBackgroundUnfocused");
        Set(p.FieldBgDisabled, "ComboBoxBackgroundDisabled");
        Set(p.FieldBgHover, "ComboBoxBackgroundPointerOver");
        Set(p.PressedOverlay, "ComboBoxBackgroundPressed");
        Set(p.Muted, "ComboBoxDropDownGlyphForeground", "ComboBoxDropDownGlyphForegroundFocused", "ComboBoxDropDownGlyphForegroundFocusedPressed");
        Set(p.PopupBg, "ComboBoxDropDownBackground");
        Set(p.Line, "ComboBoxDropDownBorderBrush");
        Set(p.Text, "ComboBoxItemForeground", "ComboBoxItemForegroundPointerOver", "ComboBoxItemForegroundPressed",
            "ComboBoxItemForegroundSelected", "ComboBoxItemForegroundSelectedPointerOver", "ComboBoxItemForegroundSelectedPressed");
        Set(p.Disabled, "ComboBoxItemForegroundDisabled", "ComboBoxItemForegroundSelectedDisabled");
        Set(clear, "ComboBoxItemBackground", "ComboBoxItemBackgroundDisabled");
        Set(p.HoverOverlay, "ComboBoxItemBackgroundPointerOver");
        Set(p.PressedOverlay, "ComboBoxItemBackgroundPressed");
        Set(p.SelectionSolid, "ComboBoxItemBackgroundSelected", "ComboBoxItemBackgroundSelectedPressed", "ComboBoxItemBackgroundSelectedDisabled");
        Set(p.SelectionSolidHover, "ComboBoxItemBackgroundSelectedPointerOver");

        Set(p.Text, "CheckBoxForegroundUnchecked", "CheckBoxForegroundUncheckedPointerOver", "CheckBoxForegroundUncheckedPressed",
            "CheckBoxForegroundChecked", "CheckBoxForegroundCheckedPointerOver", "CheckBoxForegroundCheckedPressed",
            "CheckBoxForegroundIndeterminate", "CheckBoxForegroundIndeterminatePointerOver", "CheckBoxForegroundIndeterminatePressed");
        Set(p.Disabled, "CheckBoxForegroundUncheckedDisabled", "CheckBoxForegroundCheckedDisabled", "CheckBoxForegroundIndeterminateDisabled");
        Set(p.Kind == ThemeKind.Dark ? p.Muted : p.FieldBorder, "CheckBoxCheckBackgroundStrokeUnchecked");
        Set(p.FieldBorderHover, "CheckBoxCheckBackgroundStrokeUncheckedPointerOver", "CheckBoxCheckBackgroundStrokeUncheckedPressed");
        Set(p.FieldBg, "CheckBoxCheckBackgroundFillUnchecked");
        Set(p.FieldBgHover, "CheckBoxCheckBackgroundFillUncheckedPointerOver");
        Set(p.ToggleOnBg, "CheckBoxCheckBackgroundFillChecked", "CheckBoxCheckBackgroundFillCheckedPressed",
            "CheckBoxCheckBackgroundStrokeCheckedPointerOver", "CheckBoxCheckBackgroundStrokeCheckedPressed");
        Set(p.ToggleOnBgHover, "CheckBoxCheckBackgroundFillCheckedPointerOver");
        Set(p.ToggleOnFg, "CheckBoxCheckGlyphForegroundChecked", "CheckBoxCheckGlyphForegroundCheckedPointerOver", "CheckBoxCheckGlyphForegroundCheckedPressed");

        Set(p.Text, "ToggleSwitchContentForeground");
        Set(p.Disabled, "ToggleSwitchHeaderForegroundDisabled");
        Set(p.ToggleOnBg, "ToggleSwitchFillOn", "ToggleSwitchFillOnPressed", "ToggleSwitchStrokeOn", "ToggleSwitchStrokeOnPressed");
        Set(p.ToggleOnBgHover, "ToggleSwitchFillOnPointerOver", "ToggleSwitchStrokeOnPointerOver");
        Set(p.ToggleOnFg, "ToggleSwitchKnobFillOn", "ToggleSwitchKnobFillOnPointerOver", "ToggleSwitchKnobFillOnPressed");
        Set(p.FieldBorderHover, "ToggleSwitchStrokeOff", "ToggleSwitchStrokeOffPointerOver", "ToggleSwitchStrokeOffPressed");
        Set(p.Muted, "ToggleSwitchKnobFillOff", "ToggleSwitchKnobFillOffPointerOver", "ToggleSwitchKnobFillOffPressed");
        Set(p.FieldBg, "ToggleSwitchFillOff");
        Set(p.FieldBgHover, "ToggleSwitchFillOffPointerOver", "ToggleSwitchFillOffPressed");

        // The Expander (the Inspector's "More", the feedback form's "Your setup"): Fluent's near-white header and
        // content fills were the one off-theme box in the dark look.
        Set(p.Text, "ExpanderHeaderForeground", "ExpanderHeaderForegroundPointerOver", "ExpanderHeaderForegroundPressed");
        Set(p.Disabled, "ExpanderHeaderForegroundDisabled", "ExpanderChevronForegroundDisabled");
        Set(p.ButtonBg, "ExpanderHeaderBackground");
        Set(p.FieldBgDisabled, "ExpanderHeaderBackgroundDisabled");
        Set(p.HoverOverlay, "ExpanderHeaderBackgroundPointerOver");
        Set(p.PressedOverlay, "ExpanderHeaderBackgroundPressed");
        Set(p.Line, "ExpanderHeaderBorderBrush", "ExpanderHeaderBorderBrushDisabled", "ExpanderContentBorderBrush");
        Set(p.FieldBorderHover, "ExpanderHeaderBorderBrushPointerOver", "ExpanderHeaderBorderBrushPressed");
        Set(p.Panel, "ExpanderContentBackground");
        Set(p.Muted, "ExpanderChevronForeground");
        Set(p.Text, "ExpanderChevronForegroundPointerOver", "ExpanderChevronForegroundPressed");
        Set(clear, "ExpanderChevronBackground", "ExpanderChevronBackgroundDisabled", "ExpanderChevronBorderBrush", "ExpanderChevronBorderBrushDisabled");
        Set(p.HoverOverlay, "ExpanderChevronBackgroundPointerOver");
        Set(p.PressedOverlay, "ExpanderChevronBackgroundPressed");
        Set(clear, "ExpanderChevronBorderBrushPointerOver", "ExpanderChevronBorderBrushPressed");

        // Menus (the menu bar's drop-downs and the list's right-click menu) and tooltips: our own opaque surfaces.
        Set(p.PopupBg, "MenuFlyoutPresenterBackground", "ToolTipBackground");
        Set(p.Line, "MenuFlyoutPresenterBorderBrush", "ToolTipBorderBrush");
        Set(p.Text, "MenuFlyoutItemForeground", "MenuFlyoutItemForegroundPointerOver", "MenuFlyoutItemForegroundPressed", "ToolTipForeground");
        Set(p.Disabled, "MenuFlyoutItemForegroundDisabled", "MenuFlyoutItemKeyboardAcceleratorTextForegroundDisabled", "MenuFlyoutSubItemChevronDisabled");
        Set(clear, "MenuFlyoutItemBackground", "MenuFlyoutItemBackgroundDisabled");
        Set(p.HoverOverlay, "MenuFlyoutItemBackgroundPointerOver");
        Set(p.PressedOverlay, "MenuFlyoutItemBackgroundPressed");
        Set(p.Muted, "MenuFlyoutItemKeyboardAcceleratorTextForeground", "MenuFlyoutItemKeyboardAcceleratorTextForegroundPointerOver",
            "MenuFlyoutItemKeyboardAcceleratorTextForegroundPressed", "MenuFlyoutSubItemChevron");
        Set(p.Text, "MenuFlyoutSubItemChevronPointerOver", "MenuFlyoutSubItemChevronPressed", "MenuFlyoutSubItemChevronSubMenuOpened");

        // Scroll bar thumbs: 3:1 against the pane in both themes (Fluent's light thumb is fainter than that on white).
        var thumb = p.Kind == ThemeKind.Dark ? Color.FromUInt32(0xff8fa6d0) : Color.FromUInt32(0xff8593ab);
        r["ScrollBarThumbBackgroundColor"] = thumb;
        r["ScrollBarPanningThumbBackgroundColor"] = thumb;
        Set(new ImmutableSolidColorBrush(thumb), "ScrollBarPanningThumbBackground");
        Set(p.Muted, "ScrollBarThumbFillPointerOver", "ScrollBarThumbFillPressed", "ScrollBarButtonArrowForeground");
        Set(p.Text, "ScrollBarButtonArrowForegroundPointerOver", "ScrollBarButtonArrowForegroundPressed");
    }

    /// <summary>Punch-list round, "Use Inter if it's available, else Segoe UI, consistently": one comma-separated
    /// FontFamily, which is how Avalonia itself expresses a fallback chain. Set once, on each Window.</summary>
    public static readonly FontFamily AppFont = new("Inter,Segoe UI");

    /// <summary>Binds `prop` on `o` to the current theme's "Aether.<name>" (for statements; initializers use
    /// <c>[!Prop] = Dyn(name)</c>).</summary>
    public static T Bind<T>(this T o, AvaloniaProperty prop, string name) where T : AvaloniaObject
    {
        o[!prop] = Dyn(name);
        return o;
    }

    static Setter DynSetter(AvaloniaProperty prop, string name) => new(prop, Dyn(name));

    /// <summary>The "no cover" texture: a diagonal hatch in the theme's own line color over the placeholder fill,
    /// drawn as a stroked Path (so its color is a live resource like everything else), clipped to w x h.</summary>
    public static Control Hatch(double w, double h)
    {
        var g = new StreamGeometry();
        using (var c = g.Open())
            for (double x = -h; x < w; x += 8) { c.BeginFigure(new Point(x, h), false); c.LineTo(new Point(x + h, 0)); c.EndFigure(false); }
        return new Avalonia.Controls.Shapes.Path { Data = g, Width = w, Height = h, ClipToBounds = true, StrokeThickness = 1, [!Shape.StrokeProperty] = Dyn("HatchLine") };
    }

    /// <summary>The window-level part of the look (background, default text color, pane/button/textbox/combobox
    /// styles), shared by every window in this app so none ever reverts to bare Fluent chrome.</summary>
    public static void ApplyToWindow(Window w)
    {
        w[!TemplatedControl.BackgroundProperty] = Dyn("WindowBackground");
        w[!TemplatedControl.ForegroundProperty] = Dyn("Text");
        w.FontFamily = AppFont;
        w.Styles.Add(new Style(x => x.OfType<Border>().Class("pane"))
        {
            Setters = { DynSetter(Border.BackgroundProperty, "Panel"), DynSetter(Border.BorderBrushProperty, "Line"),
                        DynSetter(Border.BoxShadowProperty, "PaneShadow"),
                        new Setter(Border.BorderThicknessProperty, new Thickness(1)), new Setter(Border.CornerRadiusProperty, new CornerRadius(12)) },
        });
        w.Styles.Add(new Style(x => x.OfType<TextBlock>().Class("sectionHeader"))
        {
            Setters = { DynSetter(TextBlock.ForegroundProperty, "Acc2"), new Setter(TextBlock.FontWeightProperty, FontWeight.Bold),
                        new Setter(TextBlock.FontSizeProperty, 12.0) },
        });
        w.Styles.Add(new Style(x => x.OfType<Button>())
        {
            Setters = { DynSetter(Button.BackgroundProperty, "ButtonBg"), DynSetter(Button.BorderBrushProperty, "Line"),
                        new Setter(Button.BorderThicknessProperty, new Thickness(1)), new Setter(Button.CornerRadiusProperty, new CornerRadius(8)),
                        DynSetter(Button.ForegroundProperty, "Text"), new Setter(Button.PaddingProperty, new Thickness(10, 5)) },
        });
        // The two call-to-action buttons (Save changes, Prepare; class "primary"/"prepare"): white on their own
        // gradient in every state but disabled, which Fluent's template would otherwise flatten to the plain button
        // fill on hover/press.
        foreach (var (cls, key) in new[] { ("primary", "PrimaryButton"), ("prepare", "PrepareButton") })
        {
            w.Styles.Add(new Style(x => x.OfType<Button>().Class(cls))
            {
                Setters = { DynSetter(Button.BackgroundProperty, key), DynSetter(Button.ForegroundProperty, "OnAccent"),
                            DynSetter(Button.BorderBrushProperty, cls == "primary" ? "PrimaryBorder" : "PrepareBorder"),
                            DynSetter(Visual.EffectProperty, cls == "primary" ? "PrimaryEffect" : "PrepareEffect") },
            });
            foreach (var (state, suffix) in new[] { (":pointerover", "Hover"), (":pressed", "Pressed") })
                w.Styles.Add(new Style(x => x.OfType<Button>().Class(cls).Class(state).Template().OfType<ContentPresenter>().Name("PART_ContentPresenter"))
                {
                    Setters = { DynSetter(ContentPresenter.BackgroundProperty, key + suffix), DynSetter(ContentPresenter.ForegroundProperty, "OnAccent"),
                                DynSetter(ContentPresenter.BorderBrushProperty, cls == "primary" ? "PrimaryBorder" : "PrepareBorder") },
                });
            w.Styles.Add(new Style(x => x.OfType<Button>().Class(cls).Class(":disabled"))
            {
                Setters = { new Setter(Visual.EffectProperty, null) },
            });
        }
        w.Styles.Add(new Style(x => x.OfType<TextBox>())
        {
            Setters = { DynSetter(TextBox.CaretBrushProperty, "Text"),
                        new Setter(TextBox.MinHeightProperty, 28.0), new Setter(TextBox.PaddingProperty, new Thickness(6, 3)) },
        });
        w.Styles.Add(new Style(x => x.OfType<ComboBox>())
        {
            Setters = { new Setter(ComboBox.MinHeightProperty, 28.0), new Setter(ComboBox.PaddingProperty, new Thickness(6, 3)) },
        });
    }

    /// <summary>The per-list part of the look (transparent background, readable row text, the blue-gradient selected
    /// bar with its accent edge, a quiet hover), shared by every ListBox in this app. Fluent's ListBoxItem template
    /// paints its own selected/hover fill on PART_ContentPresenter from shared System* brushes (not per-control keys),
    /// so these target that part directly. Also tightens Fluent's tall default item padding.</summary>
    public static void ApplyToList(ListBox list)
    {
        list.Background = Brushes.Transparent;
        list.Styles.Add(new Style(x => x.OfType<ListBoxItem>())
        {
            Setters = { DynSetter(ListBoxItem.ForegroundProperty, "Text"), new Setter(ListBoxItem.PaddingProperty, new Thickness(6, 1)),
                        new Setter(ListBoxItem.MinHeightProperty, 26.0) },
        });
        static Selector Part(Selector? x, string state) =>
            x.OfType<ListBoxItem>().Class(state).Template().OfType<ContentPresenter>().Name("PART_ContentPresenter");
        list.Styles.Add(new Style(x => Part(x, ":pointerover"))
        {
            Setters = { DynSetter(ContentPresenter.BackgroundProperty, "ListHover"), DynSetter(ContentPresenter.ForegroundProperty, "Text") },
        });
        list.Styles.Add(new Style(x => Part(x, ":pressed"))
        {
            Setters = { DynSetter(ContentPresenter.BackgroundProperty, "ListHover"), DynSetter(ContentPresenter.ForegroundProperty, "Text") },
        });
        list.Styles.Add(new Style(x => Part(x, ":selected"))
        {
            Setters = { DynSetter(ContentPresenter.BackgroundProperty, "Selection"), DynSetter(ContentPresenter.ForegroundProperty, "Text"),
                        DynSetter(ContentPresenter.BorderBrushProperty, "Acc"), new Setter(ContentPresenter.BorderThicknessProperty, new Thickness(3, 0, 0, 0)),
                        // the accent edge takes its 3px out of the left padding, so a selected row is no wider than
                        // the others (it pushed the list into a horizontal scroll bar)
                        new Setter(ContentPresenter.PaddingProperty, new Thickness(3, 1, 6, 1)) },
        });
    }
}
