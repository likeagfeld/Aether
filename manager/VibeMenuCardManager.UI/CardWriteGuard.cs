// Aether Card Manager, GPL-3.0-or-later.
// The one gate before any write to a real card, shared by every action that writes one (the save
// import now, the installer next): the target is resolved to its real volume (CardTarget / VolumeResolver), a system
// volume or an unresolvable target is refused outright, a fixed disk needs the user's explicit override for that
// resolved volume, and anything else is named -- drive, volume label, the Aether version on it, and what will be
// written -- for the user to confirm or cancel.
using Avalonia.Controls;
using Avalonia.Layout;
using VibeMenuCardManager.Core.Card;

namespace VibeMenuCardManager.UI;

public static class CardWriteGuard
{
    /// <summary>The confirmation step itself; tests replace it (no dialog on the headless platform).</summary>
    public delegate Task<bool> Ask(Window owner, string title, string text);

    public static Ask Dialog = ShowDialog;

    /// <summary>null when the user agreed; otherwise why nothing will be written.</summary>
    public static async Task<string?> Check(Window owner, CardTarget t, string action, IEnumerable<string> writes, Ask? ask = null)
    {
        if (t.Refusal != null) return t.Refusal;
        if (t.NeedsFixedOverride)
            return $"{t.Root} is on a fixed (non-removable) disk, {t.Volume.FinalPath} ({t.Volume.Id}). If this really is your SD card " +
                   "(some USB card readers show as fixed disks), tick \"This is not my system drive\" for this volume and try again.";
        bool ok = await (ask ?? Dialog)(owner, action, t.Describe(action, writes));
        return ok ? null : $"{action}: cancelled, nothing was written";
    }

    static async Task<bool> ShowDialog(Window owner, string title, string text)
    {
        bool result = false;
        var w = new Window { Title = title, Width = 560, SizeToContent = SizeToContent.Height, CanResize = false };
        var yes = new Button { Content = "Write to this card" };
        var no = new Button { Content = "Cancel", IsDefault = true };
        yes.Click += (_, _) => { result = true; w.Close(); };
        no.Click += (_, _) => w.Close();
        w.Content = new StackPanel
        {
            Margin = new Avalonia.Thickness(16),
            Spacing = 12,
            Children =
            {
                new TextBlock { Text = text, TextWrapping = Avalonia.Media.TextWrapping.Wrap },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { no, yes } },
            },
        };
        await w.ShowDialog(owner);
        return result;
    }
}
