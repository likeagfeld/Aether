// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class BatchRenameTests
{
    static GameRow Row(string path, string key, VibeMenuCardManager.Core.Disc.SystemId? sid = null) => new()
    {
        Path = path, OriginalPath = path, ScanKey = key, Sid = sid, FolderId = 0,
    };

    static readonly Dictionary<string, GameRecord> Db = new()
    {
        ["T-001"] = new GameRecord { Key = "T-001", TitleEn = "Alpha From DB" },
    };

    [Fact]
    public void DbTitle_source_uses_the_matched_GAMES_DB_record()
    {
        var row = Row("Games/alpha_game/GAME.ISO", "T-001");
        var preview = BatchRename.Compute([row], RenameSource.DbTitle, titleCase: false, Db);
        Assert.Equal("Alpha From DB", preview[0].NewTitle);
    }

    [Fact]
    public void DiscHeader_source_uses_the_System_ID_title()
    {
        var sid = SystemIdReader.Parse(SystemIdReader.Make(new Dictionary<string, string> { ["title"] = "ALPHA DISC TITLE" }));
        var row = Row("Games/alpha_game/GAME.ISO", "", sid);
        var preview = BatchRename.Compute([row], RenameSource.DiscHeader, titleCase: false, Db);
        Assert.Equal("ALPHA DISC TITLE", preview[0].NewTitle);
    }

    [Fact]
    public void FolderName_and_FileName_sources_use_the_path()
    {
        var row = Row("Games/alpha_game/GAME.ISO", "");
        Assert.Equal("alpha_game", BatchRename.Compute([row], RenameSource.FolderName, false, Db)[0].NewTitle);
        Assert.Equal("GAME", BatchRename.Compute([row], RenameSource.FileName, false, Db)[0].NewTitle);
    }

    [Fact]
    public void Title_case_transforms_the_chosen_source()
    {
        var row = Row("Games/alpha_game/GAME.ISO", "");
        var preview = BatchRename.Compute([row], RenameSource.FolderName, titleCase: true, Db);
        Assert.Equal("Alpha_Game", preview[0].NewTitle);
    }

    [Fact]
    public void A_row_with_no_match_gives_an_empty_preview_and_Apply_leaves_it_alone()
    {
        var row = Row("Games/x/GAME.ISO", "");
        var preview = BatchRename.Compute([row], RenameSource.DbTitle, false, Db);
        Assert.Equal("", preview[0].NewTitle);
        Assert.True(preview[0].NoOp);
        int n = BatchRename.Apply(preview);
        Assert.Equal(0, n);
        Assert.Equal("", row.TitleOverride);
    }

    [Fact]
    public void Apply_stages_TitleOverride_on_every_changed_row_and_counts_them()
    {
        var r1 = Row("Games/a/GAME.ISO", "T-001");
        var r2 = Row("Games/b/GAME.ISO", "");
        var preview = BatchRename.Compute([r1, r2], RenameSource.DbTitle, false, Db);
        int n = BatchRename.Apply(preview);
        Assert.Equal(1, n);
        Assert.Equal("Alpha From DB", r1.TitleOverride);
        Assert.Equal("", r2.TitleOverride);
    }

    [Fact]
    public void Apply_is_a_noop_when_the_new_title_already_matches_the_staged_override()
    {
        var row = Row("Games/a/GAME.ISO", "T-001");
        row.TitleOverride = "Alpha From DB";
        var preview = BatchRename.Compute([row], RenameSource.DbTitle, false, Db);
        Assert.True(preview[0].NoOp);
        Assert.Equal(0, BatchRename.Apply(preview));
    }
}
