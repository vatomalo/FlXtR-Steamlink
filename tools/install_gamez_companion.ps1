$ErrorActionPreference = 'Stop'

$companion = Join-Path $PSScriptRoot 'gamez_companion.py'
$config = Join-Path $PSScriptRoot 'gamez.local.json'
if (-not (Test-Path -LiteralPath $companion -PathType Leaf)) {
    throw "Missing companion script: $companion"
}
if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
    throw "Create and configure $config before installing the startup task."
}

$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1 -ExpandProperty Source
$pythonw = Join-Path (Split-Path -Parent $python) 'pythonw.exe'
if (-not (Test-Path -LiteralPath $pythonw -PathType Leaf)) {
    throw "pythonw.exe was not found beside $python"
}

$taskName = 'FlXtR Gamez Companion'
$account = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction `
    -Execute $pythonw `
    -Argument ('"{0}"' -f $companion) `
    -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal `
    -UserId $account `
    -LogonType Interactive `
    -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet `
    -StartWhenAvailable `
    -RestartCount 3 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero)

Register-ScheduledTask `
    -TaskName $taskName `
    -Action $action `
    -Trigger $trigger `
    -Principal $principal `
    -Settings $settings `
    -Description 'Starts the FlXtR Gamez launch bridge for the current Windows user.' `
    -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host "Installed and started '$taskName' for $account."
