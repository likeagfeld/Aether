// VibeMenu Card Manager, GPL-3.0-or-later.
// Help > Check for updates and the start-up check, with a fake HTTP handler: a newer release shows the banner (version
// and notes), the same version or a 404 (private repo, no release) says so in the status line, never a dialog.
using System.Net;
using Avalonia.Controls;
using VibeMenuCardManager.Core.About;
using VibeMenuCardManager.Tests.Update;
using VibeMenuCardManager.UI;

namespace VibeMenuCardManager.Tests.UI;

public class UpdateCheckTests
{
    static async Task<(bool Banner, string Status, string? Version)> Check(Func<HttpRequestMessage, HttpResponseMessage> answer) =>
        await HeadlessApp.Run(async () =>
        {
            var w = new MainWindow(removableDriveListForTests: () => []) { UpdateHttpForTests = new FakeHttp(answer) };
            await w.CheckForUpdates(manual: true);
            var r = (w.UpdateBanner.IsVisible, w.Status.Text ?? "", w.AvailableUpdate?.Version);
            w.Close();
            return r;
        });

    static HttpResponseMessage Tag(string tag) =>
        new(HttpStatusCode.OK) { Content = new StringContent($$"""{"tag_name":"{{tag}}","body":"Faster saves","html_url":"https://github.com/likeagfeld/Aether/releases/tag/{{tag}}","assets":[]}""") };

    [Fact]
    public async Task A_newer_release_shows_the_banner()
    {
        var (banner, _, ver) = await Check(_ => Tag("v9.9"));
        Assert.True(banner);
        Assert.Equal("9.9", ver);
    }

    [Fact]
    public async Task The_same_version_reads_up_to_date()
    {
        var (banner, status, _) = await Check(_ => Tag("v" + Credits.Version));
        Assert.False(banner);
        Assert.Contains("up to date", status);
    }

    [Fact]
    public async Task A_404_or_offline_reads_could_not_check_and_no_banner()
    {
        foreach (var answer in new Func<HttpRequestMessage, HttpResponseMessage>[] { _ => new HttpResponseMessage(HttpStatusCode.NotFound), _ => throw new HttpRequestException("offline") })
        {
            var (banner, status, _) = await Check(answer);
            Assert.False(banner);
            Assert.StartsWith("Could not check for updates", status);
        }
    }
}
