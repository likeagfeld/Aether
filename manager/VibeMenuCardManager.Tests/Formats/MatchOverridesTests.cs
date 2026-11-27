// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class MatchOverridesTests
{
    [Fact]
    public void Write_then_Parse_round_trips()
    {
        var matches = new Dictionary<string, string> { ["Games/Foo/GAME.ISO"] = "T-00123", ["Games/Bar/x.cue"] = "MK-81070" };
        string text = MatchOverrides.Write(matches);
        var back = MatchOverrides.Parse(text);
        Assert.Equal(matches, back);
    }

    [Fact]
    public void Empty_map_writes_nothing()
    {
        Assert.Equal("", MatchOverrides.Write(new Dictionary<string, string>()));
    }

    [Fact]
    public void Split_is_on_the_last_equals_so_a_path_may_contain_one()
    {
        var back = MatchOverrides.Parse("[MATCH]\nGames/A=B/game.cue=T-00001\n");
        Assert.Equal("T-00001", back["Games/A=B/game.cue"]);
    }

    [Fact]
    public void Ignores_lines_outside_the_MATCH_section()
    {
        string text = "[T-00001]\ntitle=Something\n\n[MATCH]\nGames/A/game.cue=T-00002\n";
        var back = MatchOverrides.Parse(text);
        Assert.Single(back);
        Assert.Equal("T-00002", back["Games/A/game.cue"]);
    }

    [Fact]
    public void UserEdits_Parse_skips_the_reserved_MATCH_section_as_a_key()
    {
        string text = "[MATCH]\nGames/A/game.cue=T-00002\n[T-00009]\ntitle=Real Edit\n";
        var edits = UserEdits.Parse(text);
        Assert.DoesNotContain("MATCH", edits.Keys);
        Assert.Equal("Real Edit", edits["T-00009"].Title);
    }

    [Fact]
    public void UserEdits_Write_and_MatchOverrides_Write_combine_into_one_valid_file()
    {
        var edits = new Dictionary<string, UserEditRecord> { ["T-00009"] = new() { Title = "Real Edit" } };
        var matches = new Dictionary<string, string> { ["Games/A/game.cue"] = "T-00002" };
        string combined = UserEdits.Write(edits) + MatchOverrides.Write(matches);
        Assert.Equal("Real Edit", UserEdits.Parse(combined)["T-00009"].Title);
        Assert.Equal("T-00002", MatchOverrides.Parse(combined)["Games/A/game.cue"]);
    }
}
