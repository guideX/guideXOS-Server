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
taskmanager.tombstone-test
taskmanager.snapshot
quit
"@
$output = $commandInput | & $ServerExe 2>&1
$output = $output | Out-String

$checks = @(
    @{ Name = "task manager launch"; Match = ($output -match "Task Manager launched, pid=\d+") },
    @{ Name = "snapshot command present"; Match = ($output -match "tabs=Processes,Performance,Tombstoned,Memory Details") },
    @{ Name = "title present"; Match = ($output -match "title=Task Manager") },
    @{ Name = "process columns present"; Match = ($output -match "processColumns=Name,CPU%,Memory,Disk%,Network%") },
    @{ Name = "performance categories present"; Match = ($output -match "performanceCategories=CPU,Memory,Disk,Network") },
    @{ Name = "memory details sections present"; Match = ($output -match "memoryDetailsSections=Memory Allocator Details;Free\(\) Call Statistics;Heap Allocator") },
    @{ Name = "tombstone details available"; Match = ($output -match "(?m)^\s*tombstoneDetailsAvailable=true\s*$") },
    @{ Name = "tombstone diagnostic history available"; Match = ($output -match "(?m)^\s*tombstoneDiagnosticHistoryAvailable=true\s*$") },
    @{ Name = "app tombstone policy available"; Match = ($output -match "(?m)^\s*appTombstonePolicyAvailable=true\s*$") },
    @{ Name = "tombstone capability source present"; Match = ($output -match "(?m)^\s*tombstoneCapabilitySource=appModelMetadata\s*$") },
    @{ Name = "tombstone restore implemented false"; Match = ($output -match "(?m)^\s*tombstoneRestoreImplemented=false\s*$") },
    @{ Name = "tombstone history capacity present"; Match = ($output -match "(?m)^\s*tombstoneHistoryCapacity=\d+\s*$") },
    @{ Name = "tombstone app capability known present"; Match = ($output -match "(?m)^\s*tombstoneAppCapabilityKnown=\d+\s*$") },
    @{ Name = "tombstone columns present"; Match = ($output -match "tombstoneColumns=Name,PID,App ID,Reason,Exit,Runtime,Restore,End") },
    @{ Name = "tombstone reason values present"; Match = ($output -match "tombstoneReasonValues=NormalExit,Terminated,Crashed,Unknown") },
    @{ Name = "tombstone restore support present"; Match = ($output -match "(?m)^\s*tombstoneRestoreSupported=false\s*$") },
    @{ Name = "tombstone end support present"; Match = ($output -match "(?m)^\s*tombstoneEndSupported=false\s*$") },
    @{ Name = "tombstoned columns present"; Match = ($output -match "tombstonedColumns=Name,PID,App ID,Reason,Exit,Runtime,Restore,End") },
    @{ Name = "tombstoned restore support present"; Match = ($output -match "(?m)^\s*tombstonedRestoreSupported=false\s*$") },
    @{ Name = "tombstoned end support present"; Match = ($output -match "(?m)^\s*tombstonedEndSupported=false\s*$") },
    @{ Name = "process count present"; Match = ($output -match "processes=\d+") },
    @{ Name = "memory used present"; Match = ($output -match "memoryUsed=\d+") },
    @{ Name = "memory total derived"; Match = ($output -match "memoryTotalDerived=true") },
    @{ Name = "memory total source"; Match = ($output -match "memoryTotalSource=allocatorHeap") },
    @{ Name = "cpu available field present"; Match = ($output -match "(?m)^\s*cpuAvailable=(true|false)\s*$") },
    @{ Name = "cpu source field present"; Match = ($output -match "(?m)^\s*cpuSource=(N/A|[^\s\r\n]+)\s*$") },
    @{ Name = "cpu sample window field present"; Match = ($output -match "(?m)^\s*cpuSampleWindowMs=(N/A|\d+)\s*$") },
    @{ Name = "process cpu available field present"; Match = ($output -match "(?m)^\s*processCpuAvailable=(true|false)\s*$") },
    @{ Name = "process cpu source field present"; Match = ($output -match "(?m)^\s*processCpuSource=(N/A|[^\s\r\n]+)\s*$") },
    @{ Name = "process cpu sample window field present"; Match = ($output -match "(?m)^\s*processCpuSampleWindowMs=(N/A|\d+)\s*$") },
    @{ Name = "process cpu rows field present"; Match = ($output -match "(?m)^\s*processCpuRowsWithCpu=\d+\s*$") },
    @{ Name = "disk unavailable"; Match = ($output -match "disk=N/A") },
    @{ Name = "network unavailable"; Match = ($output -match "network=N/A") },
    @{ Name = "synthetic counters disabled"; Match = ($output -match "syntheticCounters=false") },
    @{ Name = "deterministic tombstone smoke passed"; Match = ($output -match "(?m)^\s*tombstoneTest=passed\s*$") },
    @{ Name = "deterministic tombstone reason normal exit"; Match = ($output -match "(?m)^\s*tombstoneReason=NormalExit\s*$") },
    @{ Name = "deterministic tombstone restore unsupported"; Match = ($output -match "(?m)^\s*restoreSupported=false\s*$") },
    @{ Name = "deterministic tombstone history count present"; Match = ($output -match "(?m)^\s*tombstoneHistoryCount=\d+\s*$") },
    @{ Name = "deterministic tombstone row present"; Match = ($output -match "(?m)^\s*tombstoneRow pid=\d+ displayName=taskmanager\.tombstone-test .* reason=NormalExit .* appTombstoneCapable=N/A .* restoreSupported=false.*$") }
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

$capacityMatch = [regex]::Match($output, "(?m)^\s*tombstoneHistoryCapacity=(?<capacity>\d+)\s*$")
if (-not $capacityMatch.Success) {
    throw "Tombstone history capacity was missing."
}
$capacity = [int]$capacityMatch.Groups["capacity"].Value
if ($capacity -le 0) {
    throw "Tombstone history capacity must be greater than zero: $capacity"
}
if ($capacity -gt 64) {
    throw "Tombstone history capacity must remain bounded at 64 or below: $capacity"
}

$reasonValuesMatch = [regex]::Match($output, "(?m)^\s*tombstoneReasonValues=(?<values>[^\r\n]+)\s*$")
if (-not $reasonValuesMatch.Success) {
    throw "Tombstone reason values were missing."
}
$reasonValues = $reasonValuesMatch.Groups["values"].Value
if ($reasonValues -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
    throw "Tombstone reason values look synthetic: $reasonValues"
}

$cpuAvailable = $output -match "(?m)^\s*cpuAvailable=true\s*$"
$cpuUnavailable = $output -match "(?m)^\s*cpuAvailable=false\s*$"
$cpuValueMatch = [regex]::Match($output, "(?m)^\s*cpu=(?<value>\d+)%\s*$")
$cpuSourceMatch = [regex]::Match($output, "(?m)^\s*cpuSource=(?<source>[^\s\r\n]+)\s*$")
$cpuSampleWindowMatch = [regex]::Match($output, "(?m)^\s*cpuSampleWindowMs=(?<window>N/A|\d+)\s*$")
$cpuBusyMatch = [regex]::Match($output, "(?m)^\s*cpuBusyTimeMs=(?<busy>N/A|\d+)\s*$")
$cpuIdleMatch = [regex]::Match($output, "(?m)^\s*cpuIdleTimeMs=(?<idle>N/A|\d+)\s*$")
$processCpuAvailable = $output -match "(?m)^\s*processCpuAvailable=true\s*$"
$processCpuUnavailable = $output -match "(?m)^\s*processCpuAvailable=false\s*$"
$processCpuSourceMatch = [regex]::Match($output, "(?m)^\s*processCpuSource=(?<source>[^\s\r\n]+)\s*$")
$processCpuSampleWindowMatch = [regex]::Match($output, "(?m)^\s*processCpuSampleWindowMs=(?<window>N/A|\d+)\s*$")
$processCpuRowsMatch = [regex]::Match($output, "(?m)^\s*processCpuRowsWithCpu=(?<rows>\d+)\s*$")
$processCpuPctMatches = [regex]::Matches($output, "processRow .*?cpuPct=(?<value>\d+)%")

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
    if ($cpuSampleWindow -lt 500) {
        throw "CPU sample window is too short for a stable display sample: $cpuSampleWindow"
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
    if (-not $cpuSourceMatch.Success) {
        throw "CPU was unavailable but cpuSource was missing."
    }

    $cpuSource = $cpuSourceMatch.Groups["source"].Value
    if ($cpuSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "CPU source looks synthetic: $cpuSource"
    }
    if ($cpuSource -notmatch '(?i)warmup' -and $cpuSource -ne "N/A") {
        throw "CPU was unavailable but cpuSource did not indicate warmup: $cpuSource"
    }

    if ($cpuSampleWindowMatch.Success -and $cpuSampleWindowMatch.Groups["window"].Value -ne "N/A") {
        $cpuSampleWindow = [int]$cpuSampleWindowMatch.Groups["window"].Value
        if ($cpuSampleWindow -lt 0) {
            throw "CPU warmup sample window must not be negative: $cpuSampleWindow"
        }
        if ($cpuSampleWindow -ge 500) {
            throw "CPU warmup sample window was unexpectedly large: $cpuSampleWindow"
        }
    }
} else {
    throw "CPU availability flag was missing."
}

if ($processCpuAvailable) {
    if (-not $processCpuSourceMatch.Success) {
        throw "Process CPU availability was reported but processCpuSource was missing."
    }

    $processCpuSource = $processCpuSourceMatch.Groups["source"].Value
    if ($processCpuSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "Process CPU source looks synthetic: $processCpuSource"
    }

    if (-not $processCpuSampleWindowMatch.Success -or $processCpuSampleWindowMatch.Groups["window"].Value -eq "N/A") {
        throw "Process CPU availability was reported but processCpuSampleWindowMs was missing."
    }

    $processCpuSampleWindow = [int]$processCpuSampleWindowMatch.Groups["window"].Value
    if ($processCpuSampleWindow -le 0) {
        throw "Process CPU sample window must be positive when available: $processCpuSampleWindow"
    }
    if ($processCpuSampleWindow -lt 500) {
        throw "Process CPU sample window is too short for a stable display sample: $processCpuSampleWindow"
    }

    if (-not $processCpuRowsMatch.Success) {
        throw "Process CPU availability was reported but processCpuRowsWithCpu was missing."
    }
    $processCpuRows = [int]$processCpuRowsMatch.Groups["rows"].Value
    if ($processCpuRows -le 0) {
        throw "Process CPU availability was reported but no process rows had CPU data."
    }
    if ($processCpuPctMatches.Count -ne $processCpuRows) {
        throw "Process CPU rows with data did not match the emitted CPU percentages: rows=$processCpuRows emitted=$($processCpuPctMatches.Count)"
    }
    foreach ($match in $processCpuPctMatches) {
        $value = [int]$match.Groups["value"].Value
        if ($value -lt 0 -or $value -gt 100) {
            throw "Per-process CPU utilization out of range: $value"
        }
    }
} elseif ($processCpuUnavailable) {
    if (-not $processCpuSourceMatch.Success) {
        throw "Process CPU was unavailable but processCpuSource was missing."
    }

    $processCpuSource = $processCpuSourceMatch.Groups["source"].Value
    if ($processCpuSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "Process CPU source looks synthetic: $processCpuSource"
    }
    if ($processCpuSource -ne "N/A" -and $processCpuSource -notmatch '(?i)warmup') {
        throw "Process CPU was unavailable but source did not indicate warmup: $processCpuSource"
    }

    if ($processCpuRowsMatch.Success) {
        $processCpuRows = [int]$processCpuRowsMatch.Groups["rows"].Value
        if ($processCpuRows -ne 0) {
            throw "Process CPU was unavailable but processCpuRowsWithCpu was nonzero: $processCpuRows"
        }
    }

    if ($processCpuSampleWindowMatch.Success -and $processCpuSampleWindowMatch.Groups["window"].Value -ne "N/A") {
        $processCpuSampleWindow = [int]$processCpuSampleWindowMatch.Groups["window"].Value
        if ($processCpuSampleWindow -lt 0) {
            throw "Process CPU warmup sample window must not be negative: $processCpuSampleWindow"
        }
        if ($processCpuSampleWindow -ge 500) {
            throw "Process CPU warmup sample window was unexpectedly large: $processCpuSampleWindow"
        }
    }
} else {
    throw "Process CPU availability flag was missing."
}

Write-Host "Task Manager snapshot smoke PASS"
Write-Host $output
exit 0
