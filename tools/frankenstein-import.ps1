# Run on Windows PowerShell from a freshly cloned Frankenstein worktree.
# Only operates on the current experimental checkout; source trees remain read-only.
$ErrorActionPreference = "Stop"
$repo = (Get-Location).Path
if ((git rev-parse --is-inside-work-tree 2>$null) -ne "true") { throw "Run inside a Git worktree." }
$branch = (git branch --show-current).Trim()
if ($branch -ne "experiment/frankenstein-20261111") { throw "Wrong branch: $branch" }
if ((git status --porcelain).Count -gt 0) { throw "Experimental worktree is not clean. Start with a fresh clone." }

$root = Join-Path $HOME "Documents\Codex"
$experiments = [ordered]@{
  "PSP" = "2026-11-02\referenced-chatgpt-conversation-this-is-an\work\psp-rewrite"
  "Android" = "2026-11-02\referenced-chatgpt-conversation-this-is-an\work\desktop-release"
  "MTV" = "2026-10-26\continuing-from-check-latest-commits-chatgpt\work\FlXtR-Steamlink"
  "Audio" = "2026-10-21\i\work\FlXtR-Steamlink"
}
$artifacts = Join-Path $HOME "Documents\FRANKEN-Patches"
New-Item -ItemType Directory -Force $artifacts | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
foreach ($label in $experiments.Keys) {
  $source = Join-Path $root $experiments[$label]
  Write-Host "\n=== $label ===" -ForegroundColor Cyan
  if (!(Test-Path (Join-Path $source ".git"))) {
    $report.Add("$label : SOURCE MISSING")
    continue
  }
  $patch = Join-Path $artifacts "$label.patch"
  git -C $source diff HEAD --binary "--output=$patch"
  if ($LASTEXITCODE -ne 0) { throw "Could not export $label patch" }
  # Include files never tracked by Git, including new tests and docs.
  $untracked = @(git -C $source -c core.quotePath=false ls-files --others --exclude-standard)
  if ($LASTEXITCODE -ne 0) { throw "Could not list untracked files in $label" }
  $collisions = @()
  foreach ($relative in $untracked) {
    $destination = Join-Path $repo $relative
    if (Test-Path -LiteralPath $destination) {
      $fromHash = (Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash
      $toHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
      if ($fromHash -ne $toHash) { $collisions += $relative }
    }
  }
  if ($collisions.Count) {
    $collisions | Set-Content (Join-Path $artifacts "$label-collisions.txt")
    $report.Add("$label : SKIPPED (untracked file collisions)")
    continue
  }
  $hasPatch = (Get-Item $patch).Length -gt 0
  if ($hasPatch) {
    # Git's 3-way mode uses common ancestors, preserving newer work when possible.
    $applyOutput = @(git apply --3way --index $patch 2>&1)
    if ($LASTEXITCODE -ne 0) {
      $applyOutput | Out-File (Join-Path $artifacts "$label-conflict.txt")
      # Safe here because this is the clean, dedicated experimental clone.
      git reset --hard HEAD | Out-Null
      $report.Add("$label : SKIPPED (patch conflict; see conflict report)")
      continue
    }
  }
  foreach ($relative in $untracked) {
    $from = Join-Path $source $relative
    $to = Join-Path $repo $relative
    if (!(Test-Path -LiteralPath $to)) {
      New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
      Copy-Item -LiteralPath $from -Destination $to
    }
  }
  git add -A
  if ($LASTEXITCODE -ne 0) { throw "Stage failed in $label" }
  git diff --cached --check
  if ($LASTEXITCODE -ne 0) { throw "Whitespace validation failed in $label" }
  git diff --cached --quiet
  if ($LASTEXITCODE -eq 0) {
    $report.Add("$label : ALREADY PRESENT / NO CHANGE")
  } else {
    git commit -m "Frankenstein: integrate $label experiment"
    if ($LASTEXITCODE -ne 0) { throw "Commit failed in $label" }
    $report.Add("$label : APPLIED AND COMMITTED LOCALLY")
  }
}
$report | Tee-Object -FilePath (Join-Path $artifacts "report.txt")
Write-Host "\nExperiment head:" -ForegroundColor Green
git log -5 --oneline
Write-Host "\nNext: review report, then push with git push -u origin experiment/frankenstein-20261111"
