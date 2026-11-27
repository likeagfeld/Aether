// Aether Card Manager, GPL-3.0-or-later.
// The cover the Card Manager shows for a game (contract/SPEC.md §5.6 cover lookup order, read side): the card's own
// ART.PAK (hero, else thumb) first -- it already holds the Cover Project art most games have -- then the game
// folder's BOX.TGA. Build-time fallbacks (the covers zip, retrohead, libretro) are the caller's job when offline
// sources are configured; this class only reads what is already on the card.
using VibeMenuCardManager.Core.Formats;

namespace VibeMenuCardManager.Core.Art;

public static class CoverLookup
{
    /// <summary>The (key, disc) entry, else the (key, 0) "applies to all discs" entry (SPEC §5.2); null when ART.PAK
    /// has no art for this game at all.</summary>
    static ArtKeyEntry? Entry(ArtPakData? pak, string key, int disc)
    {
        if (pak == null || key == "") return null;
        return pak.Entries.FirstOrDefault(x => x.Key == key && x.Disc == disc) ?? pak.Entries.FirstOrDefault(x => x.Key == key && x.Disc == 0);
    }

    /// <summary>A cheap existence check (flags only, no blob read) for 700-row loads: does ART.PAK hold a hero or
    /// thumb for this game at all.</summary>
    public static bool HasCover(ArtPakData? pak, string key, int disc) =>
        Entry(pak, key, disc) is { } e && (e.Flags & (ArtPak.FHero | ArtPak.FThumb)) != 0;

    /// <summary>The (key, disc) entry's hero, else its thumb, decoded back to RGBA; null when ART.PAK has no art for
    /// this game. Decodes the blob, so callers use it lazily (visible rows only), not over a whole card at once.</summary>
    public static RgbaImage? FromArtPak(ArtPakData? pak, string key, int disc)
    {
        if (Entry(pak, key, disc) is not { } e) return null;
        if (pak!.Blob(e, "hero") is byte[] hero) return CoverEncoder.DecodeRgb555(hero, e.HeroW, e.HeroH);
        if (pak.Blob(e, "thumb") is byte[] thumb) return CoverEncoder.DecodeRgb555(thumb, e.ThumbW, e.ThumbH);
        return null;
    }
}
