param(
    [string]$ManifestUrl = 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/gamez-updates/gamez-manifest.json',
    [string]$SunshineAppsPath
)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the session setup in Administrator PowerShell on CafeLeBlanc.'
}
$root = Join-Path $env:LOCALAPPDATA 'FlXtR\Gamez'
$app = Join-Path $root 'app'
$config = Join-Path $root 'catalog.json'
if (-not (Test-Path -LiteralPath $config)) { throw 'Connect the existing Gamez companion first.' }
if (Get-Process -Name 'ff7remake*' -ErrorAction SilentlyContinue) { throw 'Exit FFVII normally before updating the launcher. No game was stopped.' }
$catalog = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
try {
    $session = Invoke-RestMethod -Uri ("http://$($catalog.Bind):$($catalog.Port)/v1/session") -Headers @{ Authorization=('Bearer '+$catalog.Token) } -TimeoutSec 2
    if ($session.state -eq 'running') { throw 'Exit the running game normally before updating.' }
} catch [System.Net.WebException] { } # Older companion has no session endpoint.
$sunshine = @(Get-CimInstance Win32_Process -Filter "Name='sunshine.exe'" | Where-Object ExecutablePath)
if (-not $sunshine.Count) { throw 'Sunshine is not running; start it before installing.' }
if (-not $SunshineAppsPath) {
    $configurations = @()
    foreach ($process in $sunshine) {
        $install = Split-Path -Parent $process.ExecutablePath
        $configurations += (Join-Path $install 'config\sunshine.conf'), (Join-Path $install 'sunshine.conf')
        foreach ($match in [regex]::Matches($process.CommandLine, '(?:"([^"]+)"|(\S+))')) {
            $argument = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            if ([IO.Path]::IsPathRooted($argument) -and $argument.EndsWith('.conf')) { $configurations += $argument }
        }
    }
    $appCandidates = @()
    foreach ($configuration in ($configurations | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $configuration -PathType Leaf)) { continue }
        $line = @(Get-Content -LiteralPath $configuration | Where-Object { $_ -match '^\s*file_apps\s*=' } | Select-Object -Last 1)
        $file = if ($line.Count) { ($line[0] -replace '^\s*file_apps\s*=\s*','').Trim().Trim('"') } else { 'apps.json' }
        if (-not [IO.Path]::IsPathRooted($file)) { $file = Join-Path (Split-Path -Parent $configuration) $file }
        if (Test-Path -LiteralPath $file -PathType Leaf) {
            $data = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            if (@($data.apps | Where-Object name -eq 'Desktop_1080').Count -eq 1) { $appCandidates += [IO.Path]::GetFullPath($file) }
        }
    }
    $appCandidates = @($appCandidates | Select-Object -Unique)
    if ($appCandidates.Count -ne 1) { throw 'Could not uniquely locate the active Sunshine apps file. Re-run with -SunshineAppsPath and its actual full path.' }
    $SunshineAppsPath = $appCandidates[0]
}
$apps = Get-Content -LiteralPath $SunshineAppsPath -Raw | ConvertFrom-Json
if (@($apps.apps | Where-Object name -eq 'Desktop_1080').Count -ne 1) { throw 'Sunshine configuration does not match the verified Desktop_1080 host.' }
$managed = @($apps.apps | Where-Object name -eq 'FlXtR Gamez')
if ($managed.Count -gt 1 -or ($managed.Count -eq 1 -and $managed[0].cmd -notmatch 'FlXtR\.Gamez\.exe.*--wait-session')) {
    throw 'An unmanaged Sunshine entry already uses FlXtR Gamez; preserving it.'
}
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
if (-not $ManifestUrl.StartsWith('https://github.com/vatomalo/FlXtR-Steamlink/releases/download/gamez-')) { throw 'Unexpected manifest source.' }
$release = Invoke-RestMethod $ManifestUrl -TimeoutSec 20
$expected = 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/gamez-' + $release.version + '/FlXtR-Gamez-win-x64.zip'
if ($release.url -ne $expected -or $release.version -notmatch '^[a-f0-9]{40}$' -or $release.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid release metadata.' }
$download = Join-Path $root 'gamez-session-update.zip'
Invoke-WebRequest -UseBasicParsing $release.url -OutFile $download -TimeoutSec 180
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $release.sha256) { throw 'Downloaded package checksum differs.' }
# Fetch the matching updater source; it independently verifies the package and runs
# the candidate's Windows child-process/argument tests before replacing the app.
$loader = Join-Path $root 'Update-And-Start.ps1'
Invoke-WebRequest -UseBasicParsing ('https://raw.githubusercontent.com/vatomalo/FlXtR-Steamlink/' + $release.version + '/companion/Update-And-Start.ps1') -OutFile $loader -TimeoutSec 20
$task = Get-ScheduledTask -TaskName 'FlXtR Gamez Companion' -ErrorAction SilentlyContinue
if ($task) { Stop-ScheduledTask -TaskName $task.TaskName }
foreach ($process in @(Get-Process -Name 'FlXtR.Gamez' -ErrorAction SilentlyContinue)) {
    if (-not $process.Path.StartsWith(($app.TrimEnd('\')+'\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Companion is running from an unexpected directory.' }
    Stop-Process -Id $process.Id -Force
    Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
}
try {
    & $loader -NoStart -PackagePath $download -ExpectedSha $release.sha256 -Version $release.version
} catch {
    if ($task -and (Test-Path -LiteralPath (Join-Path $app 'FlXtR.Gamez.exe'))) { Start-ScheduledTask -TaskName $task.TaskName }
    throw
}
$executable = Join-Path $app 'FlXtR.Gamez.exe'
$entry = [pscustomobject]@{
    name='FlXtR Gamez'
    cmd=('"'+$executable+'" --wait-session')
    'working-dir'=$app
    output=(Join-Path $root 'sunshine-session.log')
    'auto-detach'=$false
    'wait-all'=$false
    'exit-timeout'=5
    elevated=$false
    'exclude-global-prep-cmd'=$true
    'prep-cmd'=@([pscustomobject]@{ do=('"'+$executable+'" --prepare-session'); undo=('"'+$executable+'" --restore-shell'); elevated=$false })
}
$backup = $SunshineAppsPath + '.before-flxtr-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
Copy-Item -LiteralPath $SunshineAppsPath -Destination $backup
$apps.apps = @($apps.apps | Where-Object name -ne 'FlXtR Gamez') + $entry
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText(($SunshineAppsPath+'.next'), ($apps | ConvertTo-Json -Depth 30), $encoding)
Move-Item -LiteralPath ($SunshineAppsPath+'.next') -Destination $SunshineAppsPath -Force
& (Join-Path $app 'Connect-SteamLink.ps1') -StartupOnly
$services = @(Get-CimInstance Win32_Service | Where-Object { $_.State -eq 'Running' -and $_.PathName -match 'sunshine' })
if ($services.Count -eq 1) {
    Restart-Service -Name $services[0].Name -Force
    Write-Host 'Sunshine restarted with the managed FlXtR Gamez entry.'
} else {
    Write-Host 'Restart your portable Sunshine normally so it reads the new entry.'
}
Write-Host 'Installed: black launch background, Explorer shutdown before game launch, child-process tracking, stream exit and Explorer recovery.'
Write-Host 'Gamez will check its own update channel when Windows starts it.'
Write-Host 'Reply done in Codex to switch the Steam Link to FlXtR Gamez.'
