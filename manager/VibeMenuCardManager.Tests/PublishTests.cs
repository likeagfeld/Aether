// VibeMenu Card Manager, GPL-3.0-or-later.
// Final review I-3: a published Card Manager carries the pinned Noto Sans JP and its licence in fonts/ (where
// TitleRenderer.DefaultFontPath looks), and a build without the font says so in the CLI and the UI -- never "0 JP titles"
// alone.
using System.Diagnostics;
using System.Security.Cryptography;
using VibeMenuCardManager.Cli;
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Tests.Card;

namespace VibeMenuCardManager.Tests;

public class PublishTests
{
    const string FontSha = "dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073";   // Directory.Build.targets
    const string OflSha = "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2";

    [SkippableTheory]
    [InlineData("VibeMenuCardManager.UI")]
    [InlineData("VibeMenuCardManager.Cli")]
    public void Publish_output_carries_the_pinned_font_and_its_licence(string project)
    {
        Skip.IfNot(File.Exists(TitleRenderer.DefaultFontPath), "Noto Sans JP is not on this checkout (offline build)");
        using var t = new TempCard();
        var psi = new ProcessStartInfo("dotnet", ["publish", Repo.P($"manager/{project}/{project}.csproj"), "-c", "Release", "-o", t.Dir, "--nologo"])
            { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
        using var p = Process.Start(psi)!;
        string log = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit();
        Assert.True(p.ExitCode == 0, log);
        string font = Path.Combine(t.Dir, "fonts", "NotoSansJP-Regular.otf"), ofl = Path.Combine(t.Dir, "fonts", "OFL.txt");
        Assert.True(File.Exists(font), $"{project}: no fonts/NotoSansJP-Regular.otf in the publish output");
        Assert.Equal(FontSha, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(font))));
        Assert.Equal(OflSha, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(ofl))));
        using var tr = new TitleRenderer(font);                       // the published copy renders
    }

    [Fact]
    public void A_build_without_the_font_says_so_in_the_CLI()
    {
        using var t = new TempCard();
        t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card"), release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var o = new StringWriter();
        var e = new StringWriter();
        Assert.True(Program.Run(["build", card, "--release", release, "--font", Path.Combine(t.Dir, "none.otf"), "--allow-fixed"], o, e, []) == 0, e.ToString());
        Assert.Contains("warning: JP titles were NOT rendered: font missing at", e.ToString());
        Assert.Contains("none.otf", e.ToString());
    }
}
