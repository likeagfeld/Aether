// VibeMenu Card Manager, GPL-3.0-or-later.
using System.Text.Json;
using VibeMenuCardManager.Core.Saves;

namespace VibeMenuCardManager.Tests.Saves;

public class SaveImporterTests
{
    [Fact]
    public void A_card_without_vibemenu_is_refused_and_left_untouched()
    {
        using var card = new TempCard();
        var spec = ImportGolden.Spec("merge");
        card.Realize(JsonDocument.Parse(JsonSerializer.Serialize(new { files = spec.GetProperty("files")
            .EnumerateArray().Where(f => !f.GetProperty("path").GetString()!.StartsWith("aether/")).ToArray() })).RootElement);
        var before = card.Manifest();
        var e = Assert.Throws<ImportStoppedException>(() => SaveImporter.Run(new CardFs(card.Dir)));
        Assert.Contains("/aether is missing", e.Message);
        Assert.Equal(before, card.Manifest());
    }

    [Fact]
    public void A_stale_IMP_from_a_power_cut_is_never_taken_and_is_replaced()
    {
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        var stale = Path.Combine(card.Dir, "aether", "SAVES", "T-123");
        Directory.CreateDirectory(stale);
        File.WriteAllBytes(Path.Combine(stale, "ABC_SAVE_01.BUP.IMP"), [1, 2, 3]);      // a cut before the rename
        var r = SaveImporter.Run(new CardFs(card.Dir));
        Assert.Equal(8 + 8, r.Kai + r.Rings);
        Assert.False(File.Exists(Path.Combine(stale, "ABC_SAVE_01.BUP.IMP")));
        Assert.Equal(124, new FileInfo(Path.Combine(stale, "ABC_SAVE_01.BUP")).Length);
    }

    [Fact]
    public void Copies_are_verbatim_including_Kais_save_id_and_trailing_bytes()
    {
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("merge"));
        SaveImporter.Run(new CardFs(card.Dir));
        var src = File.ReadAllBytes(Path.Combine(card.Dir, "SAVES", "BRAM", "MK-81214[UT]", "SGCNAME_ABC.BUP"));
        var dst = File.ReadAllBytes(Path.Combine(card.Dir, "aether", "SAVES", "MK-81214[UT]", "SGCNAME_ABC.BUP"));
        Assert.Equal(src, dst);                       // byte 0x1B '.', and the 3 bytes after the data
        Assert.Equal((byte)'.', dst[0x1B]);
        var rings = File.ReadAllBytes(Path.Combine(card.Dir, "satiator-saves", "MK-81214", "SCORCHER_01.BUP"));
        Assert.Equal(rings, File.ReadAllBytes(Path.Combine(card.Dir, "aether", "SAVES", "MK-81214[UT]", "SCORCHER_01.BUP")));
        Assert.Equal(2u, VibeMenuCardManager.Core.Be.Get32(rings, 4));     // save_id kept
    }

    [Fact]
    public void Progress_counts_every_eighth_copy()
    {
        using var card = new TempCard();
        card.Realize(ImportGolden.Spec("corpus"));
        var said = new List<string>();
        var r = SaveImporter.Run(new CardFs(card.Dir), said.Add);
        Assert.Equal((r.Kai + r.Rings) / 8, said.Count(s => s.StartsWith("SAVES: COPYING KAI/RINGS SAVES ")));
    }
}
