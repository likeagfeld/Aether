// Aether Card Manager, GPL-3.0-or-later.
// /aether/DIAG.TXT: the console setup the menu writes, parsed for the feedback form's setup fields.
using System.Globalization;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class ConsoleSetupTests
{
    const string Sample =
        "# Aether console setup, detected by the menu at start-up. KEY=VALUE; UNKNOWN = not detectable on this console.\n" +
        "FORMAT=1\nAETHER=0.1 2bd1de1 2026-11-25\nDATE=2026-11-25 14:03:05\nSATIATOR_FW=163\n" +
        "SATIATOR_FW_RAW=163 2505030818\nBIOS=BTR_1.0191941228\nAREA=J\nAREA_CODE=1\nVIDEO=NTSC60\nCART=RAM4MB\n" +
        "CART_ID=5C\nCART_BRAM=NONE\nCART_ROM=NONE\nNETLINK=UNKNOWN\nPORT1=3DPAD_ANALOG\nPORT2=MULTITAP:PAD,PAD\n" +
        "RTC=OK\nBRAM=509/512 32KB\nGAMES=689\nLOADER=AUTO\n";

    [Fact]
    public void Parses_the_full_sample()
    {
        var s = ConsoleSetup.Parse(Sample);
        Assert.Equal(1, s.FormatVersion);
        Assert.False(s.IsNewerFormat);
        Assert.Equal("0.1", s.AetherVersion);
        Assert.Equal("2bd1de1 2026-11-25", s.AetherBuild);
        Assert.Equal(new DateTime(2026, 11, 25, 14, 3, 5), s.WrittenOn);
        Assert.Equal(163, s.SatiatorFirmware);
        Assert.Equal("163 2505030818", s.SatiatorFirmwareRaw);
        Assert.Equal("BTR_1.0191941228", s.Bios);
        Assert.Equal("1.01", s.BiosVersion);
        Assert.Equal(new DateOnly(1994, 12, 28), s.BiosDate);
        Assert.Null(s.BiosFamilyHint);
        Assert.Equal(ConsoleArea.Japan, s.Area);
        Assert.Equal(1, s.AreaCode);
        Assert.Equal(VideoStandard.Ntsc60, s.Video);
        Assert.Equal(CartKind.Ram4MB, s.Cart);
        Assert.Equal((byte)0x5C, s.CartId);
        Assert.False(s.HasCartBram);
        Assert.Null(s.CartBram);
        Assert.False(s.HasCartRom);
        Assert.Null(s.CartRom);
        Assert.Null(s.NetLink);
        Assert.Equal(new ControllerPort(false, ["3DPAD_ANALOG"]).Devices, s.Port1!.Devices);
        Assert.False(s.Port1.IsMultitap);
        Assert.Equal("3D Control Pad (analog mode)", s.Port1.Describe());
        Assert.True(s.Port2!.IsMultitap);
        Assert.Equal(["PAD", "PAD"], s.Port2.Devices);
        Assert.Equal(ClockState.Ok, s.Clock);
        Assert.Equal(new BackupMemoryUsage(509, 512, 32), s.Bram);
        Assert.False(s.BramUnformatted);
        Assert.Equal(689, s.Games);
        Assert.Equal(DefaultLoader.Auto, s.Loader);
        Assert.Equal(20, s.Raw.Count);
        Assert.Equal("UNKNOWN", s.Raw["NETLINK"]);
    }

    [Fact]
    public void Crlf_reads_the_same_as_lf()
    {
        var a = ConsoleSetup.Parse(Sample);
        var b = ConsoleSetup.Parse(Sample.Replace("\n", "\r\n"));
        Assert.Equal(a.Raw, b.Raw);
        Assert.Equal(a.WrittenOn, b.WrittenOn);
        Assert.Equal(689, b.Games);
        Assert.Equal(DefaultLoader.Auto, b.Loader);
    }

    [Fact]
    public void Comments_blank_lines_and_lines_without_equals_are_ignored()
    {
        var s = ConsoleSetup.Parse("\n# GAMES=1\n\n   \njust words\nGAMES=5\n#LOADER=JHL\n");
        Assert.Single(s.Raw);
        Assert.Equal(5, s.Games);
        Assert.Null(s.Loader);
    }

    [Fact]
    public void Unknown_keys_are_kept_and_never_an_error()
    {
        var s = ConsoleSetup.Parse("FORMAT=1\nFUTURE_THING=some value\nX2=y\n");
        Assert.Equal("some value", s.Raw["FUTURE_THING"]);
        Assert.Equal("y", s.Raw["X2"]);
    }

    [Fact]
    public void A_duplicate_key_takes_the_last_value()
    {
        var s = ConsoleSetup.Parse("GAMES=1\nGAMES=2\n");
        Assert.Equal(2, s.Games);
        Assert.Equal("2", s.Raw["GAMES"]);
    }

    [Fact]
    public void A_value_may_contain_equals()
    {
        var s = ConsoleSetup.Parse("CART_ROM=ROM T-1234 V1.000 A=B title\n");
        Assert.Equal("ROM T-1234 V1.000 A=B title", s.Raw["CART_ROM"]);
        Assert.True(s.HasCartRom);
        Assert.Equal("ROM T-1234 V1.000 A=B title", s.CartRom);
        Assert.False(s.CartRomIsPseudoSaturnKai);
    }

    [Fact]
    public void Unknown_values_are_null_typed_and_kept_raw()
    {
        string text = string.Join("\n", "FORMAT DATE SATIATOR_FW BIOS AREA AREA_CODE VIDEO CART CART_ID CART_BRAM CART_ROM NETLINK PORT1 PORT2 RTC BRAM GAMES LOADER"
            .Split(' ').Select(k => k + "=UNKNOWN"));
        var s = ConsoleSetup.Parse(text);
        Assert.Null(s.FormatVersion);
        Assert.Null(s.WrittenOn);
        Assert.Null(s.SatiatorFirmware);
        Assert.Null(s.Bios);
        Assert.Null(s.BiosVersion);
        Assert.Null(s.BiosDate);
        Assert.Null(s.Area);
        Assert.Null(s.AreaCode);
        Assert.Null(s.Video);
        Assert.Null(s.Cart);
        Assert.Null(s.CartId);
        Assert.Null(s.HasCartBram);
        Assert.Null(s.CartBram);
        Assert.Null(s.HasCartRom);
        Assert.Null(s.CartRom);
        Assert.Null(s.NetLink);
        Assert.Null(s.Port1);
        Assert.Null(s.Port2);
        Assert.Null(s.Clock);
        Assert.Null(s.Bram);
        Assert.False(s.BramUnformatted);
        Assert.Null(s.Games);
        Assert.Null(s.Loader);
        Assert.Equal(18, s.Raw.Count);
        Assert.All(s.Raw.Values, v => Assert.Equal("UNKNOWN", v));
    }

    [Fact]
    public void Missing_keys_are_null()
    {
        var s = ConsoleSetup.Parse("");
        Assert.Empty(s.Raw);
        Assert.Null(s.FormatVersion);
        Assert.Null(s.AetherVersion);
        Assert.Null(s.Port1);
    }

    [Fact]
    public void Ports_none_single_multitap_and_unrecognised_ids()
    {
        var s = ConsoleSetup.Parse("PORT1=NONE\nPORT2=MULTITAP:PAD,ID_E3,MD6PAD,NONE\n");
        Assert.True(s.Port1!.IsEmpty);
        Assert.Empty(s.Port1.Devices);
        Assert.Equal("Nothing connected", s.Port1.Describe());
        Assert.True(s.Port2!.IsMultitap);
        Assert.Equal(["PAD", "ID_E3", "MD6PAD", "NONE"], s.Port2.Devices);
        Assert.Equal("Unrecognised peripheral (ID E3)", ConsoleSetup.DescribeDevice("ID_E3"));
        Assert.Equal("Multitap: Control Pad (or 3D Control Pad in digital mode), Unrecognised peripheral (ID E3), Mega Drive 6-button pad, Nothing connected",
            s.Port2.Describe());
        var single = ConsoleSetup.Parse("PORT1=ID_7F\n").Port1!;
        Assert.False(single.IsMultitap);
        Assert.Equal(["ID_7F"], single.Devices);
    }

    [Fact]
    public void A_newer_format_still_parses()
    {
        var s = ConsoleSetup.Parse("FORMAT=2\nGAMES=12\nNEW_KEY=1\n");
        Assert.Equal(2, s.FormatVersion);
        Assert.True(s.IsNewerFormat);
        Assert.Equal(12, s.Games);
    }

    [Fact]
    public void TryLoad_is_null_for_a_missing_file_and_reads_a_present_one()
    {
        using var card = new TempCard();
        Assert.Null(ConsoleSetup.TryLoad(card.Dir));
        Assert.Null(ConsoleSetup.TryLoad(Path.Combine(card.Dir, "no-such-dir")));
        Directory.CreateDirectory(Path.Combine(card.Dir, "aether"));
        Assert.Null(ConsoleSetup.TryLoad(card.Dir));
        File.WriteAllText(Path.Combine(card.Dir, "aether", "DIAG.TXT"), Sample);
        Assert.Equal(689, ConsoleSetup.TryLoad(card.Dir)!.Games);
        Assert.Equal("aether/DIAG.TXT", ConsoleSetup.RelativePath);
    }

    [Theory]
    [InlineData("2026-13-01 00:00:00")]
    [InlineData("2026-02-30 10:00:00")]
    [InlineData("2026-11-25")]
    [InlineData("25/11/2026 14:03:05")]
    [InlineData("garbage")]
    public void A_malformed_date_is_null(string date)
    {
        var s = ConsoleSetup.Parse("DATE=" + date + "\n");
        Assert.Null(s.WrittenOn);
        Assert.Equal(date, s.Raw["DATE"]);
    }

    [Fact]
    public void The_date_is_gregorian_under_any_culture()
    {
        var c = CultureInfo.CurrentCulture;
        CultureInfo.CurrentCulture = new CultureInfo("th-TH");
        try { Assert.Equal(new DateTime(2026, 11, 25, 14, 3, 5), ConsoleSetup.Parse(Sample).WrittenOn); }
        finally { CultureInfo.CurrentCulture = c; }
    }

    [Theory]
    [InlineData("BTR_1.00D1940921", "1.00", 1994, 9, 21, null)]
    [InlineData("BTR_1.000U941115", "1.00", 1994, 11, 15, null)]
    [InlineData("BTRD1.0032941012", "1.00", 1994, 10, 12, null)]
    [InlineData("BTR_1.020H950519", "1.02", 1995, 5, 19, "Hi-Saturn")]
    [InlineData("BTRF1.030H950720", "1.03", 1995, 7, 20, "Hi-Saturn")]
    [InlineData("BTR_1.019J950703", "1.01", 1995, 7, 3, "V-Saturn")]
    public void Bios_version_date_and_family(string bios, string version, int y, int m, int d, string? family)
    {
        var s = ConsoleSetup.Parse("BIOS=" + bios + "\n");
        Assert.Equal(bios, s.Bios);
        Assert.Equal(version, s.BiosVersion);
        Assert.Equal(new DateOnly(y, m, d), s.BiosDate);
        Assert.Equal(family, s.BiosFamilyHint);
    }

    [Theory]
    [InlineData("SOMETHING_ELSE16")]
    [InlineData("BTR_1.01919412")]
    [InlineData("BTR_1.0191941399")]
    public void An_odd_bios_keeps_its_text_with_no_version(string bios)
    {
        var s = ConsoleSetup.Parse("BIOS=" + bios + "\n");
        Assert.Equal(bios, s.Bios);
        Assert.Null(s.BiosVersion);
        Assert.Null(s.BiosDate);
        Assert.Null(s.BiosFamilyHint);
    }

    [Fact]
    public void Backup_memory_cart_rom_and_other_values()
    {
        var s = ConsoleSetup.Parse("BRAM=UNFORMATTED\nCART_BRAM=8000/8192 4096KB\nCART_ROM=PSKAI 0001 3.5\nNETLINK=YES\n" +
                                   "RTC=UNSET\nCART=OTHER\nCART_ID=a1\nAREA=E\nAREA_CODE=12\nVIDEO=PAL50\nLOADER=jhl\n");
        Assert.Null(s.Bram);
        Assert.True(s.BramUnformatted);
        Assert.True(s.HasCartBram);
        Assert.Equal(new BackupMemoryUsage(8000, 8192, 4096), s.CartBram);
        Assert.True(s.CartRomIsPseudoSaturnKai);
        Assert.Equal("PSKAI 0001 3.5", s.CartRom);
        Assert.True(s.NetLink);
        Assert.Equal(ClockState.Unset, s.Clock);
        Assert.Equal(CartKind.Other, s.Cart);
        Assert.Equal((byte)0xA1, s.CartId);
        Assert.Equal(ConsoleArea.Europe, s.Area);
        Assert.Equal(12, s.AreaCode);
        Assert.Equal(VideoStandard.Pal50, s.Video);
        Assert.Equal(DefaultLoader.Jhl, s.Loader);
        Assert.False(ConsoleSetup.Parse("NETLINK=NO\n").NetLink);
        Assert.Null(ConsoleSetup.Parse("AREA_CODE=16\n").AreaCode);
    }
}
