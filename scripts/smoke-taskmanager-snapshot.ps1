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
    @{ Name = "task manager polish checkpoint present"; Match = ($output -match "(?m)^\s*taskManagerPolishCheckpoint=true\s*$") },
    @{ Name = "tombstone details available"; Match = ($output -match "(?m)^\s*tombstoneDetailsAvailable=true\s*$") },
    @{ Name = "tombstone diagnostic history available"; Match = ($output -match "(?m)^\s*tombstoneDiagnosticHistoryAvailable=true\s*$") },
    @{ Name = "app tombstone policy available"; Match = ($output -match "(?m)^\s*appTombstonePolicyAvailable=true\s*$") },
    @{ Name = "tombstone capability source present"; Match = ($output -match "(?m)^\s*tombstoneCapabilitySource=appModelMetadata\s*$") },
    @{ Name = "tombstone restore implemented false"; Match = ($output -match "(?m)^\s*tombstoneRestoreImplemented=false\s*$") },
    @{ Name = "tombstone history capacity present"; Match = ($output -match "(?m)^\s*tombstoneHistoryCapacity=\d+\s*$") },
    @{ Name = "tombstone app capability known present"; Match = ($output -match "(?m)^\s*tombstoneAppCapabilityKnown=1\s*$") },
    @{ Name = "tombstone rows with app id present"; Match = ($output -match "(?m)^\s*tombstoneRowsWithAppId=1\s*$") },
    @{ Name = "tombstone rows with policy present"; Match = ($output -match "(?m)^\s*tombstoneRowsWithPolicy=1\s*$") },
    @{ Name = "tombstone columns present"; Match = ($output -match "tombstoneColumns=Name,PID,App ID,Reason,Exit,Runtime,Restore,End") },
    @{ Name = "tombstone details pane present"; Match = ($output -match "(?m)^\s*tombstoneDetailsPane=true\s*$") },
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
    @{ Name = "disk available field present"; Match = ($output -match "(?m)^\s*diskAvailable=(true|false)\s*$") },
    @{ Name = "disk field present"; Match = ($output -match "(?m)^\s*disk=(N/A|\d+%)\s*$") },
    @{ Name = "disk source field present"; Match = ($output -match "(?m)^\s*diskSource=(N/A|[^\s\r\n]+)\s*$") },
    @{ Name = "disk sample window field present"; Match = ($output -match "(?m)^\s*diskSampleWindowMs=(N/A|\d+)\s*$") },
    @{ Name = "disk read rate field present"; Match = ($output -match "(?m)^\s*diskReadKBps=(N/A|\d+)\s*$") },
    @{ Name = "disk write rate field present"; Match = ($output -match "(?m)^\s*diskWriteKBps=(N/A|\d+)\s*$") },
    @{ Name = "disk active pct availability field present"; Match = ($output -match "(?m)^\s*diskActivePctAvailable=(true|false)\s*$") },
    @{ Name = "process disk availability field present"; Match = ($output -match "(?m)^\s*processDiskAvailable=(true|false)\s*$") },
    @{ Name = "network available field present"; Match = ($output -match "(?m)^\s*networkAvailable=(true|false)\s*$") },
    @{ Name = "network utilization availability field present"; Match = ($output -match "(?m)^\s*networkUtilizationAvailable=(true|false)\s*$") },
    @{ Name = "network source field present"; Match = ($output -match "(?m)^\s*networkSource=(N/A|[^\s\r\n]+)\s*$") },
    @{ Name = "network sample window field present"; Match = ($output -match "(?m)^\s*networkSampleWindowMs=(N/A|\d+)\s*$") },
    @{ Name = "network send rate field present"; Match = ($output -match "(?m)^\s*networkSendKBps=(N/A|\d+)\s*$") },
    @{ Name = "network receive rate field present"; Match = ($output -match "(?m)^\s*networkReceiveKBps=(N/A|\d+)\s*$") },
    @{ Name = "process network availability field present"; Match = ($output -match "(?m)^\s*processNetworkAvailable=(true|false)\s*$") },
    @{ Name = "unavailable graph label present"; Match = ($output -match "(?m)^\s*unavailableGraphLabel=N/A\s*$") },
    @{ Name = "synthetic counters disabled"; Match = ($output -match "syntheticCounters=false") },
    @{ Name = "deterministic tombstone smoke passed"; Match = ($output -match "(?m)^\s*tombstoneTest=passed\s*$") },
    @{ Name = "deterministic tombstone reason normal exit"; Match = ($output -match "(?m)^\s*tombstoneReason=NormalExit\s*$") },
    @{ Name = "deterministic tombstone app id propagated"; Match = ($output -match "(?m)^\s*appId=gxos\.builtin\.shutdowndialog\s*$") },
    @{ Name = "deterministic tombstone app capability known"; Match = ($output -match "(?m)^\s*appTombstoneCapabilityKnown=true\s*$") },
    @{ Name = "deterministic tombstone capability false"; Match = ($output -match "(?m)^\s*appTombstoneCapable=false\s*$") },
    @{ Name = "deterministic tombstone capability source"; Match = ($output -match "(?m)^\s*appTombstoneCapabilitySource=appModelMetadata\s*$") },
    @{ Name = "deterministic tombstone restore unsupported"; Match = ($output -match "(?m)^\s*restoreSupported=false\s*$") },
    @{ Name = "deterministic tombstone history count present"; Match = ($output -match "(?m)^\s*tombstoneHistoryCount=\d+\s*$") },
    @{ Name = "deterministic tombstone row present"; Match = ($output -match "(?m)^\s*tombstoneRow pid=\d+ displayName=ShutdownDialog appId=gxos\.builtin\.shutdowndialog reason=NormalExit .* appTombstoneCapable=false appTombstoneCapabilitySource=appModelMetadata restoreSupported=false.*$") }
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
$diskAvailable = $output -match "(?m)^\s*diskAvailable=true\s*$"
$diskUnavailable = $output -match "(?m)^\s*diskAvailable=false\s*$"
$diskValueMatch = [regex]::Match($output, "(?m)^\s*disk=(?<value>N/A|\d+%)\s*$")
$diskSourceMatch = [regex]::Match($output, "(?m)^\s*diskSource=(?<source>[^\s\r\n]+)\s*$")
$diskSampleWindowMatch = [regex]::Match($output, "(?m)^\s*diskSampleWindowMs=(?<window>N/A|\d+)\s*$")
$diskReadRateMatch = [regex]::Match($output, "(?m)^\s*diskReadKBps=(?<rate>N/A|\d+)\s*$")
$diskWriteRateMatch = [regex]::Match($output, "(?m)^\s*diskWriteKBps=(?<rate>N/A|\d+)\s*$")
$diskActivePctAvailable = $output -match "(?m)^\s*diskActivePctAvailable=true\s*$"
$diskActivePctUnavailable = $output -match "(?m)^\s*diskActivePctAvailable=false\s*$"
$processDiskAvailable = $output -match "(?m)^\s*processDiskAvailable=true\s*$"
$processDiskUnavailable = $output -match "(?m)^\s*processDiskAvailable=false\s*$"

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

if ($diskAvailable) {
    if (-not $diskSourceMatch.Success) {
        throw "Disk availability was reported but diskSource was missing."
    }

    $diskSource = $diskSourceMatch.Groups["source"].Value
    if ($diskSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "Disk source looks synthetic: $diskSource"
    }

    if (-not $diskSampleWindowMatch.Success -or $diskSampleWindowMatch.Groups["window"].Value -eq "N/A") {
        throw "Disk availability was reported but diskSampleWindowMs was missing."
    }

    $diskSampleWindow = [int]$diskSampleWindowMatch.Groups["window"].Value
    if ($diskSampleWindow -le 0) {
        throw "Disk sample window must be positive when available: $diskSampleWindow"
    }

    if (-not $diskReadRateMatch.Success -or $diskReadRateMatch.Groups["rate"].Value -eq "N/A") {
        throw "Disk availability was reported but diskReadKBps was missing."
    }
    if (-not $diskWriteRateMatch.Success -or $diskWriteRateMatch.Groups["rate"].Value -eq "N/A") {
        throw "Disk availability was reported but diskWriteKBps was missing."
    }

    $diskReadRate = [int64]$diskReadRateMatch.Groups["rate"].Value
    $diskWriteRate = [int64]$diskWriteRateMatch.Groups["rate"].Value
    if ($diskReadRate -lt 0 -or $diskWriteRate -lt 0) {
        throw "Disk KBps values must be non-negative."
    }

    if ($diskActivePctAvailable) {
        if (-not $diskValueMatch.Success -or $diskValueMatch.Groups["value"].Value -eq "N/A") {
            throw "Disk active utilization was reported but disk=<value>% was missing."
        }

        $diskValue = [int]($diskValueMatch.Groups["value"].Value.TrimEnd('%'))
        if ($diskValue -lt 0 -or $diskValue -gt 100) {
            throw "Disk utilization out of range: $diskValue"
        }
    } elseif ($diskActivePctUnavailable) {
        if (-not $diskValueMatch.Success -or $diskValueMatch.Groups["value"].Value -ne "N/A") {
            throw "Disk active utilization was unavailable but disk=N/A was not reported."
        }
    } else {
        throw "Disk active availability flag was missing."
    }
} elseif ($diskUnavailable) {
    if (-not $diskValueMatch.Success -or $diskValueMatch.Groups["value"].Value -ne "N/A") {
        throw "Disk was unavailable but disk=N/A was not reported."
    }
    if (-not $diskSourceMatch.Success) {
        throw "Disk was unavailable but diskSource was missing."
    }

    $diskSource = $diskSourceMatch.Groups["source"].Value
    if ($diskSource -ne "N/A") {
        throw "Disk was unavailable but diskSource was not N/A: $diskSource"
    }

    if ($diskSampleWindowMatch.Success -and $diskSampleWindowMatch.Groups["window"].Value -ne "N/A") {
        throw "Disk was unavailable but diskSampleWindowMs was not N/A."
    }
    if ($diskReadRateMatch.Success -and $diskReadRateMatch.Groups["rate"].Value -ne "N/A") {
        throw "Disk was unavailable but diskReadKBps was not N/A."
    }
    if ($diskWriteRateMatch.Success -and $diskWriteRateMatch.Groups["rate"].Value -ne "N/A") {
        throw "Disk was unavailable but diskWriteKBps was not N/A."
    }
    if ($diskActivePctAvailable) {
        throw "Disk was unavailable but diskActivePctAvailable=true was reported."
    }
} else {
    throw "Disk availability flag was missing."
}

if ($processDiskAvailable) {
    throw "processDiskAvailable must remain false until real per-process disk telemetry exists."
}

$networkAvailable = $output -match "(?m)^\s*networkAvailable=true\s*$"
$networkUnavailable = $output -match "(?m)^\s*networkAvailable=false\s*$"
$networkUtilizationAvailable = $output -match "(?m)^\s*networkUtilizationAvailable=true\s*$"
$networkUtilizationUnavailable = $output -match "(?m)^\s*networkUtilizationAvailable=false\s*$"
$networkValueMatch = [regex]::Match($output, "(?m)^\s*network=(?<value>N/A|\d+%)\s*$")
$networkSourceMatch = [regex]::Match($output, "(?m)^\s*networkSource=(?<source>[^\s\r\n]+)\s*$")
$networkSampleWindowMatch = [regex]::Match($output, "(?m)^\s*networkSampleWindowMs=(?<window>N/A|\d+)\s*$")
$networkSendRateMatch = [regex]::Match($output, "(?m)^\s*networkSendKBps=(?<rate>N/A|\d+)\s*$")
$networkReceiveRateMatch = [regex]::Match($output, "(?m)^\s*networkReceiveKBps=(?<rate>N/A|\d+)\s*$")
$processNetworkAvailable = $output -match "(?m)^\s*processNetworkAvailable=true\s*$"
$processNetworkUnavailable = $output -match "(?m)^\s*processNetworkAvailable=false\s*$"

if ($networkAvailable) {
    if (-not $networkSourceMatch.Success) {
        throw "Network availability was reported but networkSource was missing."
    }

    $networkSource = $networkSourceMatch.Groups["source"].Value
    if ($networkSource -match '(?i)(synthetic|modulo|wave|placeholder|fake|simulated)') {
        throw "Network source looks synthetic: $networkSource"
    }

    if (-not $networkSampleWindowMatch.Success -or $networkSampleWindowMatch.Groups["window"].Value -eq "N/A") {
        throw "Network availability was reported but networkSampleWindowMs was missing."
    }

    $networkSampleWindow = [int]$networkSampleWindowMatch.Groups["window"].Value
    if ($networkSampleWindow -le 0) {
        throw "Network sample window must be positive when available: $networkSampleWindow"
    }

    if (-not $networkSendRateMatch.Success -or $networkSendRateMatch.Groups["rate"].Value -eq "N/A") {
        throw "Network availability was reported but networkSendKBps was missing."
    }
    if (-not $networkReceiveRateMatch.Success -or $networkReceiveRateMatch.Groups["rate"].Value -eq "N/A") {
        throw "Network availability was reported but networkReceiveKBps was missing."
    }

    $networkSendRate = [int64]$networkSendRateMatch.Groups["rate"].Value
    $networkReceiveRate = [int64]$networkReceiveRateMatch.Groups["rate"].Value
    if ($networkSendRate -lt 0 -or $networkReceiveRate -lt 0) {
        throw "Network KBps values must be non-negative."
    }

    if ($networkUtilizationAvailable) {
        if (-not $networkValueMatch.Success -or $networkValueMatch.Groups["value"].Value -eq "N/A") {
            throw "Network utilization was reported but network=<value>% was missing."
        }

        $networkValue = [int]($networkValueMatch.Groups["value"].Value.TrimEnd('%'))
        if ($networkValue -lt 0 -or $networkValue -gt 100) {
            throw "Network utilization out of range: $networkValue"
        }
    } elseif ($networkUtilizationUnavailable) {
        if (-not $networkValueMatch.Success -or $networkValueMatch.Groups["value"].Value -ne "N/A") {
            throw "Network utilization was unavailable but network=N/A was not reported."
        }
    } else {
        throw "Network utilization availability flag was missing."
    }
} elseif ($networkUnavailable) {
    if (-not $networkValueMatch.Success -or $networkValueMatch.Groups["value"].Value -ne "N/A") {
        throw "Network was unavailable but network=N/A was not reported."
    }
    if (-not $networkSourceMatch.Success) {
        throw "Network was unavailable but networkSource was missing."
    }

    $networkSource = $networkSourceMatch.Groups["source"].Value
    if ($networkSource -ne "N/A") {
        throw "Network was unavailable but networkSource was not N/A: $networkSource"
    }

    if ($networkSampleWindowMatch.Success -and $networkSampleWindowMatch.Groups["window"].Value -ne "N/A") {
        throw "Network was unavailable but networkSampleWindowMs was not N/A."
    }
    if ($networkSendRateMatch.Success -and $networkSendRateMatch.Groups["rate"].Value -ne "N/A") {
        throw "Network was unavailable but networkSendKBps was not N/A."
    }
    if ($networkReceiveRateMatch.Success -and $networkReceiveRateMatch.Groups["rate"].Value -ne "N/A") {
        throw "Network was unavailable but networkReceiveKBps was not N/A."
    }
    if ($networkUtilizationAvailable) {
        throw "Network was unavailable but networkUtilizationAvailable=true was reported."
    }
} else {
    throw "Network availability flag was missing."
}

if ($processNetworkAvailable) {
    throw "processNetworkAvailable must remain false until real per-process network telemetry exists."
}

Write-Host "Task Manager snapshot smoke PASS"
Write-Host $output
exit 0
