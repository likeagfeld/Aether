// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class UserEditsTests
{
    [Fact]
    public void Round_trips_through_Write_and_Parse()
    {
        var edits = new Dictionary<string, UserEditRecord>
        {
            ["MK-81214"] = new() { Title = "Golden Axe: The Duel", Genre = "Fighting", Year = 1995, Region = "U", Players = 2 },
            ["T-1"] = new() { Genre = "RPG" },
        };
        string text = UserEdits.Write(edits);
        var back = UserEdits.Parse(text);
        Assert.Equal(2, back.Count);
        Assert.True(back["MK-81214"].SameAs(edits["MK-81214"]));
        Assert.True(back["T-1"].SameAs(edits["T-1"]));
    }

    [Fact]
    public void Write_skips_empty_records()
    {
        var edits = new Dictionary<string, UserEditRecord> { ["X"] = new() };
        string text = UserEdits.Write(edits);
        Assert.DoesNotContain("[X]", text);
    }

    [Fact]
    public void Parse_ignores_comments_blank_lines_and_an_unknown_key()
    {
        string text = "# a comment\n\n[T-1]\n# another comment\nyear=1999\nunknown=ignored\n";
        var edits = UserEdits.Parse(text);
        Assert.Equal(1999, edits["T-1"].Year);
    }

    [Fact]
    public void Apply_overrides_only_the_non_empty_fields_and_leaves_unmatched_records_alone()
    {
        var r1 = new GameRecord { Key = "T-1", TitleEn = "Old Title", Genre = 0, Year = 1990, RegionFlags = 1, Players = 1 };
        var r2 = new GameRecord { Key = "T-2", TitleEn = "Untouched", Year = 2000 };
        var edits = new Dictionary<string, UserEditRecord> { ["T-1"] = new() { Title = "New Title", Year = 2020 } };
        var genres = UserEdits.Apply([r1, r2], ["Action"], edits);
        Assert.Equal("New Title", r1.TitleEn);
        Assert.Equal(2020, r1.Year);
        Assert.Equal(0, r1.Genre);           // not overridden: stays as it was
        Assert.Equal(1, r1.RegionFlags);     // not overridden
        Assert.Equal("Untouched", r2.TitleEn);
        Assert.Equal(2000, r2.Year);
        Assert.Equal(["Action"], genres);
    }

    [Fact]
    public void Apply_adds_a_new_genre_name_and_reuses_an_existing_one_case_insensitively()
    {
        var r1 = new GameRecord { Key = "T-1", TitleEn = "A" };
        var r2 = new GameRecord { Key = "T-2", TitleEn = "B" };
        var edits = new Dictionary<string, UserEditRecord> { ["T-1"] = new() { Genre = "Puzzle" }, ["T-2"] = new() { Genre = "action" } };
        var genres = UserEdits.Apply([r1, r2], ["Action"], edits);
        Assert.Equal(["Action", "Puzzle"], genres);
        Assert.Equal(1, r1.Genre);   // the new "Puzzle" entry
        Assert.Equal(0, r2.Genre);   // reused "Action" (case-insensitive), not a duplicate
    }

    [Fact]
    public void Apply_sets_region_flags_from_the_letters()
    {
        var r = new GameRecord { Key = "T-1", TitleEn = "A", RegionFlags = 0 };
        UserEdits.Apply([r], [], new Dictionary<string, UserEditRecord> { ["T-1"] = new() { Region = "UE" } });
        Assert.Equal(GamesDb.RegionFlagsFrom("UE"), r.RegionFlags);
    }
}
