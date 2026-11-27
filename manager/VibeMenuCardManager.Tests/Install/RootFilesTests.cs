// VibeMenu Card Manager, GPL-3.0-or-later.
// The user's card (2026-11-25): Prepare/Update replaces /autoboot.iso only. The old autoboot.iso is kept as
// autoboot.iso.bak-<stamp>; every other root file -- autoboot.rings.iso (the Rings menu their swap scripts use),
// autobootdesc.txt (a Satiator autoboot descriptor), autoboot.cue/.elf, menu.bin -- keeps its name and its bytes.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Tests.Card;

namespace VibeMenuCardManager.Tests.Install;

public class RootFilesTests
{
    [Fact]
    public void Prepare_then_update_keeps_every_other_root_file_by_name_and_bytes()
    {
        using var t = new TempCard();
        t.Realize(System.Text.Json.JsonDocument.Parse(File.ReadAllText(BuildCheck.Case("card.json"))).RootElement);
        string card = Path.Combine(t.Dir, "card");
        string release = ReleaseFixture.Make(Path.Combine(t.Dir, "release"));
        var theirs = new Dictionary<string, byte[]>
        {
            ["autoboot.rings.iso"] = "the Rings menu, swapped in by the user's scripts"u8.ToArray(),
            ["autobootdesc.txt"] = "Satiator autoboot descriptor\r\n"u8.ToArray(),
            ["autoboot.cue"] = "FILE \"autoboot.bin\" BINARY\n"u8.ToArray(),
            ["autoboot.elf"] = [0x7F, (byte)'E', (byte)'L', (byte)'F'],
            ["AUTOBOOT.TXT"] = "notes"u8.ToArray(),
            ["menu.bin"] = "the official menu"u8.ToArray(),
        };
        foreach (var (n, b) in theirs) File.WriteAllBytes(Path.Combine(card, n), b);
        File.WriteAllText(Path.Combine(card, "autoboot.iso"), "Satiator Rings");

        var first = CardPipeline.Run(card, new PipelineOptions(release, Compat: false), 20261125, null, "s1");   // Prepare
        var second = CardPipeline.Run(card, new PipelineOptions(release, Compat: false), 20261125, null, "s2");  // Update card

        foreach (var (n, b) in theirs) Assert.Equal(b, File.ReadAllBytes(Path.Combine(card, n)));
        var root = Directory.GetFiles(card).Select(Path.GetFileName).Order(StringComparer.Ordinal).ToList();
        Assert.Equal(theirs.Keys.Concat(["autoboot.iso", "autoboot.iso.bak-s1", "autoboot.iso.bak-s2"]).Order(StringComparer.Ordinal), root);
        Assert.Equal("Satiator Rings", File.ReadAllText(Path.Combine(card, "autoboot.iso.bak-s1")));
        Assert.Equal(File.ReadAllBytes(Path.Combine(release, "autoboot.iso")), File.ReadAllBytes(Path.Combine(card, "autoboot.iso")));
        Assert.Equal(["autoboot.iso.bak-s1"], first.Install.Backups);
        Assert.Equal(["autoboot.iso.bak-s2"], second.Install.Backups);
        Assert.Contains(first.Install.Warnings, w => w.Contains("autoboot.elf", StringComparison.Ordinal));
    }
}
