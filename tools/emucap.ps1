# tools/emucap.ps1 -- emulator-tier screenshots (Mednafen 1.32.1 ss core, windowed). Reports "verified on Mednafen"
# only; never a hardware result. Based on the working session script (CopyFromScreen + nonblack sample count);
# -Extra is new here so a later task (soundrecord, Task 15) can pass extra Mednafen flags without editing this file.
# Usage: powershell -File tools/emucap.ps1 -Image <cue> -Tag <tag> -Times 10,20 [-Extra ...]
# NB: Mednafen's ss core refuses a bare 2048 B/sector ISO ("File unrecognized by ss module") -- pass a .cue.
# NB (measured Task 9): invoke this from a real PowerShell session (the PowerShell tool, pwsh.exe, powershell.exe
# ISE), not via Git Bash's `powershell -File ... -Times 30,60` -- MSYS mangles the comma-separated -Times/-Extra
# argv crossing that shell boundary ("30,60" arrives as "3060", a single huge wait). Confirmed by direct repro.
# NB (M3 Task 13): screenshots come from PrintWindow(PW_RENDERFULLCONTENT) on Mednafen's own window, not CopyFromScreen,
# which captured whatever window happened to cover Mednafen (measured: two Task 13 captures showed a terminal instead).
# A minimised window is restored first. Each PNG still names its tour step and frame counter: pick steps by those.
# NB (Task 13r): this script's own Mednafen settings (video.fs, etc.) are written to a per-run -ovconfig override file,
# not passed as bare "-setting value" command-line args. Mednafen's primary mednafen.cfg (shared with the user's own
# Mednafen use) is rewritten from the *current active settings* on every shutdown, so a bare "-video.fs 0" on the
# command line was silently persisting into that shared file every capture run (confirmed: Documentation/mednafen.html
# "Configuration Files" -- override-config settings "will NOT be written to ... and generally will not alter the
# contents of the primary configuration file"). -force_module (and anything in -Extra such as -soundrecord) are
# one-shot command options, not persisted settings, per the same doc's Command-line table, so they stay on argv.
# NB (M6 Task 22): -OvLines adds settings to that per-run override file (e.g. "filesys.path_sav <scratch dir>", so a
# backup-RAM test writes a scratch .bkr and never the user's own saves); unlike -Extra they do not persist.
# NB (M6 Task 18c): -StateTimes takes a Mednafen savestate (F5, Mednafen's default "save state" key) at each time, into a
# per-run scratch state directory (filesys.path_state in the override file, never the user's own), and copies it to
# $Dir\<tag>_s<t>.mcs for a register-level reader (tools/omsat/slavegate.py). The key goes to Mednafen's own window
# (SetForegroundWindow, as the investigation's snap.ps1 did); a time at which no new state file appears says so.
param([string]$Image, [string]$Tag, [double[]]$Times = @(4,8,12,20,35), [string[]]$Extra = @(),
      [string]$Dir = "$env:TEMP\omsat_emu", [string[]]$OvLines = @(), [double[]]$StateTimes = @(),
      [string]$Mednafen = "")
$exe = if ($Mednafen) { $Mednafen } elseif (Get-Command mednafen -ErrorAction SilentlyContinue) { (Get-Command mednafen).Source } else {
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe" }
if (-not (Test-Path $exe)) { throw "mednafen.exe not found at $exe" }
New-Item -ItemType Directory -Force $Dir | Out-Null
$ovconfig = "$Dir\$Tag.ovconfig.cfg"
Set-Content -Path $ovconfig -Value "video.fs 0"
foreach ($l in $OvLines) { Add-Content -Path $ovconfig -Value $l }
$stdir = "$Dir\${Tag}_state"
if ($StateTimes.Count) {
  New-Item -ItemType Directory -Force $stdir | Out-Null
  Get-ChildItem $stdir -Filter *.mc* -ErrorAction SilentlyContinue | Remove-Item -Force
  Add-Content -Path $ovconfig -Value "filesys.path_state $stdir"
}
# Task 14/15: the user's mednafen.cfg has "sound 0", and -soundrecord then writes a 44 B header and no samples
# (measured). A recording run turns sound on in this override file only (sound.rate is ignored here: the WAVs are 48 kHz).
if ($Extra -contains "-soundrecord") { Add-Content -Path $ovconfig -Value "sound 1`nsound.rate 44100" }
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ("W3" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W3 { [StructLayout(LayoutKind.Sequential)] public struct R { public int L,T,Ri,B; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c); }
"@ }
if (-not ("K3" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public class K3 { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra); }
"@ }
$args2 = @("-force_module","ss","-ovconfig",$ovconfig) + $Extra + @("`"$Image`"")
$p = Start-Process -FilePath $exe -ArgumentList $args2 -PassThru -RedirectStandardOutput "$Dir\$Tag.log"
$t0 = Get-Date
$events = @(); foreach ($t in $Times) { $events += ,@($t, "shot") }; foreach ($t in $StateTimes) { $events += ,@($t, "state") }
$events = @($events | Sort-Object { $_[0] })
foreach ($ev in $events) {
  $t = $ev[0]
  if ($ev[1] -eq "state") {
    while (((Get-Date)-$t0).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 100 }
    $p.Refresh(); if ($p.HasExited) { "exited at $t s code $($p.ExitCode)"; break }
    for ($i = 0; $i -lt 20 -and [K3]::GetForegroundWindow() -ne $p.MainWindowHandle; $i++) { [void][K3]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 100 }
    [K3]::keybd_event(0x74, 0x3F, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [K3]::keybd_event(0x74, 0x3F, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    $f = Get-ChildItem $stdir -Filter *.mc* -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($f) { Copy-Item $f.FullName "$Dir\${Tag}_s$t.mcs" -Force; Remove-Item $f.FullName; "state at $t s -> $Dir\${Tag}_s$t.mcs" }
    else { "state at $t s: no state file written" }
    continue
  }
  while (((Get-Date)-$t0).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 250 }
  $p.Refresh(); if ($p.HasExited) { "exited at $t s code $($p.ExitCode)"; break }
  if ([W3]::IsIconic($p.MainWindowHandle)) { [void][W3]::ShowWindow($p.MainWindowHandle, 4); Start-Sleep -Milliseconds 300 }
  $r = New-Object W3+R; [void][W3]::GetWindowRect($p.MainWindowHandle, [ref]$r)
  $w = $r.Ri-$r.L; $h = $r.B-$r.T
  if ($w -le 0) { "no window at $t"; continue }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc(); [void][W3]::PrintWindow($p.MainWindowHandle, $hdc, 2); $g.ReleaseHdc($hdc)
  $bmp.Save("$Dir\${Tag}_t$t.png")
  $nz = 0; for ($y=40; $y -lt $h-10; $y+=8) { for ($x=10; $x -lt $w-10; $x+=8) { $c=$bmp.GetPixel($x,$y); if ($c.R+$c.G+$c.B -gt 48) { $nz++ } } }
  "t=$t saved $Dir\${Tag}_t$t.png nonblack_samples=$nz"
}
# Task 14/15: close the window first (WM_CLOSE) so Mednafen shuts down cleanly and finalizes a -soundrecord WAV;
# a forced kill left the WAV at 0 bytes (measured). The kill stays as the fallback after 8 s.
if (-not $p.HasExited) { [void]$p.CloseMainWindow(); [void]$p.WaitForExit(8000) }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
