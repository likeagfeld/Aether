// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Card;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Card;

public class EditHistoryTests
{
    static CardLoad Load(params GameRow[] rows)
    {
        var load = new CardLoad { Root = "X", Db = new GamesDbData([], [], new Dictionary<string, string>(), 0) };
        load.Rows.AddRange(rows);
        return load;
    }

    static GameRow Row(string path = "Games/A/GAME.ISO") => new() { Path = path, OriginalPath = path };

    [Fact]
    public void Undo_restores_a_rows_field_without_replacing_the_row_instance()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        h.Push(load);
        row.TitleOverride = "Renamed";

        Assert.True(h.Undo(load));
        Assert.Equal("", row.TitleOverride);
        Assert.Same(row, load.Rows[0]);       // same instance: selection/identity survives
    }

    [Fact]
    public void Redo_reapplies_what_undo_undid()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        h.Push(load);
        row.Favorite = true;
        h.Undo(load);
        Assert.False(row.Favorite);

        Assert.True(h.Redo(load));
        Assert.True(row.Favorite);
    }

    [Fact]
    public void A_new_change_after_undo_clears_the_redo_branch()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        h.Push(load); row.Loader = "jhl";
        h.Undo(load);
        Assert.True(h.CanRedo);

        h.Push(load); row.Loader = "cwx";
        Assert.False(h.CanRedo);
    }

    [Fact]
    public void Undo_and_redo_restore_a_match_override_and_UserEdits()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        h.Push(load);
        row.MatchOverride = "T-99999";
        load.UserEdits["T-1"] = new UserEditRecord { Title = "Override" };

        Assert.True(h.Undo(load));
        Assert.Equal("", row.MatchOverride);
        Assert.Empty(load.UserEdits);

        Assert.True(h.Redo(load));
        Assert.Equal("T-99999", row.MatchOverride);
        Assert.Equal("Override", load.UserEdits["T-1"].Title);
    }

    [Fact]
    public void At_least_20_levels_are_kept()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        for (int i = 0; i < 25; i++) { h.Push(load); row.TitleOverride = $"v{i}"; }

        int undone = 0;
        while (h.Undo(load)) undone++;
        Assert.True(undone >= 20, $"only {undone} levels were kept");
    }

    [Fact]
    public void Undo_with_nothing_staged_does_nothing_and_returns_false()
    {
        var load = Load(Row());
        var h = new EditHistory();
        Assert.False(h.Undo(load));
        Assert.False(h.Redo(load));
    }

    [Fact]
    public void Removing_a_row_and_undoing_restores_membership_not_just_the_Removed_flag()
    {
        var row = Row();
        var load = Load(row);
        var h = new EditHistory();

        h.Push(load);
        load.Rows.Remove(row);   // simulating a structural change some action might make

        Assert.True(h.Undo(load));
        Assert.Contains(row, load.Rows);
    }
}
