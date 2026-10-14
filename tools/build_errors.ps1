<#
.SYNOPSIS
  Runs build.ps1 and prints only the compiler errors (unique), then the last lines of the log.
  The full log is written to build\build_log.txt (UTF-8). Used during porting to iterate on compile errors.

  .\tools\build_errors.ps1 [build.ps1 arguments]     e.g. .\tools\build_errors.ps1 reconfigure
#>
$root = Split-Path $PSScriptRoot
$log = Join-Path $root 'build\build_log.txt'
New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
$ok = $true
try { & (Join-Path $root 'build.ps1') @args 2>&1 | Out-File -Encoding utf8 $log } catch { $ok = $false }
$errors = Select-String -Path $log -Pattern 'error:|undefined reference' | ForEach-Object { $_.Line -replace '^S:/', '' } | Select-Object -Unique
$errors | Select-Object -First 80
Write-Host ''
Write-Host ("{0} unique error lines; build {1}" -f @($errors).Count, $(if ($ok) { 'succeeded' } else { 'FAILED' }))
