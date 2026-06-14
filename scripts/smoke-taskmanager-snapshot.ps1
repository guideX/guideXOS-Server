param(
    [switch]$Build,
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ServerExe = Join-Path $Root "guideXOSServer.exe"

if ($Build) {
    & (Join-Path $Root "build-kernel.bat")
}

if (-not (Test-Path $ServerExe)) {
    throw "Server executable not found: $ServerExe"
}

$commandInput = @"
taskmanager.snapshot
quit
"@
$output = $commandInput | & $ServerExe 2>&1

$checks = @(
    @{ Name = "snapshot command present"; Match = ($output -match "tabs=Processes,Performance,Tombstoned,Memory Details") },
    @{ Name = "title present"; Match = ($output -match "title=Task Manager") },
    @{ Name = "process count present"; Match = ($output -match "processes=\d+") },
    @{ Name = "memory used present"; Match = ($output -match "memoryUsed=\d+") },
    @{ Name = "memory total derived"; Match = ($output -match "memoryTotalDerived=true") },
    @{ Name = "memory total source"; Match = ($output -match "memoryTotalSource=allocatorHeap") },
    @{ Name = "disk unavailable"; Match = ($output -match "disk=N/A") },
    @{ Name = "network unavailable"; Match = ($output -match "network=N/A") },
    @{ Name = "synthetic counters disabled"; Match = ($output -match "syntheticCounters=false") }
)

$failed = @()
foreach ($check in $checks) {
    if (-not $check.Match) {
        $failed += $check.Name
    }
}

if ($failed.Count -gt 0) {
    Write-Host "Task Manager snapshot smoke FAIL" -ForegroundColor Red
    Write-Host $output
    foreach ($item in $failed) {
        Write-Host "Missing/failed: $item" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Task Manager snapshot smoke PASS"
Write-Host $output
exit 0
