param([string]$Executable = "$env:LOCALAPPDATA\FlXtR\Gamez\app\FlXtR.Gamez.exe")
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Gamez executable missing: $Executable" }
Start-Process -FilePath $Executable -ArgumentList '--background' -WindowStyle Hidden
