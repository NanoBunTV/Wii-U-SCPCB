<#
.SYNOPSIS
  Runs a .wuhb in Cemu for a few seconds, saves a screenshot of the Cemu window and prints the app's log lines.

.DESCRIPTION
  Used for automated checks during development (see Docs/BUILDING.md). Uses the project's portable Cemu copy
  (_ref\tools\cemu, own settings and log, windowed) so it never collides with other projects using the main Cemu
  install. Cemu writes the app's OSReport output to portable\log.txt as "[OSConsole]" lines.

  .\tools\cemu_run.ps1                                 run build\scpcb.wuhb for 10 s
  .\tools\cemu_run.ps1 -Seconds 20 -Shot shot.png      longer run, screenshot path
#>
param(
	[string]$Wuhb = (Join-Path $PSScriptRoot '..\build\scpcb.wuhb'),
	[int]$Seconds = 10,
	[string]$Shot = (Join-Path $PSScriptRoot '..\build\cemu_shot.png'),
	[string]$Cemu = (Join-Path $PSScriptRoot '..\_ref\tools\cemu\Cemu.exe'),
	[string]$Filter = 'OSConsole|error|crash|exception|Unhandled',
	[switch]$Interpreter		# Cemu's PPC interpreter : slower, but exact memory accesses (for debugging crashes)
)
$ErrorActionPreference = 'Stop'

$Cemu = (Resolve-Path $Cemu).Path
# Only our portable copy matters; other projects may run the main Cemu install at the same time
if (Get-Process Cemu -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Cemu }) { throw 'This project''s Cemu is already running. Close it first.' }
$log = Join-Path (Split-Path $Cemu) 'portable\log.txt'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CemuWin {
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@

[void][CemuWin]::SetProcessDPIAware()   # real pixel sizes on scaled displays
$wuhbFull = (Resolve-Path $Wuhb).Path
$cemuArgs = @('-g', "`"$wuhbFull`"")
if ($Interpreter) { $cemuArgs += '--force-interpreter' }
$p = Start-Process -FilePath $Cemu -ArgumentList $cemuArgs -PassThru
try {
	Start-Sleep -Seconds $Seconds
	$p.Refresh()
	$h = $p.MainWindowHandle
	if ($h -ne [IntPtr]::Zero) {
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
		$bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
		$bmp.Dispose()
		Write-Host "Screenshot: $Shot ($w x $hgt)"
	} else {
		Write-Host 'No Cemu window found for a screenshot'
	}
}
finally {
	Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
	Start-Sleep -Milliseconds 500
}
Get-Content $log | Select-String -Pattern $Filter
