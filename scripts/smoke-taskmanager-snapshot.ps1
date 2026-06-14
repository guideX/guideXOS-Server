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
taskmgr
taskmanager.snapshot
quit
"@
$output = $commandInput | & $ServerExe 2>&1

$checks = @(
    @{ Name = "task manager launch"; Match = ($output -match "Task Manager launched, pid=\d+") },
    @{ Name = "snapshot command present"; Match = ($output -match "tabs=Processes,Performance,Tombstoned,Memory Details") },
    @{ Name = "title present"; Match = ($output -match "title=Task Manager") },
    @{ Name = "process columns present"; Match = ($output -match "processColumns=Name,CPU%,Memory,Disk%,Network%") },
    @{ Name = "performance categories present"; Match = ($output -match "performanceCategories=CPU,Memory,Disk,Network") },
    @{ Name = "memory details sections present"; Match = ($output -match "memoryDetailsSections=Memory Allocator Details;Free\(\) Call Statistics;Heap Allocator") },
    @{ Name = "tombstoned columns present"; Match = ($output -match "tombstonedColumns=Name,PID,App ID,Reason,Restore,End") },
    @{ Name = "tombstoned restore support present"; Match = ($output -match "tombstonedRestoreSupported=(true|false|N/A)") },
    @{ Name = "tombstoned end support present"; Match = ($output -match "tombstonedEndSupported=(true|false|N/A)") },
    @{ Name = "process count present"; Match = ($output -match "processes=\d+") },
    @{ Name = "memory used present"; Match = ($output -match "memoryUsed=\d+") },
    @{ Name = "memory total derived"; Match = ($output -match "memoryTotalDerived=true") },
    @{ Name = "memory total source"; Match = ($output -match "memoryTotalSource=allocatorHeap") },
    @{ Name = "cpu available field present"; Match = ($output -match "cpuAvailable=(true|false)") },
    @{ Name = "cpu source field present"; Match = ($output -match "cpuSource=(N/A|[^\s\r\n]+)") },
    @{ Name = "cpu sample window field present"; Match = ($output -match "cpuSampleWindowMs=(N/A|\d+)") },
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

$cpuAvailable = $output -match "cpuAvailable=true"
$cpuUnavailable = $output -match "cpuAvailable=false"
$cpuValueMatch = [regex]::Match($output, "cpu=(?<value>\d+)%")
$cpuSourceMatch = [regex]::Match($output, "cpuSource=(?<source>[^\s\r\n]+)")
$cpuSampleWindowMatch = [regex]::Match($output, "cpuSampleWindowMs=(?<window>N/A|\d+)")
$cpuBusyMatch = [regex]::Match($output, "cpuBusyTimeMs=(?<busy>N/A|\d+)")
$cpuIdleMatch = [regex]::Match($output, "cpuIdleTimeMs=(?<idle>N/A|\d+)")

if ($cpuAvailable) {
    if (-not $cpuValueMatch.Success) {
        throw "CPU availability was reported but cpu=<value>% was missing."
    }

    $cpuValue = [int]$cpuValueMatch.Groups["value"].Value
    if ($cpuValue -lt 0 -or $cpuValue -gt 100) {
        throw "CPU utilization out of range: $cpuValue"
    }

    if (-not $cpuSourceMatch.Success) {
        throw "CPU availability was reported but cpuSource was missing."
    }

    $cpuSource = $cpuSourceMatch.Groups["source"].Value
    if ($cpuSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "CPU source looks synthetic: $cpuSource"
    }

    if (-not $cpuSampleWindowMatch.Success -or $cpuSampleWindowMatch.Groups["window"].Value -eq "N/A") {
        throw "CPU availability was reported but cpuSampleWindowMs was missing."
    }

    $cpuSampleWindow = [int]$cpuSampleWindowMatch.Groups["window"].Value
    if ($cpuSampleWindow -le 0) {
        throw "CPU sample window must be positive when available: $cpuSampleWindow"
    }

    if (-not $cpuBusyMatch.Success -or $cpuBusyMatch.Groups["busy"].Value -eq "N/A") {
        throw "CPU availability was reported but cpuBusyTimeMs was missing."
    }
    if (-not $cpuIdleMatch.Success -or $cpuIdleMatch.Groups["idle"].Value -eq "N/A") {
        throw "CPU availability was reported but cpuIdleTimeMs was missing."
    }
} elseif ($cpuUnavailable) {
    if (-not ($output -match "cpu=N/A")) {
        throw "CPU was unavailable but cpu=N/A was not reported."
    }
    if (-not ($output -match "cpuSource=N/A")) {
        throw "CPU was unavailable but cpuSource=N/A was not reported."
    }
} else {
    throw "CPU availability flag was missing."
}

Write-Host "Task Manager snapshot smoke PASS"
Write-Host $output
exit 0
