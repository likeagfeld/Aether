// Aether Card Manager, GPL-3.0-or-later.
// NOTICE.txt (packaging): full attributions for everything a package bundles -- the .NET libraries, the
// Noto Sans JP font, the cover sources the bundled cover pack draws on, and Pseudo Saturn Kai's cheat data -- so
// license obligations are met without CREDITS.md carrying paths, versions or licenses for things that are not
// "credited for source code or inspiration" (that table's own, narrower job). Generated into every package's root
// by manager/release/package.ps1; never committed itself.
namespace VibeMenuCardManager.Core.About;

public static class NoticeFile
{
    /// <summary>Every bundled dependency's name, checked by NoticeFileTests to be in Text() (name only -- this is
    /// not a license-text validator, just "did we forget to mention it").</summary>
    public static readonly string[] Required =
    [
        "Avalonia", "SkiaSharp", "HarfBuzzSharp", "System.IO.Hashing", ".NET runtime",
        "Noto Sans JP", "SIL Open Font License",
        "Satiator Rings Cover Project", "retrohead", "libretro-thumbnails", "libretro-database", "CC-BY-SA-4.0",
        "Pseudo Saturn Kai", "GPL-2.0", "Israel Jacquez",
        "satiator-menu", "MPL-2.0", "libsatiator", "font8x8",
    ];

    public static string Text() => """
        Aether Card Manager and the bundled Aether release -- third-party notices

        This package bundles the following, each under its own license (see LICENSES/ for the full texts). The Card
        Manager is GPL-3.0-or-later; the menu's own source is BSD-3-Clause (some files MPL-2.0, below).

        -- Libraries (Aether Card Manager) -----------------------------------------------------------------------
        Avalonia UI -- https://avaloniaui.net/ -- MIT License
        SkiaSharp -- https://github.com/mono/SkiaSharp -- MIT License (itself wrapping Skia, BSD-3-Clause)
        HarfBuzzSharp -- https://github.com/mono/SkiaSharp -- MIT License (itself wrapping HarfBuzz, MIT)
        System.IO.Hashing (.NET) -- https://github.com/dotnet/runtime -- MIT License
        The .NET runtime (self-contained in this package) -- https://github.com/dotnet/runtime -- MIT License

        -- The menu and LAUNCHER.BIN (release/autoboot.iso, release/LAUNCHER.BIN) ---------------------------------
        satiator-menu -- James Laird-Wah -- https://github.com/satiator/satiator-menu -- Mozilla Public License 2.0
          (MPL-2.0): the cue and disc-header parsing and the JHL boot (cdparse, cue2desc, jhboot); their source is in
          menu/launcher/ of the Aether repository
        libsatiator -- James Laird-Wah -- https://github.com/satiator/libsatiator -- public domain
        font8x8 -- Daniel Hepper -- https://github.com/dhepper/font8x8 -- public domain
        Pseudo Saturn Kai's cheat bootstrap and in-RAM cheat handler -- cafe-alpha and the Yabause team -- GPL-2.0,
          kept verbatim (menu/launcher/kai/); the bootstrap carries Israel Jacquez's MIT notice (Yabause), kept as is

        -- Font -----------------------------------------------------------------------------------------------------
        Noto Sans JP -- https://fonts.google.com/noto/specimen/Noto+Sans+JP -- Google -- SIL Open Font License 1.1

        -- Cover art (the bundled cover pack, COVERS.PAK) -------------------------------------------------------------
        Satiator Rings Cover Project -- Felinni Marinas and the Satiator Rings Cover Project contributors -- box art
          scans and renders assembled for the Satiator Rings project; used here with thanks, no separate license
          stated by the source -- treated as all rights reserved by the original photographers/publishers otherwise.
        retrohead's Satiator box scans (files-ds-scene.net/retrohead) -- box art scans; used here with thanks, no
          separate license stated by the source.
        libretro-thumbnails (Sega_-_Saturn) and the libretro-database project -- https://github.com/libretro -- cover
          thumbnails -- Creative Commons Attribution-ShareAlike 4.0 International (CC-BY-SA-4.0)

        The games' own cover art, titles and screenshots remain their original publishers' and developers' property;
        nothing here grants rights to a game itself, only to the cataloguing/scan work the sources above did.

        -- Cheat data -------------------------------------------------------------------------------------------------
        Pseudo Saturn Kai cheat database -- cafe-alpha and the Pseudo Saturn Kai project
          (https://ppcenter.webou.net/pskai/) -- GNU General Public License v2.0 (GPL-2.0)

        Full license texts are in LICENSES/ next to this file.
        """;
}
