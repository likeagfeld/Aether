// VibeMenu Card Manager, GPL-3.0-or-later.
// Mutation demos for the installer golden: a doctored expect must make the real comparison go RED.
using VibeMenuCardManager.Core.Install;
using VibeMenuCardManager.Tests.Install;

namespace VibeMenuCardManager.Tests.Mutation;

public class InstallMutationTests
{
    static List<string> RunAgainst(string c, Func<List<TempCard.Row>, List<TempCard.Row>> doctor)
    {
        var x = InstallCheck.Expect(c);
        var (t, src, target) = InstallCheck.Realize(c);
        using (t)
        {
            for (int i = 0; i < InstallCheck.Spec(c).GetProperty("runs").GetInt32(); i++) Installer.Install(target, src, x.GetProperty("stamp").GetString());
            return InstallCheck.Diff(InstallCheck.CardTree(t), doctor(InstallCheck.Rows(x)));
        }
    }

    [Fact]
    public void Unmutated_is_green() => Assert.Empty(RunAgainst("replace", r => r));

    [Fact]
    public void A_backup_the_python_install_did_not_make_is_caught() =>
        Assert.Contains("extra autoboot.iso.bak-20261122-120000.1", RunAgainst("replace", r => r.Where(x => x.Path != "autoboot.iso.bak-20261122-120000.1").ToList()));

    [Fact]
    public void A_changed_menu_bin_hash_is_caught() =>
        Assert.Contains("differs menu.bin", RunAgainst("replace", r => r.Select(x => x.Path == "menu.bin" ? x with { Sha256 = new string('0', 64) } : x).ToList()));
}
