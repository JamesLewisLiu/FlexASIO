param([switch]$LowLatency)
$ErrorActionPreference = 'Stop'
$package = Split-Path $PSScriptRoot -Parent
$source = Join-Path $package $(if ($LowLatency) { 'FlexASIO-low-latency.toml' } else { 'FlexASIO.toml' })
if (-not (Test-Path -LiteralPath $source)) { throw 'Run this script from the extracted binary package.' }
$destination = Join-Path ([Environment]::GetFolderPath('UserProfile')) 'FlexASIO.toml'
if (Test-Path -LiteralPath $destination) {
    $backup = "$destination.$(Get-Date -Format yyyyMMdd-HHmmss-fff).bak"
    Copy-Item -LiteralPath $destination -Destination $backup
    Write-Host "Saved previous configuration: $backup"
}
Copy-Item -LiteralPath $source -Destination $destination -Force
Write-Host "Configured: $destination"
Write-Host 'Check output.device matches your physical speakers; restart the game to load the configuration.'
