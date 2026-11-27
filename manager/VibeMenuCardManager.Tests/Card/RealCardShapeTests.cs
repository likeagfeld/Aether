// VibeMenu Card Manager, GPL-3.0-or-later.
// Coordinator fix round (diagnosed on the user's real card, mirrored read-only at G:\SATIATOR BACKUP -- 703 root
// names, 842 images, ~615 BOX.TGA; never written here): a synthetic layout that copies its real shape -- flat root
// folders, a multi-disc game in per-disc subfolders, and an image sitting beside a subfolder (like the real card's
// Andretti Racing and PhantasM) -- to prove the scan fixes (own-folder BOX.TGA detection, the OwnFolder flag the
// display-name rule needs) independent of the UI.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;

namespace VibeMenuCardManager.Tests.Card;

public class RealCardShapeTests
{
    static byte[] MakeIso(string product) =>
        SystemIdReader.Make(new Dictionary<string, string> { ["product"] = product, ["area"] = "U", ["title"] = "SOME DISC TITLE", ["device"] = "CD-1/1" });

    /// <summary>The real card's shape, scaled down: a flat single-image game folder; a multi-disc game in its own
    /// per-disc subfolders (PhantasM); and an image sitting beside a subfolder, with its own BOX.TGA (Andretti).</summary>
    static string BuildCard(string root)
    {
        string games = Path.Combine(root, "Games");
        Directory.CreateDirectory(Path.Combine(games, "AlphaGame"));
        File.WriteAllBytes(Path.Combine(games, "AlphaGame", "AlphaGame.iso"), MakeIso("T-00001"));

        Directory.CreateDirectory(Path.Combine(games, "PhantasM (JP)", "Disc 1"));
        Directory.CreateDirectory(Path.Combine(games, "PhantasM (JP)", "Disc 2"));
        File.WriteAllBytes(Path.Combine(games, "PhantasM (JP)", "Disc 1", "PHANTASM_1.iso"), MakeIso("T-00002"));
        File.WriteAllBytes(Path.Combine(games, "PhantasM (JP)", "Disc 2", "PHANTASM_2.iso"), MakeIso("T-00002"));

        Directory.CreateDirectory(Path.Combine(games, "Andretti Racing (USA)", "Extra"));
        File.WriteAllBytes(Path.Combine(games, "Andretti Racing (USA)", "andretti.iso"), MakeIso("T-00003"));
        File.WriteAllBytes(Path.Combine(games, "Andretti Racing (USA)", "BOX.TGA"),
            VibeMenuCardManager.Core.Art.TgaEncoder.Encode(new VibeMenuCardManager.Core.Art.RgbaImage(4, 4, new byte[4 * 4 * 4])));

        Directory.CreateDirectory(Path.Combine(games, "FolderArtOnly"));
        File.WriteAllBytes(Path.Combine(games, "FolderArtOnly", "BOX.TGA"),
            VibeMenuCardManager.Core.Art.TgaEncoder.Encode(new VibeMenuCardManager.Core.Art.RgbaImage(4, 4, new byte[4 * 4 * 4])));
        return root;
    }

    [Fact]
    public void Parent_indices_always_match_the_real_on_disk_structure()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261124);
        for (int i = 1; i < res.Folders.Count; i++)
        {
            var f = res.Folders[i];
            string parentPath = res.Folders[f.Parent].Path;
            bool ok = parentPath == "" ? !f.Path.Contains('/') : f.Path.StartsWith(parentPath + "/", StringComparison.Ordinal);
            Assert.True(ok, $"{f.Path} claims parent {parentPath}");
        }
    }

    [Fact]
    public void A_dedicated_single_image_folder_is_OwnFolder_true()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261124);
        var alpha = res.Entries.Single(e => e.Path.EndsWith("AlphaGame.iso"));
        Assert.True(res.OwnFolder[alpha.Path]);
    }

    [Fact]
    public void Each_PhantasM_style_per_disc_subfolder_is_its_own_OwnFolder_true_game()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261124);
        var disc1 = res.Entries.Single(e => e.Path.EndsWith("PHANTASM_1.iso"));
        var disc2 = res.Entries.Single(e => e.Path.EndsWith("PHANTASM_2.iso"));
        Assert.True(res.OwnFolder[disc1.Path]);
        Assert.True(res.OwnFolder[disc2.Path]);
        Assert.Equal("Games/PhantasM (JP)/Disc 1", disc1.Path[..disc1.Path.LastIndexOf('/')]);
    }

    [Fact]
    public void An_image_beside_a_subfolder_is_OwnFolder_false_and_still_finds_its_BOX_TGA()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261124);
        var andretti = res.Entries.Single(e => e.Path.EndsWith("andretti.iso"));
        Assert.False(res.OwnFolder[andretti.Path]);
        Assert.True(andretti.HasBox, "a loose image beside a subfolder must still see its own folder's BOX.TGA");
    }

    [Fact]
    public void A_folder_with_no_image_at_all_is_a_folder_art_candidate_not_a_game()
    {
        using var card = new TempCard();
        BuildCard(card.Dir);
        var res = CardScanner.Scan(new FsSource(card.Dir), [], new Dictionary<string, string>(), 20261124);
        Assert.DoesNotContain(res.Entries, e => e.Path.Contains("FolderArtOnly"));
        Assert.Contains(res.Folders, f => f.Path == "Games/FolderArtOnly");
    }
}
