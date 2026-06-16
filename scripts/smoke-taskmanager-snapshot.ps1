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
taskmanager.network-snapshot-wait
taskmanager.tombstone-test
taskmanager.snapshot
quit
"@
$output = $commandInput | & $ServerExe 2>&1
$output = $output | Out-String

function Get-FirstRegexValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Group = "value"
    )

    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -eq 0) {
        return $null
    }

    return $matches[0].Groups[$Group].Value
}

function Get-LastRegexValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Group = "value"
    )

    $matches = [regex]::Matches($Text, $Pattern)
    if ($matches.Count -eq 0) {
        return $null
    }

    return $matches[$matches.Count - 1].Groups[$Group].Value
}

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
    @{ Name = "network rates availability field present"; Match = ($output -match "(?m)^\s*networkRatesAvailable=(true|false)\s*$") },
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

$networkStableSmoke = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkStableSmoke=(?<value>true|false)\s*$"
$networkStableWaitMs = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkStableWaitMs=(?<value>\d+)\s*$"
$networkStableRatesAvailable = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkStableRatesAvailable=(?<value>true|false)\s*$"

$warmupNetworkAvailable = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkAvailable=(?<value>true|false)\s*$" -Group "value"
$warmupNetworkRatesAvailable = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkRatesAvailable=(?<value>true|false)\s*$" -Group "value"
$warmupNetworkSource = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkSource=(?<value>[^\s\r\n]+)\s*$" -Group "value"
$warmupNetworkSampleWindow = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkSampleWindowMs=(?<value>N/A|\d+)\s*$" -Group "value"
$warmupNetworkSendRate = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkSendKBps=(?<value>N/A|\d+)\s*$" -Group "value"
$warmupNetworkReceiveRate = Get-FirstRegexValue -Text $output -Pattern "(?m)^\s*networkReceiveKBps=(?<value>N/A|\d+)\s*$" -Group "value"

$stableNetworkAvailable = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkAvailable=(?<value>true|false)\s*$" -Group "value"
$stableNetworkRatesAvailable = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkRatesAvailable=(?<value>true|false)\s*$" -Group "value"
$stableNetworkUtilizationAvailable = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkUtilizationAvailable=(?<value>true|false)\s*$" -Group "value"
$stableNetworkValue = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*network=(?<value>N/A|\d+%)\s*$"
$stableNetworkSource = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkSource=(?<value>[^\s\r\n]+)\s*$" -Group "value"
$stableNetworkSampleWindow = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkSampleWindowMs=(?<value>N/A|\d+)\s*$" -Group "value"
$stableNetworkSendRate = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkSendKBps=(?<value>N/A|\d+)\s*$" -Group "value"
$stableNetworkReceiveRate = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*networkReceiveKBps=(?<value>N/A|\d+)\s*$" -Group "value"
$processNetworkAvailable = Get-LastRegexValue -Text $output -Pattern "(?m)^\s*processNetworkAvailable=(?<value>true|false)\s*$" -Group "value"
$processNetworkUnavailable = $processNetworkAvailable -eq "false"

if ($networkStableSmoke -ne "true") {
    throw "Stable network wait diagnostic did not report success."
}
if ($null -eq $networkStableWaitMs) {
    throw "Stable network wait diagnostic did not report a wait duration."
}
if ([int64]$networkStableWaitMs -lt 0) {
    throw "Stable network wait duration must not be negative."
}
if ($networkStableRatesAvailable -ne "true") {
    throw "Stable network wait diagnostic did not report network rates as available."
}

if ($warmupNetworkAvailable -ne "true") {
    throw "Warmup network availability was missing."
}
if ($warmupNetworkRatesAvailable -ne "false") {
    throw "Warmup network rates should be unavailable."
}
if ($warmupNetworkSource -ne "hostedSocketCountersWarmup") {
    throw "Warmup network source was not hostedSocketCountersWarmup: $warmupNetworkSource"
}
if ($null -eq $warmupNetworkSampleWindow -or $warmupNetworkSampleWindow -eq "N/A") {
    throw "Warmup network sample window was missing."
}
if ([int64]$warmupNetworkSampleWindow -le 0 -or [int64]$warmupNetworkSampleWindow -ge 500) {
    throw "Warmup network sample window should remain below the stable threshold: $warmupNetworkSampleWindow"
}
if ($warmupNetworkSendRate -ne "N/A") {
    throw "Warmup network send KBps should be N/A."
}
if ($warmupNetworkReceiveRate -ne "N/A") {
    throw "Warmup network receive KBps should be N/A."
}

if ($stableNetworkAvailable -ne "true") {
    throw "Stable network availability was missing."
}
if ($stableNetworkRatesAvailable -ne "true") {
    throw "Stable network rates should be available."
}
if ($stableNetworkSource -ne "hostedSocketCounters") {
    throw "Stable network source was not hostedSocketCounters: $stableNetworkSource"
}
if ($null -eq $stableNetworkSampleWindow -or $stableNetworkSampleWindow -eq "N/A") {
    throw "Stable network sample window was missing."
}
if ([int64]$stableNetworkSampleWindow -lt 500) {
    throw "Stable network sample window is too short for a stable display sample: $stableNetworkSampleWindow"
}
if ($stableNetworkSendRate -eq $null -or $stableNetworkSendRate -eq "N/A") {
    throw "Stable network send KBps was missing."
}
if ($stableNetworkReceiveRate -eq $null -or $stableNetworkReceiveRate -eq "N/A") {
    throw "Stable network receive KBps was missing."
}
if ([int64]$stableNetworkSendRate -lt 0 -or [int64]$stableNetworkReceiveRate -lt 0) {
    throw "Stable network KBps values must be non-negative."
}
if ($stableNetworkUtilizationAvailable -ne "false") {
    throw "Stable network utilization must remain unavailable."
}
if ($stableNetworkValue -ne "N/A") {
    throw "Stable network utilization should be reported as N/A."
}

if ($processNetworkAvailable -ne "false") {
    throw "processNetworkAvailable must remain false until real per-process network telemetry exists."
}

Write-Host "Task Manager snapshot smoke PASS"
Write-Host $output
exit 0
