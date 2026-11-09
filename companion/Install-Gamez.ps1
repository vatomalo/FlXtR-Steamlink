param([string]$Source = "$PSScriptRoot\publish")
$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:LOCALAPPDATA 'FlXtR\Gamez\app'
if (!(Test-Path -LiteralPath (Join-Path $Source 'FlXtR.Gamez.exe'))) { throw 'Publish the app first; see README.' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -Path "$Source\*" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$PSScriptRoot\Start-Gamez.ps1" -Destination $destination -Force
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name 'FlXtR Gamez' -Value ('"' + (Join-Path $destination 'FlXtR.Gamez.exe') + '" --background')
Start-Process -FilePath (Join-Path $destination 'FlXtR.Gamez.exe') -ArgumentList '--startup','--background' -WindowStyle Hidden
