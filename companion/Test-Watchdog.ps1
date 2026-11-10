param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Updater,[switch]$Worker,[string]$TestRoot)
$ErrorActionPreference='Stop'
if($Worker){
    $root=$TestRoot
    $app=Split-Path -Parent $Executable
    $NoStart=$false
    function Start-Process {
        param($FilePath,$ArgumentList,$WindowStyle,[switch]$PassThru)
        if($FilePath -ne $Executable){throw 'Unexpected watchdog child path'}
        Microsoft.PowerShell.Management\Start-Process -FilePath $Executable -ArgumentList @('--watchdog-test-worker',('"'+$TestRoot+'"')) -WindowStyle Hidden -PassThru
    }
    function Invoke-RestMethod {
        param($Uri,$Headers,$TimeoutSec)
        $attempt=[int](Get-Content -LiteralPath (Join-Path $TestRoot 'attempts.txt'))
        [pscustomobject]@{ui_age_ms=$(if($attempt -eq 2){70000}else{0});launching_for_ms=0}
    }
    $tokens=$null;$errors=$null
    $ast=[System.Management.Automation.Language.Parser]::ParseFile($Updater,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors|Out-String)}
    $body=@($ast.EndBlock.Statements | Where-Object {$_ -is [System.Management.Automation.Language.IfStatementAst] -and $_.Extent.Text.Contains('$restarts = @()')})
    if($body.Count -ne 1){throw 'Watchdog body not found'}
    & ([scriptblock]::Create($body[0].Extent.Text))
    exit
}
$Executable=[IO.Path]::GetFullPath($Executable);$Updater=[IO.Path]::GetFullPath($Updater)
$TestRoot=Join-Path $env:TEMP ('FlXtR-watchdog-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestRoot | Out-Null
[IO.File]::WriteAllText((Join-Path $TestRoot 'catalog.json'),'{"Bind":"127.0.0.1","Port":1,"Token":"test-token"}')
$supervisor=$null
try {
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Worker','-Executable',('"'+$Executable+'"'),'-Updater',('"'+$Updater+'"'),'-TestRoot',('"'+$TestRoot+'"'))
    $supervisor=Microsoft.PowerShell.Management\Start-Process powershell.exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline=(Get-Date).AddSeconds(45)
    while(-not(Test-Path (Join-Path $TestRoot 'pid-3.txt')) -and (Get-Date) -lt $deadline){Start-Sleep -Milliseconds 200}
    if(-not(Test-Path (Join-Path $TestRoot 'pid-3.txt'))){throw 'Watchdog did not recover both exit and hung UI'}
    foreach($attempt in 1,2){
        $childId=[int](Get-Content (Join-Path $TestRoot "pid-$attempt.txt"))
        if(Get-Process -Id $childId -ErrorAction SilentlyContinue){throw "Failed child $attempt is still running"}
    }
    $childId=[int](Get-Content (Join-Path $TestRoot 'pid-3.txt'))
    if(-not(Get-Process -Id $childId -ErrorAction SilentlyContinue)){throw 'Recovered child is not running'}
    Write-Output 'PASS: real PowerShell supervisor restarted an exited child, killed only its hung child, and started a healthy replacement.'
} finally {
    if($supervisor -and -not $supervisor.HasExited){Stop-Process -Id $supervisor.Id -Force}
    foreach($file in @(Get-ChildItem -LiteralPath $TestRoot -Filter 'pid-*.txt')){
        $childId=[int](Get-Content -LiteralPath $file.FullName)
        $child=Get-Process -Id $childId -ErrorAction SilentlyContinue
        if($child -and $child.Path -eq $Executable){Stop-Process -Id $childId -Force}
    }
    $absolute=[IO.Path]::GetFullPath($TestRoot);$allowed=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
    if(-not $absolute.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected test cleanup path'}
    Remove-Item -LiteralPath $absolute -Recurse -Force
}
