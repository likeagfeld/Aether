# Aether Card Manager, GPL-3.0-or-later.
#Requires -Version 7.4
# Self-contained single-folder releases for win-x64, win-arm64, osx-arm64, osx-x64, linux-x64 and
# linux-arm64, packaged as a Windows .zip, a macOS .zip of the .app bundle (unsigned, an .icns made from the Aether
# icon) and a Linux .tar.gz (plus an .AppImage on a Linux host with appimagetool). The .tar.gz and the macOS .zip are
# written by unixpack.py with real Unix modes (0755 on the binaries), which Windows' own tar and Compress-Archive drop.
# 0.1 is packaged locally (the menu build needs the local SGL, the 991 MB BGM no CI cache holds) and uploaded by hand.
# Run from anywhere; always resolves paths from the repo root.
#
# Usage (PowerShell, repo root or anywhere):
#   pwsh manager/release/package.ps1 [-Rids win-x64,linux-x64] [-SkipMenuBuild] [-OutDir build/dist] [-Final] [-Build yyyyMMdd.HHmm]
#
# -Final marks a normal (non-pre-release) release; without it the build is a pre-release. Either way every package's
# Card Manager carries this run's build identity (version, UTC build time, commit, pre-release or not) and the run
# writes version.json with the same identity: upload it with the packages (it is in SHA256SUMS.txt), so a Card
# Manager can tell a newer build of the same version from its own (Core/Update/AppUpdate.cs). -Build reuses a build
# time (a release cut across several runs, e.g. release.yml's jobs); it defaults to now (UTC).
#
# -SkipMenuBuild skips the Saturn cross-build (Docker's omsat-build image) and packages whatever is already in
# build/dist_release/ (or produces a release folder with no autoboot.iso/LAUNCHER.BIN, which CardPipeline.CheckRelease refuses --
# useful only for iterating on the .NET publish/packaging steps themselves, never for a real release).
#
# Every package bundles: the release folder (autoboot.iso, LAUNCHER.BIN, VRTILES.BIN, LOADBG.BIN, BGM.MUS, THEMES,
# the compat tables, CHEATS.PAK, GAMES.DB, jp_titles.json -- CardPipeline.ReleaseRequired/BuildOptionsFor), the cover pack
# (covers/COVERS.PAK, Core/Art/CoverPack.cs DefaultPath) and the Noto Sans JP font (fonts/NotoSansJP-Regular.otf,
# Core/Art/TitleRenderer.cs DefaultFontPath) -- all next to the exe, so a packaged Prepare SD Card finds them with no
# setup (MainWindow's constructor auto-fills ReleasePath from "release" next to the exe).
#
# Nothing here is committed: build/ is gitignored, and this writes only under build/.

param(
    [string[]]$Rids = @("win-x64", "win-arm64", "osx-arm64", "osx-x64", "linux-x64", "linux-arm64"),
    [string]$OutDir = "build/dist",
    [switch]$SkipMenuBuild,
    # CI (.github/workflows/release.yml): a release folder another job already built (Docker/Saturn cross-build is
    # only straightforward on an ubuntu runner) -- used as-is, skipping the whole release-folder assembly section.
    [string]$ReleaseDirOverride,
    [switch]$Final,
    [ValidatePattern('^\d{8}\.\d{4}$')]
    [string]$Build = [DateTime]::UtcNow.ToString("yyyyMMdd.HHmm")
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
Set-Location $Root

function Section($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Warn($msg) { Write-Warning $msg }

$Sha = (git rev-parse --short=7 HEAD).Trim()
$Dirty = [bool](git status --porcelain -uno)
if ($Dirty) { Warn "working tree has uncommitted changes to tracked files; packaging HEAD (git archive) regardless -- the dirty files are not in the package" }
$Version = "0.1"   # Aether Card Manager's own version (Credits.Version); never bumped except on the user's say-so
$Prerelease = -not $Final
# the build identity every publish below embeds (Directory.Build.props) and version.json repeats
$Identity = @("-p:AetherBuild=$Build", "-p:AetherCommit=$Sha", "-p:AetherPrerelease=$($Prerelease.ToString().ToLowerInvariant())")

$ExportDir = Join-Path $Root "build/dist_src"
$ReleaseDir = Join-Path $Root "build/dist_release"
$PubRoot = Join-Path $Root "build/dist_pub"
$PkgRoot = Join-Path $Root "build/dist_pkg"
$DistDir = Join-Path $Root $OutDir

# =========================================================================================================
# 1. A clean export of HEAD (never the working tree: "the release refuses a stale menu" -- nothing
#    uncommitted, nothing left over from a previous build, leaks into the packaged menu).
# =========================================================================================================
Section "Clean export of HEAD ($Sha)"
if (Test-Path $ExportDir) { Remove-Item -Recurse -Force $ExportDir }
New-Item -ItemType Directory -Force -Path $ExportDir | Out-Null
git archive HEAD | tar -x -C $ExportDir
Write-Host "Exported to $ExportDir"

# =========================================================================================================
# 2. The release folder: the Saturn menu (built from the clean export, Docker's omsat-build image), the
#    static assets, the compat tables, GAMES.DB/jp_titles.json, music and a sample theme.
# =========================================================================================================
if ($ReleaseDirOverride) {
    Section "Release folder (override: $ReleaseDirOverride)"
    $ReleaseDir = (Resolve-Path $ReleaseDirOverride).Path
} else {
Section "Release folder"
if (Test-Path $ReleaseDir) { Remove-Item -Recurse -Force $ReleaseDir }
New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null

if (-not $SkipMenuBuild) {
    $haveDocker = $null -ne (Get-Command docker -ErrorAction SilentlyContinue)
    if (-not $haveDocker) {
        Warn "docker not found: cannot cross-build the Saturn menu here. Re-run with -SkipMenuBuild to package without" +
             " it (Prepare SD Card will refuse the bundled release until autoboot.iso/LAUNCHER.BIN are added), or build on" +
             " a machine with Docker and the omsat-build image (see .github/workflows/release.yml for a from-scratch build)."
        throw "docker is required to build the menu (or pass -SkipMenuBuild)"
    }
    # The SGL headers (build/sgl, ~1.3 MB): a toolchain input, gitignored (not in `git archive`), not rebuilt by
    # sbuild.py itself -- copied in so the clean export has everything sbuild.py's ROOT-relative paths expect.
    $sglSrc = Join-Path $Root "build/sgl"
    if (-not (Test-Path $sglSrc)) { throw "build/sgl (the SGL headers) is missing; cannot cross-build the menu" }
    New-Item -ItemType Directory -Force -Path (Join-Path $ExportDir "build") | Out-Null
    Copy-Item $sglSrc (Join-Path $ExportDir "build/sgl") -Recurse -Force

    Push-Location $ExportDir
    try {
        $env:OM_BUILD_ID = $Sha
        & docker run --rm -e OM_BUILD_ID -v "${ExportDir}:/work" -w /work omsat-build python3 tools/sbuild.py --launcher
        if ($LASTEXITCODE -ne 0) { throw "sbuild.py --launcher failed (exit $LASTEXITCODE)" }
        & docker run --rm -e OM_BUILD_ID -v "${ExportDir}:/work" -w /work omsat-build python3 tools/sbuild.py --app
        if ($LASTEXITCODE -ne 0) { throw "sbuild.py --app failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
    Copy-Item (Join-Path $ExportDir "menu/app/out/autoboot.iso") (Join-Path $ReleaseDir "autoboot.iso")
    Copy-Item (Join-Path $ExportDir "menu/launcher/out/LAUNCHER.BIN") (Join-Path $ReleaseDir "LAUNCHER.BIN")
    Write-Host "Built autoboot.iso and LAUNCHER.BIN from the clean export"
} else {
    Warn "-SkipMenuBuild: no autoboot.iso/LAUNCHER.BIN -- this is not a releasable package"
}

Copy-Item (Join-Path $ExportDir "menu/app/assets/VRTILES.BIN") (Join-Path $ReleaseDir "VRTILES.BIN")
Copy-Item (Join-Path $ExportDir "menu/app/assets/LOADBG.BIN") (Join-Path $ReleaseDir "LOADBG.BIN")

New-Item -ItemType Directory -Force -Path (Join-Path $ReleaseDir "compat") | Out-Null
foreach ($f in @("loader_compat.json", "controller_compat.json", "save_tools.json")) {
    Copy-Item (Join-Path $ExportDir "tools/data/compat/$f") (Join-Path $ReleaseDir "compat/$f")
}
# The shipped cheats (SPEC 5.15): CardPipeline.Run installs /aether/CHEATS.PAK only from the release folder's own.
Copy-Item (Join-Path $ExportDir "tools/data/cheats/CHEATS.PAK") (Join-Path $ReleaseDir "CHEATS.PAK")

$PythonExe = Get-Command python, python3, py -ErrorAction SilentlyContinue | Select-Object -First 1
function PyRun([string[]]$ArgList) {
    if ($PythonExe.Name -eq "py.exe" -or $PythonExe.Name -eq "py") { & $PythonExe.Source @ArgList } else { & $PythonExe.Source @ArgList }
}

$GamesDb = Join-Path $Root "build/db/GAMES.DB"
$JpTitles = Join-Path $Root "build/db/jp_titles.json"
if (-not (Test-Path $GamesDb) -or -not (Test-Path $JpTitles)) {
    throw "build/db/GAMES.DB and build/db/jp_titles.json are required (tools/omsat's own GAMES.DB build); not present here"
}

# GAMES.DB must be at least as current as the tools that produce it, or packaging would ship a
# database built against an older version of the builder. GAMES.DB carries only a build_date (YYYYMMDD), not a
# commit, so this compares that date against the newest commit touching the actual producer chain in this checkout
# -- the same kind of staleness check ReleaseAssembler.Staleness already does for the menu image, which this
# script's own direct build/db/GAMES.DB copy otherwise bypasses entirely. build/db/GAMES.DB is the Rings Cover
# Project xlsx imported by tools/omsat/coverproject.py ("python -m omsat.coverproject --xlsx ... --out
# build/db/GAMES.DB"); tools/omsat/cardbuild.py's own --gamesdb only READS it as a seed for a per-card subset
# elsewhere, so it is not part of this pathspec. Deliberately narrow (coverproject.py itself, plus gamesdb.py's
# write_gamesdb/GameRecord format it writes through, and keys.py's code-cell parsing), not all of tools/omsat or
# tools/data -- that wider pathspec flags every unrelated tools/ commit forever (e.g. a change to hwf.py, a
# test-only gate) and the check stops meaning anything.
$GamesDbTools = "tools/omsat/coverproject.py", "tools/omsat/gamesdb.py", "tools/omsat/keys.py"
$toolsNewestDate = (git -C $Root log -1 --format=%cs -- $GamesDbTools).Trim()
if (-not $toolsNewestDate) { throw "could not determine the newest commit touching $GamesDbTools" }
$toolsNewestInt = [int]($toolsNewestDate -replace '-', '')
Push-Location (Join-Path $ExportDir "tools")
try {
    $gdbBuildDateOut = & $PythonExe.Source -c "from omsat import binfmt as bf; print(bf.validate_common(open(r'$GamesDb', 'rb').read(), bf.MAGIC_GDB).build_date)"
} finally { Pop-Location }
$gdbBuildDate = [int]($gdbBuildDateOut.Trim())
if ($gdbBuildDate -lt $toolsNewestInt) {
    $today = Get-Date -Format "yyyyMMdd"
    throw "build/db/GAMES.DB is stale: built $gdbBuildDate, but its own builder (coverproject.py/gamesdb.py/keys.py) has a commit from $toolsNewestDate. Rebuild it (python -m omsat.coverproject --xlsx <the Rings sheet> --out build/db/GAMES.DB --report build/db/report.json --jp-titles build/db/jp_titles.json --date $today) before packaging -- coverproject.py's own --date default is a fixed 20261117, so a rebuild with no --date just reproduces this failure."
}

Copy-Item $GamesDb (Join-Path $ReleaseDir "GAMES.DB")
Copy-Item $JpTitles (Join-Path $ReleaseDir "jp_titles.json")

$Bgm300 = Join-Path $Root "build/music/BGM300.MUS"
$MusicSrc = Join-Path $Root "build/music/vmii_master_22k_s16.wav"
$BgmOut = Join-Path $ReleaseDir "BGM.MUS"
if (Test-Path $Bgm300) {
    # The original WAV master is gone. BGM300.MUS is the ready-made, already-trimmed replacement (the DJ Mute
    # City mix with its first 300 s cut, start position 0) -- used as-is, never rebuilt from the WAV master,
    # when present. Still validated with `omsat.sound info` (OMSATMUS magic/CRC/format checks, via check_mus --
    # info itself exits 0 even when check_mus finds problems, so the printed "[]" has to be checked, not just the
    # exit code) and the start position re-confirmed, so a bad or mis-swapped file doesn't silently ship.
    Push-Location (Join-Path $ExportDir "tools")
    try { $infoOut = (PyRun @("-m", "omsat.sound", "info", $Bgm300)) -join "`n" }
    finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "omsat.sound info failed on $Bgm300 (exit $LASTEXITCODE)" }
    Write-Host "omsat.sound info: $infoOut"
    if ($infoOut -notmatch "\[\]\s*$") { throw "$Bgm300 failed omsat.sound info's own OMSATMUS checks: $infoOut" }
    if ($infoOut -notmatch "'start_frame':\s*0\b") { throw "$Bgm300 start position is not 0: $infoOut" }
    Copy-Item $Bgm300 $BgmOut -Force
    Write-Host "Used BGM.MUS as-is from $Bgm300"
} elseif (Test-Path $MusicSrc) {
    Push-Location (Join-Path $ExportDir "tools")
    try { PyRun @("-m", "omsat.sound", "music", $MusicSrc, "--out", $BgmOut, "--trim-start", "300", "--start", "0") }
    finally { Pop-Location }
    # PyRun's own exit code was never checked, so a failed build/music call (reproduced with a
    # degenerate/near-silent test WAV: omsat.sound raised a math domain error on the loudness analysis) still let this
    # script print "Built BGM.MUS" and package a zip with no BGM.MUS in it -- exactly what the throw above exists to
    # prevent. Check both the exit code and that BGM.MUS actually exists afterward.
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $BgmOut)) { throw "BGM.MUS was not produced from $MusicSrc (omsat.sound exited $LASTEXITCODE)" }
    Write-Host "Built BGM.MUS (trimmed from 5:00)"
} else {
    throw "neither build/music/BGM300.MUS nor build/music/vmii_master_22k_s16.wav is present; BGM.MUS cannot be produced"
}

try {
    Push-Location (Join-Path $ExportDir "tools")
    PyRun @("-m", "omsat.theme", "--demo", "--out", (Join-Path $ReleaseDir "THEMES/SAMPLE"), "--name", "SAMPLE", "--panel-alpha", "60")
    Pop-Location
    Write-Host "Built THEMES/SAMPLE"
} catch {
    Pop-Location -ErrorAction SilentlyContinue
    Warn "could not build the sample theme ($($_.Exception.Message)); packaging without THEMES/SAMPLE (optional)"
}

Write-Host "Release folder ready: $ReleaseDir"
Get-ChildItem $ReleaseDir -Recurse -File | ForEach-Object { Write-Host ("  {0,10:N0}  {1}" -f $_.Length, $_.FullName.Substring($ReleaseDir.Length + 1)) }
}   # end of "else" (no -ReleaseDirOverride): the release-folder assembly section

# Also embedded into the Card Manager itself (VibeMenuCardManager.Core.csproj's conditional EmbeddedResource,
# Condition="Exists('..\..\build\db\GAMES.DB')", read back by BuiltInDb.Load()). $ExportDir is a clean git archive
# export, so build/db (gitignored) is not there unless copied in -- same reason build/sgl is copied in above for
# the menu build. Without this, every dotnet publish/run below (now from $ExportDir) would silently build a Card
# Manager with no built-in GAMES.DB (BuiltInDb.Load() falls back to null, then to the card's own, older Load.Db).
New-Item -ItemType Directory -Force -Path (Join-Path $ExportDir "build/db") | Out-Null
Copy-Item (Join-Path $ReleaseDir "GAMES.DB") (Join-Path $ExportDir "build/db/GAMES.DB") -Force

# =========================================================================================================
# 3. The cover pack and the font, bundled next to the exe (not inside release/ -- separate auto-detected
#    paths, Core/Art/CoverPack.cs and Core/Art/TitleRenderer.cs).
# =========================================================================================================
$CoverPack = Join-Path $Root "build/coverpack/COVERS.PAK"
if (-not (Test-Path $CoverPack)) { throw "build/coverpack/COVERS.PAK is required (tools/omsat/coverpack.py); not present here" }

# The font is the pinned NotoSansJP-Regular.otf (the 4.5 MB JP subset) that every publish already carries, with its
# OFL.txt (manager/Directory.Build.targets, SHA-256 checked there); never build/fonts' 9.6 MB variable NotoSansJP[wght].
$FontSha = ([regex]::Match((Get-Content -Raw (Join-Path $ExportDir "manager/Directory.Build.targets")), "<VmcmFontSha>([0-9A-Fa-f]{64})</VmcmFontSha>")).Groups[1].Value
if (-not $FontSha) { throw "no <VmcmFontSha> in manager/Directory.Build.targets" }

# NOTICE.txt (the third-party attributions; CREDITS.md stays short): `vmcm notice`, run once with
# the host's own runtime (not a RID-specific publish, which may not run on this machine) and copied into every package.
Section "NOTICE.txt"
$NoticePath = Join-Path $Root "build/dist_NOTICE.txt"
# From $ExportDir (the clean git archive of HEAD), never the working tree -- otherwise an
# uncommitted change to the CLI's own source could leak into the published NOTICE.txt.
dotnet run --project (Join-Path $ExportDir "manager/VibeMenuCardManager.Cli/VibeMenuCardManager.Cli.csproj") -c Release -- notice | Out-File -Encoding utf8 $NoticePath
if ($LASTEXITCODE -ne 0 -or (Get-Item $NoticePath).Length -eq 0) { throw "vmcm notice failed to produce NOTICE.txt" }

# =========================================================================================================
# 4. Publish the .NET apps (self-contained) for every RID, then assemble and package per OS.
# =========================================================================================================
if (Test-Path $PubRoot) { Remove-Item -Recurse -Force $PubRoot }
if (Test-Path $PkgRoot) { Remove-Item -Recurse -Force $PkgRoot }
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$Results = @()
# unixpack.py from the clean export (never the working tree), run with this host's Python
$PyExe = (Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $PyExe) { throw "Python is required (manager/release/unixpack.py writes the Linux and macOS archives)" }
$UnixPack = Join-Path $ExportDir "manager/release/unixpack.py"
function UnixPack([string[]]$ArgList) {
    & $PyExe $UnixPack @ArgList
    if ($LASTEXITCODE -ne 0) { throw "unixpack.py $($ArgList[0]) failed (exit $LASTEXITCODE)" }
}

foreach ($rid in $Rids) {
    Section "Publish + package: $rid"
    $tgtWin = $rid.StartsWith("win-")
    $tgtMac = $rid.StartsWith("osx-")
    $tgtLinux = $rid.StartsWith("linux-")

    $pubUi = Join-Path $PubRoot "$rid/ui"
    $pubCli = Join-Path $PubRoot "$rid/cli"
    # Published from $ExportDir (the clean git archive of HEAD), never the working tree -- the
    # Card Manager binary is what ships, so it must never carry an uncommitted change.
    dotnet publish (Join-Path $ExportDir "manager/VibeMenuCardManager.UI/VibeMenuCardManager.UI.csproj") -c Release -r $rid --self-contained true `
        -p:PublishSingleFile=false -p:IncludeNativeLibrariesForSelfExtract=true @Identity -o $pubUi
    if ($LASTEXITCODE -ne 0) { Warn "${rid}: UI publish failed, skipping this RID"; continue }
    dotnet publish (Join-Path $ExportDir "manager/VibeMenuCardManager.Cli/VibeMenuCardManager.Cli.csproj") -c Release -r $rid --self-contained true `
        -p:PublishSingleFile=false @Identity -o $pubCli
    if ($LASTEXITCODE -ne 0) { Warn "${rid}: CLI publish failed, skipping this RID"; continue }

    $pkg = Join-Path $PkgRoot $rid
    New-Item -ItemType Directory -Force -Path $pkg | Out-Null
    Copy-Item "$pubUi/*" $pkg -Recurse -Force
    Copy-Item "$pubCli/*" $pkg -Recurse -Force    # vmcm(.exe) alongside AetherCardManager(.exe); no name clashes

    Copy-Item $ReleaseDir (Join-Path $pkg "release") -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $pkg "covers") | Out-Null
    Copy-Item $CoverPack (Join-Path $pkg "covers/COVERS.PAK")
    $pkgFont = Join-Path $pkg "fonts/NotoSansJP-Regular.otf"
    if (-not (Test-Path -LiteralPath $pkgFont) -or (Get-FileHash -LiteralPath $pkgFont -Algorithm SHA256).Hash -ne $FontSha.ToUpperInvariant() -or
        -not (Test-Path -LiteralPath (Join-Path $pkg "fonts/OFL.txt"))) {
        throw "${rid}: the publish did not carry the pinned fonts/NotoSansJP-Regular.otf (SHA-256 $FontSha) and fonts/OFL.txt"
    }
    Copy-Item $NoticePath (Join-Path $pkg "NOTICE.txt")
    # NOTICE.txt: "Full license texts are in LICENSES/ next to this file."
    Copy-Item (Join-Path $ExportDir "LICENSES") (Join-Path $pkg "LICENSES") -Recurse -Force

    if ($tgtWin) {
        $zipPath = Join-Path $DistDir "AetherCardManager-$Version-$rid.zip"
        Compress-Archive -Path "$pkg/*" -DestinationPath $zipPath -Force
        $Results += [pscustomobject]@{ Rid = $rid; Kind = "zip"; Path = $zipPath }
    } elseif ($tgtLinux) {
        $tgzPath = Join-Path $DistDir "AetherCardManager-$Version-$rid.tar.gz"
        UnixPack @("tar", $pkg, $tgzPath)    # <rid>/ at the top, as AppUpdate.Stage lifts it
        $Results += [pscustomobject]@{ Rid = $rid; Kind = "tar.gz"; Path = $tgzPath }
        $appimagetool = Get-Command appimagetool -ErrorAction SilentlyContinue
        if ($IsLinux -and $appimagetool) {
            # AppDir: Contents become the AppImage's root; .desktop + a top-level icon (an AppImage convention),
            # AppRun execs the real binary. ARCH picks the right appimagetool-built runtime for the RID.
            $appDirPath = Join-Path $PkgRoot "AppDir-$rid"
            if (Test-Path $appDirPath) { Remove-Item -Recurse -Force $appDirPath }
            New-Item -ItemType Directory -Force -Path $appDirPath | Out-Null
            Copy-Item "$pkg/*" $appDirPath -Recurse -Force
            Copy-Item (Join-Path $Root "assets/logo/aether_icon_1024.png") (Join-Path $appDirPath "aethercardmanager.png")
            @"
[Desktop Entry]
Type=Application
Name=Aether Card Manager
Exec=AetherCardManager
Icon=aethercardmanager
Categories=Utility;
"@ | Set-Content -Encoding UTF8 (Join-Path $appDirPath "aethercardmanager.desktop")
            @"
#!/bin/sh
HERE="`$(dirname "`$(readlink -f "`${0}")")"
exec "`${HERE}/AetherCardManager" "`$@"
"@ | Set-Content -Encoding UTF8 -NoNewline (Join-Path $appDirPath "AppRun")
            & chmod +x (Join-Path $appDirPath "AppRun") (Join-Path $appDirPath "AetherCardManager")
            $arch = if ($rid -eq "linux-arm64") { "aarch64" } else { "x86_64" }
            $appImagePath = Join-Path $DistDir "AetherCardManager-$Version-$rid.AppImage"
            $env:ARCH = $arch
            & appimagetool $appDirPath $appImagePath
            $Results += [pscustomobject]@{ Rid = $rid; Kind = "AppImage"; Path = $appImagePath }
        } else {
            Warn "${rid}: no .AppImage built here -- appimagetool needs a real Linux host (FUSE/an ELF AppImage runtime); the tar.gz above is a full substitute. .github/workflows/release.yml builds the real .AppImage on an ubuntu runner."
        }
    } elseif ($tgtMac) {
        # A .app bundle (Contents/MacOS, Contents/Resources, Info.plist, an .icns) zipped with its Unix modes: no .dmg
        # (no hdiutil here); unsigned, so the first launch is right-click > Open. AppUpdate.AssetFor has no macOS
        # asset: the Card Manager opens the release page there.
        $appDir = Join-Path (Join-Path $PkgRoot "app-$rid") "AetherCardManager.app"
        $macOS = Join-Path $appDir "Contents/MacOS"
        $resources = Join-Path $appDir "Contents/Resources"
        if (Test-Path $appDir) { Remove-Item -Recurse -Force $appDir }
        New-Item -ItemType Directory -Force -Path $macOS | Out-Null
        New-Item -ItemType Directory -Force -Path $resources | Out-Null
        Copy-Item "$pkg/*" $macOS -Recurse -Force
        UnixPack @("icns", (Join-Path $Root "assets/logo/aether_icon_1024.png"), (Join-Path $resources "AppIcon.icns"))
        $plist = @"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Aether Card Manager</string>
  <key>CFBundleDisplayName</key><string>Aether Card Manager</string>
  <key>CFBundleIdentifier</key><string>org.openmenusatiator.aethercardmanager</string>
  <key>CFBundleVersion</key><string>$Version</string>
  <key>CFBundleShortVersionString</key><string>$Version</string>
  <key>CFBundleExecutable</key><string>AetherCardManager</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
"@
        [System.IO.File]::WriteAllText((Join-Path $appDir "Contents/Info.plist"), ($plist -replace "`r`n", "`n"), (New-Object System.Text.UTF8Encoding $false))
        $zipPath = Join-Path $DistDir "AetherCardManager-$Version-$rid.zip"
        UnixPack @("zip", $appDir, $zipPath)
        $Results += [pscustomobject]@{ Rid = $rid; Kind = "zip (.app, unsigned)"; Path = $zipPath }
    }
}

# version.json: this run's build identity, the update check's answer to "is that release a newer build of my version?"
$versionPath = Join-Path $DistDir "version.json"
$versionJson = [ordered]@{ version = $Version; build = $Build; commit = $Sha; prerelease = $Prerelease } | ConvertTo-Json -Compress
[System.IO.File]::WriteAllText($versionPath, "$versionJson`n", [System.Text.Encoding]::ASCII)
$Results += [pscustomobject]@{ Rid = "all"; Kind = "version"; Path = $versionPath }

# SHA256SUMS.txt (sha256sum's "<hex>  <name>" lines) for every package this run made: upload it with them -- the Card
# Manager's self-update (Core/Update/AppUpdate.cs) refuses a download whose hash does not match its line.
$sumsPath = Join-Path $DistDir "SHA256SUMS.txt"
$sums = $Results | Sort-Object { Split-Path $_.Path -Leaf } | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.Path -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) }
# LF line ends: Set-Content writes CRLF on Windows, and `sha256sum -c` then reads each name with a trailing \r
[System.IO.File]::WriteAllText($sumsPath, (($sums | ForEach-Object { "$_`n" }) -join ""), [System.Text.Encoding]::ASCII)
$Results += [pscustomobject]@{ Rid = "all"; Kind = "sha256"; Path = $sumsPath }

Section "Done"
foreach ($r in $Results) {
    $size = (Get-Item $r.Path).Length
    Write-Host ("{0,-14} {1,-10} {2,12:N0} B  {3}" -f $r.Rid, $r.Kind, $size, $r.Path)
}
Write-Host "`nNothing here was committed (build/ is gitignored)."
