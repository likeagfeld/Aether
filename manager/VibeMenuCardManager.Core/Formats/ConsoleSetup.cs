// Aether Card Manager, GPL-3.0-or-later.
// /aether/DIAG.TXT: the console setup the menu detected at start-up, KEY=VALUE lines ('#' comments). Read-only here:
// the feedback form pre-fills its "Your setup" fields from it. Unknown keys are kept in Raw; any value may be UNKNOWN,
// which every typed property reads as null.
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using VibeMenuCardManager.Core.Card;

namespace VibeMenuCardManager.Core.Formats;

/// <summary>SMPC area letter (AREA).</summary>
public enum ConsoleArea { Japan, AsiaNtsc, NorthAmerica, SouthAmericaNtsc, Korea, AsiaPal, Europe, SouthAmericaPal }

/// <summary>VIDEO: NTSC60 or PAL50.</summary>
public enum VideoStandard { Ntsc60, Pal50 }

/// <summary>CART: the expansion cartridge's ID byte read as a kind.</summary>
public enum CartKind { None, Ram1MB, Ram4MB, Other }

/// <summary>RTC: OK, UNSET (never set since the backup battery died or was removed), BAD (set but invalid).</summary>
public enum ClockState { Ok, Unset, Bad }

/// <summary>LOADER: the card's default loader.</summary>
public enum DefaultLoader { Auto, Cwx, Jhl }

/// <summary>A backup memory "&lt;free&gt;/&lt;total&gt; &lt;size&gt;KB": blocks free, blocks total, size in KB.</summary>
public sealed record BackupMemoryUsage(int FreeBlocks, int TotalBlocks, int SizeKb);

/// <summary>A controller port. NONE reads as IsMultitap false with an empty Devices list. A single device is a list
/// of one. "MULTITAP:a,b,..." is IsMultitap true with its devices in order (a multitap with 0-1 devices reads as
/// NONE / a single device: SGL hides it). Device tokens are kept as written, upper-case (PAD, 3DPAD_ANALOG, ANALOG,
/// RACER, MOUSE, GUN, KEYBOARD, MD3PAD, MD6PAD, MDMOUSE, NONE inside a multitap, ID_xx).</summary>
public sealed record ControllerPort(bool IsMultitap, IReadOnlyList<string> Devices)
{
    /// <summary>True for NONE: nothing connected.</summary>
    public bool IsEmpty => !IsMultitap && Devices.Count == 0;

    /// <summary>Human text, e.g. "3D Control Pad (analog mode)", "Multitap: Control Pad, Control Pad", "Nothing connected".</summary>
    public string Describe() =>
        IsMultitap ? "Multitap: " + string.Join(", ", Devices.Select(ConsoleSetup.DescribeDevice))
        : Devices.Count == 0 ? "Nothing connected" : ConsoleSetup.DescribeDevice(Devices[0]);
}

public sealed partial class ConsoleSetup
{
    /// <summary>The file's path relative to the card root ("aether/DIAG.TXT").</summary>
    public const string RelativePath = CardPaths.Folder + "/DIAG.TXT";
    /// <summary>The highest FORMAT this parser was written for; a higher FormatVersion still parses, best effort.</summary>
    public const int SupportedFormat = 1;
    const string Unknown = "UNKNOWN";

    /// <summary>Every KEY=VALUE line as written (value trimmed), including keys this parser does not know.
    /// Duplicate key: the last one wins. Keys are upper-case.</summary>
    public IReadOnlyDictionary<string, string> Raw { get; }

    /// <summary>FORMAT; null when missing or not an integer. Compare with SupportedFormat to warn.</summary>
    public int? FormatVersion { get; }
    /// <summary>True when FormatVersion is above SupportedFormat (fields may be missing or mean something new).</summary>
    public bool IsNewerFormat => FormatVersion > SupportedFormat;

    /// <summary>AETHER's first token, the menu version (e.g. "0.1").</summary>
    public string? AetherVersion { get; }
    /// <summary>AETHER after the version (e.g. "2bd1de1 2026-11-25" or "dev"); null when absent.</summary>
    public string? AetherBuild { get; }

    /// <summary>DATE, the console clock when the menu last WROTE this file. The menu rewrites DIAG.TXT only when some
    /// other line changes (DATE is left out of that comparison), so this is when Aether last saw this setup change,
    /// or first wrote it -- not the last boot. Null for UNKNOWN or an invalid date (a console with a dead clock).</summary>
    public DateTime? WrittenOn { get; }

    /// <summary>SATIATOR_FW, e.g. 163.</summary>
    public int? SatiatorFirmware { get; }
    /// <summary>SATIATOR_FW_RAW, as the firmware reported it.</summary>
    public string? SatiatorFirmwareRaw { get; }

    /// <summary>BIOS, the 16-character BIOS ROM header string as read (e.g. "BTR_1.0191941228").</summary>
    public string? Bios { get; }
    /// <summary>BIOS characters 4..7 (e.g. "1.01"); null when BIOS is not of the "BTR?d.dd??YYMMDD" shape.</summary>
    public string? BiosVersion { get; }
    /// <summary>BIOS's last six characters as a 19YY-MM-DD date; null when BIOS is not of that shape.</summary>
    public DateOnly? BiosDate { get; }
    /// <summary>"Hi-Saturn" when BIOS character 9 is 'H', "V-Saturn" when it is 'J' (the only two mappings the
    /// sample dumps support); null otherwise (unknown, not "a standard Saturn").</summary>
    public string? BiosFamilyHint { get; }

    /// <summary>AREA (J T U B K A E L).</summary>
    public ConsoleArea? Area { get; }
    /// <summary>AREA_CODE, the raw SMPC area code 0-15 (ground truth over Area).</summary>
    public int? AreaCode { get; }
    /// <summary>VIDEO.</summary>
    public VideoStandard? Video { get; }

    /// <summary>CART.</summary>
    public CartKind? Cart { get; }
    /// <summary>CART_ID, the cartridge ID byte (0x5A 1 MB, 0x5C 4 MB, 0xFF none).</summary>
    public byte? CartId { get; }
    /// <summary>CART_BRAM present: true for a usage line, false for NONE, null for UNKNOWN/missing.</summary>
    public bool? HasCartBram { get; }
    /// <summary>CART_BRAM's usage when it is "&lt;free&gt;/&lt;total&gt; &lt;size&gt;KB".</summary>
    public BackupMemoryUsage? CartBram { get; }
    /// <summary>CART_ROM present: true for a header line, false for NONE, null for UNKNOWN/missing.</summary>
    public bool? HasCartRom { get; }
    /// <summary>CART_ROM's text when present (e.g. "PSKAI 0001 3.5" or "ROM &lt;product&gt; &lt;version&gt; &lt;title&gt;").</summary>
    public string? CartRom { get; }
    /// <summary>CART_ROM's first word is PSKAI (a Pseudo Saturn Kai flashed cart).</summary>
    public bool CartRomIsPseudoSaturnKai => CartRom != null && CartRom.Split(' ')[0].Equals("PSKAI", StringComparison.OrdinalIgnoreCase);

    /// <summary>NETLINK: YES true, NO false, UNKNOWN null (the menu writes UNKNOWN today: no safe detection).</summary>
    public bool? NetLink { get; }

    /// <summary>PORT1; null for UNKNOWN/missing/unparsable.</summary>
    public ControllerPort? Port1 { get; }
    /// <summary>PORT2; null for UNKNOWN/missing/unparsable.</summary>
    public ControllerPort? Port2 { get; }

    /// <summary>RTC.</summary>
    public ClockState? Clock { get; }

    /// <summary>BRAM (internal backup RAM) usage; null for UNFORMATTED, UNKNOWN or missing.</summary>
    public BackupMemoryUsage? Bram { get; }
    /// <summary>BRAM reads UNFORMATTED.</summary>
    public bool BramUnformatted => Is("BRAM", "UNFORMATTED");

    /// <summary>GAMES, the game count on the card.</summary>
    public int? Games { get; }
    /// <summary>LOADER.</summary>
    public DefaultLoader? Loader { get; }

    [GeneratedRegex("^[A-Z0-9_]+$")] private static partial Regex KeyRe();
    [GeneratedRegex(@"^(\d+)/(\d+) (\d+)KB$", RegexOptions.IgnoreCase)] private static partial Regex UsageRe();
    [GeneratedRegex(@"^BTR.\d\.\d\d..(\d\d)(\d\d)(\d\d)$")] private static partial Regex BiosRe();
    [GeneratedRegex("^[0-9A-Fa-f]{2}$")] private static partial Regex HexByteRe();

    ConsoleSetup(Dictionary<string, string> raw)
    {
        Raw = raw;
        FormatVersion = Int("FORMAT");

        if (Value("AETHER") is { } ae)
        {
            int sp = ae.IndexOf(' ');
            AetherVersion = sp < 0 ? ae : ae[..sp];
            string rest = sp < 0 ? "" : ae[(sp + 1)..].Trim();
            AetherBuild = rest.Length == 0 ? null : rest;
        }

        if (Value("DATE") is { } d && DateTime.TryParseExact(d, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture,
                DateTimeStyles.None, out var w))
            WrittenOn = w;

        SatiatorFirmware = Int("SATIATOR_FW");
        SatiatorFirmwareRaw = Value("SATIATOR_FW_RAW");

        Bios = Value("BIOS");
        if (Bios != null && BiosRe().Match(Bios) is { Success: true } bm)
        {
            int yy = int.Parse(bm.Groups[1].Value, CultureInfo.InvariantCulture);
            int mm = int.Parse(bm.Groups[2].Value, CultureInfo.InvariantCulture);
            int dd = int.Parse(bm.Groups[3].Value, CultureInfo.InvariantCulture);
            if (mm is >= 1 and <= 12 && dd >= 1 && dd <= DateTime.DaysInMonth(1900 + yy, mm))
            {
                BiosVersion = Bios[4..8];
                BiosDate = new DateOnly(1900 + yy, mm, dd);
                BiosFamilyHint = Bios[9] switch { 'H' => "Hi-Saturn", 'J' => "V-Saturn", _ => null };
            }
        }

        Area = Upper("AREA") switch
        {
            "J" => ConsoleArea.Japan, "T" => ConsoleArea.AsiaNtsc, "U" => ConsoleArea.NorthAmerica,
            "B" => ConsoleArea.SouthAmericaNtsc, "K" => ConsoleArea.Korea, "A" => ConsoleArea.AsiaPal,
            "E" => ConsoleArea.Europe, "L" => ConsoleArea.SouthAmericaPal, _ => null,
        };
        AreaCode = Int("AREA_CODE") is int ac and >= 0 and <= 15 ? ac : null;
        Video = Upper("VIDEO") switch { "NTSC60" => VideoStandard.Ntsc60, "PAL50" => VideoStandard.Pal50, _ => null };

        Cart = Upper("CART") switch
        {
            "NONE" => CartKind.None, "RAM1MB" => CartKind.Ram1MB, "RAM4MB" => CartKind.Ram4MB, "OTHER" => CartKind.Other, _ => null,
        };
        if (Value("CART_ID") is { } cid && HexByteRe().IsMatch(cid))
            CartId = byte.Parse(cid, NumberStyles.HexNumber, CultureInfo.InvariantCulture);

        (HasCartBram, CartBram) = Upper("CART_BRAM") switch
        {
            null => (null, null),
            "NONE" => (false, null),
            _ => Usage(Value("CART_BRAM")!) is { } u ? (true, u) : ((bool?)null, (BackupMemoryUsage?)null),
        };
        (HasCartRom, CartRom) = Upper("CART_ROM") switch
        {
            null => ((bool?)null, (string?)null),
            "NONE" => (false, null),
            _ => (true, Value("CART_ROM")),
        };

        NetLink = Upper("NETLINK") switch { "YES" => true, "NO" => false, _ => null };
        Port1 = Port(Upper("PORT1"));
        Port2 = Port(Upper("PORT2"));
        Clock = Upper("RTC") switch { "OK" => ClockState.Ok, "UNSET" => ClockState.Unset, "BAD" => ClockState.Bad, _ => null };
        Bram = Value("BRAM") is { } b ? Usage(b) : null;
        Games = Int("GAMES") is int g and >= 0 ? g : null;
        Loader = Upper("LOADER") switch { "AUTO" => DefaultLoader.Auto, "CWX" => DefaultLoader.Cwx, "JHL" => DefaultLoader.Jhl, _ => null };
    }

    /// <summary>Parses DIAG.TXT's text. Never throws: unknown keys are kept in Raw, lines without '=' are ignored.</summary>
    public static ConsoleSetup Parse(string text)
    {
        var raw = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string line in text.TrimStart('﻿').Split('\n'))
        {
            string s = line.TrimEnd('\r').Trim(' ', '\t');
            if (s.Length == 0 || s[0] == '#') continue;
            int eq = s.IndexOf('=');
            if (eq <= 0) continue;
            string key = s[..eq].Trim(' ', '\t').ToUpperInvariant();
            if (!KeyRe().IsMatch(key)) continue;
            raw[key] = s[(eq + 1)..].Trim(' ', '\t');
        }
        return new ConsoleSetup(raw);
    }

    /// <summary>Reads &lt;cardRoot&gt;/aether/DIAG.TXT; null when it is absent or cannot be read (never throws for that).</summary>
    public static ConsoleSetup? TryLoad(string cardRoot)
    {
        try
        {
            string path = Path.Combine(cardRoot, CardPaths.Folder, "DIAG.TXT");
            return File.Exists(path) ? Parse(File.ReadAllText(path, Encoding.ASCII)) : null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return null;
        }
    }

    /// <summary>Human text for one device token, e.g. "3DPAD_ANALOG" -> "3D Control Pad (analog mode)",
    /// "ID_E3" -> "Unrecognised peripheral (ID E3)". An unlisted token is returned as written.</summary>
    public static string DescribeDevice(string token) => token.ToUpperInvariant() switch
    {
        "PAD" => "Control Pad (or 3D Control Pad in digital mode)",
        "3DPAD_ANALOG" => "3D Control Pad (analog mode)",
        "ANALOG" => "Analog stick (e.g. Mission Stick)",
        "RACER" => "Arcade Racer",
        "MOUSE" => "Mouse",
        "GUN" => "Light gun",
        "KEYBOARD" => "Keyboard",
        "MD3PAD" => "Mega Drive 3-button pad",
        "MD6PAD" => "Mega Drive 6-button pad",
        "MDMOUSE" => "Mega Drive mouse",
        "NONE" => "Nothing connected",
        var t when t.StartsWith("ID_", StringComparison.Ordinal) && HexByteRe().IsMatch(t[3..]) => $"Unrecognised peripheral (ID {t[3..]})",
        _ => token,
    };

    string? Value(string key) => Raw.TryGetValue(key, out var v) && v.Length > 0 && !v.Equals(Unknown, StringComparison.OrdinalIgnoreCase) ? v : null;

    string? Upper(string key) => Value(key)?.ToUpperInvariant();

    bool Is(string key, string word) => Upper(key) == word;

    int? Int(string key) =>
        Value(key) is { } v && int.TryParse(v, NumberStyles.None, CultureInfo.InvariantCulture, out int n) ? n : null;

    static BackupMemoryUsage? Usage(string v)
    {
        var m = UsageRe().Match(v);
        if (!m.Success) return null;
        int I(int g) => int.TryParse(m.Groups[g].Value, NumberStyles.None, CultureInfo.InvariantCulture, out int n) ? n : -1;
        int f = I(1), t = I(2), k = I(3);
        return f < 0 || t < 0 || k < 0 ? null : new BackupMemoryUsage(f, t, k);
    }

    static ControllerPort? Port(string? v)
    {
        if (v == null) return null;
        if (v == "NONE") return new ControllerPort(false, []);
        if (v.StartsWith("MULTITAP:", StringComparison.Ordinal))
        {
            var devs = v["MULTITAP:".Length..].Split(',').Select(t => t.Trim()).ToList();
            return devs.Any(t => t.Length == 0) ? null : new ControllerPort(true, devs);
        }
        return v.Contains(',') || v.Contains(' ') ? null : new ControllerPort(false, [v]);
    }
}
