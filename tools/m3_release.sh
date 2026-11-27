#!/bin/sh
# Usage (repo root): sh tools/m3_release.sh [--card E:] [--source DIR]
#
# M3 release. Idempotent: run it again and it rebuilds the same artifacts, reuses the cached
# downloads (official menu, cover fallbacks) and the music file when its source has not changed.
#   1. Gates: pytest (incl. the version pin), hwf, fake card, contract fixtures --check, host tests, all three builds,
#      the --app release hygiene check (omsat.relcheck).
#   2. Release artifacts in build/release/: autoboot.iso, LAUNCHER.BIN, LOADBG.BIN, BGM.MUS, THEMES/SAMPLE, the card
#      data (LIBRARY.IDX, GAMES.DB, ART.PAK, manifest.csv, report.json), the official menu.bin, SHA256SUMS.txt.
#   3. A staged install into build/release/stage (a copy target with a placeholder menu.bin).
#   4. Only with --card: the user's SD card. Nothing on any card is written without --card.
#      The card data is then built by scanning the card itself (read only); without --card the scan source is
#      --source (default "G:/Saturn Games", read only). The tree must be clean (the build string names its commit).
set -e
CARD=""
SRC="G:/Saturn Games"
while [ $# -gt 0 ]; do
    case "$1" in
        --card) CARD="$2"; shift 2 ;;
        --source) SRC="$2"; shift 2 ;;
        *) echo "usage: sh tools/m3_release.sh [--card E:] [--source DIR]" >&2; exit 2 ;;
    esac
done
REL=build/release
VERSION="$(sed -n 's/^#define VIBEMENU_VERSION "\(.*\)"$/\1/p' menu/src/version.h)"
WAV=build/music/vmii_master_22k_s16.wav
ZIP="W:/SEGA SATURN RINGS Cover Project-20261117T130330Z-1-001.zip"
# The official Satiator menu (satiator.net -> info.satiator.net/menu/): v71, the latest, released after FW161
# (menu v68+ supports FW161's multi-disc and modchip modes, menu/CHANGELOG). latest.zip was byte-identical to this file on
# 2026-11-19. No hash is published upstream; these two are the ones measured that day (both URLs agreed, zip CRC OK).
MENU_URL="https://info.satiator.net/menu/satiator_menu000071.zip"
MENU_ZIP_SHA=2f94875f6dd87c6774fb83cbb80fb84d24a26839f28e2f21c5ad54c91bc3710b
MENU_BIN_SHA=860053b144984a008aa272847c4c2dcb14d140a72b6cb5957689e164cd9607e9

if [ -n "$CARD" ]; then
    if [ -n "$(git status --porcelain -uno)" ]; then
        echo "m3_release: refusing --card on a tree with uncommitted changes (the build string must name its commit)" >&2
        exit 1
    fi
    LABEL="$(cmd //c vol "$CARD" 2>/dev/null | sed -n 's/.*Volume in drive .* is \(.*\)\r*$/\1/p' | tr -d '\r')"
    if [ "$LABEL" != "VIBEMENU" ] && [ "$LABEL" != "AETHER" ]; then    # the Aether rename: either test-card label
        echo "m3_release: $CARD is labeled '$LABEL', not VIBEMENU or AETHER -- refusing to write" >&2
        exit 1
    fi
    for d in Games XBAND; do
        [ -d "$CARD/$d" ] || { echo "m3_release: $CARD/$d missing -- is this the prepared card?" >&2; exit 1; }
    done
    SRC="$CARD/"
fi

echo "== Aether $VERSION release ($(git log -1 --format='%h %cd' --date=format:%Y-%m-%d))"
# 1. gates
( cd tools && python -m pytest -q && python -m omsat.hwf && python -m omsat.fakecard )
( cd contract/fixtures && python make_fixtures.py --check )
sh tools/hosttest.sh
sh tools/sbuild.sh --launcher
sh tools/sbuild.sh --app
if [ -n "$CARD" ]; then ( cd tools && python -m omsat.relcheck ); else ( cd tools && python -m omsat.relcheck --allow-dirty ); fi
mkdir -p "$REL"
cp menu/app/out/autoboot.iso menu/app/out/menu.map "$REL/"     # before --app-fake: the release image, kept
sh tools/sbuild.sh --app-fake

# 2. release artifacts
cp menu/launcher/out/LAUNCHER.BIN menu/app/assets/LOADBG.BIN menu/app/assets/VRTILES.BIN "$REL/"   # VRTILES: M6 C3
if [ ! -f "$REL/BGM.MUS" ] || [ "$WAV" -nt "$REL/BGM.MUS" ]; then
    ( cd tools && python -m omsat.sound music "../$WAV" --out "../$REL/BGM.MUS" --trim-start 300 --start 0 )   # the mix from 5:00
fi
( cd tools && python -m omsat.sound info "../$REL/BGM.MUS" )
( cd tools && python -m omsat.theme --demo --out "../$REL/THEMES/SAMPLE" --name SAMPLE --panel-alpha 60 \
    && python -m omsat.check "../$REL/THEMES/SAMPLE/BG.BIN" "../$REL/BGM.MUS" )
( cd tools && python -c "
from omsat.artsrc import folder_card
from omsat.cardbuild import _card_font
for n in ('Games', 'XBAND'):
    folder_card(n.upper(), _card_font()).save('../$REL/folder_' + n.lower() + '.png')
" )
if [ ! -f "$REL/menu.bin" ]; then
    mkdir -p "$REL/dl"
    [ -f "$REL/dl/menu.zip" ] || curl -sfL -m 120 -o "$REL/dl/menu.zip" "$MENU_URL"
    echo "$MENU_ZIP_SHA *$REL/dl/menu.zip" | sha256sum -c -
    python -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; open(sys.argv[2],'wb').write(z.read('menu.bin'))" \
        "$REL/dl/menu.zip" "$REL/menu.bin"
fi
echo "$MENU_BIN_SHA *$REL/menu.bin" | sha256sum -c -
rm -rf "$REL/card"
( cd tools && python -m omsat.cardbuild "$SRC" --gamesdb ../build/db/GAMES.DB --jp-titles ../build/db/jp_titles.json \
    --covers-zip "$ZIP" --art-cache "../$REL/art_cache" --title-cards \
    --folder-art "Games=../$REL/folder_games.png" --folder-art "XBAND=../$REL/folder_xband.png" \
    --art-prefer "T-12518H=libretro" \
    --out "../$REL/card" > "../$REL/card_report.json" )
# T-12518H (VR Golf '97): libretro-thumbnails' 512x862 cover over retrohead's 64x100 one, above.
python -c "
import json; r = json.load(open('$REL/card_report.json', encoding='utf-8'))
print('cardbuild:', r['scan']['entries'], 'images,', r['scan']['matched'], 'matched; covers', r['covers'], '; jp', r['jp']['rendered'], '/', r['jp']['denominator'], '; folder art', r['folder_art'])
"
( cd "$REL" && sha256sum autoboot.iso LAUNCHER.BIN LOADBG.BIN VRTILES.BIN BGM.MUS menu.bin THEMES/SAMPLE/THEME.INI THEMES/SAMPLE/BG.BIN \
    card/LIBRARY.IDX card/GAMES.DB card/ART.PAK > SHA256SUMS.txt && cat SHA256SUMS.txt )

# 3. staged install (never the card)
rm -rf "$REL/stage" && mkdir -p "$REL/stage"
printf 'placeholder official menu for staging only\n' > "$REL/stage/menu.bin"
( cd tools && python -m omsat.install --target "../$REL/stage" --iso "../$REL/autoboot.iso" --launcher "../$REL/LAUNCHER.BIN" \
    --data "../$REL/card" --loadbg "../$REL/LOADBG.BIN" --vrtiles "../$REL/VRTILES.BIN" --music "../$REL/BGM.MUS" --theme "SAMPLE=../$REL/THEMES/SAMPLE" )

# 4. the card
[ -n "$CARD" ] || { echo "m3_release: done (no --card: no card touched)"; exit 0; }
( cd tools && python -m omsat.cardput --card "$CARD" --release "../$REL" --version "$VERSION" \
    --menu-url "$MENU_URL" --menu-sha "$MENU_BIN_SHA" --manifest "../$REL/card_manifest.txt" )
