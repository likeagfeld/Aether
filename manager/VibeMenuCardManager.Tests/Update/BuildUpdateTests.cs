// Aether Card Manager, GPL-3.0-or-later.
// Telling two builds of one version apart (the 0.1 pre-release vs the final 0.1): each release's version.json gives its
// build time, the running app knows its own (InformationalVersion), and the update check -- with a fake HTTP handler
// routing /releases/latest, /releases and each version.json -- offers a newer build of the same version, never an
// older or the same one, a final build only finals, and falls back to the version alone without a version.json.
using System.Net;
using VibeMenuCardManager.Core.Update;

namespace VibeMenuCardManager.Tests.Update;

public class BuildUpdateTests
{
    const string Old = "20261120.0900", Mid = "20261125.1200", New = "20261127.1532";

    static readonly BuildInfo PreBuild = new("0.1", Mid, "aaaaaaa", Prerelease: true);
    static readonly BuildInfo FinalBuild = new("0.1", Mid, "aaaaaaa", Prerelease: false);

    /// <summary>A release's JSON as GitHub serves it; `build` null = no version.json asset.</summary>
    static string Rel(string tag, bool pre, string? build) =>
        $$"""{"tag_name":"{{tag}}","prerelease":{{(pre ? "true" : "false")}},"draft":false,"body":"notes","html_url":"https://github.com/likeagfeld/Aether/releases/tag/{{tag}}","assets":[{"name":"AetherCardManager-0.1-win-x64.zip","browser_download_url":"https://dl.example/{{tag}}/AetherCardManager-0.1-win-x64.zip","size":10}{{(build == null ? "" : $$""",{"name":"version.json","browser_download_url":"https://dl.example/{{tag}}/version.json","size":80}""")}}]}""";

    static string VersionJson(string build, bool pre) =>
        $$"""{"version":"0.1","build":"{{build}}","commit":"bbbbbbb","prerelease":{{(pre ? "true" : "false")}}}""";

    /// <summary>`latest` (null = 404) at /releases/latest, `list` (null = 404) at /releases, and each release's
    /// version.json from `builds` (tag -> (build, pre)).</summary>
    static FakeHttp Serve(string? latest, string[]? list, Dictionary<string, (string Build, bool Pre)>? builds = null) => new(req =>
    {
        string url = req.RequestUri!.ToString();
        HttpResponseMessage Ok(string s) => new(HttpStatusCode.OK) { Content = new StringContent(s) };
        if (url == AppUpdate.LatestUrl) return latest == null ? new(HttpStatusCode.NotFound) : Ok(latest);
        if (url == AppUpdate.ListUrl) return list == null ? new(HttpStatusCode.NotFound) : Ok("[" + string.Join(",", list) + "]");
        foreach (var (tag, b) in builds ?? [])
            if (url == $"https://dl.example/{tag}/version.json") return Ok(VersionJson(b.Build, b.Pre));
        return new(HttpStatusCode.NotFound);
    });

    static Task<UpdateCheck> Check(FakeHttp http, BuildInfo running) => AppUpdate.CheckAsync(new HttpClient(http), running);

    [Fact]
    public async Task A_pre_release_build_is_offered_the_final_of_the_same_version_built_later()
    {
        var http = Serve(Rel("v0.1", pre: false, New), null, new() { ["v0.1"] = (New, false) });
        var c = await Check(http, PreBuild);
        Assert.True(c.Reached);
        Assert.Equal(("0.1", New, false), (c.Newer!.Version, c.Newer.Build!.Stamp, c.Newer.Prerelease));
        Assert.Equal("A newer build of Aether 0.1 is available", VibeMenuCardManager.UI.MainWindow.UpdateHeadline(c.Newer, PreBuild));
    }

    [Fact]
    public async Task A_final_build_is_not_offered_its_own_build()
    {
        var http = Serve(Rel("v0.1", pre: false, Mid), null, new() { ["v0.1"] = (Mid, false) });
        var c = await Check(http, FinalBuild);
        Assert.True(c.Reached);
        Assert.Null(c.Newer);
        Assert.DoesNotContain(http.Asked, u => u == AppUpdate.ListUrl);   // a final build only looks at finals
    }

    [Fact]
    public async Task A_pre_release_build_is_offered_a_newer_pre_release_from_the_list()
    {
        // no final yet: /releases/latest is a 404, the list has the re-cut pre-release
        var http = Serve(null, [Rel("v0.1-rc2", pre: true, New), Rel("v0.1-rc1", pre: true, Mid)],
                         new() { ["v0.1-rc2"] = (New, true), ["v0.1-rc1"] = (Mid, true) });
        var c = await Check(http, PreBuild);
        Assert.Equal(("v0.1-rc2", New), (c.Newer!.Tag, c.Newer.Build!.Stamp));
        Assert.Contains(AppUpdate.ListUrl, http.Asked);
    }

    [Fact]
    public async Task A_final_build_is_never_offered_a_pre_release()
    {
        var http = Serve(null, [Rel("v0.1-rc2", pre: true, New)], new() { ["v0.1-rc2"] = (New, true) });
        var c = await Check(http, FinalBuild);
        Assert.False(c.Reached);
        Assert.Null(c.Newer);
    }

    [Fact]
    public async Task An_older_build_of_the_same_version_is_not_offered()
    {
        var http = Serve(Rel("v0.1", pre: false, Old), [Rel("v0.1", pre: false, Old)], new() { ["v0.1"] = (Old, false) });
        Assert.Null((await Check(http, PreBuild)).Newer);
        Assert.Null((await Check(http, FinalBuild)).Newer);
    }

    [Fact]
    public async Task Without_a_version_json_only_a_higher_version_is_offered()
    {
        var same = await Check(Serve(Rel("v0.1", pre: false, null), [Rel("v0.1", pre: false, null)]), PreBuild);
        Assert.True(same.Reached);
        Assert.Null(same.Newer);
        var higher = await Check(Serve(Rel("v0.2", pre: false, null), null), FinalBuild);
        Assert.Equal("0.2", higher.Newer!.Version);
        Assert.Null(higher.Newer.Build);
        Assert.Equal("Aether Card Manager 0.2 is available (you have 0.1)", VibeMenuCardManager.UI.MainWindow.UpdateHeadline(higher.Newer, FinalBuild));
    }

    [Fact]
    public async Task A_build_with_no_build_time_compares_by_version_alone()
    {
        var debug = new BuildInfo("0.1", null, null, Prerelease: true);
        var http = Serve(Rel("v0.1", pre: false, New), null, new() { ["v0.1"] = (New, false) });
        Assert.Null((await Check(http, debug)).Newer);
    }

    [Theory]
    [InlineData("0.1+20261127.1532.c33b131", "0.1", "20261127.1532", "c33b131")]
    [InlineData("0.1+.c33b131", "0.1", null, "c33b131")]
    [InlineData("0.1+20261127.1532.", "0.1", "20261127.1532", null)]
    [InlineData("0.1", "0.1", null, null)]
    public void The_build_identity_reads_from_the_informational_version(string info, string ver, string? stamp, string? commit)
    {
        var b = BuildInfo.FromInformationalVersion(info, prerelease: true);
        Assert.Equal((ver, stamp, commit), (b.Version, b.Stamp, b.Commit));
    }

    [Fact]
    public void Version_json_round_trips_and_the_running_build_has_an_identity()
    {
        var b = BuildInfo.FromJson(VersionJson(New, false))!;
        Assert.Equal(("0.1", New, "bbbbbbb", false), (b.Version, b.Stamp, b.Commit, b.Prerelease));
        Assert.Null(BuildInfo.FromJson("not json"));
        var me = BuildInfo.Running();
        Assert.Equal(VibeMenuCardManager.Core.About.Credits.Version, me.Version);
        Assert.NotNull(me.Commit);   // git's short SHA in a checkout (Directory.Build.targets)
    }
}
