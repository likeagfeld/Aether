// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Card;

namespace VibeMenuCardManager.Tests.Card;

public class PreSaveChecksTests
{
    static CardLoad Load() => new() { Root = "X", Db = new VibeMenuCardManager.Core.Formats.GamesDbData([], [], new Dictionary<string, string>(), 0) };

    [Fact]
    public void Not_enough_free_space_is_reported_with_the_needed_and_available_MB()
    {
        var problems = PreSaveChecks.Run("X", Load(), rebuild: null, freeBytes: 100);
        Assert.Contains(problems, p => p.Contains("not enough free space"));
    }

    [Fact]
    public void Plenty_of_free_space_and_no_existing_targets_passes()
    {
        using var dir = new TempCard();
        var problems = PreSaveChecks.Run(dir.Dir, Load(), rebuild: null, freeBytes: long.MaxValue);
        Assert.Empty(problems);
    }

    [Fact]
    public void A_read_only_target_file_is_reported_and_blocks()
    {
        using var dir = new TempCard();
        Directory.CreateDirectory(Path.Combine(dir.Dir, "aether"));
        string gdb = Path.Combine(dir.Dir, "aether", "GAMES.DB");
        File.WriteAllBytes(gdb, [1, 2, 3]);
        File.SetAttributes(gdb, FileAttributes.ReadOnly);
        try
        {
            var problems = PreSaveChecks.Run(dir.Dir, Load(), rebuild: null, freeBytes: long.MaxValue);
            Assert.Contains(problems, p => p.Contains("GAMES.DB") && p.Contains("read-only"));
        }
        finally { File.SetAttributes(gdb, FileAttributes.Normal); }
    }

    [Fact]
    public void A_file_open_exclusively_elsewhere_is_reported_as_locked()
    {
        using var dir = new TempCard();
        Directory.CreateDirectory(Path.Combine(dir.Dir, "aether"));
        string favs = Path.Combine(dir.Dir, "aether", "FAVS.TXT");
        File.WriteAllBytes(favs, [1]);
        using var locked = new FileStream(favs, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        var problems = PreSaveChecks.Run(dir.Dir, Load(), rebuild: null, freeBytes: long.MaxValue);
        Assert.Contains(problems, p => p.Contains("FAVS.TXT") && (p.Contains("open in another program") || p.Contains("access denied")));
    }

    [Fact]
    public void EstimateBytesNeeded_sums_staged_covers_and_the_rebuilt_files()
    {
        var load = Load();
        var rebuild = new RebuildResult(new byte[10], new byte[20], new byte[30],
            new ScanResult([], [], [], 0, 0, 0, 0, 0, new Dictionary<string, uint>(), new Dictionary<string, VibeMenuCardManager.Core.Disc.SystemId?>(), new Dictionary<string, bool>(),
                new Dictionary<string, List<string>>()),
            new VibeMenuCardManager.Core.Formats.GamesDbData([], [], new Dictionary<string, string>(), 0));
        long n = PreSaveChecks.EstimateBytesNeeded(load, rebuild);
        Assert.Equal(10 + 20 + 30, n);
    }

    [Fact]
    public void TargetPaths_names_every_file_Apply_would_touch()
    {
        var load = Load();
        var paths = PreSaveChecks.TargetPaths("X", load).ToList();
        Assert.Contains(paths, p => p.EndsWith("GAMES.DB"));
        Assert.Contains(paths, p => p.EndsWith("FAVS.TXT"));
        Assert.Contains(paths, p => p.EndsWith("USEREDIT.INI"));
    }
}
