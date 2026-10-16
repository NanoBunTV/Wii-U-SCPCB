<#
.SYNOPSIS
  Runs a .wuhb in Cemu for a few seconds, saves a screenshot of the Cemu window and prints the app's log lines.

.DESCRIPTION
  Used for automated checks during development (see Docs/BUILDING.md). Uses the project's portable Cemu copy
  (_ref\tools\cemu, own settings and log, windowed) so it never collides with other projects using the main Cemu
  install. Cemu writes the app's OSReport output to portable\log.txt as "[OSConsole]" lines.

  .\tools\cemu_run.ps1                                 run build\scpcb.wuhb for 10 s
  .\tools\cemu_run.ps1 -Seconds 20 -Shot shot.png      longer run, screenshot path
  .\tools\cemu_run.ps1 -Seconds 130 -ShotsAt 95,110    also screenshots at 95 s and 110 s (shot_95.png ...)
  .\tools\cemu_run.ps1 -Audio -Volume 15             audible run (only when the owner asks for a listening test)
  .\tools\cemu_run.ps1 -Pad                          also opens Cemu's GamePad view; each screenshot gets a _pad copy

  Runs are SILENT by default : every run sets the portable Cemu's TV / GamePad volume to 0 and no output device.
  -Audio uses the device saved in portable\audio_device.txt at -Volume percent (default 15).
#>
param(
	[string]$Wuhb = (Join-Path $PSScriptRoot '..\build\scpcb.wuhb'),
	[int]$Seconds = 10,
	[string]$Shot = (Join-Path $PSScriptRoot '..\build\cemu_shot.png'),
	[string]$Cemu = (Join-Path $PSScriptRoot '..\_ref\tools\cemu\Cemu.exe'),
	[string]$Filter = 'OSConsole|error|crash|exception|Unhandled',
	[switch]$Interpreter,		# Cemu's PPC interpreter : slower, but exact memory accesses (for debugging crashes)
	[int[]]$ShotsAt = @(),		# extra screenshots at these times (seconds since start)
	[switch]$Audio,				# audible run (see above); otherwise always muted
	[int]$Volume = 15,
	[switch]$Pad				# open Cemu's separate GamePad window too and screenshot it (<shot>_pad.png)
)
$ErrorActionPreference = 'Stop'

$Cemu = (Resolve-Path $Cemu).Path
# Only our portable copy matters; other projects may run the main Cemu install at the same time
if (Get-Process Cemu -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Cemu }) { throw 'This project''s Cemu is already running. Close it first.' }
$log = Join-Path (Split-Path $Cemu) 'portable\log.txt'

# Sound : muted unless -Audio (a broken audio path once blasted loud static through the owner's headphones)
$settings = Join-Path (Split-Path $Cemu) 'portable\settings.xml'
$devFile = Join-Path (Split-Path $Cemu) 'portable\audio_device.txt'
$dev = ''; $vol = 0
if ($Audio) {
	if (Test-Path $devFile) { $dev = (Get-Content $devFile -Raw).Trim() }
	$vol = [Math]::Max(0, [Math]::Min(100, $Volume))
	Write-Host "AUDIO ON : TV volume $vol %"
}
$xml = Get-Content $settings -Raw
$xml = $xml -replace '<TVVolume>\d+</TVVolume>', "<TVVolume>$vol</TVVolume>"
$xml = $xml -replace '<PadVolume>\d+</PadVolume>', '<PadVolume>0</PadVolume>'
$xml = $xml -replace '<TVDevice>[^<]*</TVDevice>', "<TVDevice>$dev</TVDevice>"
$xml = $xml -replace '<PadDevice>[^<]*</PadDevice>', '<PadDevice></PadDevice>'
# GamePad view : a normal window (854x480 picture) when -Pad, else closed
$xml = $xml -replace '<open_pad>\w+</open_pad>', "<open_pad>$(if ($Pad) { 'true' } else { 'false' })</open_pad>"
if ($Pad) {
	$xml = $xml -replace '<pad_maximized>\w+</pad_maximized>', '<pad_maximized>false</pad_maximized>'
	$xml = $xml -replace '<pad_position>\s*<x>-?\d+</x>\s*<y>-?\d+</y>', "<pad_position>`n        <x>40</x>`n        <y>40</y>"
	$xml = $xml -replace '<pad_size>\s*<x>\d+</x>\s*<y>\d+</y>', "<pad_size>`n        <x>870</x>`n        <y>519</y>"
}
Set-Content $settings $xml -NoNewline

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CemuWin {
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder s, int n);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
	// a visible top-level window of the process whose title contains `part`
	public static IntPtr Find(uint pid, string part) {
		IntPtr found = IntPtr.Zero;
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p != pid || !IsWindowVisible(h)) return true;
			var sb = new System.Text.StringBuilder(256); GetWindowText(h, sb, 256);
			if (sb.ToString().Contains(part)) { found = h; return false; }
			return true;
		}, IntPtr.Zero);
		return found;
	}
}
'@

[void][CemuWin]::SetProcessDPIAware()   # real pixel sizes on scaled displays
$wuhbFull = (Resolve-Path $Wuhb).Path
$cemuArgs = @('-g', "`"$wuhbFull`"")
if ($Interpreter) { $cemuArgs += '--force-interpreter' }
$p = Start-Process -FilePath $Cemu -ArgumentList $cemuArgs -PassThru
function Save-Shot([string]$path, [IntPtr]$h = [IntPtr]::Zero) {
	if ($h -eq [IntPtr]::Zero) { $h = [CemuWin]::Find([uint32]$p.Id, 'Cemu ') }		# the main window ("Cemu 2.6 - FPS...")
	if ($h -eq [IntPtr]::Zero) { Write-Host 'No Cemu window found for a screenshot'; return }
	$r = New-Object CemuWin+RECT
	[void][CemuWin]::GetWindowRect($h, [ref]$r)
	$w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
	# PrintWindow captures only Cemu's own window contents (even when covered), never the rest of the screen
	$bmp = New-Object System.Drawing.Bitmap $w, $hgt
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$hdc = $g.GetHdc()
	$ok = [CemuWin]::PrintWindow($h, $hdc, 2)   # PW_RENDERFULLCONTENT
	$g.ReleaseHdc($hdc)
	$g.Dispose()
	if (-not $ok) { Write-Host 'PrintWindow failed' }
	$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
	Write-Host "Screenshot: $path ($w x $hgt)"
	if ($Pad -and $path -notlike '*_pad.png') {
		$ph = [CemuWin]::Find([uint32]$p.Id, 'GamePad')
		if ($ph -eq [IntPtr]::Zero) { Write-Host 'No GamePad window found' }
		else { Save-Shot ([IO.Path]::ChangeExtension($path, $null).TrimEnd('.') + '_pad.png') $ph }
	}
}

try {
	$start = Get-Date
	foreach ($t in ($ShotsAt | Sort-Object)) {
		if ($t -ge $Seconds) { continue }
		$wait = $t - ((Get-Date) - $start).TotalSeconds
		if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
		Save-Shot ([IO.Path]::ChangeExtension($Shot, $null).TrimEnd('.') + "_$t.png")
	}
	$wait = $Seconds - ((Get-Date) - $start).TotalSeconds
	if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
	Save-Shot $Shot
}
finally {
	Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
	Start-Sleep -Milliseconds 500
}
Get-Content $log | Select-String -Pattern $Filter
