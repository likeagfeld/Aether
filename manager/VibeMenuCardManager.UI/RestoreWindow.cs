// Aether Card Manager, GPL-3.0-or-later.
// File > "Restore from backup…" (PC backup/restore): pick a PC snapshot, see what differs from
// the card as it stands, stage the restore. Staging only mutates the already-open CardLoad in memory
// (Core.Backup.CardRestore.Stage) -- nothing reaches the card until the user runs Save changes, through the same
// CardWriteGuard confirmation every other edit goes through.
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using VibeMenuCardManager.Core.Backup;
using VibeMenuCardManager.Core.Card;

namespace VibeMenuCardManager.UI;

public sealed class RestoreWindow : Window
{
    readonly MainWindow _owner;
    readonly CardLoad _load;
    readonly string _root;
    public readonly ListBox Snapshots = new();
    public readonly ListBox Diff = new();
    public readonly Button StageButton = new() { Content = "Stage restore", IsEnabled = false };
    public readonly TextBlock Hint = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };

    /// <summary>The diff shown for whichever snapshot is selected right now (tests read this instead of walking
    /// ListBoxItem visuals, same convention as ProblemsWindow.Shown).</summary>
    public List<RestoreDiffEntry> Shown { get; private set; } = [];

    public RestoreWindow(MainWindow owner, CardLoad load, List<SnapshotInfo> snapshots)
    {
        _owner = owner; _load = load; _root = load.Root;
        Title = "Restore from backup";
        Width = 760; Height = 520;
        AppTheme.ApplyToWindow(this);
        AppTheme.ApplyToList(Snapshots);
        AppTheme.ApplyToList(Diff);

        Snapshots.ItemTemplate = new FuncDataTemplate<SnapshotInfo>((s, _) => new TextBlock
        {
            Text = $"{s.Time:yyyy-MM-dd HH:mm} -- {s.Manifest.Files.Count} files, {Kb(s.Manifest.Files.Sum(f => f.Size))}",
            Margin = new Thickness(4, 2),
        }, supportsRecycling: true);
        Snapshots.ItemsSource = snapshots;

        Diff.ItemTemplate = new FuncDataTemplate<RestoreDiffEntry>((e, _) =>
        {
            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,110,*"), Margin = new Thickness(4, 2) };
            var path = new TextBlock { Text = e.CardPath, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center };
            var action = new TextBlock
            {
                Text = e.Action switch { RestoreAction.WillRestore => "Will restore", RestoreAction.Skipped => "Skipped", _ => "Unchanged" },
                [!TextBlock.ForegroundProperty] = AppTheme.Dyn(e.Action == RestoreAction.WillRestore ? "Acc2" : e.Action == RestoreAction.Skipped ? "Warn" : "Muted"),
                VerticalAlignment = VerticalAlignment.Center,
            };
            var reason = new TextBlock { Text = e.Reason, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis, [!TextBlock.ForegroundProperty] = AppTheme.Dyn("Muted"), VerticalAlignment = VerticalAlignment.Center };
            ToolTip.SetTip(path, e.CardPath); ToolTip.SetTip(reason, e.Reason);
            Grid.SetColumn(path, 0); Grid.SetColumn(action, 1); Grid.SetColumn(reason, 2);
            grid.Children.Add(path); grid.Children.Add(action); grid.Children.Add(reason);
            return grid;
        }, supportsRecycling: true);

        Snapshots.SelectionChanged += (_, _) => RefreshDiff();
        StageButton.Click += (_, _) => Stage();

        Hint.Text = "Pick a snapshot to see what it would change. Nothing is written to the card until Save changes.";
        var left = new DockPanel { Children = { Snapshots } };
        var right = new DockPanel
        {
            Children =
            {
                new StackPanel { Children = { Hint, StageButton }, Spacing = 8, Margin = new Thickness(8) }.Also(s => DockPanel.SetDock(s, Dock.Bottom)),
                Diff,
            },
        };
        var split = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*") };
        Grid.SetColumn(left, 0); Grid.SetColumn(right, 1);
        split.Children.Add(left); split.Children.Add(right);
        Content = split;

        if (snapshots.Count > 0) Snapshots.SelectedIndex = 0;
    }

    static string Kb(long bytes) => bytes < 1024 * 1024 ? $"{bytes / 1024.0:F0} KB" : $"{bytes / 1024.0 / 1024.0:F1} MB";

    void RefreshDiff()
    {
        if (Snapshots.SelectedItem is not SnapshotInfo snap) { Shown = []; Diff.ItemsSource = Shown; StageButton.IsEnabled = false; return; }
        Shown = CardRestore.Plan(_load, _root, snap);
        Diff.ItemsSource = Shown;
        StageButton.IsEnabled = Shown.Any(e => e.Action == RestoreAction.WillRestore);
    }

    /// <summary>Tests: runs the same staging Clicking StageButton would, without needing a routed-event/visual-tree
    /// dance just to fire a Button's Click handler (TreeAndProblemsTests' FixProblem follows the same convention).</summary>
    public void StageForTests() => Stage();

    void Stage()
    {
        if (Snapshots.SelectedItem is not SnapshotInfo snap) return;
        _owner.StageForUndo();   // so Ctrl+Z reverts the restore like any other edit, not a lingering half-state
        var result = CardRestore.Stage(_load, _root, snap);
        int applied = result.Count(e => e.Action == RestoreAction.WillRestore);
        _owner.RefreshAfterRestore();
        _owner.Status.Text = $"Restore staged from {snap.Name}: {applied} file(s). Nothing was written -- run Save changes to apply it.";
        Close();
    }
}
