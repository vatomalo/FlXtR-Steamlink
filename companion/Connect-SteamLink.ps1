param(
    [string]$HostAddress = '192.168.10.187',
    [string]$SteamLinkAddress = '192.168.10.2'
)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this one-time network setup in PowerShell as Administrator on CafeLeBlanc.'
}
if (-not (Get-NetIPAddress -AddressFamily IPv4 | Where-Object IPAddress -eq $HostAddress)) {
    throw "This computer does not own $HostAddress. Run this on CafeLeBlanc."
}
foreach ($address in @($HostAddress, $SteamLinkAddress)) {
    $parsed = $null
    if (-not [Net.IPAddress]::TryParse($address, [ref]$parsed) -or $parsed.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
        throw 'Host and Steam Link addresses must be IPv4 addresses.'
    }
}
$directory = Join-Path $env:LOCALAPPDATA 'FlXtR\Gamez'
$executable = Join-Path $directory 'app\FlXtR.Gamez.exe'
$config = Join-Path $directory 'catalog.json'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Companion missing: $executable" }
if (-not (Test-Path -LiteralPath $config -PathType Leaf)) { throw "Catalog missing: $config" }
foreach ($command in @('netsh.exe', 'tar.exe', 'scp.exe')) { Get-Command $command -ErrorAction Stop | Out-Null }
$catalog = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$games = @($catalog.Games | Where-Object { $_.Title -eq 'Final Fantasy VII Remake' })
if ($games.Count -ne 1) { throw 'Expected exactly one Final Fantasy VII Remake entry; review the companion catalog first.' }
$game = $games[0]
$path = [IO.Path]::GetFullPath($game.Exe)
$roots = @('A:\Gamez\', 'B:\Gamez\', 'D:\Gamez\', 'E:\Gamez\')
if (-not ($roots | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })) {
    throw 'The selected game is outside the companion library roots.'
}
if ([IO.Path]::GetExtension($path) -ne '.exe' -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Game executable missing: $path"
}
if (@($catalog.Games | Where-Object { $_.Id -eq '1' -and $_.Exe -ne $game.Exe }).Count) {
    throw 'Game ID 1 already belongs to another game; preserving that mapping.'
}
if ([string]$catalog.Token -notmatch '^[A-Fa-f0-9]{64}$') { throw 'Unexpected companion token format; preserve and review the existing configuration.' }
Write-Host "Connecting FlXtR to this game: $path"
$metadata = Get-Item -LiteralPath $path
$hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
$processes = @(Get-Process -Name 'FlXtR.Gamez' -ErrorAction SilentlyContinue)
foreach ($process in $processes) {
    if ($process.Path -ne $executable) { throw 'A companion is running from another location; close it and retry.' }
}
$prefix = "http://${HostAddress}:47777/"
$reservation = & netsh.exe http show urlacl "url=$prefix" 2>&1
if (-not (($reservation | Out-String).Contains($prefix))) {
    & netsh.exe http add urlacl "url=$prefix" "user=$($identity.Name)" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Could not reserve the companion HTTP listener.' }
}
$ruleName = 'FlXtR.Gamez.SteamLink.47777'
if (Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue) {
    Set-NetFirewallRule -Name $ruleName -Enabled True -Direction Inbound -Action Allow -Profile Any | Out-Null
    Get-NetFirewallRule -Name $ruleName | Get-NetFirewallAddressFilter | Set-NetFirewallAddressFilter -LocalAddress $HostAddress -RemoteAddress $SteamLinkAddress | Out-Null
} else {
    New-NetFirewallRule -Name $ruleName -DisplayName 'FlXtR Gamez from Steam Link' -Direction Inbound -Action Allow -Profile Any -Protocol TCP -LocalAddress $HostAddress -LocalPort 47777 -RemoteAddress $SteamLinkAddress | Out-Null
}
foreach ($process in $processes) {
    Stop-Process -Id $process.Id -Force
    Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
}
# Re-read after the scanner has stopped, preserving discoveries saved meanwhile.
$catalog = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$game = @($catalog.Games | Where-Object { $_.Exe -eq $path })
if ($game.Count -ne 1) { throw 'The selected catalog entry changed during setup; restart the companion and review it.' }
$game = $game[0]
$backup = Join-Path $directory ('catalog.before-steamlink.' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json')
Copy-Item -LiteralPath $config -Destination $backup
$game.Id = '1'
$game.Approved = $true
$game | Add-Member -NotePropertyName Hash -NotePropertyValue $hash -Force
$game | Add-Member -NotePropertyName ReviewedLength -NotePropertyValue $metadata.Length -Force
$game | Add-Member -NotePropertyName ReviewedWriteTime -NotePropertyValue $metadata.LastWriteTimeUtc.Ticks -Force
$catalog.Bind = $HostAddress
$catalog.Port = 47777
$catalog.AllowedClients = @($SteamLinkAddress, $HostAddress, '127.0.0.1')
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$config.next", ($catalog | ConvertTo-Json -Depth 12), $encoding)
Move-Item -LiteralPath "$config.next" -Destination $config -Force
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name 'FlXtR Gamez' -Value ('"' + $executable + '" --background')
Start-Process -FilePath $executable -ArgumentList '--background' -WindowStyle Hidden
$ready = $false
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    try {
        $reply = Invoke-RestMethod -Uri ($prefix + 'v1/games') -Headers @{ Authorization = ('Bearer ' + $catalog.Token) } -TimeoutSec 2
        if (@($reply.games | Where-Object id -eq '1').Count -eq 1) { $ready = $true; break }
    } catch { Start-Sleep -Milliseconds 300 }
}
if (-not $ready) { throw 'Companion did not become ready; the catalog backup was preserved.' }
$link = Join-Path $directory 'steamlink-setup'
New-Item -ItemType Directory -Path $link -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $link 'gamez-token'), ($catalog.Token + "`n"), $encoding)
$deviceConfig = "GAMEZ_HOST=${HostAddress}:47777`nGAMEZ_TOKEN_FILE=/home/apps/greenlink/gamez-token`nGAMEZ_SUNSHINE_APP='Desktop_1080'`n"
[IO.File]::WriteAllText((Join-Path $link 'gamez-bridge.conf'), $deviceConfig, $encoding)
$archive = Join-Path $link 'gamez-link-setup.tar'
& tar.exe -cf $archive -C $link gamez-token gamez-bridge.conf
if ($LASTEXITCODE -ne 0) { throw 'Could not package the private device configuration.' }
Write-Host 'Companion ready. Copying its private connection settings to the Steam Link.'
Write-Host 'Enter the Steam Link root password if SSH asks; do not paste it into chat.'
& scp.exe -O $archive "root@${SteamLinkAddress}:/home/apps/greenlink/gamez-link-setup.tar"
if ($LASTEXITCODE -ne 0) { throw 'Companion is configured, but copying the device settings failed. Retry this script to complete it.' }
Write-Host 'Setup uploaded. Reply done in Codex so the Steam Link side can be activated.'
