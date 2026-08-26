$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$smoke = Join-Path $PSScriptRoot "smoke-navigator-hosted.ps1"

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $smoke
$smokeExit = $LASTEXITCODE

$latest = Get-ChildItem (Join-Path $root "logs") -Filter "navigator-hosted-smoke-*.log" |
    Where-Object { $_.Name -notlike "*.err.log" -and $_.Name -notlike "*.in" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$required = @(
    "CHECK PASS CSS phase 2I same-document recomputation preserves bounded focus state",
    "CHECK PASS CSS phase 2I reload clears focus, runtime state, and pressed input",
    "CHECK PASS CSS phase 2I generated Page Info and Save Page Text preserve source ownership",
    "CHECK PASS CSS phase 2I history is URL-only and non-persistent",
    "CHECK PASS CSS phase 2I redirect replacement owns final document and blocks stale activation",
    "CHECK PASS CSS phase 2I local-file, failure, and parser-recovery ownership",
    "NAVIGATOR_SMOKE_RESULT: PASS"
)

$pass = $smokeExit -eq 0 -and $null -ne $latest
if ($pass) {
    $text = Get-Content $latest.FullName -Raw
    foreach ($marker in $required) {
        if ($text.IndexOf($marker, [System.StringComparison]::Ordinal) -lt 0) {
            $pass = $false
            Write-Host "Missing Phase 2I marker: $marker"
        }
    }
}

if ($pass) {
    Write-Output "NAVIGATOR_PHASE2I_RESULT: PASS"
    exit 0
}

Write-Output "NAVIGATOR_PHASE2I_RESULT: FAIL"
exit 1
