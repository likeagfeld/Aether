#!/bin/sh
# Usage: tools/sbuild.sh menu/spikes/<name> [--tracks menu/spikes/<name>/tracks] | --app | --app-fake | --launcher
# OM_BUILD_ID ("<git short SHA>[-dirty] <commit date>") comes from the host's git -- the build image has none --
# and reaches the menu's diagnostics page through tools/sbuild.py (-DOM_BUILD_ID, menu/src/version.h). A tree with
# uncommitted changes to tracked files is marked -dirty: such a build does not identify its source.
OM_BUILD_ID="$(git log -1 --format='%h %cd' --date=format:%Y-%m-%d 2>/dev/null || echo 'nogit')"
if [ -n "$(git status --porcelain -uno 2>/dev/null)" ]; then
    OM_BUILD_ID="$(echo "$OM_BUILD_ID" | sed 's/ /-dirty /')"
fi
export OM_BUILD_ID
MSYS_NO_PATHCONV=1 exec docker run --rm -e OM_BUILD_ID -v "$(pwd -W 2>/dev/null || pwd):/work" -w /work omsat-build python3 tools/sbuild.py "$@"
