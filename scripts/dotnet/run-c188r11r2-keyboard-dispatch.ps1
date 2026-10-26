param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
    [Parameter()][string]$CandidateRoot = '',
    [Parameter()][switch]$MemoryWatchpoints,
    [Parameter()][ValidateRange(120,600)][int]$MaxRuntimeSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R2 keyboard runner requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out\dotnet\c188r11-managed-state-provenance'))
$bootRoot = [System.IO.Path]::GetFullPath($BootEvidencePath)
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $bootRoot)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "BootEvidencePath must be a unique directory below $phaseRoot."
}
if (Test-Path -LiteralPath $bootRoot) {
    throw "Refusing to overwrite C188R11R2 guest evidence: $bootRoot"
}
$null = New-Item -ItemType Directory -Path $bootRoot
$candidateRoot = if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    Join-Path $phaseRoot 'candidate\keyboard-dispatch-001'
} else {
    [System.IO.Path]::GetFullPath($CandidateRoot)
}
$candidateRelative = [System.IO.Path]::GetRelativePath($phaseRoot, $candidateRoot)
if ($candidateRelative -eq '.' -or
    $candidateRelative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($candidateRelative)) {
    throw "CandidateRoot must be a unique directory below $phaseRoot."
}
$candidateEsp = Join-Path $candidateRoot 'esp'
$candidateStageManifestPath = Join-Path $candidateRoot 'ramdisk-staging.json'
foreach ($requiredCandidatePath in @(
    $candidateEsp,
    (Join-Path $candidateEsp 'kernel.elf'),
    (Join-Path $candidateEsp 'ramdisk.img'),
    (Join-Path $candidateEsp 'EFI\BOOT\BOOTX64.EFI'),
    $candidateStageManifestPath)) {
    if (-not (Test-Path -LiteralPath $requiredCandidatePath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $requiredCandidatePath -PathType Container)) {
        throw "Validated C188R11R2 candidate input is missing: $requiredCandidatePath"
    }
}
$candidateStageManifest = Get-Content -LiteralPath $candidateStageManifestPath -Raw | ConvertFrom-Json
if ($candidateStageManifest.result -ne 'PASS') {
    throw 'Retained C188R11R2 candidate staging manifest is not PASS.'
}
$candidateMediaHashes = [ordered]@{}
foreach ($relativeMediaPath in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $candidateMediaHashes[$relativeMediaPath.Replace('\','/')] =
        (Get-FileHash -LiteralPath (Join-Path $candidateEsp $relativeMediaPath) -Algorithm SHA256).Hash
}
if ($candidateMediaHashes['kernel.elf'] -ne $ExpectedKernelSha256.ToUpperInvariant() -or
    $candidateMediaHashes['ramdisk.img'] -ne $candidateStageManifest.ramdiskSha256.ToUpperInvariant()) {
    throw 'Retained C188R11R2 candidate hashes do not match the requested kernel or staging manifest.'
}
$runEsp = Join-Path $bootRoot 'esp'
Copy-Item -LiteralPath $candidateEsp -Destination $runEsp -Recurse
foreach ($relativeMediaPath in @('kernel.elf','ramdisk.img','EFI\BOOT\BOOTX64.EFI')) {
    $normalizedMediaPath = $relativeMediaPath.Replace('\','/')
    $copiedHash = (Get-FileHash -LiteralPath (Join-Path $runEsp $relativeMediaPath) -Algorithm SHA256).Hash
    if ($copiedHash -ne $candidateMediaHashes[$normalizedMediaPath]) {
        throw "C188R11R2 candidate copy hash mismatch for $normalizedMediaPath."
    }
}
[ordered]@{
    result='PASS'
    sourceEsp=$candidateEsp
    stagingManifest=$candidateStageManifestPath
    stagingManifestSha256=(Get-FileHash -LiteralPath $candidateStageManifestPath -Algorithm SHA256).Hash
    expectedKernelSha256=$ExpectedKernelSha256.ToUpperInvariant()
    sourceMediaHashes=$candidateMediaHashes
    copiedEsp=$runEsp
    copiedMediaHashes=$candidateMediaHashes
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r2-candidate-input-manifest.json') -Encoding utf8
$runner = Join-Path $PSScriptRoot 'run-c188-qac-guest.ps1'
$predicateHelpers = Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1'
$r2PredicateHelpers = Join-Path $PSScriptRoot 'c188r11r2-runner-predicates.ps1'
. $predicateHelpers
. $r2PredicateHelpers
$control = Join-Path $PSScriptRoot 'c188-qmp-control.py'
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$pwsh = Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1
$activePath = Join-Path $bootRoot 'active.json'
$serialPath = Join-Path $bootRoot 'serial.log'
$controllerStdout = Join-Path $bootRoot 'r2-controller.stdout.log'
$controllerStderr = Join-Path $bootRoot 'r2-controller.stderr.log'
$controllerManifest = Join-Path $bootRoot 'r2-keyboard-dispatch.json'
$gdbScriptPath = Join-Path $bootRoot 'r4-memory-watchpoints.gdb'
$gdbStdoutPath = Join-Path $bootRoot 'r4-gdb.stdout.log'
$gdbStderrPath = Join-Path $bootRoot 'r4-gdb.stderr.log'
$qmpActions = [System.Collections.Generic.List[object]]::new()
foreach ($path in @($controllerStdout,$controllerStderr,$controllerManifest,
    $gdbScriptPath,$gdbStdoutPath,$gdbStderrPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite C188R11R2 evidence: $path" }
}

$localQemu = @(Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue)
if ($localQemu.Count -ne 0) {
    throw 'Local QEMU process census is nonempty; the R2 guest was not attempted.'
}
$gdbCommand = if ($MemoryWatchpoints) {
    Get-Command 'gdb.exe' -ErrorAction Stop | Select-Object -First 1
} else { $null }

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $pwsh.Source
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$runnerArguments = @(
    '-NoProfile','-File',$runner,
    '-BootEvidencePath',$bootRoot,
    '-ExpectedKernelSha256',$ExpectedKernelSha256,
    '-Scenario','managed-state-provenance',
    '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
    '-BootTimeoutSeconds','180',
    '-AdmissionTimeoutSeconds','60')
if ($MemoryWatchpoints) { $runnerArguments += '-EnableGdbStub' }
foreach ($argument in $runnerArguments) {
    [void]$psi.ArgumentList.Add([string]$argument)
}
$qacRunner = [System.Diagnostics.Process]::Start($psi)
if (-not $qacRunner) { throw 'Failed to start the bounded C188R11R2 QAC guest runner.' }
$stdoutTask = $qacRunner.StandardOutput.ReadToEndAsync()
$stderrTask = $qacRunner.StandardError.ReadToEndAsync()
$inputSent = $false
$focusVerified = $false
$runnerExitCode = -1
$shutdownRequested = $false
$gdbProcess = $null
$watchpointStatus = if ($MemoryWatchpoints) { 'WAITING_FOR_REGISTRY_SNAPSHOT' } else { 'NOT_REQUESTED' }
$watchpointAttempted = $false

function Invoke-C188R11R2Control([string[]]$Arguments, [string]$Name) {
    $output = (& $python.Source $control '--active' $activePath @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $logPath = Join-Path $bootRoot ($Name + '.log')
    Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
    if ($exitCode -ne 0) {
        throw "QMP action '$Name' failed with exit code $($exitCode): $output"
    }
    return [ordered]@{name=$Name;exitCode=$exitCode;log=$logPath}
}

function Start-C188R11R4MemoryWatchpoints($Snapshot, [int]$GdbPort) {
    if (-not $MemoryWatchpoints -or $GdbPort -le 0 -or
        $Snapshot.Groups['registry'].Value -eq '0000000000000000' -or
        $Snapshot.Groups['slot'].Value -eq '0000000000000000') {
        $script:watchpointStatus = 'INVALID_TARGET_ADDRESS'
        return
    }
    $registryAddress = [Convert]::ToUInt64(
        $Snapshot.Groups['registry'].Value,16)
    $registryLengthAddress = ($registryAddress + 8u).ToString('X16')
    $poolEntriesSlot = $Snapshot.Groups['slot'].Value
    $gdbText = @"
set pagination off
set confirm off
set architecture i386:x86-64
target remote 127.0.0.1:$GdbPort
set `$c188_hits = 0
set `$c188_len_addr = (unsigned int*)0x$registryLengthAddress
set `$c188_slot_addr = (unsigned long long*)0x$poolEntriesSlot
set `$c188_old_len = *`$c188_len_addr
set `$c188_old_entries = *`$c188_slot_addr
watch *`$c188_len_addr
set `$c188_len_bp = `$bpnum
watch *`$c188_slot_addr
set `$c188_slot_bp = `$bpnum
commands `$c188_len_bp
  silent
  set `$c188_hits = `$c188_hits + 1
  set `$c188_new_len = *`$c188_len_addr
  printf "[C188R11R4-WATCH] hit=%u field=registry-length old=%u new=%u pc=%p\n", `$c188_hits, `$c188_old_len, `$c188_new_len, `$pc
  x/8i `$pc-16
  info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11
  set `$c188_old_len = `$c188_new_len
  if `$c188_hits >= 16
    disable `$c188_len_bp
    disable `$c188_slot_bp
    printf "[C188R11R4-WATCH] bounded-hit-limit reached=16\n"
  end
  continue
end
commands `$c188_slot_bp
  silent
  set `$c188_hits = `$c188_hits + 1
  set `$c188_new_entries = *`$c188_slot_addr
  printf "[C188R11R4-WATCH] hit=%u field=pool-entries old=%016llx new=%016llx pc=%p\n", `$c188_hits, `$c188_old_entries, `$c188_new_entries, `$pc
  x/8i `$pc-16
  info registers rax rbx rcx rdx rsi rdi rbp rsp r8 r9 r10 r11
  set `$c188_old_entries = `$c188_new_entries
  if `$c188_hits >= 16
    disable `$c188_len_bp
    disable `$c188_slot_bp
    printf "[C188R11R4-WATCH] bounded-hit-limit reached=16\n"
  end
  continue
end
printf "[C188R11R4-WATCH] watchpoints-armed registry-length=0x$registryLengthAddress pool-entries=0x$poolEntriesSlot\n"
continue
"@
    Set-Content -LiteralPath $gdbScriptPath -Value $gdbText -Encoding ascii
    $script:watchpointAttempted = $true
    $script:watchpointStatus = 'STARTING'
    $script:gdbProcess = Start-Process -FilePath $gdbCommand.Source `
        -ArgumentList @('-q','-batch','-x',$gdbScriptPath) `
        -RedirectStandardOutput $gdbStdoutPath `
        -RedirectStandardError $gdbStderrPath `
        -WindowStyle Hidden -PassThru
    if (-not $script:gdbProcess) {
        $script:watchpointStatus = 'FAILED_TO_START'
        return
    }

    $armDeadline = (Get-Date).AddSeconds(20)
    do {
        if (Test-Path -LiteralPath $gdbStdoutPath -PathType Leaf) {
            $gdbTextNow = [System.IO.File]::ReadAllText($gdbStdoutPath)
            if ($gdbTextNow.Contains('[C188R11R4-WATCH] watchpoints-armed')) {
                $script:watchpointStatus = 'ARMED'
                return
            }
        }
        if ($script:gdbProcess.HasExited) { break }
        Start-Sleep -Milliseconds 50
    } while ((Get-Date) -lt $armDeadline)

    $script:watchpointStatus = if ($script:gdbProcess.HasExited) {
        'GDB_SETUP_FAILED'
    } else { 'GDB_SETUP_TIMEOUT' }
    if (-not $script:gdbProcess.HasExited) {
        Stop-Process -Id $script:gdbProcess.Id -Force -ErrorAction SilentlyContinue
    }
}

try {
    $readyDeadline = (Get-Date).AddSeconds(210)
    $active = $null
    $serialText = ''
    do {
        if ($qacRunner.HasExited) {
            throw "QAC runner exited before R2 input qualification (exit $($qacRunner.ExitCode))."
        }
        if ((Test-Path -LiteralPath $activePath -PathType Leaf) -and
            (Test-Path -LiteralPath $serialPath -PathType Leaf)) {
            try {
                $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
                $serialText = Get-Content -LiteralPath $serialPath -Raw
                if ($MemoryWatchpoints -and -not $watchpointAttempted) {
                    $registryInitSnapshot = Get-C188R11MemorySnapshot `
                        $serialText 'registry-init'
                    if ($registryInitSnapshot.Success) {
                        Start-C188R11R4MemoryWatchpoints $registryInitSnapshot `
                            ([int]$active.gdbPort)
                    }
                }
                if ([int]$active.screenWidth -gt 0 -and [int]$active.screenHeight -gt 0) {
                    $ready = Test-C188R11NotesReady $serialText
                    $focusVerified = [regex]::IsMatch($serialText,
                        '(?m)^\[C188R11R2_KEY\] stage=main-loop-focus window=[A-Fa-f0-9]{8} title=Managed Notes owner=.+? result=focused\r?$')
                    if ($ready -and $focusVerified) { break }
                }
            } catch { $ready = $false; $focusVerified = $false }
        }
        Start-Sleep -Milliseconds $(if ($MemoryWatchpoints -and -not $watchpointAttempted) { 25 } else { 250 })
    } while ((Get-Date) -lt $readyDeadline)
    if (-not $ready) { throw 'C188R11 readiness predicate did not reach the corrected managed-return boundary.' }
    if (-not $focusVerified) {
        throw 'Notes was not the active compositor keyboard target at main-loop readiness; no keyboard input was sent.'
    }

    $beforeStatus = Get-Content -LiteralPath (Join-Path $bootRoot 'qac-before.status.txt') -Raw
    $beforeList = Get-Content -LiteralPath (Join-Path $bootRoot 'qac-before.list.txt') -Raw
    if ($beforeStatus -notmatch 'Coordinator:\s+Online' -or
        $beforeStatus -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
        $beforeStatus -notmatch 'Managed sessions:\s+0; queued:\s+0' -or
        $beforeList -notmatch 'Reservations:\s+0') {
        throw 'QAC pre-admission census was not exclusive and empty; no key event was sent.'
    }
    $duringListPath = Join-Path $bootRoot 'qac-during.list.txt'
    $duringStatusPath = Join-Path $bootRoot 'qac-during.status.txt'
    $duringCensusDeadline = (Get-Date).AddSeconds(15)
    while ((-not (Test-Path -LiteralPath $duringListPath -PathType Leaf) -or
            -not (Test-Path -LiteralPath $duringStatusPath -PathType Leaf)) -and
            -not $qacRunner.HasExited -and (Get-Date) -lt $duringCensusDeadline) {
        Start-Sleep -Milliseconds 50
    }
    if (-not (Test-Path -LiteralPath $duringListPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $duringStatusPath -PathType Leaf)) {
        throw 'QAC did not publish the during-run census before the input gate; no key event was sent.'
    }
    $duringList = Get-Content -LiteralPath $duringListPath -Raw
    $duringStatus = Get-Content -LiteralPath $duringStatusPath -Raw
    $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
    $duringListOwnsGuest = $duringList -match 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running' -and
        $duringList -match ('\(PID\s+' + [regex]::Escape([string]$active.pid) + '\)')
    $duringStatusOwnsGuest = [bool]$active.qacAdmissionConfirmed -and
        $duringStatus -match 'QEMU processes:\s+1 \(0 foreign\)' -and
        $duringStatus -match 'Managed sessions:\s+1; queued:\s+0; blocked:\s+0'
    if (-not $duringListOwnsGuest -and -not $duringStatusOwnsGuest) {
        throw 'QAC did not show the owned acceptance reservation for the active guest; no key event was sent.'
    }

    $qmpActions.Add((Invoke-C188R11R2Control @('capture','before-input') 'r2-qmp-capture-before-input'))
    $serialText = Get-Content -LiteralPath $serialPath -Raw
    if (-not [regex]::IsMatch($serialText,
            '(?m)^\[C188R11R2_KEY\] stage=main-loop-focus window=[A-Fa-f0-9]{8} title=Managed Notes owner=.+? result=focused\r?$')) {
        throw 'Notes lost the active-window focus snapshot before input; no keyboard event was sent.'
    }

    # This is the only keyboard transaction in the one authorized guest:
    # QMP qcode a make followed by its matching break.
    $qmpActions.Add((Invoke-C188R11R2Control @('key','a') 'r2-qmp-key-a-down-up'))
    $inputSent = $true
    $qmpActions.Add((Invoke-C188R11R2Control @('capture','after-input') 'r2-qmp-capture-after-input'))
    $qmpActions | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bootRoot 'r2-qmp-actions.json') -Encoding utf8
    [void](Invoke-C188R11R2Control @('quit') 'r2-qmp-owned-quit')
    $shutdownRequested = $true
    if (-not $qacRunner.WaitForExit(90000)) {
        throw 'QAC did not release the targeted guest after its owned QMP quit.'
    }
    $runnerExitCode = $qacRunner.ExitCode
} catch {
    if (-not $qacRunner.HasExited -and (Test-Path -LiteralPath $activePath -PathType Leaf)) {
        try {
            [void](Invoke-C188R11R2Control @('quit') 'r2-qmp-owned-quit-after-error')
            $shutdownRequested = $true
        } catch { }
        [void]$qacRunner.WaitForExit(90000)
    }
    if ($qacRunner.HasExited) { $runnerExitCode = $qacRunner.ExitCode }
    throw
} finally {
    if ($gdbProcess -and -not $gdbProcess.HasExited) {
        [void]$gdbProcess.WaitForExit(5000)
        if (-not $gdbProcess.HasExited) {
            Stop-Process -Id $gdbProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($qacRunner.HasExited) {
        Set-Content -LiteralPath $controllerStdout -Value $stdoutTask.GetAwaiter().GetResult() -Encoding utf8
        Set-Content -LiteralPath $controllerStderr -Value $stderrTask.GetAwaiter().GetResult() -Encoding utf8
    }
}

$serialText = if (Test-Path -LiteralPath $serialPath) {
    Get-Content -LiteralPath $serialPath -Raw
} else { '' }
$qacStderrPath = Join-Path $bootRoot 'qac.stderr.log'
$qacStderr = if (Test-Path -LiteralPath $qacStderrPath) {
    Get-Content -LiteralPath $qacStderrPath -Raw
} else { '' }
$reservation = [regex]::Match($qacStderr, 'QAC reservation requested: id=([0-9a-f]+)')
$released = [regex]::Match($qacStderr, 'QAC reservation released: id=([0-9a-f]+)')
$qemuChild = [regex]::Match($qacStderr,
    'QAC QEMU exited: reservation=([0-9a-f]+) pid=(\d+) exitCode=(\d+)')
$qacAfterStatusPath = Join-Path $bootRoot 'qac-after.status.txt'
$qacAfterListPath = Join-Path $bootRoot 'qac-after.list.txt'
$qacAfterStatus = if (Test-Path -LiteralPath $qacAfterStatusPath) {
    Get-Content -LiteralPath $qacAfterStatusPath -Raw
} else { '' }
$qacAfterList = if (Test-Path -LiteralPath $qacAfterListPath) {
    Get-Content -LiteralPath $qacAfterListPath -Raw
} else { '' }
$releaseVerified = $shutdownRequested -and $reservation.Success -and
    $released.Success -and $qemuChild.Success -and
    $reservation.Groups[1].Value -eq $released.Groups[1].Value -and
    $reservation.Groups[1].Value -eq $qemuChild.Groups[1].Value -and
    $qemuChild.Groups[3].Value -eq '0' -and
    $qacAfterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
    $qacAfterList -match 'Reservations:\s+0' -and
    $qacAfterList -notmatch [regex]::Escape($reservation.Groups[1].Value)
$ownedQemuExited = $qemuChild.Success -and
    -not (Get-Process -Id ([int]$qemuChild.Groups[2].Value) -ErrorAction SilentlyContinue)
$trace = Get-C188R11R2KeyboardEvidence $serialText
$memoryInitialization = Get-C188R11MemorySnapshot $serialText 'registry-init'
$memoryLaunch = Get-C188R11MemorySnapshot $serialText 'managed-return'
$memoryInput = Get-C188R11MemorySnapshot $serialText 'input-dispatch-entry'
$memoryStorageStable = Test-C188R11MemoryStorageStable $serialText
$memoryStorageOwnedByPersistentHeap =
    Test-C188R11MemoryStorageInPersistentHeap $serialText
$memoryStorageLocalized = $false
if ($memoryInitialization.Success -and $memoryLaunch.Success -and
    $memoryInput.Success -and $trace.registryCount -eq 0 -and
    $trace.residentMask -eq 0) {
    $memoryStorageLocalized =
        $memoryInitialization.Groups['registry'].Value -eq $memoryLaunch.Groups['registry'].Value -and
        $memoryLaunch.Groups['registry'].Value -eq $memoryInput.Groups['registry'].Value -and
        $memoryInitialization.Groups['owner'].Value -eq $memoryLaunch.Groups['owner'].Value -and
        $memoryLaunch.Groups['owner'].Value -eq $memoryInput.Groups['owner'].Value -and
        $memoryInitialization.Groups['slot'].Value -eq $memoryLaunch.Groups['slot'].Value -and
        $memoryLaunch.Groups['slot'].Value -eq $memoryInput.Groups['slot'].Value -and
        $memoryInitialization.Groups['gc'].Value -eq $memoryLaunch.Groups['gc'].Value -and
        $memoryLaunch.Groups['gc'].Value -eq $memoryInput.Groups['gc'].Value -and
        $memoryInput.Groups['live'].Value -eq '0000000000000000' -and
        $memoryInput.Groups['held'].Value -eq $memoryLaunch.Groups['live'].Value -and
        $memoryInput.Groups['heldLen'].Value -eq '5'
}
$gdbText = if (Test-Path -LiteralPath $gdbStdoutPath -PathType Leaf) {
    [System.IO.File]::ReadAllText($gdbStdoutPath)
} else { '' }
$gdbErrorText = if (Test-Path -LiteralPath $gdbStderrPath -PathType Leaf) {
    [System.IO.File]::ReadAllText($gdbStderrPath)
} else { '' }
$watchpointHits = [regex]::Matches($gdbText,
    '(?m)^\[C188R11R4-WATCH\] hit=\d+ field=(?:registry-length|pool-entries)')
if ($MemoryWatchpoints -and $watchpointStatus -eq 'ARMED') {
    $watchpointStatus = if ($watchpointHits.Count -gt 0) {
        'ARMED_HITS_CAPTURED'
    } else { 'ARMED_NO_HITS' }
}
$baseManifestPath = Join-Path $bootRoot 'acceptance-manifest.json'
$baseManifest = if (Test-Path -LiteralPath $baseManifestPath) {
    Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
} else { $null }
$degradedCensus = $qacStderr -match 'reservation .* is degraded.*QEMU process census is incomplete'
$rootCauseFixedAndQualified = $trace.traceComplete -and
    $trace.registryCount -eq 5 -and $trace.residentMask -eq 1 -and
    $trace.lookupResult -eq 0 -and $trace.notesCallbackEntry -and
    $memoryStorageStable -and $memoryStorageOwnedByPersistentHeap -and
    $baseManifest -and $baseManifest.result -eq 'PASS' -and
    $releaseVerified -and $ownedQemuExited -and $runnerExitCode -eq 0
$outcome = if ($rootCauseFixedAndQualified) {
    'ROOT_CAUSE_FIXED_AND_QUALIFIED'
} elseif ($memoryStorageLocalized) {
    'MEMORY_FAILURE_LOCALIZED'
} elseif ($watchpointHits.Count -gt 0) {
    'ROOT_CAUSE_PROVEN'
} else {
    'BLOCKED_OR_INCONCLUSIVE'
}
$manifest = [ordered]@{
    outcome=$outcome
    inputSent=$inputSent
    selectedInput='QMP qcode a down/up; Set-2 scancode 0x1C maps to ASCII a'
    shiftExpectedManagedCallback=$false
    focusVerifiedBeforeInput=$focusVerified
    qacPreAdmissionExclusive=($beforeStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and $beforeList -match 'Reservations:\s+0')
    qacOwnedAcceptanceRunning=[bool]($duringListOwnsGuest -or $duringStatusOwnsGuest)
    qacReservationId=$(if ($reservation.Success) { $reservation.Groups[1].Value } else { '' })
    qemuPid=$(if ($qemuChild.Success) { $qemuChild.Groups[2].Value } else { '' })
    qacReleaseVerified=[bool]$releaseVerified
    ownedQemuExited=[bool]$ownedQemuExited
    qacDegradedProcessCensusWarning=[bool]$degradedCensus
    trace=$trace
    memorySnapshots=[ordered]@{
        registryInitialization=$(if ($memoryInitialization.Success) { $memoryInitialization.Value } else { $null })
        managedLaunchReturn=$(if ($memoryLaunch.Success) { $memoryLaunch.Value } else { $null })
        firstInput=$(if ($memoryInput.Success) { $memoryInput.Value } else { $null })
    }
    memoryFailureLocalized=[bool]$memoryStorageLocalized
    memoryStorageStable=[bool]$memoryStorageStable
    memoryStorageOwnedByPersistentHeap=[bool]$memoryStorageOwnedByPersistentHeap
    rootCauseFixedAndQualified=[bool]$rootCauseFixedAndQualified
    memoryWatchpointsRequested=[bool]$MemoryWatchpoints
    memoryWatchpointStatus=$watchpointStatus
    memoryWatchpointHitCount=$watchpointHits.Count
    gdbScriptPath=$(if (Test-Path -LiteralPath $gdbScriptPath) { $gdbScriptPath } else { '' })
    gdbStdoutPath=$(if (Test-Path -LiteralPath $gdbStdoutPath) { $gdbStdoutPath } else { '' })
    gdbStdoutSha256=$(if (Test-Path -LiteralPath $gdbStdoutPath) { (Get-FileHash -LiteralPath $gdbStdoutPath -Algorithm SHA256).Hash } else { '' })
    gdbStderrPath=$(if (Test-Path -LiteralPath $gdbStderrPath) { $gdbStderrPath } else { '' })
    gdbStderrSha256=$(if (Test-Path -LiteralPath $gdbStderrPath) { (Get-FileHash -LiteralPath $gdbStderrPath -Algorithm SHA256).Hash } else { '' })
    gdbStderr=$gdbErrorText
    notesCallbackReached=[bool]$trace.notesCallbackEntry
    registryCount=$trace.registryCount
    residentMask=$trace.residentMask
    registryPopulated=$trace.registryPopulated
    registryLookupResult=$trace.lookupResult
    serialPath=$serialPath
    serialSha256=$(if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' })
    qmpLogPath=(Join-Path $bootRoot 'qmp-control.log')
    qmpLogSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'qmp-control.log')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'qmp-control.log') -Algorithm SHA256).Hash } else { '' })
    beforeScreenshot=(Join-Path $bootRoot 'before-input.png')
    afterScreenshot=(Join-Path $bootRoot 'after-input.png')
    beforeScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'before-input.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'before-input.png') -Algorithm SHA256).Hash } else { '' })
    afterScreenshotSha256=$(if (Test-Path -LiteralPath (Join-Path $bootRoot 'after-input.png')) { (Get-FileHash -LiteralPath (Join-Path $bootRoot 'after-input.png') -Algorithm SHA256).Hash } else { '' })
    qacBaseManifest=$baseManifestPath
    runnerExitCode=$runnerExitCode
}
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $controllerManifest -Encoding utf8
Write-Output "C188R11R4_MEMORY=$outcome INPUT_SENT=$inputSent MANAGED_STATE=$($trace.managedStateQualified) CALLBACK=$($trace.notesCallbackEntry) REGISTRY_COUNT=$($trace.registryCount) RESIDENT_MASK=$($trace.residentMask) MEMORY_STABLE=$memoryStorageStable WATCHPOINTS=$watchpointStatus HITS=$($watchpointHits.Count) QAC_RELEASE=$releaseVerified QAC_DEGRADED=$degradedCensus"
Write-Output "C188R11R2_KEYBOARD_MANIFEST=$controllerManifest"
if (-not $releaseVerified -or -not $ownedQemuExited) {
    throw 'The owned C188R11R2 QAC process or reservation did not complete verified shutdown.'
}
