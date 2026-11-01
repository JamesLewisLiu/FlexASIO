param([ValidateSet('x64', 'x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run Install-x64.cmd (or Install-x86.cmd) as administrator. Close the ASIO host first.'
}
$package = Split-Path $PSScriptRoot -Parent
$source = Join-Path $package $Architecture
if (-not (Test-Path -LiteralPath (Join-Path $source 'FlexASIO.dll'))) { throw 'Run this script from the extracted binary package.' }
$programFiles = [Environment]::GetEnvironmentVariable('ProgramW6432')
if (-not $programFiles) { $programFiles = $env:ProgramFiles }
$destination = Join-Path $programFiles "FlexASIO-Mixer\$Architecture"
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $destination -Force
$registrationTool = Join-Path $env:WINDIR 'System32\regsvr32.exe'
if ($Architecture -eq 'x86' -and [Environment]::Is64BitOperatingSystem) {
    $registrationTool = Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe'
}
$env:PATH = "$destination;$env:PATH"
$result = Start-Process -FilePath $registrationTool -ArgumentList @('/s', ('"' + (Join-Path $destination 'FlexASIO.dll') + '"')) -Wait -PassThru -WindowStyle Hidden
if ($result.ExitCode -ne 0) { throw "Driver registration failed: $($result.ExitCode)" }
Write-Host "Registered FlexASIO ($Architecture) at $destination"
Write-Host 'Next: run Use-Config.cmd as your normal Windows user, then select FlexASIO in the existing game ASIO driver setting.'
