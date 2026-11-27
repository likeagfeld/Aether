// Aether Card Manager, GPL-3.0-or-later.
// Undo/redo of staged edits: a memento stack of the whole staged state (every
// GameRow and FolderRow's editable fields, and UserEdits), at least 20 levels. GameRow and FolderRow instances are
// not recreated -- only their field values are restored -- so the grid's selection and the tree's identity survive
// an undo/redo, not just the data.
using VibeMenuCardManager.Core.Art;
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Card;

public sealed class EditSnapshot
{
    public required List<GameRow> RowList;
    public required List<FolderRow> FolderList;
    public required Dictionary<GameRow, (string TitleOverride, bool Favorite, string Loader, RgbaImage? PendingCover, bool RestoreCoverStaged, bool Removed, bool IsNew, string MatchOverride)> Rows;
    public required Dictionary<FolderRow, (string Path, int Parent, bool IsNew, RgbaImage? PendingCover, bool RemoveArtStaged)> Folders;
    public required Dictionary<string, UserEditRecord> UserEdits;
    /// <summary>PC backup/restore: a staged restore is itself an edit, so Undo must revert it
    /// too -- without these two, undoing after Restore from backup would put every row/folder back but leave
    /// PendingRestoreFrom set and PendingRestoreRawFiles still queued, so Save changes would silently write the
    /// raw-file half of a restore the user just said to undo.</summary>
    public required string? PendingRestoreFrom;
    public required List<(string CardPath, byte[] Data)> PendingRestoreRawFiles;
}

public sealed class EditHistory
{
    public const int MaxLevels = 20;
    readonly List<EditSnapshot> _undo = [];
    readonly List<EditSnapshot> _redo = [];

    public int UndoCount => _undo.Count;
    public int RedoCount => _redo.Count;
    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;

    public static EditSnapshot Capture(CardLoad load) => new()
    {
        RowList = [.. load.Rows],
        FolderList = [.. load.Folders],
        Rows = load.Rows.ToDictionary(r => r, r => (r.TitleOverride, r.Favorite, r.Loader, r.PendingCover, r.RestoreCoverStaged, r.Removed, r.IsNew, r.MatchOverride)),
        Folders = load.Folders.ToDictionary(f => f, f => (f.Path, f.Parent, f.IsNew, f.PendingCover, f.RemoveArtStaged)),
        UserEdits = load.UserEdits.ToDictionary(kv => kv.Key, kv => kv.Value.Clone()),
        PendingRestoreFrom = load.PendingRestoreFrom,
        PendingRestoreRawFiles = [.. load.PendingRestoreRawFiles],
    };

    public static void Restore(CardLoad load, EditSnapshot s)
    {
        load.Rows.Clear(); load.Rows.AddRange(s.RowList);
        load.Folders.Clear(); load.Folders.AddRange(s.FolderList);
        foreach (var (row, v) in s.Rows)
            (row.TitleOverride, row.Favorite, row.Loader, row.PendingCover, row.RestoreCoverStaged, row.Removed, row.IsNew, row.MatchOverride) = v;
        foreach (var (folder, v) in s.Folders)
            (folder.Path, folder.Parent, folder.IsNew, folder.PendingCover, folder.RemoveArtStaged) = v;
        load.UserEdits = s.UserEdits.ToDictionary(kv => kv.Key, kv => kv.Value.Clone());
        load.PendingRestoreFrom = s.PendingRestoreFrom;
        load.PendingRestoreRawFiles = [.. s.PendingRestoreRawFiles];
    }

    /// <summary>Call before making a change: pushes the current state onto the undo stack (capped at MaxLevels,
    /// oldest dropped) and clears the redo stack (a fresh branch).</summary>
    public void Push(CardLoad load)
    {
        _undo.Add(Capture(load));
        if (_undo.Count > MaxLevels) _undo.RemoveAt(0);
        _redo.Clear();
    }

    public bool Undo(CardLoad load)
    {
        if (_undo.Count == 0) return false;
        var cur = Capture(load);
        var prev = _undo[^1];
        _undo.RemoveAt(_undo.Count - 1);
        _redo.Add(cur);
        if (_redo.Count > MaxLevels) _redo.RemoveAt(0);
        Restore(load, prev);
        return true;
    }

    public bool Redo(CardLoad load)
    {
        if (_redo.Count == 0) return false;
        var cur = Capture(load);
        var next = _redo[^1];
        _redo.RemoveAt(_redo.Count - 1);
        _undo.Add(cur);
        if (_undo.Count > MaxLevels) _undo.RemoveAt(0);
        Restore(load, next);
        return true;
    }

    public void Clear() { _undo.Clear(); _redo.Clear(); }
}
