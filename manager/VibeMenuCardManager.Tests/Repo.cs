// VibeMenu Card Manager, GPL-3.0-or-later.
namespace VibeMenuCardManager.Tests;

/// <summary>The repository root: walks up from the test binary until contract/fixtures exists.</summary>
public static class Repo
{
    public static readonly string Root = Find();

    static string Find()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            if (Directory.Exists(Path.Combine(d.FullName, "contract", "fixtures")))
                return d.FullName;
        throw new InvalidOperationException("repo root (contract/fixtures) not found above " + AppContext.BaseDirectory);
    }

    public static string P(string rel) => Path.Combine(Root, rel.Replace('/', Path.DirectorySeparatorChar));
}
