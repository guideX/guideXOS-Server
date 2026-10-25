param(
    [Parameter(Mandatory=$true)][string]$BootEvidencePath,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedKernelSha256,
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
$qmpActions = [System.Collections.Generic.List[object]]::new()
foreach ($path in @($controllerStdout,$controllerStderr,$controllerManifest)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite C188R11R2 evidence: $path" }
}

$localQemu = @(Get-Process -Name 'qemu-system-x86_64' -ErrorAction SilentlyContinue)
if ($localQemu.Count -ne 0) {
    throw 'Local QEMU process census is nonempty; the R2 guest was not attempted.'
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $pwsh.Source
$psi.WorkingDirectory = $repo
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($argument in @(
    '-NoProfile','-File',$runner,
    '-BootEvidencePath',$bootRoot,
    '-ExpectedKernelSha256',$ExpectedKernelSha256,
    '-Scenario','managed-state-provenance',
    '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
    '-BootTimeoutSeconds','180',
    '-AdmissionTimeoutSeconds','60')) {
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
                if ([int]$active.screenWidth -gt 0 -and [int]$active.screenHeight -gt 0) {
                    $serialText = Get-Content -LiteralPath $serialPath -Raw
                    $ready = Test-C188R11NotesReady $serialText
                    $focusVerified = [regex]::IsMatch($serialText,
                        '(?m)^\[C188R11R2_KEY\] stage=main-loop-focus window=[A-Fa-f0-9]{8} title=Managed Notes owner=\S+ result=focused\r?$')
                    if ($ready -and $focusVerified) { break }
                }
            } catch { $ready = $false; $focusVerified = $false }
        }
        Start-Sleep -Milliseconds 500
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
    $duringList = Get-Content -LiteralPath (Join-Path $bootRoot 'qac-during.list.txt') -Raw
    $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
    if ($duringList -notmatch 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running' -or
        $duringList -notmatch ('\(PID\s+' + [regex]::Escape([string]$active.pid) + '\)')) {
        throw 'QAC did not show the owned acceptance reservation for the active guest; no key event was sent.'
    }

    $qmpActions.Add((Invoke-C188R11R2Control @('capture','before-input') 'r2-qmp-capture-before-input'))
    $serialText = Get-Content -LiteralPath $serialPath -Raw
    if (-not [regex]::IsMatch($serialText,
            '(?m)^\[C188R11R2_KEY\] stage=main-loop-focus window=[A-Fa-f0-9]{8} title=Managed Notes owner=\S+ result=focused\r?$')) {
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
$baseManifestPath = Join-Path $bootRoot 'acceptance-manifest.json'
$baseManifest = if (Test-Path -LiteralPath $baseManifestPath) {
    Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
} else { $null }
$degradedCensus = $qacStderr -match 'reservation .* is degraded.*QEMU process census is incomplete'
$outcome = if ($trace.managedStateQualified) {
    'MANAGED_DISPATCH_QUALIFIED'
} elseif ($trace.irqScancodeReceived -or $trace.kernelDequeued -or
    $trace.focusedNotesTarget -or $trace.nativeManagedEntry) {
    'INPUT_BOUNDARY_LOCALIZED'
} else {
    'DIAGNOSTIC_INCONCLUSIVE'
}
$manifest = [ordered]@{
    outcome=$outcome
    inputSent=$inputSent
    selectedInput='QMP qcode a down/up; Set-2 scancode 0x1C maps to ASCII a'
    shiftExpectedManagedCallback=$false
    focusVerifiedBeforeInput=$focusVerified
    qacPreAdmissionExclusive=($beforeStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and $beforeList -match 'Reservations:\s+0')
    qacOwnedAcceptanceRunning=($duringList -match 'guideXOSServer\s+\|\s+acceptance\s+\|\s+Running' -and $duringList -match ('\(PID\s+' + [regex]::Escape([string]$active.pid) + '\)'))
    qacReservationId=$(if ($reservation.Success) { $reservation.Groups[1].Value } else { '' })
    qemuPid=$(if ($qemuChild.Success) { $qemuChild.Groups[2].Value } else { '' })
    qacReleaseVerified=[bool]$releaseVerified
    ownedQemuExited=[bool]$ownedQemuExited
    qacDegradedProcessCensusWarning=[bool]$degradedCensus
    trace=$trace
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
Write-Output "C188R11R2_KEYBOARD=$outcome INPUT_SENT=$inputSent MANAGED_STATE=$($trace.managedStateQualified) CALLBACK=$($trace.notesCallbackEntry) REGISTRY_COUNT=$($trace.registryCount) RESIDENT_MASK=$($trace.residentMask) QAC_RELEASE=$releaseVerified QAC_DEGRADED=$degradedCensus"
Write-Output "C188R11R2_KEYBOARD_MANIFEST=$controllerManifest"
if (-not $releaseVerified -or -not $ownedQemuExited) {
    throw 'The owned C188R11R2 QAC process or reservation did not complete verified shutdown.'
}
