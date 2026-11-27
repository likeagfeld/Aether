// VibeMenu Card Manager, GPL-3.0-or-later.
// The Card Manager's self-update, with a fake HTTP handler (never the network): the version compare, a 404/offline/
// no-release check read as "can't check", the asset for each platform, a size or SHA-256 mismatch refused, the folder
// swap and its rollback, and the updater script doing the same swap.
using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using VibeMenuCardManager.Core.Update;

namespace VibeMenuCardManager.Tests.Update;

sealed class FakeHttp(Func<HttpRequestMessage, HttpResponseMessage> answer) : HttpMessageHandler
{
    public readonly List<string> Asked = [];
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
    {
        Asked.Add(request.RequestUri!.ToString());
        return Task.FromResult(answer(request));
    }
}

public class AppUpdateTests
{
    static HttpResponseMessage Ok(string body) => new(HttpStatusCode.OK) { Content = new StringContent(body) };
    static HttpResponseMessage Ok(byte[] body) => new(HttpStatusCode.OK) { Content = new ByteArrayContent(body) };

    static string Latest(string tag, params (string Name, long Size)[] assets) =>
        $$"""{"tag_name":"{{tag}}","body":"What's new","html_url":"https://github.com/likeagfeld/Aether/releases/tag/{{tag}}","assets":[{{string.Join(",",
            assets.Select(a => $$"""{"name":"{{a.Name}}","browser_download_url":"https://dl.example/{{a.Name}}","size":{{a.Size}}}"""))}}]}""";

    [Theory]
    [InlineData("0.1", "0.1.1", -1)]
    [InlineData("0.1.1", "0.2", -1)]
    [InlineData("0.2", "0.10", -1)]
    [InlineData("v0.2", "0.2.0", 0)]
    [InlineData("cardmanager-1.0", "0.9.9", 1)]
    public void Versions_compare_by_their_numbers(string a, string b, int want) => Assert.Equal(want, Math.Sign(AppVersion.Compare(a, b)));

    [Fact]
    public async Task A_404_offline_or_unparsable_answer_is_cant_check_never_an_error()
    {
        foreach (var answer in new Func<HttpRequestMessage, HttpResponseMessage>[]
                 {
                     _ => new HttpResponseMessage(HttpStatusCode.NotFound),                  // private repo, or no release yet
                     _ => throw new HttpRequestException("offline"),
                     _ => throw new TaskCanceledException("timeout"),
                     _ => Ok("<html>not json</html>"),
                     _ => Ok("""{"message":"no tag"}"""),
                 })
            Assert.Null(await AppUpdate.LatestAsync(new HttpClient(new FakeHttp(answer))));
    }

    [Fact]
    public async Task A_newer_release_is_found_with_its_notes_and_this_platforms_package()
    {
        var fake = new FakeHttp(_ => Ok(Latest("v0.2", ("AetherCardManager-0.2-win-x64.zip", 10), ("AetherCardManager-0.2-linux-x64.tar.gz", 11), ("SHA256SUMS.txt", 3))));
        var r = await AppUpdate.LatestAsync(new HttpClient(fake));
        Assert.Equal(AppUpdate.LatestUrl, Assert.Single(fake.Asked));
        Assert.Equal(("0.2", "What's new"), (r!.Version, r.Notes));
        Assert.Same(r, AppUpdate.Newer(r, "0.1"));
        Assert.Null(AppUpdate.Newer(r, "0.2"));
        Assert.Equal("AetherCardManager-0.2-win-x64.zip", AppUpdate.AssetFor(r, "win-x64")!.Name);
        Assert.Equal("AetherCardManager-0.2-linux-x64.tar.gz", AppUpdate.AssetFor(r, "linux-x64")!.Name);
        Assert.Null(AppUpdate.AssetFor(r, "osx-arm64"));     // the .app zip: the user installs it from the release page
        Assert.Null(AppUpdate.AssetFor(r, "win-arm64"));     // not in this release
    }

    static (ReleaseInfo R, Func<HttpRequestMessage, HttpResponseMessage> Serve) Release(byte[] pkg, string sums, long? size = null)
    {
        string name = "AetherCardManager-0.2-win-x64.zip";
        var r = new ReleaseInfo("0.2", "v0.2", "", AppUpdate.ReleasesPage,
            [new(name, "https://dl.example/" + name, size ?? pkg.Length), new("SHA256SUMS.txt", "https://dl.example/SHA256SUMS.txt", sums.Length)]);
        return (r, req => req.RequestUri!.AbsolutePath.EndsWith(".txt", StringComparison.Ordinal) ? Ok(sums) : Ok(pkg));
    }

    [Fact]
    public async Task The_download_is_kept_only_when_its_size_and_sha256_match()
    {
        using var t = new TempCard();
        byte[] pkg = Encoding.ASCII.GetBytes("a package");
        string sha = Convert.ToHexStringLower(SHA256.HashData(pkg));
        string good = $"{sha}  AetherCardManager-0.2-win-x64.zip\n{new string('0', 64)}  other.zip\n";

        var (r, serve) = Release(pkg, good);
        string path = await AppUpdate.DownloadVerifiedAsync(new HttpClient(new FakeHttp(serve)), r, r.Assets[0], t.Dir);
        Assert.Equal(pkg, File.ReadAllBytes(path));

        string bad = good.Replace(sha, new string('f', 64));
        (r, serve) = Release(pkg, bad);
        string dir2 = Path.Combine(t.Dir, "b");
        var e = await Assert.ThrowsAsync<UpdateException>(() => AppUpdate.DownloadVerifiedAsync(new HttpClient(new FakeHttp(serve)), r, r.Assets[0], dir2));
        Assert.Contains("SHA-256", e.Message);
        Assert.False(File.Exists(Path.Combine(dir2, r.Assets[0].Name)));

        (r, serve) = Release(pkg, good, size: pkg.Length + 1);
        Assert.Contains("the release says", (await Assert.ThrowsAsync<UpdateException>(() =>
            AppUpdate.DownloadVerifiedAsync(new HttpClient(new FakeHttp(serve)), r, r.Assets[0], Path.Combine(t.Dir, "c")))).Message);

        (r, serve) = Release(pkg, $"{sha}  some-other-name.zip\n");
        Assert.Contains("no line", (await Assert.ThrowsAsync<UpdateException>(() =>
            AppUpdate.DownloadVerifiedAsync(new HttpClient(new FakeHttp(serve)), r, r.Assets[0], Path.Combine(t.Dir, "d")))).Message);
    }

    static string App(string root, string version)
    {
        string app = Path.Combine(root, "VibeMenuCardManager");
        Directory.CreateDirectory(Path.Combine(app, "release"));
        File.WriteAllText(Path.Combine(app, "version.txt"), version);
        File.WriteAllText(Path.Combine(app, "release", "autoboot.iso"), "menu " + version);
        return app;
    }

    [Fact]
    public void A_staged_package_swaps_in_and_the_old_version_is_kept()
    {
        using var t = new TempCard();
        string app = App(t.Dir, "0.1");
        string zip = Path.Combine(t.Dir, "pkg.zip");
        string src = App(Path.Combine(t.Dir, "src"), "0.2");
        ZipFile.CreateFromDirectory(src, zip);
        string staged = AppUpdate.Stage(zip, app);
        AppUpdate.Swap(app, staged);
        Assert.Equal("0.2", File.ReadAllText(Path.Combine(app, "version.txt")));
        Assert.Equal("0.1", File.ReadAllText(Path.Combine(app + ".old", "version.txt")));
        Assert.False(Directory.Exists(staged));
    }

    [Fact]
    public void A_swap_that_fails_rolls_back_to_the_old_version()
    {
        using var t = new TempCard();
        string app = App(t.Dir, "0.1");
        string staged = App(Path.Combine(t.Dir, "staged"), "0.2");
        Assert.Throws<IOException>(() => AppUpdate.Swap(app, staged, (from, to) => { if (from == staged) throw new IOException("injected: the new folder is locked"); }));
        Assert.Equal("0.1", File.ReadAllText(Path.Combine(app, "version.txt")));
        Assert.Equal("menu 0.1", File.ReadAllText(Path.Combine(app, "release", "autoboot.iso")));
        Assert.False(Directory.Exists(app + ".old"));
    }

    /// <summary>The updater script itself (PowerShell, Windows only): the swap, and the rollback when the staged
    /// folder is gone. The relaunched "exe" is a harmless stand-in.</summary>
    [SkippableTheory]
    [InlineData(true)]
    [InlineData(false)]
    public void The_windows_updater_script_swaps_or_rolls_back(bool stagedExists)
    {
        Skip.IfNot(OperatingSystem.IsWindows(), "the PowerShell updater is the Windows one");
        using var t = new TempCard();
        string app = App(t.Dir, "0.1");
        string staged = Path.Combine(t.Dir, "VibeMenuCardManager.update");
        if (stagedExists) Directory.Move(App(Path.Combine(t.Dir, "s"), "0.2"), staged);
        string script = Path.Combine(t.Dir, "update.ps1");
        File.WriteAllText(script, AppUpdate.Script(windows: true, pid: 0, app, staged, exe: Path.Combine(Environment.SystemDirectory, "where.exe")));
        var p = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo("powershell.exe",
            ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script]) { UseShellExecute = false, CreateNoWindow = true })!;
        Assert.True(p.WaitForExit(60_000));
        Assert.Equal(stagedExists ? 0 : 1, p.ExitCode);
        Assert.Equal(stagedExists ? "0.2" : "0.1", File.ReadAllText(Path.Combine(app, "version.txt")));
    }
}
