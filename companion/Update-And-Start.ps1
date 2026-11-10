param([switch]$NoStart, [string]$PackagePath, [string]$ExpectedSha, [string]$Version)
$ErrorActionPreference = 'Stop'
$root = Join-Path $env:LOCALAPPDATA 'FlXtR\Gamez'
$app = Join-Path $root 'app'
$backup = Join-Path $root 'app.previous'
New-Item -ItemType Directory -Path $root -Force | Out-Null
function Assert-ManagedPath([string]$Path) {
    $base = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    if (-not [IO.Path]::GetFullPath($Path).StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Update path is outside Gamez storage.' }
}
function Install-Package([string]$Zip, [string]$Sha, [string]$Commit) {
    if ($Sha -notmatch '^[a-fA-F0-9]{64}$' -or $Commit -notmatch '^[a-f0-9]{40}$') { throw 'Invalid release metadata.' }
    if ((Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash -ne $Sha) { throw 'Gamez package checksum mismatch.' }
    $stage = Join-Path $root ('.update-' + [Guid]::NewGuid().ToString('N'))
    Assert-ManagedPath $stage
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
        try {
            $prefix = [IO.Path]::GetFullPath($stage).TrimEnd('\') + '\'
            foreach ($entry in $archive.Entries) {
                $target = [IO.Path]::GetFullPath((Join-Path $stage $entry.FullName))
                if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe package entry.' }
            }
        } finally { $archive.Dispose() }
        [IO.Compression.ZipFile]::ExtractToDirectory($Zip, $stage)
        $candidate = Join-Path $stage 'FlXtR.Gamez.exe'
        $metadata = Get-Content -LiteralPath (Join-Path $stage 'gamez-build.json') -Raw | ConvertFrom-Json
        if ($metadata.commit -ne $Commit -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw 'Incomplete Gamez package.' }
        $result = Join-Path $stage 'self-test.json'
        $test = Start-Process -FilePath $candidate -ArgumentList @('--session-self-test', ('"' + $result + '"')) -WindowStyle Hidden -PassThru
        if (-not $test.WaitForExit(30000)) { $test.Kill(); throw 'Gamez lifecycle self-test timed out.' }
        $test.Refresh()
        if ($test.ExitCode -ne 0 -or -not (Get-Content -LiteralPath $result -Raw | ConvertFrom-Json).ok) { throw 'Gamez lifecycle self-test failed.' }
        if (Get-Process -Name 'FlXtR.Gamez' -ErrorAction SilentlyContinue) { throw 'Close the running companion before installing.' }
        Assert-ManagedPath $backup
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
        Assert-ManagedPath $app
        if (Test-Path -LiteralPath $app) { Move-Item -LiteralPath $app -Destination $backup }
        try {
            Assert-ManagedPath $stage; Assert-ManagedPath $app
            Move-Item -LiteralPath $stage -Destination $app
        } catch {
            if (Test-Path -LiteralPath $backup) {
                Assert-ManagedPath $backup; Assert-ManagedPath $app
                Move-Item -LiteralPath $backup -Destination $app
            }
            throw
        }
        Copy-Item -LiteralPath (Join-Path $app 'Update-And-Start.ps1') -Destination (Join-Path $root 'Update-And-Start.ps1') -Force
        Write-Output 'Installed verified Gamez package; previous app retained for rollback.'
    } finally {
        if (Test-Path -LiteralPath $stage) { Assert-ManagedPath $stage; Remove-Item -LiteralPath $stage -Recurse -Force }
    }
}
if ($PackagePath) {
    Install-Package $PackagePath $ExpectedSha $Version
} else {
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $release = Invoke-RestMethod 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/gamez-updates/gamez-manifest.json' -TimeoutSec 8
        $installed = $null
        if (Test-Path -LiteralPath (Join-Path $app 'gamez-build.json')) { $installed = Get-Content (Join-Path $app 'gamez-build.json') -Raw | ConvertFrom-Json }
        if ($installed.commit -ne $release.version) {
            $expectedUrl = 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/gamez-' + $release.version + '/FlXtR-Gamez-win-x64.zip'
            if ($release.url -ne $expectedUrl) { throw 'Unexpected Gamez download URL.' }
            $download = Join-Path $root 'gamez-update.zip'
            Invoke-WebRequest -UseBasicParsing -Uri $release.url -OutFile $download -TimeoutSec 180
            Install-Package $download $release.sha256 $release.version
        }
    } catch { Add-Content -LiteralPath (Join-Path $root 'updater.log') -Value ((Get-Date).ToString('s') + ' ' + $_.Exception.Message) }
}
if (-not $NoStart) {
    $process = Start-Process -FilePath (Join-Path $app 'FlXtR.Gamez.exe') -ArgumentList '--background' -WindowStyle Hidden -PassThru
    $process.WaitForExit(); $process.Refresh()
    exit $process.ExitCode
}
