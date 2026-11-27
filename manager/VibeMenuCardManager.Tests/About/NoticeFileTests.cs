// VibeMenu Card Manager, GPL-3.0-or-later.
using VibeMenuCardManager.Core.About;

namespace VibeMenuCardManager.Tests.About;

public class NoticeFileTests
{
    [Fact]
    public void Every_bundled_dependency_is_named()
    {
        string text = NoticeFile.Text();
        foreach (var name in NoticeFile.Required)
            Assert.Contains(name, text);
    }

    [Fact]
    public void Mutation_a_removed_dependency_is_caught()
    {
        string text = NoticeFile.Text().Replace("SkiaSharp", "");
        Assert.DoesNotContain("SkiaSharp", text);
    }
}
