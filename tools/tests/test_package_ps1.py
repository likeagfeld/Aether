# Final review minors (manager/release/package.ps1): the Card Manager and NOTICE.txt must be published/built from
# the clean `git archive` export ($ExportDir), never the working tree, so an uncommitted change can never leak into
# a packaged binary; a missing BGM source must fail packaging, not just warn (the user requires BGM.MUS in the
# package); and build/db/GAMES.DB must be checked against the newest commit touching the tools that build it,
# failing if it is stale. package.ps1 has no end-to-end automated test (it drives Docker + multi-RID dotnet publish),
# so these are static contract checks on the script's own text.
import re
from pathlib import Path

PS1 = Path(__file__).resolve().parents[2] / "manager" / "release" / "package.ps1"


def _text():
    return PS1.read_text(encoding="utf-8")


def test_the_UI_and_CLI_are_published_from_the_clean_export_not_the_working_tree():
    t = _text()
    for proj in ("VibeMenuCardManager.UI/VibeMenuCardManager.UI.csproj", "VibeMenuCardManager.Cli/VibeMenuCardManager.Cli.csproj"):
        lines = [ln for ln in t.splitlines() if proj in ln and ln.lstrip().startswith("dotnet publish")]
        assert len(lines) == 1, f"expected exactly one `dotnet publish` line for {proj}, found {len(lines)}"
        assert "ExportDir" in lines[0], f"{proj} is published from {lines[0]!r}, not $ExportDir (the clean git archive)"


def test_NOTICE_txt_is_built_from_the_clean_export_not_the_working_tree():
    t = _text()
    i = t.index("VibeMenuCardManager.Cli.csproj")
    line_start = t.rindex("\n", 0, i) + 1
    line = t[line_start:t.index("\n", i)]
    assert line.lstrip().startswith("dotnet run --project"), f"not a `dotnet run --project` line: {line!r}"
    assert "ExportDir" in line, f"NOTICE.txt's vmcm is run from {line!r}, not $ExportDir (the clean git archive)"


def test_a_missing_BGM_source_fails_packaging_instead_of_only_warning():
    t = _text()
    # fd191cd: BGM300.MUS first, the WAV master second, and with neither the final else throws
    m = re.search(r"\}\s*else\s*\{\s*throw\s+\"neither build/music/BGM300\.MUS nor build/music/vmii_master_22k_s16\.wav", t)
    assert m, "a missing BGM source does not `throw` -- packaging can still succeed with no BGM.MUS"
    assert "Warn \"no build/music" not in t, "a missing BGM source is still only a Warn, not a throw"


def test_the_ready_made_BGM300_MUS_is_preferred_and_checked():
    t = _text()
    # The WAV master is gone; build/music/BGM300.MUS (the mix from 5:00, start position 0, faded in by the menu) is
    # what ships. It must be the first branch, validated by omsat.sound info (its "[]" problem list and start_frame 0).
    i300, iwav = t.index("if (Test-Path $Bgm300)"), t.index("elseif (Test-Path $MusicSrc)")
    assert i300 < iwav, "the WAV master is tried before BGM300.MUS"
    assert re.search(r'\$Bgm300\s*=\s*Join-Path \$Root "build/music/BGM300\.MUS"', t)
    block = t[i300:iwav]
    assert '"omsat.sound", "info", $Bgm300' in block, "BGM300.MUS is not checked with omsat.sound info"
    assert "start_frame" in block and r"\[\]" in block, "BGM300.MUS's start position or check list is not checked"
    assert re.search(r"Copy-Item \$Bgm300 \$BgmOut", block), "BGM300.MUS is not what goes in as BGM.MUS"


def test_a_failed_or_silent_BGM_build_also_fails_packaging():
    t = _text()
    # omsat.sound music can exit non-zero (or, as reproduced with a degenerate/silent test WAV, raise after writing
    # nothing) without PyRun's $LASTEXITCODE ever being checked -- package.ps1 printed "Built BGM.MUS" and shipped a
    # zip with no BGM.MUS at all. There must be a check, right after that PyRun call, that catches both: the
    # process's own exit code and BGM.MUS ($BgmOut) actually existing afterward.
    i = t.index('"omsat.sound", "music", $MusicSrc')
    block = t[i:i + 900]
    assert re.search(r"LASTEXITCODE", block), "no $LASTEXITCODE check after the BGM build"
    assert re.search(r"Test-Path \$BgmOut", block), "no Test-Path check that BGM.MUS was actually produced"


def test_CHEATS_PAK_goes_into_the_release_folder():
    t = _text()
    # CardPipeline.Run installs /aether/CHEATS.PAK only from the release folder's CHEATS.PAK
    assert re.search(r'Copy-Item \(Join-Path \$ExportDir "tools/data/cheats/CHEATS\.PAK"\) \(Join-Path \$ReleaseDir "CHEATS\.PAK"\)', t), \
        "CHEATS.PAK is never copied into the release folder -- a packaged Prepare installs no cheats"


def test_GAMES_DB_is_checked_against_the_newest_commit_touching_the_tools_that_build_it():
    t = _text()
    assert "build/db/GAMES.DB" in t
    # A staleness check: the newest commit touching GAMES.DB's actual producer chain, compared against the DB's own
    # build_date, with a throw when the DB predates it. build/db/GAMES.DB is the Rings Cover Project xlsx imported by
    # tools/omsat/coverproject.py (--out, not cardbuild.py's --gamesdb, which only reads it as a seed for a per-card
    # subset elsewhere) -- deliberately that, plus gamesdb.py's write_gamesdb format, not a blanket tools/omsat or
    # tools/data pathspec, which flags every unrelated tools/ commit forever and the check stops meaning anything
    # (this literally happened with the first two versions of this check).
    assert "coverproject.py" in t, "no mention of coverproject.py, the actual producer of build/db/GAMES.DB"
    assert "gamesdb.py" in t, "no mention of gamesdb.py (write_gamesdb's own format)"
    assert re.search(r"git[^\n]*log[^\n]*\$\w*[Tt]ools\b", t), "no `git log ...` staleness check against the producer pathspec"
    assert "tools/omsat tools/data" not in t, "the staleness pathspec is still the whole tools/omsat and tools/data trees"
    assert re.search(r"build_date", t), "no build_date read back from GAMES.DB to compare against"
    # The repro command in the throw message must actually clear the gate it just failed: coverproject.py's own
    # --date default is hardcoded 20261117, so a rebuild with no --date reproduces the same stale date forever.
    i = t.index("is stale:")
    throw_msg = t[i:t.index('"', i)]
    assert "--date" in throw_msg, "the staleness throw's repro command omits --date -- coverproject.py defaults to a fixed 20261117"


def test_GAMES_DB_is_also_copied_into_the_clean_export_for_the_embedded_resource():
    t = _text()
    # VibeMenuCardManager.Core.csproj embeds build/db/GAMES.DB (BuiltInDb.Load()) with Condition="Exists(...)". Since
    # publishing moved to $ExportDir (a clean git archive, so build/ -- gitignored -- starts out empty there), that
    # embed would otherwise silently disappear from every shipped Card Manager.
    assert re.search(r"Copy-Item[^\n]*GAMES\.DB[^\n]*ExportDir[^\n]*build/db|Copy-Item[^\n]*ExportDir[^\n]*build/db[^\n]*GAMES\.DB", t), \
        "build/db/GAMES.DB is never copied into $ExportDir/build/db -- the Card Manager's embedded GAMES.DB would be dropped"


def test_LICENSES_travels_next_to_NOTICE_txt():
    t = _text()
    # NOTICE.txt (Core/About/NoticeFile.cs) says the full license texts are in LICENSES/ next to it
    assert re.search(r'Copy-Item \(Join-Path \$ExportDir "LICENSES"\) \(Join-Path \$pkg "LICENSES"\) -Recurse', t), \
        "the packages carry NOTICE.txt but not the LICENSES/ folder it points to"


def test_SHA256SUMS_txt_has_LF_line_ends():
    t = _text()
    # `sha256sum -c` (what a Linux or macOS user runs) fails on CRLF; Set-Content writes CRLF on Windows
    i = t.index("$sumsPath = ")
    block = t[i:t.index("$Results +=", i)]
    assert "Set-Content -LiteralPath $sumsPath" not in block, "SHA256SUMS.txt is written by Set-Content (CRLF on Windows)"
    assert re.search(r"WriteAllText\(\$sumsPath[^\n]*`n", block), "SHA256SUMS.txt is not written with LF line ends"


def test_the_Linux_and_macOS_archives_are_written_with_Unix_modes():
    t = _text()
    # Windows' tar and Compress-Archive drop the modes: the binaries would unpack non-executable
    assert re.search(r'UnixPack @\("tar", \$pkg, \$tgzPath\)', t), "the Linux .tar.gz is not written by unixpack.py"
    assert re.search(r'UnixPack @\("zip", \$appDir, \$zipPath\)', t), "the macOS .app zip is not written by unixpack.py"
    assert "tar -czf" not in t and "Compress-Archive -Path $appDir" not in t
    assert '"AetherCardManager-$Version-$rid.zip"' in t and '-app.zip"' not in t and "& hdiutil" not in t
    assert '$UnixPack = Join-Path $ExportDir "manager/release/unixpack.py"' in t, "unixpack.py is not run from the clean export"


def test_the_packaged_font_is_the_pinned_subset_not_the_variable_font():
    t = _text()
    assert not re.search(r'Join-Path \$Root "build/fonts"', t) and "NotoSansJP*" not in t,         "packaging still copies a font from build/fonts"
    assert "<VmcmFontSha>" in t and re.search(r"Get-FileHash -LiteralPath \$pkgFont[^\n]*\$FontSha", t), \
        "the packaged fonts/NotoSansJP-Regular.otf is not checked against the pinned SHA-256"


def test_unixpack_modes_and_archives(tmp_path):
    import importlib.util
    import stat
    import tarfile
    import zipfile
    spec = importlib.util.spec_from_file_location("unixpack", PS1.parent / "unixpack.py")
    up = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(up)
    top = tmp_path / "Thing.app"
    (top / "Contents" / "MacOS").mkdir(parents=True)
    (top / "Contents" / "MacOS" / "app").write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 60)    # Mach-O 64
    (top / "Contents" / "MacOS" / "lib.so").write_bytes(b"\x7fELF" + b"\0" * 60)
    (top / "Contents" / "MacOS" / "data.json").write_text("{}")
    up.make_zip(top, tmp_path / "t.zip")
    up.make_tar(top, tmp_path / "t.tar.gz")
    want = {"Thing.app/": 0o755, "Thing.app/Contents/MacOS/app": 0o755, "Thing.app/Contents/MacOS/lib.so": 0o755,
            "Thing.app/Contents/MacOS/data.json": 0o644}
    for arc in ("t.zip", "t.tar.gz"):
        got = dict((n, m) for m, n in up.listing(tmp_path / arc))
        for n, m in want.items():
            assert got[n] == m, f"{arc}: {n} is {got[n]:o}, not {m:o}"
    with zipfile.ZipFile(tmp_path / "t.zip") as z:
        zi = z.getinfo("Thing.app/Contents/MacOS/app")
        assert zi.create_system == 3 and stat.S_ISREG(zi.external_attr >> 16)
    with tarfile.open(tmp_path / "t.tar.gz") as tf:
        assert all(m.uid == 0 and m.gid == 0 for m in tf.getmembers())
