// Aether Card Manager, GPL-3.0-or-later.
// The menu's folder on the SD card (the Aether rename, 2026-11-25): every menu file lives under /aether/ (menu/src/carddir.h
// OM_CARD_DIR). A card set up before the rename has /vibemenu/ instead; CardFolderMigration moves it.
namespace VibeMenuCardManager.Core.Card;

public static class CardPaths
{
    /// <summary>The menu's folder name at the card's root.</summary>
    public const string Folder = "aether";
    /// <summary>The same as a card path ("/aether"), no trailing '/'.</summary>
    public const string Root = "/" + Folder;
    /// <summary>The folder's name before the Aether rename: hidden from the game list, detected, moved, never written.</summary>
    public const string OldFolder = "vibemenu";

    /// <summary>The menu image's System ID title, now ("AETHER", sbuild.py) or before the rename ("VIBEMENU"): an
    /// in-place update must recognise both, or it would treat an old card as a foreign menu (no pre-update backup).</summary>
    public static bool IsMenuTitle(string? title) =>
        title != null && (title.StartsWith("AETHER", StringComparison.Ordinal) || title.StartsWith("VIBEMENU", StringComparison.Ordinal));

    /// <summary>The menu's build string's product word, now or before the rename (menu/src/version.h OM_BUILD_STRING).</summary>
    public const string BuildWord = "(?:Aether|VibeMenu)";

    /// <summary>A root entry that is the menu's own folder, now or before the rename (ASCII case-insensitive: FAT).</summary>
    public static bool IsMenuFolderName(string? name) =>
        string.Equals(name, Folder, StringComparison.OrdinalIgnoreCase) || string.Equals(name, OldFolder, StringComparison.OrdinalIgnoreCase);
}
