// VibeMenu Card Manager, GPL-3.0-or-later.
// the source-drift comparator must go GREEN on the pinned sources and RED on one changed byte.
using System.Security.Cryptography;
using VibeMenuCardManager.Tests.Saves;

namespace VibeMenuCardManager.Tests.Mutation;

public class SourcePinMutationTests
{
    static string[] Pin() => File.ReadAllLines(Repo.P("manager/golden/sources.txt"));

    /// <summary>Positive control: the pin's own sources, as run_golden.sh extracted them (git archive into the
    /// gitignored manager/golden/out/src), match sources.txt. Skipped when the harness has not run on this checkout.</summary>
    [SkippableFact]
    public void The_archived_pin_sources_match_sources_txt()
    {
        var src = Repo.P("manager/golden/out/src");
        Skip.IfNot(Directory.Exists(src), "manager/golden/out/src is absent: run_golden.sh has not run on this checkout");
        Assert.Empty(SaveImporterGoldenTests.SourceDrift(src, Pin()));
    }

    [Fact]
    public void One_changed_byte_and_one_missing_file_are_caught()
    {
        using var t = new TempCard();
        var a = Path.Combine(t.Dir, "menu", "a.c");
        Directory.CreateDirectory(Path.GetDirectoryName(a)!);
        File.WriteAllText(a, "int x;\r\n");                           // CRLF checkout: hashed as LF
        string sha = Convert.ToHexStringLower(SHA256.HashData("int x;\n"u8));
        string[] pin = ["pinned 0000000", $"{sha} menu/a.c", $"{sha} menu/b.c"];
        Assert.Equal(["menu/b.c: missing"], SaveImporterGoldenTests.SourceDrift(t.Dir, pin));
        // a lone CR is a change, not a line ending: stripping every CR would turn this
        // into "int x;\n" -- the pinned text -- and miss it; only a CR before an LF may be dropped
        File.WriteAllText(a, "i\rnt x;\n");
        Assert.Contains("menu/a.c: differs from the pin", SaveImporterGoldenTests.SourceDrift(t.Dir, pin));
        File.WriteAllText(a, "int y;\n");
        Assert.Contains("menu/a.c: differs from the pin", SaveImporterGoldenTests.SourceDrift(t.Dir, pin));
    }
}
