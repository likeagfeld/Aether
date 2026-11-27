// VibeMenu Card Manager, GPL-3.0-or-later.
// dates and numbers are culture-independent. th-TH formats years in the Buddhist calendar (2026 -> 2569)
// and ar-SA uses the Um Al-Qura calendar; neither may reach a file the console reads.
using System.Globalization;
using VibeMenuCardManager.Cli;
using VibeMenuCardManager.Core.Disc;
using VibeMenuCardManager.Core.Formats;
using VibeMenuCardManager.Core.Saves;
using VibeMenuCardManager.Tests.Card;

namespace VibeMenuCardManager.Tests;

public class CultureTests
{
    static T Under<T>(string culture, Func<T> f)
    {
        var (c, u) = (CultureInfo.CurrentCulture, CultureInfo.CurrentUICulture);
        CultureInfo.CurrentCulture = CultureInfo.CurrentUICulture = new CultureInfo(culture);
        try { return f(); }
        finally { (CultureInfo.CurrentCulture, CultureInfo.CurrentUICulture) = (c, u); }
    }

    [Theory]
    [InlineData("th-TH")]
    [InlineData("ar-SA")]
    [InlineData("tr-TR")]
    public void Build_date_is_gregorian_yyyymmdd(string culture)
    {
        Assert.Equal(20261122u, Under(culture, () => Program.BuildDate(new DateTime(2026, 11, 22))));
        // the CLI scan's LIBRARY.IDX carries today's Gregorian date
        using var card = new TempCard();
        using var outd = new TempCard();
        ScanFixture.Realize(ScanFixture.Load(), card.Dir);
        int rc = Under(culture, () => Program.Run(["scan", card.Dir, "--gamesdb", Repo.P("contract/fixtures/tiny.games.db"), "--out", outd.Dir, "--allow-fixed"],
                                                  TextWriter.Null, TextWriter.Null, []));   // temp is on C: (final review I-1 guard)
        Assert.Equal(0, rc);
        var d = LibraryIndex.Read(File.ReadAllBytes(Path.Combine(outd.Dir, "LIBRARY.IDX"))).BuildDate;
        Assert.Equal((uint)DateTime.Now.Year, d / 10000);
    }

    [Theory]
    [InlineData("th-TH")]
    [InlineData("ar-SA")]
    public void Files_and_parsing_do_not_follow_the_culture(string culture)
    {
        var r = new ImportResult { Kai = 12, Rings = 3, Older = 1 };
        Assert.Equal("kai=12 rings=3 present=0 skipped=0 conflicts=0 older=1 cart_only=0 alternates=0\n", Under(culture, r.ImportTxtLine));
        Assert.Equal((2, 3), Under(culture, () => SystemIdReader.DiscNM("CD-2/3")));
        Assert.Equal("A~3.BUP", Under(culture, () => SaveNames.FileName("A\0\0\0\0\0\0\0\0\0\0\0"u8, ["A.BUP", "A~1.BUP", "A~2.BUP"])));
    }
}
