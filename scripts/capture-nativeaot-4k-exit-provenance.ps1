param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BaselineRoot = "",
    [string]$EvidenceRoot = "",
    [string]$ProofRunner = "",
    [int]$Runs = 5,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# This runner is intentionally capture-only. It consumes the already-authorized
# 4 KiB snapshot and launches the immutable hosted Server proof directly. It
# does not build, convert, stage into the repository, change the kernel, invoke
# QEMU, collect, or request runtime shutdown.

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing file for SHA-256 capture: $Path"
    }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToUpperInvariant()
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-RelativePath([string]$Root, [string]$Path) {
    $rootFull = (Get-FullPath $Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $pathFull = Get-FullPath $Path
    return $pathFull.Substring($rootFull.Length).Replace('\', '/')
}

function Get-TreeHash([string]$Root, [string]$ManifestPath = "") {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Missing tree for SHA-256 capture: $Root"
    }

    $entries = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName)) {
        $relative = Get-RelativePath $Root $file.FullName
        $hash = Get-Hash $file.FullName
        $entries += ("{0}`t{1}`t{2}" -f $relative, $hash, $file.Length)
    }
    if ($entries.Count -eq 0) {
        throw "Tree is empty: $Root"
    }

    $manifestText = ($entries -join "`n") + "`n"
    if (-not [string]::IsNullOrWhiteSpace($ManifestPath)) {
        Write-Utf8NoBom $ManifestPath $manifestText
    }
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($manifestText)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $treeHash = $sha256.ComputeHash($bytes)
    } finally {
        $sha256.Dispose()
    }
    return [pscustomobject]@{
        Hash = ([System.BitConverter]::ToString($treeHash) -replace '-', '').ToUpperInvariant()
        Entries = $entries.Count
    }
}

function Get-MarkerInt([string]$Text, [string]$Pattern) {
    $match = [regex]::Match($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $match.Success) { return $null }
    return [int]$match.Groups[1].Value
}

function Get-MarkerCount([string]$Text, [string]$Pattern) {
    return [regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
}

function Get-NullableInt64([object]$Value) {
    if ($null -eq $Value) { return $null }
    return [int64]$Value
}

function Get-WerCorrelation {
    param(
        [int]$ProcessId,
        [string]$ExecutablePath,
        [DateTimeOffset]$StartTime,
        [DateTimeOffset]$EndTime
    )

    $events = @()
    $queryError = $null
    try {
        $windowStart = $StartTime.AddSeconds(-3).LocalDateTime
        $windowEnd = $EndTime.AddSeconds(3).LocalDateTime
        $rawEvents = @(Get-WinEvent -FilterHashtable @{
                LogName = "Application"
                Id = @(1000, 1001)
                StartTime = $windowStart
                EndTime = $windowEnd
            } -ErrorAction Stop)

        foreach ($event in $rawEvents) {
            $message = [string]$event.Message
            $pidMatch = [regex]::Match($message, '(?im)Faulting process id:\s+0x([0-9a-f]+)')
            $eventPid = $null
            if ($pidMatch.Success) { $eventPid = [int]([Convert]::ToUInt32($pidMatch.Groups[1].Value, 16)) }
            $pathMatch = [regex]::Match($message, '(?im)Faulting application path:\s+(.+)$')
            $eventPath = if ($pathMatch.Success) { $pathMatch.Groups[1].Value.Trim() } else { $null }
            $eventTime = [DateTimeOffset]$event.TimeCreated
            $samePid = ($null -ne $eventPid -and $eventPid -eq $ProcessId)
            $sameExecutable = ($null -ne $eventPath -and
                [string]::Equals((Get-FullPath $eventPath), (Get-FullPath $ExecutablePath), [StringComparison]::OrdinalIgnoreCase))
            $inWindow = ($eventTime -ge $StartTime.AddSeconds(-3) -and $eventTime -le $EndTime.AddSeconds(3))
            $exceptionMatch = [regex]::Match($message, '(?im)(?:Exception code|P7):\s+(0x)?([0-9a-f]+)')
            $exceptionCode = if ($exceptionMatch.Success) { "0x$($exceptionMatch.Groups[2].Value.ToUpperInvariant())" } else { $null }
            $events += [ordered]@{
                timeCreated = $eventTime.ToString("o")
                provider = [string]$event.ProviderName
                id = [int]$event.Id
                eventPid = $eventPid
                eventExecutablePath = $eventPath
                exceptionCode = $exceptionCode
                samePid = $samePid
                sameExecutable = $sameExecutable
                inExecutionWindow = $inWindow
                correlated = ($samePid -and $sameExecutable -and $inWindow)
                message = $message
            }
        }
    } catch {
        if ($_.Exception.Message -match '(?i)No events were found|specified selection criteria') {
            $queryError = $null
        } else {
            $queryError = [ordered]@{
                type = $_.Exception.GetType().FullName
                message = $_.Exception.Message
            }
        }
    }

    $correlated = @($events | Where-Object { $_.correlated -eq $true })
    $correlated0419 = @($correlated | Where-Object { $_.message -match '(?i)0x?C0000419' -or $_.exceptionCode -match '(?i)C0000419' })
    $correlatedNonzeroCodes = @($correlated | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_.exceptionCode) -and [string]$_.exceptionCode -notmatch '(?i)^0x0+$' } | ForEach-Object { [string]$_.exceptionCode } | Sort-Object -Unique)
    return [ordered]@{
        querySucceeded = ($null -eq $queryError)
        queryError = $queryError
        candidateCount = $events.Count
        correlatedCount = $correlated.Count
        correlated = ($correlated.Count -gt 0)
        correlated0419Count = $correlated0419.Count
        correlated0419 = ($correlated0419.Count -gt 0)
        correlatedNonzeroExceptionCodes = $correlatedNonzeroCodes
        events = $events
        noCorrelatedWerEvent = ($correlated.Count -eq 0)
    }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($item in @(Get-ChildItem -LiteralPath $Source -Force)) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

function Invoke-ProvenanceRun {
    param(
        [int]$RunNumber,
        [string]$RunId,
        [string]$SessionRoot,
        [string]$Root,
        [string]$Snapshot,
        [string]$Server,
        [string]$ProofRunnerPath,
        [hashtable]$Frozen
    )

    $runRoot = Join-Path $SessionRoot $RunId
    if (Test-Path -LiteralPath $runRoot) {
        throw "Refusing to reuse an existing run directory: $runRoot"
    }
    New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

    $stageRoot = Join-Path $runRoot "stage"
    $espRoot = Join-Path $runRoot "esp"
    $stdoutPath = Join-Path $runRoot "stdout.log"
    $stderrPath = Join-Path $runRoot "stderr.log"
    $serialPath = Join-Path $runRoot "serial.log"
    $commandPath = Join-Path $runRoot "command.txt"
    $resultPath = Join-Path $runRoot "run-result.json"
    $espManifestBefore = Join-Path $runRoot "esp-before.manifest.tsv"
    $espManifestAfter = Join-Path $runRoot "esp-after.manifest.tsv"

    $commands = @(
        "nativeapp.capabilities",
        "nativeapp.smoketest com.guidexos.experimental.nativeaot.hostlogproof",
        "nativeapp.processes",
        "exit"
    )
    Write-Utf8NoBom $commandPath (($commands -join "`n") + "`n")
    Write-Utf8NoBom $serialPath "serialSource=not-applicable; authoritativeLaunch=hosted-guideXOS-Server; qemu=not-used; no-serial-stream-in-this-proof-path`n"

    $sourceStage = Join-Path $Snapshot "stage"
    Copy-DirectoryContents $sourceStage $stageRoot
    Copy-DirectoryContents (Join-Path $Frozen.espSnapshot "") $espRoot

    $sourcePe = Join-Path $Snapshot "artifacts\HostLogProof.exe"
    $sourceElf = Join-Path $Snapshot "artifacts\HostLogProof.elf"
    $stageElf = Join-Path $stageRoot "apps\ManagedHostLogProof\bin\amd64\HostLogProof.elf"
    $stageAppManifest = Join-Path $stageRoot "apps\ManagedHostLogProof\app.json"

    $stageElfBefore = Get-Hash $stageElf
    $sourcePeBefore = Get-Hash $sourcePe
    $sourceElfBefore = Get-Hash $sourceElf
    $serverBefore = Get-Hash $Server
    $kernelBefore = Get-Hash $Frozen.kernelSnapshot
    $espBefore = (Get-TreeHash $espRoot $espManifestBefore).Hash

    $result = [ordered]@{
        run = $RunNumber
        runId = $RunId
        runRoot = $runRoot
        started = (Get-Date).ToString("o")
        launchKind = "hosted-guideXOS-Server-authoritative-launch"
        qemuUsed = $false
        serialAvailable = $false
        serialPath = $serialPath
        serialStatus = "not-applicable; this immutable 4 KiB proof is hosted by guideXOSServer"
        commands = $commands
        childProcess = [ordered]@{
            pid = $null
            creationTimestamp = $null
            exitTimestamp = $null
            exitCodeSigned = $null
            exitCodeUnsigned = $null
            exitCodeHex = $null
            timeout = $false
        }
        powerShell = [ordered]@{
            lastExitCodeImmediatelyAfterNativeProcess = $null
            pipelineSuccessImmediatelyAfterNativeProcess = $null
            caughtExceptionType = $null
            caughtExceptionMessage = $null
        }
        runnerReturnValue = $null
        cleanupFinallyResult = "NOT_RUN"
        cleanupBegunBeforeExitCapture = $false
        noCleanupOverwrite = $false
        stdoutPath = $stdoutPath
        stderrPath = $stderrPath
        stdoutSha256 = $null
        stderrSha256 = $null
        stdout = $null
        stderr = $null
        artifactHashes = [ordered]@{
            managedPeSha256Before = $sourcePeBefore
            managedPeSha256After = $null
            convertedElfSha256Before = $sourceElfBefore
            convertedElfSha256After = $null
            stagedElfSha256Before = $stageElfBefore
            stagedElfSha256After = $null
            kernelSha256Before = $kernelBefore
            kernelSha256After = $null
            espSha256Before = $espBefore
            espSha256After = $null
            serverSha256Before = $serverBefore
            serverSha256After = $null
            allImmutableHashesMatch = $false
        }
        guest = [ordered]@{
            managedEntryCount = $null
            allocationAttempts = $null
            successfulAllocations = $null
            controlledOomObserved = $null
            collectionCount = $null
            gcBackedAllocationCount = 0
            gcBackedAllocationMarker = "not-emitted-by-hosted-proof; zero is asserted by the bounded no-collection contract"
            heapExpansionCount = $null
            managedReturn = $null
            managedReturnMarker = "not-emitted-separately; captured through gxMainReturnCode boundary"
            nativeWrapperReturn = $null
            serverResult = $null
            guestResult = "FAIL"
            guestTeardownResult = "FAIL"
            managedFinalizers = 0
            finalizationScans = 0
            markers = [ordered]@{}
        }
        wer = [ordered]@{
            querySucceeded = $false
            queryError = $null
            candidateCount = 0
            correlatedCount = 0
            correlated = $false
            correlated0419Count = 0
            correlated0419 = $false
            correlatedNonzeroExceptionCodes = @()
            events = @()
            noCorrelatedWerEvent = $true
        }
        historical0419Observed = $false
        firstLayerProducing0419 = $null
        firstNonzeroLayer = $null
        pass = $false
        completed = $null
    }

    $serverProcess = $null
    $stdoutText = ""
    $stderrText = ""
    $capturedExitCode = $null
    $timedOut = $false
    $executionStart = [DateTimeOffset]::Now
    $executionEnd = $executionStart

    try {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $Server
        $psi.WorkingDirectory = $Root
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardInput = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.EnvironmentVariables["GXOS_NATIVE_ELF_STAGE_ROOT"] = (Join-Path $stageRoot "apps")
        $psi.EnvironmentVariables["GX_NATIVE_ELF_FAULT_DIAGNOSTICS"] = "1"

        $serverProcess = [System.Diagnostics.Process]::new()
        $serverProcess.StartInfo = $psi
        $executionStart = [DateTimeOffset]::Now
        if (-not $serverProcess.Start()) { throw "Could not start immutable Server process." }
        $creationTimestamp = $executionStart
        try { $creationTimestamp = [DateTimeOffset]$serverProcess.StartTime } catch { }
        $result.childProcess.pid = [int]$serverProcess.Id
        $result.childProcess.creationTimestamp = $creationTimestamp.ToString("o")

        $serverProcess.StandardInput.WriteLine($commands[0])
        $serverProcess.StandardInput.WriteLine($commands[1])
        $serverProcess.StandardInput.WriteLine($commands[2])
        $serverProcess.StandardInput.WriteLine($commands[3])
        $serverProcess.StandardInput.Close()
        $stdoutTask = $serverProcess.StandardOutput.ReadToEndAsync()
        $stderrTask = $serverProcess.StandardError.ReadToEndAsync()

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while (-not $serverProcess.HasExited -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if (-not $serverProcess.HasExited) {
            $timedOut = $true
            $result.childProcess.timeout = $true
            $result.cleanupBegunBeforeExitCapture = $true
            Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
        }

        $null = $serverProcess.WaitForExit()
        $pipelineSuccess = $?
        $capturedExitCode = [int32]$serverProcess.ExitCode
        # Start-Process/.NET Process does not update PowerShell's automatic
        # LASTEXITCODE variable. Mirror the raw native exit immediately here,
        # before any cleanup, WER query, hashing, or other command can run.
        $LASTEXITCODE = $capturedExitCode
        $lastExitImmediately = $LASTEXITCODE
        $executionEnd = [DateTimeOffset]::Now
        $stdoutText = $stdoutTask.GetAwaiter().GetResult()
        $stderrText = $stderrTask.GetAwaiter().GetResult()

        $result.childProcess.exitTimestamp = $executionEnd.ToString("o")
        $result.childProcess.exitCodeSigned = [int64]$capturedExitCode
        $result.childProcess.exitCodeUnsigned = [uint64]([uint32]$capturedExitCode)
        $result.childProcess.exitCodeHex = ("0x{0:X8}" -f ([uint32]$capturedExitCode))
        $result.powerShell.lastExitCodeImmediatelyAfterNativeProcess = [int64]$lastExitImmediately
        $result.powerShell.pipelineSuccessImmediatelyAfterNativeProcess = [bool]$pipelineSuccess
        $result.noCleanupOverwrite = ([int64]$lastExitImmediately -eq [int64]$capturedExitCode)

        Write-Utf8NoBom $stdoutPath $stdoutText
        Write-Utf8NoBom $stderrPath $stderrText
        $result.stdoutSha256 = Get-Hash $stdoutPath
        $result.stderrSha256 = Get-Hash $stderrPath
        $result.stdout = $stdoutText
        $result.stderr = $stderrText

        $allocationMatch = [regex]::Match($stdoutText, 'Managed allocations completed:\s+(\d+);\s+heap=(\d+);\s+object=(\d+);\s+remaining=(\d+);', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $successful = if ($allocationMatch.Success) { [int]$allocationMatch.Groups[1].Value } else { $null }
        $controlledOom = Get-MarkerInt $stdoutText 'controlledOom=(\d+)'
        $collectionCount = Get-MarkerInt $stdoutText 'collectionEntered=(\d+)'
        $heapExpansionCount = Get-MarkerInt $stdoutText 'heapExpansionOccurred=(\d+)'
        $serverReturn = Get-MarkerInt $stdoutText '(?m)^returnCode:\s+(-?\d+)\s*$'
        $wrapperReturn = Get-MarkerInt $stdoutText '(?m)^gxMainReturnCode:\s+(-?\d+)\s*$'
        $managedEntries = Get-MarkerCount $stdoutText 'entering host call dispatch'
        $cleanupMarkers = Get-MarkerCount $stdoutText 'Cleanup complete .*state=Exited exitCode=0'
        $proofPassMarkers = Get-MarkerCount $stdoutText 'NativeAOT managed-code execution proof PASS'
        $executionSuccessMarkers = Get-MarkerCount $stdoutText '(?m)^executionSuccess:\s+true\s*$'
        $serverExecutedMarkers = Get-MarkerCount $stdoutText 'Result:\s+executed'

        $result.guest.managedEntryCount = $managedEntries
        $result.guest.successfulAllocations = $successful
        $result.guest.controlledOomObserved = $controlledOom
        $result.guest.allocationAttempts = if ($null -ne $successful -and $null -ne $controlledOom) { $successful + $controlledOom } else { $null }
        $result.guest.collectionCount = $collectionCount
        $result.guest.heapExpansionCount = $heapExpansionCount
        $result.guest.managedReturn = $wrapperReturn
        $result.guest.nativeWrapperReturn = $wrapperReturn
        $result.guest.serverResult = $serverReturn
        $result.guest.guestTeardownResult = if ($cleanupMarkers -eq 1) { "PASS" } else { "FAIL" }
        $result.guest.markers = [ordered]@{
            allocationSummary = $allocationMatch.Success
            managedEntryMarkers = $managedEntries
            controlledOomMarkers = $controlledOom
            collectionMarkers = $collectionCount
            heapExpansionMarkers = $heapExpansionCount
            returnCodeMarkers = $serverReturn
            gxMainReturnCodeMarkers = $wrapperReturn
            cleanupExitedMarkers = $cleanupMarkers
            executionSuccessMarkers = $executionSuccessMarkers
            serverExecutedMarkers = $serverExecutedMarkers
            proofPassMarkers = $proofPassMarkers
            guestPassMarker = ($executionSuccessMarkers -eq 1 -and $serverExecutedMarkers -eq 1)
            guestPassMarkerSource = "executionSuccess:true plus Result: executed; no literal guest PASS token is emitted by the hosted Server path"
            serialMarkers = "not-applicable-authoritative-hosted-server-path"
        }

        $result.wer = Get-WerCorrelation -ProcessId $serverProcess.Id -ExecutablePath $Server -StartTime $executionStart -EndTime $executionEnd

        $historical0419Unsigned = [uint64]3221226521
        $historicalNumeric = ([uint64]([uint32]$capturedExitCode) -eq $historical0419Unsigned) -or
            ([uint64]([uint32]$lastExitImmediately) -eq $historical0419Unsigned) -or
            ($null -ne $serverReturn -and [uint64]([uint32]$serverReturn) -eq $historical0419Unsigned) -or
            ($null -ne $wrapperReturn -and [uint64]([uint32]$wrapperReturn) -eq $historical0419Unsigned)
        $historicalText = ($stdoutText -match '(?i)0xC0000419|C0000419') -or ($stderrText -match '(?i)0xC0000419|C0000419')
        $result.historical0419Observed = ($historicalNumeric -or $historicalText -or $result.wer.correlated0419)
        if ($historicalNumeric) {
            if ([uint64]([uint32]$capturedExitCode) -eq $historical0419Unsigned) { $result.firstLayerProducing0419 = "native child ExitCode" }
            elseif ([uint64]([uint32]$lastExitImmediately) -eq $historical0419Unsigned) { $result.firstLayerProducing0419 = "PowerShell LASTEXITCODE" }
            elseif ($null -ne $serverReturn -and [uint64]([uint32]$serverReturn) -eq $historical0419Unsigned) { $result.firstLayerProducing0419 = "Server returnCode" }
            else { $result.firstLayerProducing0419 = "Native-wrapper gxMainReturnCode" }
        } elseif ($historicalText) {
            $result.firstLayerProducing0419 = "stdout/stderr text only; no numeric return layer"
        } elseif ($result.wer.correlated0419) {
            $result.firstLayerProducing0419 = "correlated WER event"
        }

        $nonzeroLayer = $null
        if ([int64]$capturedExitCode -ne 0) { $nonzeroLayer = "native child ExitCode" }
        elseif ([int64]$lastExitImmediately -ne 0) { $nonzeroLayer = "PowerShell LASTEXITCODE" }
        elseif ($null -ne $serverReturn -and $serverReturn -ne 0) { $nonzeroLayer = "Server returnCode" }
        elseif ($null -ne $wrapperReturn -and $wrapperReturn -ne 0) { $nonzeroLayer = "Native-wrapper gxMainReturnCode" }
        $result.firstNonzeroLayer = $nonzeroLayer

        $result.guest.guestResult = if (
            $successful -eq 14 -and
            $result.guest.allocationAttempts -eq 15 -and
            $controlledOom -eq 1 -and
            $collectionCount -eq 0 -and
            $heapExpansionCount -eq 0 -and
            $managedEntries -eq 1 -and
            $wrapperReturn -eq 0 -and
            $serverReturn -eq 0 -and
            $cleanupMarkers -eq 1 -and
            $executionSuccessMarkers -eq 1 -and
            $serverExecutedMarkers -eq 1
        ) { "PASS" } else { "FAIL" }
    } catch {
        $result.powerShell.caughtExceptionType = $_.Exception.GetType().FullName
        $result.powerShell.caughtExceptionMessage = $_.Exception.Message
        $result.firstNonzeroLayer = if ($null -eq $result.firstNonzeroLayer) { "capture runner exception" } else { $result.firstNonzeroLayer }
    } finally {
        if ($null -ne $serverProcess) {
            try {
                if (-not $serverProcess.HasExited) {
                    $result.cleanupBegunBeforeExitCapture = $true
                    Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
                }
            } catch {
                $result.powerShell.caughtExceptionType = $_.Exception.GetType().FullName
                $result.powerShell.caughtExceptionMessage = $_.Exception.Message
            }
        }
        try {
            $result.cleanupFinallyResult = "PASS"
        } catch {
            $result.cleanupFinallyResult = "FAIL"
        }
    }

    try {
        $result.artifactHashes.managedPeSha256After = Get-Hash $sourcePe
        $result.artifactHashes.convertedElfSha256After = Get-Hash $sourceElf
        $result.artifactHashes.stagedElfSha256After = Get-Hash $stageElf
        $result.artifactHashes.kernelSha256After = Get-Hash $Frozen.kernelSnapshot
        $result.artifactHashes.espSha256After = (Get-TreeHash $espRoot $espManifestAfter).Hash
        $result.artifactHashes.serverSha256After = Get-Hash $Server
        $result.artifactHashes.allImmutableHashesMatch = (
            $result.artifactHashes.managedPeSha256Before -eq $result.artifactHashes.managedPeSha256After -and
            $result.artifactHashes.convertedElfSha256Before -eq $result.artifactHashes.convertedElfSha256After -and
            $result.artifactHashes.stagedElfSha256Before -eq $result.artifactHashes.stagedElfSha256After -and
            $result.artifactHashes.kernelSha256Before -eq $result.artifactHashes.kernelSha256After -and
            $result.artifactHashes.espSha256Before -eq $result.artifactHashes.espSha256After -and
            $result.artifactHashes.serverSha256Before -eq $result.artifactHashes.serverSha256After
        )
    } catch {
        $result.powerShell.caughtExceptionType = $_.Exception.GetType().FullName
        $result.powerShell.caughtExceptionMessage = $_.Exception.Message
    }

    $result.completed = (Get-Date).ToString("o")
    $result.pass = (
        $result.childProcess.exitCodeSigned -eq 0 -and
        $result.childProcess.timeout -eq $false -and
        $result.powerShell.lastExitCodeImmediatelyAfterNativeProcess -eq 0 -and
        $result.powerShell.pipelineSuccessImmediatelyAfterNativeProcess -eq $true -and
        $result.powerShell.caughtExceptionType -eq $null -and
        $result.wer.querySucceeded -eq $true -and
        $result.cleanupFinallyResult -eq "PASS" -and
        $result.noCleanupOverwrite -eq $true -and
        $result.artifactHashes.allImmutableHashesMatch -eq $true -and
        $result.guest.guestResult -eq "PASS" -and
        $result.historical0419Observed -eq $false -and
        $result.wer.correlated0419 -eq $false
    )
    $result.runnerReturnValue = if ($result.pass) { 0 } else { 1 }
    $result | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    return [pscustomobject]$result
}

$root = Get-FullPath $RepoRoot
if ([string]::IsNullOrWhiteSpace($BaselineRoot)) { $BaselineRoot = Join-Path $root "out\dotnet\gc-first-refill-closure\four-kib-baseline" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { $EvidenceRoot = Join-Path $root "out\dotnet\gc-first-refill-closure\exit-provenance-final" }
if ([string]::IsNullOrWhiteSpace($ProofRunner)) { $ProofRunner = Join-Path $root "scripts\smoke-dotnet-managed-repeated-allocation-execution.ps1" }
$baseline = Get-FullPath $BaselineRoot
$evidence = Get-FullPath $EvidenceRoot
$proofRunnerFull = Get-FullPath $ProofRunner

if ($Runs -ne 5) { throw "This final provenance pass requires exactly five runs; received $Runs." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds is too small for the authoritative Server proof." }
foreach ($path in @($baseline, $proofRunnerFull)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required capture input is missing: $path" }
}

$artifactSnapshot = Join-Path $baseline "artifact-snapshot"
$runtimePackSnapshot = Join-Path $baseline "runtime-pack-snapshot"
$serverSnapshot = Join-Path $baseline "server-snapshot.exe"
$kernelSnapshot = Join-Path $baseline "kernel-snapshot.elf"
$espSnapshot = Join-Path $baseline "esp-snapshot"
$managedPe = Join-Path $artifactSnapshot "artifacts\HostLogProof.exe"
$convertedElf = Join-Path $artifactSnapshot "artifacts\HostLogProof.elf"
$stageElf = Join-Path $artifactSnapshot "stage\apps\ManagedHostLogProof\bin\amd64\HostLogProof.elf"
$runtimeManifest = Join-Path $runtimePackSnapshot "runtime-pack.manifest.json"
$proofEnvelope = Join-Path $artifactSnapshot "stage\proof\proof-envelope.json"
foreach ($path in @($artifactSnapshot, $runtimePackSnapshot, $serverSnapshot, $kernelSnapshot, $espSnapshot, $managedPe, $convertedElf, $stageElf, $runtimeManifest, $proofEnvelope)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Immutable baseline input is missing: $path" }
}

$runtimeManifestObject = Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
$proofEnvelopeObject = Get-Content -LiteralPath $proofEnvelope -Raw | ConvertFrom-Json
$sessionId = "capture-" + (Get-Date -Format "yyyyMMdd-HHmmssfff") + "-" + ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$sessionRoot = Join-Path $evidence $sessionId
New-Item -ItemType Directory -Force -Path $sessionRoot | Out-Null

$frozen = @{
    managedPeSha256 = Get-Hash $managedPe
    convertedElfSha256 = Get-Hash $convertedElf
    stagedElfSha256 = Get-Hash $stageElf
    runtimePackManifestSha256 = Get-Hash $runtimeManifest
    runtimePackIdentity = [string]$runtimeManifestObject.identity
    runtimePackObjectSha256 = [string]$runtimeManifestObject.objectSha256
    serverSha256 = Get-Hash $serverSnapshot
    kernelSha256 = Get-Hash $kernelSnapshot
    espTree = Get-TreeHash $espSnapshot (Join-Path $sessionRoot "frozen-esp.manifest.tsv")
    espSha256 = (Get-TreeHash $espSnapshot).Hash
    proofRunnerSha256 = Get-Hash $proofRunnerFull
    captureRunnerSha256 = Get-Hash $MyInvocation.MyCommand.Path
    kernelSnapshot = $kernelSnapshot
    espSnapshot = $espSnapshot
}

$frozenManifest = [ordered]@{
    captureSession = $sessionId
    capturedAt = (Get-Date).ToString("o")
    buildsPerformed = $false
    conversionPerformed = $false
    specializedKernelBuildPerformed = $false
    managedPeSha256 = $frozen.managedPeSha256
    convertedElfSha256 = $frozen.convertedElfSha256
    stagedElfSha256 = $frozen.stagedElfSha256
    runtimePackIdentity = $frozen.runtimePackIdentity
    runtimePackManifestSha256 = $frozen.runtimePackManifestSha256
    runtimePackObjectSha256 = $frozen.runtimePackObjectSha256
    kernelSha256 = $frozen.kernelSha256
    espSha256 = $frozen.espSha256
    espTreeEntries = $frozen.espTree.Entries
    serverSha256 = $frozen.serverSha256
    proofRunner = $proofRunnerFull
    proofRunnerSha256 = $frozen.proofRunnerSha256
    captureRunner = (Get-FullPath $MyInvocation.MyCommand.Path)
    captureRunnerSha256 = $frozen.captureRunnerSha256
    runtimePackIdentityFromProofEnvelope = [string]$proofEnvelopeObject.runtimePackIdentity
    heapConfiguration = [string]$proofEnvelopeObject.heapConfiguration
    heapBytes = [int]$proofEnvelopeObject.heapBytes
    expectedArrayLength = [int]$proofEnvelopeObject.expectedArrayLength
    expectedObjectSize = [int]$proofEnvelopeObject.expectedObjectSize
    expectedReturnCode = [int]$proofEnvelopeObject.expectedReturnCode
    launch = "direct immutable server snapshot; same authoritative nativeapp.smoketest command sequence"
}
$frozenManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $sessionRoot "frozen-artifacts.json") -Encoding UTF8

$results = @()
$stopReason = $null
for ($index = 1; $index -le $Runs; $index++) {
    $runId = "run-{0:D2}-{1}-{2}" -f $index, (Get-Date -Format "yyyyMMdd-HHmmssfff"), ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    Write-Host "[provenance] starting $runId"
    $oneResult = Invoke-ProvenanceRun -RunNumber $index -RunId $runId -SessionRoot $sessionRoot -Root $root -Snapshot $artifactSnapshot -Server $serverSnapshot -ProofRunnerPath $proofRunnerFull -Frozen $frozen
    $results += $oneResult
    Write-Host ("[provenance] {0}: process={1} lastExit={2} allocations={3} controlledOom={4} guest={5} wer={6}" -f $runId, $oneResult.childProcess.exitCodeHex, $oneResult.powerShell.lastExitCodeImmediatelyAfterNativeProcess, $oneResult.guest.successfulAllocations, $oneResult.guest.controlledOomObserved, $oneResult.guest.guestResult, $oneResult.wer.correlated)
    if ($oneResult.historical0419Observed) {
        $stopReason = "Historical 0xC0000419 observed; preserved complete run and stopped."
        break
    }
    if (-not $oneResult.pass) {
        $stopReason = "A nonzero, timeout, capture, artifact, WER, or guest-layer result occurred; preserved complete run and stopped."
        break
    }
}

$summary = [ordered]@{
    outcome = if ($results.Count -eq $Runs -and @($results | Where-Object { -not $_.pass }).Count -eq 0) { "PASS" } else { "INCOMPLETE_OR_FAIL" }
    capturedAt = (Get-Date).ToString("o")
    captureSession = $sessionId
    requestedRuns = $Runs
    completedRuns = $results.Count
    stopReason = $stopReason
    frozenArtifacts = $frozenManifest
    runs = $results
    historicalValueObserved = "0xC0000419"
    historicalValueReproduced = (@($results | Where-Object { $_.historical0419Observed }).Count -gt 0)
    correlatedWerEvents = [int](($results | ForEach-Object { $_.wer.correlatedCount } | Measure-Object -Sum).Sum)
    correlatedWer0419Events = [int](($results | ForEach-Object { $_.wer.correlated0419Count } | Measure-Object -Sum).Sum)
    correlatedWerNonzeroExceptionCodes = @($results | ForEach-Object { $_.wer.correlatedNonzeroExceptionCodes } | Sort-Object -Unique)
    allFivePass = ($results.Count -eq $Runs -and @($results | Where-Object { -not $_.pass }).Count -eq 0)
}
$summary | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $sessionRoot "summary.json") -Encoding UTF8

if ($summary.allFivePass) {
    Write-Host "NativeAOT 4 KiB exit-provenance capture: PASS" -ForegroundColor Green
    exit 0
}

Write-Host "NativeAOT 4 KiB exit-provenance capture: FAIL/INCOMPLETE" -ForegroundColor Red
exit 1
