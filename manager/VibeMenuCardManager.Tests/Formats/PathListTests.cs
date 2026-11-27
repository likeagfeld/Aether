// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class PathListTests
{
    [Fact]
    public void Blank_and_comment_lines_are_ignored_and_order_is_kept()
    {
        var r = PathList.Parse("Games/A.cue\n# comment\n\nGames/B.cue\n", PathList.FavsMax, null);
        Assert.Equal(["Games/A.cue", "Games/B.cue"], r.Paths);
        Assert.Equal(0, r.Dropped);
    }

    [Fact]
    public void A_path_not_on_the_card_is_dropped_and_counted()
    {
        var known = new HashSet<string> { "Games/A.cue" };
        var r = PathList.Parse("Games/A.cue\nGames/Gone.cue\n", PathList.FavsMax, known);
        Assert.Equal(["Games/A.cue"], r.Paths);
        Assert.Equal(1, r.Dropped);
    }

    [Fact]
    public void A_duplicate_collapses_to_the_first_occurrence()
    {
        var r = PathList.Parse("Games/A.cue\nGames/A.cue\n", PathList.FavsMax, null);
        Assert.Equal(["Games/A.cue"], r.Paths);
        Assert.Equal(1, r.Dropped);
    }

    [Fact]
    public void A_list_past_cap_drops_the_rest()
    {
        var r = PathList.Parse("A\nB\nC\n", 2, null);
        Assert.Equal(["A", "B"], r.Paths);
        Assert.Equal(1, r.Dropped);
    }

    [Fact]
    public void Write_emits_one_path_per_line_LF_only()
    {
        byte[] b = PathList.Write(["Games/A.cue", "Games/B.cue"], PathList.FavsMax);
        Assert.Equal("Games/A.cue\nGames/B.cue\n", System.Text.Encoding.UTF8.GetString(b));
    }

    [Fact]
    public void Write_truncates_to_the_cap()
    {
        byte[] b = PathList.Write(["A", "B", "C"], 2);
        Assert.Equal("A\nB\n", System.Text.Encoding.UTF8.GetString(b));
    }
}
