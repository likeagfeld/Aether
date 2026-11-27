// VibeMenu Card Manager, GPL-3.0-or-later.
// SPEC §5.14 PERGAME.INI: parse/put/delete mirror the menu's own writer, ported from tools/omsat/pergame.py.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Tests.Formats;

public class PerGameIniTests
{
    const string Example = "# /aether/PERGAME.INI -- Aether per-game options. One section per game.\n" +
                           "[MK-81045@U]\ntitle=Golden Axe - The Duel\nloader=jhl\nsaves=on\nslot=3\ncheats=off\n";

    [Fact]
    public void Parses_the_SPEC_example()
    {
        var g = PerGameIni.Get(Example, "MK-81045@U")!;
        Assert.Equal("Golden Axe - The Duel", g.Title);
        Assert.Equal("jhl", g.Loader);
        Assert.Equal("on", g.Saves);
        Assert.Equal("3", g.Slot);
        Assert.Equal("off", g.Cheats);
    }

    [Fact]
    public void A_default_value_is_never_written()
    {
        var text = PerGameIni.Put("", new PerGameEntry { Id = "MK-81214@UT", Loader = "auto" });
        Assert.Equal("[MK-81214@UT]\n", text);
    }

    [Fact]
    public void A_non_default_key_is_added_after_the_sections_last_line()
    {
        string text = PerGameIni.Put(Example, new PerGameEntry { Id = "MK-81045@U", Title = "Golden Axe - The Duel", Loader = "jhl", Saves = "on", Slot = "3", Cheats = "off", Igr = "off" });
        Assert.Equal(Example + "igr=off\n", text);
    }

    [Fact]
    public void Changing_a_key_keeps_every_other_line_byte_for_byte()
    {
        string text = PerGameIni.Put(Example, new PerGameEntry { Id = "MK-81045@U", Title = "Golden Axe - The Duel", Loader = "cwx", Saves = "on", Slot = "3", Cheats = "off" });
        Assert.Contains("loader=cwx\n", text);
        Assert.Contains("title=Golden Axe - The Duel\n", text);
        Assert.Contains("saves=on\n", text);
    }

    [Fact]
    public void Delete_drops_the_whole_section()
    {
        string text = PerGameIni.Delete(Example, "MK-81045@U");
        Assert.Equal("# /aether/PERGAME.INI -- Aether per-game options. One section per game.\n", text);
    }

    [Fact]
    public void A_new_game_is_appended()
    {
        string text = PerGameIni.Put(Example, new PerGameEntry { Id = "T-1@J", Loader = "jhl" });
        Assert.EndsWith("[T-1@J]\nloader=jhl\n", text);
    }

    [Fact]
    public void Dump_round_trips_an_unchanged_game_byte_for_byte()
    {
        var games = PerGameIni.Parse(Example);
        string text = PerGameIni.Dump(games, Example);
        Assert.Equal(Example, text);
    }

    [Theory]
    [InlineData("GS-9055@JTUE", true)]
    [InlineData("MK-81214@", true)]
    [InlineData("mk-81214@u", false)]
    [InlineData("MK-81214@ABCDEFGHI", false)]
    public void IsId_matches_the_checker_form(string id, bool valid) => Assert.Equal(valid, PerGameIni.IsId(id));
}
